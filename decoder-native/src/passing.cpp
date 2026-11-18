#include "yurlaps_decoder/passing.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <optional>
#include <utility>

namespace yurlaps::decoder {
namespace {

constexpr std::uint64_t PARKED_MIN_US = 1'000'000;
constexpr std::uint64_t PARKED_WINDOW_US = 500'000;
constexpr std::size_t PARKED_WINDOW_MIN_FRAMES = 8;
constexpr float PLATEAU_START_DB = 2.0f;
constexpr float PLATEAU_KEEP_DB = 3.0f;
constexpr std::array<float, 11> SMOOTHING_FIR{
    0.00000000f, 0.01320163f, 0.05883750f, 0.12796555f, 0.19141461f,
    0.21716141f, 0.19141461f, 0.12796555f, 0.05883750f, 0.01320163f,
    0.00000000f,
};

struct Peak {
    std::size_t index{};
    float value{};
    float prominence{};
};

struct WaveformEstimate {
    std::uint64_t sample_index{};
    std::uint64_t duration_samples{};
};

std::vector<HitSample> ordered(const std::vector<HitSample>& hits) {
    std::vector<HitSample> result = hits;
    std::stable_sort(result.begin(), result.end(), [](const HitSample& a, const HitSample& b) {
        return a.sample_index < b.sample_index;
    });
    return result;
}

std::vector<float> interpolate_uniform(const std::vector<HitSample>& hits) {
    std::vector<float> result(129, hits.front().rssi_db);
    const std::uint64_t first = hits.front().sample_index;
    const std::uint64_t last = hits.back().sample_index;
    const std::uint64_t span = last - first;
    if (span == 0) return result;

    std::size_t right = 1;
    for (std::size_t i = 0; i <= 128; ++i) {
        const double target = static_cast<double>(first)
            + static_cast<double>(span) * static_cast<double>(i) / 128.0;
        while (right + 1 < hits.size() && hits[right].sample_index < target) ++right;
        if (target <= hits.front().sample_index) {
            result[i] = hits.front().rssi_db;
        } else if (target >= hits.back().sample_index) {
            result[i] = hits.back().rssi_db;
        } else {
            const auto& left_hit = hits[right - 1];
            const auto& right_hit = hits[right];
            const double width = static_cast<double>(right_hit.sample_index - left_hit.sample_index);
            const double fraction = width > 0.0
                ? (target - static_cast<double>(left_hit.sample_index)) / width
                : 0.0;
            result[i] = static_cast<float>(left_hit.rssi_db
                + fraction * (right_hit.rssi_db - left_hit.rssi_db));
        }
    }
    return result;
}

int reflected_index(int index, int size) {
    if (size <= 1) return 0;
    while (index < 0 || index >= size) {
        if (index < 0) index = -index;
        else index = 2 * size - 2 - index;
    }
    return index;
}

std::vector<float> convolve(const std::vector<float>& input) {
    const int radius = static_cast<int>(SMOOTHING_FIR.size() / 2);
    std::vector<float> result(input.size(), 0.0f);
    for (std::size_t i = 0; i < input.size(); ++i) {
        for (std::size_t k = 0; k < SMOOTHING_FIR.size(); ++k) {
            const int source = reflected_index(
                static_cast<int>(i) + static_cast<int>(k) - radius,
                static_cast<int>(input.size()));
            result[i] += SMOOTHING_FIR[k] * input[static_cast<std::size_t>(source)];
        }
    }
    return result;
}

std::vector<float> zero_phase_smooth(const std::vector<float>& input) {
    auto forward = convolve(input);
    std::reverse(forward.begin(), forward.end());
    auto backward = convolve(forward);
    std::reverse(backward.begin(), backward.end());
    return backward;
}

std::vector<Peak> find_peaks(const std::vector<float>& values, float minimum_prominence) {
    std::vector<Peak> peaks;
    for (std::size_t i = 1; i + 1 < values.size(); ++i) {
        if (values[i] <= values[i - 1] || values[i] <= values[i + 1]) continue;
        float left_min = values[i];
        for (std::size_t j = i; j > 0; --j) {
            left_min = std::min(left_min, values[j]);
            if (values[j] > values[i]) break;
        }
        float right_min = values[i];
        for (std::size_t j = i; j + 1 < values.size(); ++j) {
            right_min = std::min(right_min, values[j]);
            if (values[j] > values[i]) break;
        }
        const float prominence = values[i] - std::max(left_min, right_min);
        if (prominence >= minimum_prominence) peaks.push_back({i, values[i], prominence});
    }
    return peaks;
}

float first_crossing(const std::vector<float>& values, float threshold) {
    if (values.front() >= threshold) return 0.0f;
    for (std::size_t i = 1; i < values.size(); ++i) {
        if (values[i] < threshold) continue;
        const float denominator = values[i] - values[i - 1];
        if (std::fabs(denominator) < 1e-6f) return static_cast<float>(i);
        return static_cast<float>(i - 1)
            + (threshold - values[i - 1]) / denominator;
    }
    return 0.0f;
}

float last_crossing(const std::vector<float>& values, float threshold) {
    const int last = static_cast<int>(values.size() - 1);
    if (values.back() >= threshold) return static_cast<float>(last);
    for (int i = last - 1; i >= 0; --i) {
        if (values[static_cast<std::size_t>(i)] < threshold) continue;
        const float denominator = values[static_cast<std::size_t>(i)]
            - values[static_cast<std::size_t>(i + 1)];
        if (std::fabs(denominator) < 1e-6f) return static_cast<float>(i);
        return static_cast<float>(i + 1)
            - (threshold - values[static_cast<std::size_t>(i + 1)]) / denominator;
    }
    return static_cast<float>(last);
}

WaveformEstimate from_crossings(const std::vector<HitSample>& hits,
                                std::size_t first,
                                std::size_t last) {
    const std::uint64_t span = hits.back().sample_index - hits.front().sample_index;
    const std::uint64_t duration = static_cast<std::uint64_t>(
        std::llround(static_cast<double>(last - first) * static_cast<double>(span) / 128.0));
    const double offset = static_cast<double>(first) * static_cast<double>(span) / 128.0
        + static_cast<double>(duration) / 2.0;
    return {
        hits.front().sample_index + static_cast<std::uint64_t>(std::llround(offset)),
        duration,
    };
}

WaveformEstimate waveform_point(const std::vector<HitSample>& input) {
    const auto hits = ordered(input);
    if (hits.size() < 16 || hits.back().sample_index == hits.front().sample_index) {
        return {hits.front().sample_index, 0};
    }

    const auto uniform = interpolate_uniform(hits);
    const auto smoothed = zero_phase_smooth(uniform);

    std::vector<float> inverted(uniform.size());
    std::transform(uniform.begin(), uniform.end(), inverted.begin(), [](float value) {
        return -value;
    });
    const auto dips = find_peaks(inverted, 3.0f);
    const auto peaks = find_peaks(smoothed, 1.0f);
    if (dips.size() == 3) return from_crossings(hits, dips.front().index, dips.back().index);
    if (peaks.size() == 2) return from_crossings(hits, peaks.front().index, peaks.back().index);

    const float maximum = *std::max_element(smoothed.begin(), smoothed.end());
    const float threshold = maximum - 6.0f;
    const float first = first_crossing(smoothed, threshold);
    const float last = last_crossing(smoothed, threshold);
    const double center = (static_cast<double>(first) + static_cast<double>(last)) / 2.0;
    const double span = static_cast<double>(hits.back().sample_index - hits.front().sample_index);
    return {
        hits.front().sample_index + static_cast<std::uint64_t>(std::llround(center * span / 128.0)),
        0,
    };
}

std::pair<std::size_t, std::size_t> find_plateau(const std::vector<HitSample>& hits,
                                                  double sample_rate) {
    std::pair<std::size_t, std::size_t> parked{0, 0};
    std::size_t window_end = 0;
    const auto us = [sample_rate](std::uint64_t samples) {
        return static_cast<double>(samples) * 1'000'000.0 / sample_rate;
    };

    for (std::size_t i = 0; i < hits.size(); ++i) {
        window_end = std::max(window_end, i);
        while (window_end < hits.size()
            && us(hits[window_end].sample_index - hits[i].sample_index) <= PARKED_WINDOW_US) {
            ++window_end;
        }
        if (window_end == hits.size()) break;
        if (window_end - i < PARKED_WINDOW_MIN_FRAMES) continue;

        auto [minimum, maximum] = std::minmax_element(
            hits.begin() + static_cast<std::ptrdiff_t>(i),
            hits.begin() + static_cast<std::ptrdiff_t>(window_end),
            [](const HitSample& left, const HitSample& right) { return left.rssi_db < right.rssi_db; });
        float low = minimum->rssi_db;
        float high = maximum->rssi_db;
        if (high - low > PLATEAU_START_DB) continue;

        std::size_t end = window_end;
        for (; end < hits.size(); ++end) {
            low = std::min(low, hits[end].rssi_db);
            high = std::max(high, hits[end].rssi_db);
            if (high - low > PLATEAU_KEEP_DB) break;
        }
        if (end > i && us(hits[end - 1].sample_index - hits[i].sample_index) > PARKED_MIN_US) {
            parked = {i, end};
        }
        i = end > 0 ? end - 1 : i;
    }
    return parked;
}

WaveformEstimate departure_point(const std::vector<HitSample>& hits,
                                 std::size_t plateau_start,
                                 std::size_t plateau_end) {
    std::vector<HitSample> tail(hits.begin() + static_cast<std::ptrdiff_t>(plateau_end - 1), hits.end());
    if (tail.size() >= 16) {
        const auto point = waveform_point(tail);
        if (point.duration_samples > 0) return point;
    }

    std::vector<float> levels;
    levels.reserve(plateau_end - plateau_start);
    for (std::size_t i = plateau_start; i < plateau_end; ++i) levels.push_back(hits[i].rssi_db);
    std::nth_element(levels.begin(), levels.begin() + levels.size() / 2, levels.end());
    const float plateau = levels[levels.size() / 2];
    for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
        if (std::fabs(it->rssi_db - plateau) <= PLATEAU_KEEP_DB) {
            return {it->sample_index, 0};
        }
    }
    return {hits.back().sample_index, 0};
}

float selected_mer(const std::vector<HitSample>& hits) {
    std::vector<HitSample> by_rssi = hits;
    const auto k = std::min<std::size_t>(2, by_rssi.size()) - 1;
    std::nth_element(by_rssi.begin(), by_rssi.begin() + static_cast<std::ptrdiff_t>(k), by_rssi.end(),
        [](const HitSample& left, const HitSample& right) { return left.rssi_db > right.rssi_db; });
    return by_rssi[k].mer_db;
}

}  // namespace

