#include "yurlaps_decoder/opn.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace yurlaps::decoder {
namespace {

constexpr int STATES = 256;
constexpr int STEPS = 40;
constexpr std::uint16_t POLY_A = 0x1af;
constexpr std::uint16_t POLY_B = 0x11d;

int parity(std::uint16_t value) {
    int result = 0;
    while (value) {
        result ^= static_cast<int>(value & 1u);
        value >>= 1u;
    }
    return result;
}

struct Node {
    int metric = std::numeric_limits<int>::min() / 4;
    std::uint8_t previous = 0;
    bool input = false;
};

std::pair<int, int> encoded_pair(int state, int input) {
    const auto register_value = static_cast<std::uint16_t>(((state << 1) | input) & 0x1ff);
    return {parity(register_value & POLY_A), parity(register_value & POLY_B)};
}

int symbol_score(std::uint8_t observed, int expected) {
    // Maximize confidence and avoid a hard threshold. The absolute scale is
    // irrelevant; only path ordering matters.
    const int confidence = expected ? observed : (255 - observed);
    return confidence;
}

std::uint8_t crc8(const std::array<std::uint8_t, 3>& bytes) {
    std::uint8_t crc = 0;
    for (const auto byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80u) ? static_cast<std::uint8_t>((crc << 1u) ^ 0x07u)
                                : static_cast<std::uint8_t>(crc << 1u);
        }
    }
    return crc;
}

}  // namespace

std::optional<std::uint32_t> decode_opn(const std::array<std::uint8_t, 80>& softbits) {
    std::array<std::array<Node, STATES>, STEPS + 1> paths{};
    paths[0][0].metric = 0;

    for (int step = 0; step < STEPS; ++step) {
        for (int state = 0; state < STATES; ++state) {
            const auto& current = paths[step][state];
            if (current.metric == std::numeric_limits<int>::min() / 4) continue;
            for (int input = 0; input <= 1; ++input) {
                const auto [a, b] = encoded_pair(state, input);
                const int next_state = ((state << 1) | input) & 0xff;
                const int metric = current.metric
                    + symbol_score(softbits[step * 2], a)
                    + symbol_score(softbits[step * 2 + 1], b);
                auto& next = paths[step + 1][next_state];
                if (metric > next.metric) {
                    next.metric = metric;
                    next.previous = static_cast<std::uint8_t>(state);
                    next.input = input != 0;
                }
            }
        }
    }

    // The OPN encoder appends eight zero bits for the trellis tail.
    if (paths[STEPS][0].metric == std::numeric_limits<int>::min() / 4) return std::nullopt;
    std::array<bool, STEPS> decoded{};
    int state = 0;
    for (int step = STEPS; step > 0; --step) {
        decoded[step - 1] = paths[step][state].input;
        state = paths[step][state].previous;
    }

    std::array<std::uint8_t, 4> bytes{};
    for (int bit = 0; bit < 32; ++bit) {
        bytes[bit / 8] = static_cast<std::uint8_t>((bytes[bit / 8] << 1u) | (decoded[bit] ? 1u : 0u));
    }
    for (int bit = 32; bit < 40; ++bit) {
        if (decoded[bit]) return std::nullopt;
    }

    if (crc8({bytes[0], bytes[1], bytes[2]}) != bytes[3]) return std::nullopt;
    const std::uint32_t id = (static_cast<std::uint32_t>(bytes[0]) << 16u)
        | (static_cast<std::uint32_t>(bytes[1]) << 8u) | bytes[2];
    return id;
}

}  // namespace yurlaps::decoder
