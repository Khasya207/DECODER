#pragma once

#include <string>

#include "yurlaps_decoder/iq.hpp"

namespace yurlaps::decoder {

struct RtlOptions {
    std::string serial;
    int gain_db = 28;
    std::uint64_t center_frequency_hz = 5'000'000;
    std::uint32_t sample_rate = 2'500'000;
};

// Blocks until the RTL-SDR stream ends or the device reports an error. The
// build enables the hardware backend only when librtlsdr is found.
int run_rtl_sdr(const RtlOptions& options, IqDecoder& decoder, std::string& error);

}  // namespace yurlaps::decoder
