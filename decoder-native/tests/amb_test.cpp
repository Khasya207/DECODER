#include "yurlaps_decoder/amb.hpp"
#include "yurlaps_decoder/iq.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

namespace {
int parity(std::uint32_t value) {
    int result = 0;
    while (value) { result ^= static_cast<int>(value & 1u); value >>= 1u; }
    return result;
}
}

int main() {
    constexpr std::uint32_t id = 3446708;
    constexpr std::uint8_t status = 0;
    constexpr std::uint16_t preamble = 0x51e4;
    std::uint32_t message = 0;
    int id_bit = 23;
    int status_bit = 7;
    for (int index = 0; index < 32; ++index) {
        const bool from_id = (index % 4) != 0;
        const std::uint32_t bit = from_id
            ? ((id >> id_bit--) & 1u)
            : ((status >> status_bit--) & 1u);
        message |= bit << index;
    }

    std::array<std::uint8_t, 80> encoded{};
    std::uint64_t history = 0;
    int previous = 0;
    for (int step = 0; step < 40; ++step) {
        const int input = step < 32 ? static_cast<int>((message >> (31 - step)) & 1u) : 0;
        history = (history << 1u) | static_cast<std::uint64_t>(input);
        const auto state = static_cast<std::uint32_t>(history & 0xffffffu);
        const int first = previous ^ (parity(state & 0xeec20f) & 1);
        const int second = first ^ (parity(state & 0xeec20d) & 1);
        encoded[step * 2] = first ? 255 : 0;
        encoded[step * 2 + 1] = second ? 255 : 0;
        previous = second;
    }
    const auto decoded = yurlaps::decoder::decode_amb_rc3(encoded);
    assert(decoded && decoded->id == id && decoded->status == status);

    std::vector<yurlaps::decoder::IqSample> samples;
    auto symbol = [&samples](float value) {
        samples.push_back({value, 0.0f});
        samples.push_back({value, 0.0f});
    };
    for (int bit = 0; bit < 16; ++bit) symbol((preamble & (1u << (15 - bit))) ? 0.8f : -0.8f);
    for (const auto value : encoded) symbol(value ? 0.8f : -0.8f);
    samples.push_back({0.0f, 0.0f});

    std::vector<yurlaps::decoder::IqPassing> passings;
    yurlaps::decoder::IqDecoder decoder;
    decoder.set_callback([&passings](const yurlaps::decoder::IqPassing& passing) { passings.push_back(passing); });
    decoder.feed(samples);
    decoder.flush();
    assert(passings.size() == 1);
    assert(passings.front().protocol == yurlaps::decoder::IqProtocol::AMB);
    assert(passings.front().id == id);
}
