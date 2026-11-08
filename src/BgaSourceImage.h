#pragma once
// Unscaled BGA still for GPU-side scaling: uploading e.g. 256x256 (256 KB) instead of a
// window-sized CPU-scaled copy (1920x1080 = 8.3 MB) every time the BGA frame changes.
#include <cstdint>
#include <vector>

struct BgaSourceImage {
    int width{}, height{};
    std::vector<uint32_t> pixels; // premultiplied BGRA, opaque
};
