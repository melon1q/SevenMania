#pragma once

#include <algorithm>
#include <optional>

namespace sevenmania::skin::lr2 {

enum class UpscaleFilter { Original, Bicubic, Nearest, Hybrid };

constexpr UpscaleFilter upscaleFilterFromSetting(unsigned value) noexcept {
    switch (value) {
    case 1: return UpscaleFilter::Bicubic;
    case 2: return UpscaleFilter::Nearest;
    case 3: return UpscaleFilter::Hybrid;
    default: return UpscaleFilter::Original;
    }
}

struct FrameScale {
    UpscaleFilter filter;
    int width, height;
    float x, y, outputWidth, outputHeight;

    void toCanvas(float& px, float& py) const noexcept {
        px = (px - x) * width / outputWidth;
        py = (py - y) * height / outputHeight;
    }
};

inline std::optional<FrameScale> frameScale(UpscaleFilter filter, int width, int height,
    int outputWidth, int outputHeight, bool stretch) noexcept {
    if (filter == UpscaleFilter::Original ||
        !((width == 640 && height == 480) || (width == 1280 && height == 720)) ||
        outputWidth < width || outputHeight < height) return std::nullopt;
    const float sx = float(outputWidth) / width;
    const float sy = float(outputHeight) / height;
    const float fit = std::min(sx, sy);
    if ((stretch ? std::max(sx, sy) : fit) <= 1.f) return std::nullopt;
    const float w = stretch ? float(outputWidth) : width * fit;
    const float h = stretch ? float(outputHeight) : height * fit;
    if (filter == UpscaleFilter::Hybrid && width == 1280) filter=UpscaleFilter::Bicubic;
    return FrameScale{filter, width, height, (outputWidth - w) / 2, (outputHeight - h) / 2, w, h};
}

}
