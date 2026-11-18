#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <unordered_map>
#include <vector>

namespace yurlaps::decoder {

enum class IqProtocol { OPN, AMB };

struct IqSample {
    float i{};
    float q{};
};

struct IqPassing {
    std::uint64_t sample_index{};
    std::uint64_t timestamp_us{};
    IqProtocol protocol{IqProtocol::OPN};
    std::uint32_t id{};
    float rssi_db{};
    std::uint32_t hits{};
    std::uint64_t duration_us{};
};

using PassingCallback = std::function<void(const IqPassing&)>;

// Clean-room OPN baseband detector. The input is unsigned interleaved RTL-SDR
// IQ at 2.5 MS/s, centered on the 5 MHz OPN carrier. No OpenStint code is
// linked or required.
class IqDecoder {
public:
    explicit IqDecoder(double sample_rate = 2'500'000.0);

    void set_callback(PassingCallback callback);
    void feed(const std::uint8_t* interleaved_iq, std::size_t byte_count);
    void feed(const std::vector<IqSample>& samples);
    void flush();
    std::uint64_t samples_seen() const noexcept { return absolute_samples_; }
    double sample_rate() const noexcept { return sample_rate_; }

private:
    double sample_rate_;
    std::vector<IqSample> buffer_;
    std::deque<float> noise_window_;
    std::uint64_t absolute_samples_ = 0;
    PassingCallback callback_;

    struct Pending {
        IqPassing passing;
        std::uint64_t first_sample{};
        std::uint64_t last_sample{};
        double weighted_sample_sum{};
        double weight_sum{};
    };
    std::unordered_map<std::uint64_t, Pending> pending_;

    void scan();
    void observe_noise(const std::vector<IqSample>& samples);
    float noise_floor_db() const;
    void add_hit(const IqPassing& hit);
    void flush_expired(std::uint64_t now_sample);
    void emit(Pending& pending);
};

}  // namespace yurlaps::decoder