PassingEstimate estimate_passing(const std::vector<HitSample>& input, double sample_rate) {
    const auto hits = ordered(input);
    if (hits.empty()) return {};

    const auto maximum = std::max_element(hits.begin(), hits.end(),
        [](const HitSample& left, const HitSample& right) { return left.rssi_db < right.rssi_db; });
    const float max_rssi = maximum->rssi_db;
    std::uint64_t passing_sample = hits.front().sample_index;
    std::uint64_t duration_samples = 0;

    if (hits.size() < 16) {
        const float threshold = max_rssi - 6.0f;
        double weighted_sum = 0.0;
        double weight_total = 0.0;
        for (const auto& hit : hits) {
            if (hit.rssi_db < threshold) continue;
            const double weight = std::pow(10.0, static_cast<double>(hit.rssi_db) / 20.0);
            weighted_sum += static_cast<double>(hit.sample_index) * weight;
            weight_total += weight;
        }
        passing_sample = weight_total > 0.0
            ? static_cast<std::uint64_t>(std::llround(weighted_sum / weight_total))
            : (hits.front().sample_index + hits.back().sample_index) / 2;
    } else {
        const auto span = hits.back().sample_index - hits.front().sample_index;
        const auto plateau = span > static_cast<std::uint64_t>(
            PARKED_MIN_US * sample_rate / 1'000'000.0)
            ? find_plateau(hits, sample_rate)
            : std::pair<std::size_t, std::size_t>{0, 0};
        const auto point = plateau.second > plateau.first
            ? departure_point(hits, plateau.first, plateau.second)
            : waveform_point(hits);
        passing_sample = point.sample_index;
        duration_samples = point.duration_samples;
    }

    const auto duration_us = static_cast<std::uint64_t>(std::llround(
        static_cast<double>(duration_samples) * 1'000'000.0 / sample_rate));
    return {passing_sample, duration_us, max_rssi, selected_mer(hits)};
}

}  // namespace yurlaps::decoder
