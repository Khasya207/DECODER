#include "yurlaps_decoder/iq.hpp"

#include <array>
#include <cassert>
#include <complex>
#include <cstdint>
#include <vector>

namespace {

int parity(std::uint16_t value) {
    int result = 0;
    while (value) { result ^= static_cast<int>(value & 1u); value >>= 1u; }
    return result;
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

int main() {
    constexpr std::uint32_t id = 7654321;
    constexpr std::uint16_t poly_a = 0x1af;
    constexpr std::uint16_t poly_b = 0x11d;
    constexpr std::uint16_t preamble = 0xf9a8;
    const std::array<std::uint8_t, 3> id_bytes{
        static_cast<std::uint8_t>(id >> 16u),
        static_cast<std::uint8_t>(id >> 8u),
        static_cast<std::uint8_t>(id),
    };
    const std::array<std::uint8_t, 4> bytes{
        id_bytes[0], id_bytes[1], id_bytes[2], crc8(id_bytes)};

    std::array<bool, 40> bits{};
    for (int bit = 0; bit < 32; ++bit) bits[bit] = (bytes[bit / 8] >> (7 - bit % 8)) & 1u;
    std::vector<yurlaps::decoder::IqSample> samples;
    samples.reserve(193);
    auto append_symbol = [&samples](float value) {
        samples.push_back({value, 0.0f});
        samples.push_back({value, 0.0f});
    };
    for (int repeat = 0; repeat < 2; ++repeat) {
        for (int bit = 0; bit < 16; ++bit) {
            append_symbol((preamble & (1u << (15 - bit))) ? 0.8f : -0.8f);
        }
        int state = 0;
        for (int step = 0; step < 40; ++step) {
            const int input = bits[step] ? 1 : 0;
            const auto reg = static_cast<std::uint16_t>(((state << 1) | input) & 0x1ff);
            append_symbol(parity(reg & poly_a) ? 0.8f : -0.8f);
            append_symbol(parity(reg & poly_b) ? 0.8f : -0.8f);
            state = ((state << 1) | input) & 0xff;
        }
        samples.push_back({0.0f, 0.0f});
    }

    std::vector<yurlaps::decoder::IqPassing> passings;
    std::vector<yurlaps::decoder::IqStatus> statuses;
    yurlaps::decoder::IqDecoder decoder;
    decoder.set_callback([&passings](const yurlaps::decoder::IqPassing& passing) {
        passings.push_back(passing);
    });
    decoder.set_status_callback([&statuses](const yurlaps::decoder::IqStatus& status) {
        statuses.push_back(status);
    });
    decoder.feed(samples);
    decoder.flush();
    assert(passings.size() == 1);
    assert(passings.front().id == id);
    assert(passings.front().hits == 2);
    assert(passings.front().mer_db > 20.0f);
    assert(passings.front().duration_us == 0);

    yurlaps::decoder::IqDecoder status_decoder(1'000.0);
    std::vector<yurlaps::decoder::IqStatus> status_samples;
    status_decoder.set_status_callback([&status_samples](const yurlaps::decoder::IqStatus& status) {
        status_samples.push_back(status);
    });
    auto status_input = samples;
    status_input.insert(status_input.end(), 1'200, {0.0f, 0.0f});
    status_decoder.feed(status_input);
    status_decoder.flush();
    assert(!status_samples.empty());
    assert(status_samples.front().frames_received == 2);
    assert(status_samples.front().frames_processed == 2);
}
