#include "yurlaps_decoder/rtl.hpp"

#include <sstream>

#ifdef YURLAPS_WITH_RTLSDR
#include <rtl-sdr.h>

namespace yurlaps::decoder {
namespace {

struct Context {
    IqDecoder* decoder{};
};

void receive(unsigned char* bytes, std::uint32_t length, void* opaque) {
    auto* context = static_cast<Context*>(opaque);
    context->decoder->feed(bytes, length);
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

    if ((result = rtlsdr_set_sample_rate(device, options.sample_rate)) != 0) {
        error = "rtlsdr_set_sample_rate gagal: " + std::to_string(result); close(); return 2;
    }
    if ((result = rtlsdr_set_center_freq(device, options.center_frequency_hz)) != 0) {
        error = "rtlsdr_set_center_freq gagal: " + std::to_string(result); close(); return 2;
    }
    if ((result = rtlsdr_set_tuner_gain_mode(device, 1)) != 0) {
        error = "rtlsdr_set_tuner_gain_mode gagal: " + std::to_string(result); close(); return 2;
    }
    if ((result = rtlsdr_set_tuner_gain(device, options.gain_db * 10)) != 0) {
        error = "rtlsdr_set_tuner_gain gagal: " + std::to_string(result); close(); return 2;
    }
    rtlsdr_reset_buffer(device);
    Context context{&decoder};
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
