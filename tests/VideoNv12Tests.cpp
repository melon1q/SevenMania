#include "VideoNv12.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
static void check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
static int channel(uint32_t p, int shift) { return int((p >> shift) & 255); }
int main() { try {
    using namespace nv12;
    std::vector<uint8_t> frame(bytes(4, 2));
    uint32_t out[8]{};
    const auto fill = [&](uint8_t y, uint8_t u, uint8_t v) {
        std::fill_n(frame.begin(), 8, y); for (size_t i = 8; i < frame.size(); i += 2) { frame[i] = u; frame[i + 1] = v; }
    };
    fill(16, 128, 128); toBgra(frame.data(), 4, 2, Matrix::Bt709, false, out);
    check(out[0] == 0xff000000u, "limited black");
    fill(235, 128, 128); toBgra(frame.data(), 4, 2, Matrix::Bt709, false, out);
    check(channel(out[0], 16) >= 254 && channel(out[0], 8) >= 254 && channel(out[0], 0) >= 254, "limited white");
    fill(0, 128, 128); toBgra(frame.data(), 4, 2, Matrix::Bt601, true, out);
    check(out[0] == 0xff000000u, "full-range black");
    // BT.709 limited red (R'G'B' 1,0,0): Y=63 U=102 V=240.
    fill(63, 102, 240); toBgra(frame.data(), 4, 2, Matrix::Bt709, false, out);
    check(channel(out[0], 16) >= 250 && channel(out[0], 8) <= 5 && channel(out[0], 0) <= 5, "BT.709 red");
    // BT.601 limited red: Y=81 U=90 V=240.
    fill(81, 90, 240); toBgra(frame.data(), 4, 2, Matrix::Bt601, false, out);
    check(channel(out[0], 16) >= 250 && channel(out[0], 8) <= 5 && channel(out[0], 0) <= 5, "BT.601 red");
    // pack(): padded pitch and plane height (1080 -> 1088 style).
    const int w = 4, h = 2, pitch = 8, aligned = 4;
    std::vector<uint8_t> buffer(pitch * aligned * 3 / 2, 0xee);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) buffer[y * pitch + x] = uint8_t(10 * y + x);
    for (int x = 0; x < w; ++x) buffer[pitch * aligned + x] = uint8_t(100 + x);
    std::vector<uint8_t> packed;
    check(pack(buffer.data(), buffer.size(), pitch, w, h, aligned, packed) && packed.size() == bytes(w, h), "pack failed");
    check(packed[0] == 0 && packed[5] == 11 && packed[8] == 100 && packed[11] == 103, "pack layout wrong");
    check(!pack(buffer.data(), buffer.size(), pitch, 3, 2, aligned, packed), "odd width accepted");
    check(!pack(buffer.data(), 10, pitch, w, h, aligned, packed), "short buffer accepted");
    {   // Informational: CPU cost of one 1080p conversion (decode worker, not the frame loop).
        std::vector<uint8_t> big(bytes(1920, 1080), 128); std::vector<uint32_t> rgb(1920 * 1080);
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 10; ++i) toBgra(big.data(), 1920, 1080, Matrix::Bt709, false, rgb.data());
        std::cout << "1080p NV12->BGRA: " << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 10 << " ms\n";
    }
    std::cout << "nv12 tests passed\n"; return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
