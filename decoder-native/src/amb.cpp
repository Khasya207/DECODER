#include "yurlaps_decoder/amb.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

namespace yurlaps::decoder {
namespace {

constexpr int BEAM_WIDTH = 64;
constexpr int MAX_MISMATCH = 4;
constexpr std::uint32_t CODE_A = 0xeec20f;
constexpr std::uint32_t CODE_B = 0xeec20d;

struct Path {
    std::uint64_t history{};
    int previous_symbol{};
    int mismatches{};
    int score{};
};

int parity(std::uint32_t value) {
    return std::popcount(value);
}

}  // namespace

std::optional<AmbFrame> decode_amb_rc3(const std::array<std::uint8_t, 80>& softbits) {
    std::array<Path, BEAM_WIDTH * 2> paths{};
    std::array<Path, BEAM_WIDTH * 2> next{};
    int path_count = 1;
    paths[0] = {};

    for (int step = 0; step < 40; ++step) {
        int next_count = 0;
        const int branches = step < 32 ? 2 : 1;
        for (int index = 0; index < path_count; ++index) {
            const auto& path = paths[index];
            for (int input = 0; input < branches; ++input) {
                const auto history = (path.history << 1u) | static_cast<std::uint64_t>(input);
                const auto state = static_cast<std::uint32_t>(history & 0xffffffu);
                const int first = path.previous_symbol ^ (parity(state & CODE_A) & 1);
                const int second = first ^ (parity(state & CODE_B) & 1);
                const int hard_a = softbits[step * 2] >= 128 ? 1 : 0;
                const int hard_b = softbits[step * 2 + 1] >= 128 ? 1 : 0;
                const int mismatches = path.mismatches + (first != hard_a) + (second != hard_b);
                if (mismatches > MAX_MISMATCH) continue;
                const int weight_a = static_cast<int>(softbits[step * 2]) - 128;
                const int weight_b = static_cast<int>(softbits[step * 2 + 1]) - 128;
                next[next_count++] = {
                    history,
                    second,
                    mismatches,
                    path.score + (first ? weight_a : -weight_a) + (second ? weight_b : -weight_b),
                };
            }
        }
        if (next_count == 0) return std::nullopt;
        if (next_count > BEAM_WIDTH) {
            std::nth_element(next.begin(), next.begin() + BEAM_WIDTH, next.begin() + next_count,
                [](const Path& left, const Path& right) { return left.score > right.score; });
            next_count = BEAM_WIDTH;
        }
        std::copy_n(next.begin(), next_count, paths.begin());
        path_count = next_count;
    }

    const auto best = std::max_element(paths.begin(), paths.begin() + path_count,
        [](const Path& left, const Path& right) { return left.score < right.score; });
    const auto message = static_cast<std::uint32_t>(best->history >> 8u);
    std::uint8_t status = 0;
    std::uint32_t id = 0;
    for (int index = 0; index < 32; ++index) {
        const auto bit = (message >> index) & 1u;
        if ((index % 4) == 0) status = static_cast<std::uint8_t>((status << 1u) | bit);
        else id = (id << 1u) | bit;
    }
    if (id > 9999999u) return std::nullopt;
    return AmbFrame{id, status};
}

}  // namespace yurlaps::decoder
