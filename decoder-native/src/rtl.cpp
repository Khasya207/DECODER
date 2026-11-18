#include "yurlaps_decoder/rtl.hpp"

#include <chrono>
#include <cmath>
#include <string_view>
#include <sstream>

#ifdef YURLAPS_WITH_RTLSDR
#include <rtl-sdr.h>

namespace yurlaps::decoder {
namespace {

struct Context {
    IqDecoder* decoder{};
    std::uint64_t start_us{};
    double sample_rate{};
};

std::uint64_t steady_us() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void receive(unsigned char* bytes, std::uint32_t length, void* opaque) {
    auto* context = static_cast<Context*>(opaque);
    // librtlsdr invokes the callback after the buffer has been filled. Pass
    // the timestamp for its first sample, not the callback's end time.
    const auto callback_us = steady_us() - context->start_us;
    const auto sample_count = static_cast<std::uint64_t>(length / 2u);
    const auto buffer_us = static_cast<std::uint64_t>(std::llround(
        static_cast<double>(sample_count) * 1'000'000.0 / context->sample_rate));
    context->decoder->feed(bytes, length,
        callback_us > buffer_us ? callback_us - buffer_us : 0);
}

bool is_blog_v4(rtlsdr_dev_t* device, std::uint32_t index) {
    if (rtlsdr_get_tuner_type(device) == RTLSDR_TUNER_R828D) return true;
    char manufacturer[256]{};
    char product[256]{};
    char serial[256]{};
    if (rtlsdr_get_device_usb_strings(index, manufacturer, product, serial) != 0) return false;
    const std::string_view name(product);
    return name.find("V4") != std::string_view::npos
        || name.find("Blog") != std::string_view::npos;
}

}  // namespace

int run_rtl_sdr(const RtlOptions& options, IqDecoder& decoder, std::string& error) {
    rtlsdr_dev_t* device = nullptr;
    const std::uint32_t index = options.serial.empty()
        ? 0u : rtlsdr_get_index_by_serial(options.serial.c_str());
    if (index == static_cast<std::uint32_t>(-1)) {
        error = "RTL-SDR serial tidak ditemukan: " + options.serial;
        return 2;
    }
    int result = rtlsdr_open(&device, index);
    if (result != 0) { error = "rtlsdr_open gagal: " + std::to_string(result); return 2; }
    auto close = [&] { rtlsdr_close(device); };

    const bool blog_v4 = is_blog_v4(device, index);
    if (!blog_v4) {
        // Standard RTL2832U dongles need direct sampling to receive the 5 MHz
        // OPN carrier. Blog V4 has its own upconverter, so bypassing the
        // tuner on it would produce no passings.
        if ((result = rtlsdr_set_direct_sampling(device, 2)) != 0) {
            error = "rtlsdr_set_direct_sampling gagal: " + std::to_string(result);
            close();
            return 2;
        }
    }
    if ((result = rtlsdr_set_sample_rate(device, options.sample_rate)) != 0) {
        error = "rtlsdr_set_sample_rate gagal: " + std::to_string(result); close(); return 2;
    }
    if ((result = rtlsdr_set_center_freq(device, options.center_frequency_hz)) != 0) {
        error = "rtlsdr_set_center_freq gagal: " + std::to_string(result); close(); return 2;
    }
    if ((result = rtlsdr_set_tuner_bandwidth(device, 1'700'000)) != 0) {
        error = "rtlsdr_set_tuner_bandwidth gagal: " + std::to_string(result); close(); return 2;
    }
    if ((result = rtlsdr_set_tuner_gain_mode(device, 1)) != 0) {
        error = "rtlsdr_set_tuner_gain_mode gagal: " + std::to_string(result); close(); return 2;
    }
    if ((result = rtlsdr_set_tuner_gain(device, options.gain_db * 10)) != 0) {
        error = "rtlsdr_set_tuner_gain gagal: " + std::to_string(result); close(); return 2;
    }
    rtlsdr_reset_buffer(device);
    Context context{&decoder, steady_us(), static_cast<double>(options.sample_rate)};
    result = rtlsdr_read_async(device, receive, &context, 0, 16'384);
    close();
    if (result != 0) { error = "rtlsdr_read_async gagal: " + std::to_string(result); return 2; }
    return 0;
}

}  // namespace yurlaps::decoder

#else

namespace yurlaps::decoder {
int run_rtl_sdr(const RtlOptions&, IqDecoder&, std::string& error) {
    error = "backend RTL-SDR belum dikompilasi; gunakan -DYURLAPS_WITH_RTLSDR=ON";
    return 2;
}
}  // namespace yurlaps::decoder

#endif
