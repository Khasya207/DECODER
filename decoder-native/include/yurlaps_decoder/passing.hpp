#pragma once

#include <cstdint>
#include <vector>

namespace yurlaps::decoder {

struct HitSample {
    std::uint64_t sample_index{};
    float rssi_db{};
    float mer_db{};
};

struct PassingEstimate {
    std::uint64_t sample_index{};
    std::uint64_t duration_us{};
    float rssi_db{};
    float mer_db{};
};

// Estimate the passing point from the complete hit waveform. The rules mirror
// OpenStint: amplitude weighting for short passings, wire-crossing detection
// for long passings, and a zero duration when no crossing can be established.
PassingEstimate estimate_passing(const std::vector<HitSample>& hits, double sample_rate);

}  // namespace yurlaps::decoder
