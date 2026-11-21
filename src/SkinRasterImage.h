#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <filesystem>
#include "TurntableImage.h"

inline bool loadRasterBitmap(Gdiplus::Bitmap& bitmap, TurntableImage& image, UINT expectedWidth, UINT expectedHeight) {
    const UINT width = bitmap.GetWidth(), height = bitmap.GetHeight();
    if (bitmap.GetLastStatus() != Gdiplus::Ok || width == 0 || height == 0 || width > 4096 || height > 4096 ||
        (expectedWidth && width != expectedWidth) || (expectedHeight && height != expectedHeight)) return false;
    Gdiplus::Rect bounds(0, 0, static_cast<INT>(width), static_cast<INT>(height));
    Gdiplus::BitmapData data{};
    if (bitmap.LockBits(&bounds, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) return false;
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height);
    const auto* bytes = static_cast<const uint8_t*>(data.Scan0);
    for (UINT y = 0; y < height; ++y) {
        const auto* row = reinterpret_cast<const uint32_t*>(bytes + static_cast<ptrdiff_t>(y) * data.Stride);
        std::copy_n(row, width, pixels.data() + static_cast<size_t>(y) * width);
    }
    bitmap.UnlockBits(&data);
    return image.setPixels(static_cast<int>(width), static_cast<int>(height), pixels.data());
}

inline bool loadRasterPngFile(const std::filesystem::path& path, TurntableImage& image, UINT expectedWidth = 0, UINT expectedHeight = 0) {
    Gdiplus::Bitmap bitmap(path.c_str());
    return loadRasterBitmap(bitmap, image, expectedWidth, expectedHeight);
}
