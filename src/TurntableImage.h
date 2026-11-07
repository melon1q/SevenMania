#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

class TurntableImage {
    int width_{}, height_{};
    std::vector<uint32_t> pixels_;
    uint64_t generation_{};

public:
    bool setPixels(int width, int height, const uint32_t* pixels) {
        if (width <= 0 || height <= 0 || !pixels) return false;
        width_ = width;
        height_ = height;
        pixels_.assign(pixels, pixels + static_cast<size_t>(width) * height);
        ++generation_;
        return true;
    }

    bool loaded() const { return !pixels_.empty(); }
    int width() const { return width_; }
    int height() const { return height_; }
    const uint32_t* pixels() const { return pixels_.data(); }
    uint64_t generation() const { return generation_; }

    void draw(uint32_t* target, int targetWidth, int targetHeight, int centerX, int centerY, double angle, int destinationSize = 0) const {
        if (!loaded() || !target || targetWidth <= 0 || targetHeight <= 0) return;
        if (destinationSize <= 0) destinationSize = std::max(width_, height_);
        const double cosine = std::cos(angle), sine = std::sin(angle);
        const int left = centerX - destinationSize / 2, top = centerY - destinationSize / 2;
        const int fromX = std::max(0, left), toX = std::min(targetWidth, left + destinationSize);
        const int fromY = std::max(0, top), toY = std::min(targetHeight, top + destinationSize);
        const double halfWidth = width_ / 2.0, halfHeight = height_ / 2.0;
        const double scaleX = static_cast<double>(width_) / destinationSize;
        const double scaleY = static_cast<double>(height_) / destinationSize;

        for (int y = fromY; y < toY; ++y) {
            auto* output = target + static_cast<size_t>(y) * targetWidth;
            const double dy = (y - centerY) * scaleY;
            for (int x = fromX; x < toX; ++x) {
                const double dx = (x - centerX) * scaleX;
                const int sourceX = static_cast<int>(std::floor(cosine * dx + sine * dy + halfWidth));
                const int sourceY = static_cast<int>(std::floor(-sine * dx + cosine * dy + halfHeight));
                if (sourceX < 0 || sourceY < 0 || sourceX >= width_ || sourceY >= height_) continue;

                const uint32_t source = pixels_[static_cast<size_t>(sourceY) * width_ + sourceX];
                const unsigned alpha = source >> 24;
                if (!alpha) continue;
                if (alpha == 255) { output[x] = source; continue; }
                const uint32_t background = output[x];
                const unsigned inverse = 255 - alpha;
                const unsigned red = (((source >> 16) & 255) * alpha + ((background >> 16) & 255) * inverse + 127) / 255;
                const unsigned green = (((source >> 8) & 255) * alpha + ((background >> 8) & 255) * inverse + 127) / 255;
                const unsigned blue = ((source & 255) * alpha + (background & 255) * inverse + 127) / 255;
                output[x] = 0xff000000u | (red << 16) | (green << 8) | blue;
            }
        }
    }
};
