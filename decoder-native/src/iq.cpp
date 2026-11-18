#include "yurlaps_decoder/iq.hpp"

#include "yurlaps_decoder/opn.hpp"
#include "yurlaps_decoder/amb.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>

namespace yurlaps::decoder {
namespace {

constexpr int SAMPLES_PER_SYMBOL = 2;
constexpr int PREAMBLE_SYMBOLS = 16;
constexpr int PAYLOAD_SYMBOLS = 80;
constexpr int FRAME_SAMPLES = (PREAMBLE_SYMBOLS + PAYLOAD_SYMBOLS) * SAMPLES_PER_SYMBOL;
constexpr int FRAME_WINDOW_SAMPLES = FRAME_SAMPLES + SAMPLES_PER_SYMBOL - 1;
constexpr std::uint64_t QUIET_WINDOW_SAMPLES = 625'000; // 250 ms at 2.5 MS/s
constexpr std::uint16_t OPN_PREAMBLE = 0xf9a8;
constexpr std::uint16_t AMB_PREAMBLE = 0x51e4;

struct Candidate {
    float score = 0.0f;
    int phase = 0;
    std::complex<float> correlation{};
    float energy = 0.0f;
    IqProtocol protocol = IqProtocol::OPN;
};

Candidate match_preamble(const std::vector<IqSample>& samples, std::size_t offset,
                        std::uint16_t preamble, IqProtocol protocol) {
    Candidate best;
    for (int phase = 0; phase < SAMPLES_PER_SYMBOL; ++phase) {
        std::complex<float> correlation{};
        float energy = 0.0f;
        for (int symbol = 0; symbol < PREAMBLE_SYMBOLS; ++symbol) {
            const auto& a = samples[offset + static_cast<std::size_t>(symbol * SAMPLES_PER_SYMBOL + phase)];
            const std::complex<float> sample{a.i, a.q};
            const float expected = (preamble & (1u << (PREAMBLE_SYMBOLS - 1 - symbol))) ? 1.0f : -1.0f;
            correlation += sample * expected;
            energy += std::norm(sample);
        }
        const float score = energy > 1e-6f
            ? std::norm(correlation) / (energy * static_cast<float>(PREAMBLE_SYMBOLS))
            : 0.0f;
        if (score > best.score) best = {score, phase, correlation, energy, protocol};
    }
    return best;
}

}  // namespace

IqDecoder::IqDecoder(double sample_rate) : sample_rate_(sample_rate) {}

void IqDecoder::set_callback(PassingCallback callback) {
    callback_ = std::move(callback);
}

void IqDecoder::feed(const std::uint8_t* interleaved_iq, std::size_t byte_count) {
    if (!interleaved_iq || byte_count < 2) return;
    buffer_.reserve(buffer_.size() + byte_count / 2);
    for (std::size_t i = 0; i + 1 < byte_count; i += 2) {
        buffer_.push_back({
            (static_cast<float>(interleaved_iq[i]) - 127.5f) / 127.5f,
            (static_cast<float>(interleaved_iq[i + 1]) - 127.5f) / 127.5f,
        });
    }
    scan();
}

void IqDecoder::feed(const std::vector<IqSample>& samples) {
    buffer_.insert(buffer_.end(), samples.begin(), samples.end());
    scan();
}

void IqDecoder::flush() {
    for (auto& [key, pending] : pending_) {
        (void)key;
        emit(pending);
    }
    pending_.clear();
}

void IqDecoder::emit(Pending& pending) {
    if (!callback_) return;
    pending.passing.sample_index = (pending.first_sample + pending.last_sample) / 2;
    pending.passing.timestamp_us = static_cast<std::uint64_t>(std::llround(
        static_cast<double>(pending.passing.sample_index) * 1'000'000.0 / sample_rate_));
    pending.passing.duration_us = static_cast<std::uint64_t>(std::llround(
        static_cast<double>(pending.last_sample - pending.first_sample) * 1'000'000.0 / sample_rate_));
    callback_(pending.passing);
}

void IqDecoder::flush_expired(std::uint64_t now_sample) {
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (now_sample > it->second.last_sample && now_sample - it->second.last_sample > QUIET_WINDOW_SAMPLES) {
            emit(it->second);
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
}

void IqDecoder::add_hit(const IqPassing& hit) {
    const auto key = (static_cast<std::uint64_t>(hit.protocol == IqProtocol::AMB) << 32u) | hit.id;
    flush_expired(hit.sample_index);
    auto found = pending_.find(key);
    if (found == pending_.end()) {
        pending_.emplace(key, Pending{hit, hit.sample_index, hit.sample_index});
        return;
    }
    auto& pending = found->second;
    pending.last_sample = hit.sample_index;
    pending.passing.hits += hit.hits;
    pending.passing.rssi_db = std::max(pending.passing.rssi_db, hit.rssi_db);
}

void IqDecoder::scan() {
    std::size_t cursor = 0;
    while (buffer_.size() - cursor >= static_cast<std::size_t>(FRAME_WINDOW_SAMPLES)) {
        Candidate best;
        std::size_t best_offset = cursor;
        // A preamble can start at either sample phase. Scan the entire current
        // window and keep only the strongest normalized correlation.
        for (std::size_t offset = cursor;
             offset + FRAME_WINDOW_SAMPLES <= buffer_.size(); ++offset) {
            for (const auto [preamble, protocol] : {
                     std::pair<std::uint16_t, IqProtocol>{OPN_PREAMBLE, IqProtocol::OPN},
                     std::pair<std::uint16_t, IqProtocol>{AMB_PREAMBLE, IqProtocol::AMB}}) {
                const auto candidate = match_preamble(buffer_, offset, preamble, protocol);
                if (candidate.score > best.score) { best = candidate; best_offset = offset; }
            }
        }

        // Noise has no stable phase relationship with the Barker preamble.
        // This threshold is deliberately conservative; sensitivity tuning is
        // measured with captures, not hidden in the race engine.
        if (best.score < 0.58f) {
            cursor = buffer_.size() - static_cast<std::size_t>(FRAME_WINDOW_SAMPLES) + 1;
            break;
        }

        const float phase = std::arg(best.correlation);
        const std::complex<float> correction = std::polar(1.0f, -phase);
        std::array<std::uint8_t, 80> softbits{};
        float signal = 0.0f;
        std::uint32_t hits = 0;
        for (int symbol = 0; symbol < PAYLOAD_SYMBOLS; ++symbol) {
            const std::size_t sample_offset = best_offset
                + static_cast<std::size_t>((PREAMBLE_SYMBOLS + symbol) * SAMPLES_PER_SYMBOL + best.phase);
            std::complex<float> value{};
            for (int s = 0; s < SAMPLES_PER_SYMBOL; ++s) {
                const auto& a = buffer_[sample_offset + static_cast<std::size_t>(s)];
                value += std::complex<float>{a.i, a.q} * correction;
            }
            value /= static_cast<float>(SAMPLES_PER_SYMBOL);
            signal += std::abs(value);
            const float normalized = std::clamp(value.real() / 1.0f, -1.0f, 1.0f);
            softbits[symbol] = static_cast<std::uint8_t>(std::clamp(
                static_cast<int>(127.5f + normalized * 127.5f), 0, 255));
            if (std::abs(value) > 0.15f) ++hits;
        }

        std::optional<std::uint32_t> id;
        if (best.protocol == IqProtocol::OPN) {
            id = decode_opn(softbits);
        } else if (const auto frame = decode_amb_rc3(softbits)) {
            // RC3 status frames have the low three bits clear; other status
            // values are retained for the caller to classify later.
            id = frame->id;
        }
        if (id) {
            const std::uint64_t sample_index = absolute_samples_ + best_offset
                + static_cast<std::uint64_t>(PREAMBLE_SYMBOLS * SAMPLES_PER_SYMBOL);
            const float average_signal = signal / static_cast<float>(PAYLOAD_SYMBOLS);
            add_hit({
                sample_index,
                static_cast<std::uint64_t>(std::llround(
                    static_cast<double>(sample_index) * 1'000'000.0 / sample_rate_)),
                best.protocol,
                *id,
                20.0f * std::log10(std::max(average_signal, 1e-6f)),
                hits,
                0,
            });
            cursor = best_offset + static_cast<std::size_t>(FRAME_SAMPLES);
        } else {
            cursor = best_offset + 1;
        }
    }

    if (cursor > 0) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(cursor));
        absolute_samples_ += cursor;
    }
}

}  // namespace yurlaps::decoder
