#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace yurlaps::decoder {

// Decode the 80 soft symbols after the OPN preamble. A symbol uses the
// conventional 0..255 confidence scale: 0 means a hard zero and 255 a hard
// one. The frame contains a 24-bit id, CRC-8, and the convolutional tail.
std::optional<std::uint32_t> decode_opn(const std::array<std::uint8_t, 80>& softbits);

}  // namespace yurlaps::decoder
