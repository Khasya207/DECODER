#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace yurlaps::decoder {

struct AmbFrame {
    std::uint32_t id{};
    std::uint8_t status{};
};

// Decode the legacy AMB/RC3 frame after its preamble. RC4 is not handled by
// this module and has no path into the result type.
std::optional<AmbFrame> decode_amb_rc3(const std::array<std::uint8_t, 80>& softbits);

}  // namespace yurlaps::decoder
