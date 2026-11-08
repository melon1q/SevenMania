#pragma once
// NV12 (4:2:0, Y plane then interleaved UV plane) helpers shared by the video decoder
// (CPU copy for HUD regions / GDI paths) and the GPU video-processor path.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace nv12 {

enum class Matrix { Bt601, Bt709 };

// One decoded NV12 frame, tightly packed: width*height luma bytes, then width*height/2 UV bytes.
struct View {
    int width{}, height{};
    Matrix matrix{Matrix::Bt709};
    bool fullRange{};
    uint64_t generation{};
    const uint8_t* data{};
    std::shared_ptr<const void> owner; // keeps `data` alive
    explicit operator bool() const { return data && width > 0 && height > 0; }
};

inline size_t bytes(int width, int height) { return static_cast<size_t>(width) * height * 3 / 2; }

// Fixed-point (x1024) YUV -> BGRA, matching the D3D11 video processor's nominal
// ranges (16-235 limited, or 0-255 full). Chroma is shared by each 2x2 block.
inline void toBgra(const uint8_t* source, int width, int height, Matrix matrix, bool fullRange, uint32_t* out) {
    struct Coefficients { int y, rv, gu, gv, bu; };
    const Coefficients c = fullRange
        ? (matrix == Matrix::Bt709 ? Coefficients{1024, 1613, 192, 479, 1900} : Coefficients{1024, 1436, 352, 731, 1815})
        : (matrix == Matrix::Bt709 ? Coefficients{1192, 1836, 218, 546, 2163} : Coefficients{1192, 1634, 401, 833, 2066});
    const int yOffset = fullRange ? 0 : 16;
    const uint8_t* uvPlane = source + static_cast<size_t>(width) * height;
    const auto clamp = [](int v) { return static_cast<uint32_t>(std::clamp(v >> 10, 0, 255)); };
    for (int y = 0; y < height; ++y) {
        const uint8_t* luma = source + static_cast<size_t>(y) * width;
        const uint8_t* uv = uvPlane + static_cast<size_t>(y / 2) * width;
        uint32_t* row = out + static_cast<size_t>(y) * width;
        for (int x = 0; x < width; ++x) {
            const int l = (luma[x] - yOffset) * c.y + 512;
            const int u = uv[x & ~1] - 128, v = uv[(x & ~1) + 1] - 128;
            row[x] = 0xff000000u | (clamp(l + c.rv * v) << 16) | (clamp(l - c.gu * u - c.gv * v) << 8) | clamp(l + c.bu * u);
        }
    }
}

// Copy a decoder buffer (luma pitch `pitch`, chroma plane starting after `alignedHeight`
// luma rows) into a tightly packed NV12 frame.
inline bool pack(const uint8_t* buffer, size_t length, size_t pitch, int width, int height, int alignedHeight, std::vector<uint8_t>& out) {
    if (width <= 0 || height <= 0 || (width & 1) || (height & 1) || pitch < static_cast<size_t>(width) || alignedHeight < height) return false;
    const size_t chromaOffset = pitch * static_cast<size_t>(alignedHeight);
    if (length < chromaOffset + pitch * static_cast<size_t>(height / 2 - 1) + static_cast<size_t>(width)) return false;
    out.resize(bytes(width, height));
    for (int y = 0; y < height; ++y)
        std::copy_n(buffer + static_cast<size_t>(y) * pitch, width, out.data() + static_cast<size_t>(y) * width);
    uint8_t* uvOut = out.data() + static_cast<size_t>(width) * height;
    for (int y = 0; y < height / 2; ++y)
        std::copy_n(buffer + chromaOffset + static_cast<size_t>(y) * pitch, width, uvOut + static_cast<size_t>(y) * width);
    return true;
}

} // namespace nv12
