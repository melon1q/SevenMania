#pragma once
#include "LaneLayout.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// One preloaded six-frame sprite, reused by all hits. No per-frame GDI objects,
// image decoding or unbounded particle allocations.
class PlayFeedback {
    static constexpr int spriteWidth = 128, spriteHeight = 128;
    static constexpr int spriteFrameCount = 6;
    struct Splash { int lane{}; double born = -1; };
    std::array<Splash, 64> splashes_{};
    std::array<std::array<uint32_t, spriteWidth * spriteHeight>, spriteFrameCount> sprites_{};
    size_t next_{};
    unsigned combo_{};
    unsigned maxCombo_{};
    int keys_=7,players_=1;bool popn_{};
public:
    void configure(const LaneLayout& layout){keys_=layout.keys;players_=layout.players;popn_=layout.popn;}
    PlayFeedback() {
        for (int y = 0; y < spriteHeight; ++y) for (int x = 0; x < spriteWidth; ++x) {
            const double nx = (x + 0.5 - spriteWidth / 2.0) / (spriteWidth / 2.0);
            const double ny = (y + 0.5 - spriteHeight / 2.0) / (spriteHeight / 2.0);
            const double radius = std::hypot(nx, ny);
            const double coverage = std::clamp((0.82 - radius) * 64 + 0.5, 0.0, 1.0);
            const double gradient = std::clamp(radius / 0.8, 0.0, 1.0);
            const unsigned r = static_cast<unsigned>(120 - 94 * gradient);
            const unsigned g = static_cast<unsigned>(226 - 94 * gradient);
            const unsigned b = 255;
            const unsigned alpha = static_cast<unsigned>(coverage * 225);
            const uint32_t color = (alpha << 24) | (r << 16) | (g << 8) | b;
            for (auto& sprite : sprites_) sprite[static_cast<size_t>(y) * spriteWidth + x] = color;
        }
    }
    bool setSpriteSheet(int width, int height, const uint32_t* pixels) {
        if (!pixels || width <= 0 || height <= 0 || width % spriteFrameCount != 0) return false;
        const int frameWidth = width / spriteFrameCount;
        for (int frame = 0; frame < spriteFrameCount; ++frame) {
            for (int y = 0; y < spriteHeight; ++y) {
                const int sourceY = y * height / spriteHeight;
                for (int x = 0; x < spriteWidth; ++x) {
                    const int sourceX = frame * frameWidth + x * frameWidth / spriteWidth;
                    sprites_[static_cast<size_t>(frame)][static_cast<size_t>(y) * spriteWidth + x] =
                        pixels[static_cast<size_t>(sourceY) * width + sourceX];
                }
            }
        }
        return true;
    }
    static double scale(double age) {
        if (age < 0 || age >= 0.28) return 0;
        if (age < 0.18) {
            const double t = age / 0.18 - 1;
            const double eased = 1 + 2.70158 * t * t * t + 1.70158 * t * t;
            return 0.12 + 0.88 * eased;
        }
        return 1;
    }
    static double opacity(double age) {
        if (age < 0 || age >= 0.28) return 0;
        return age < 0.18 ? 1 : std::clamp((0.28 - age) / 0.10, 0.0, 1.0);
    }
    void reset() { combo_ = maxCombo_ = 0; next_ = 0; for (auto& splash : splashes_) splash.born = -1; }
    void addCombo(unsigned count) {
        combo_ += count;
        maxCombo_ = std::max(maxCombo_, combo_);
    }
    void hit(int lane, double time) {
        if (lane < 0 || lane >= 16) return;
        addCombo(1);
        splashes_[next_] = { lane, time };
        next_ = (next_ + 1) % splashes_.size();
    }
    void miss() { combo_ = 0; }
    unsigned combo() const { return combo_; }
    unsigned maxCombo() const { return maxCombo_; }
    template<class Draw> void drawSprites(int fieldX, int laneWidth, int judgeY, double time, int scratchWidth, Draw draw) const {
        const LaneLayout layout{fieldX, laneWidth, scratchWidth,keys_,players_,popn_};
        for (const auto& splash : splashes_) {
            if (splash.born < 0) continue;
            const double age = time - splash.born, size = scale(age);
            if (size <= 0) continue;
            const int extent = std::max(1, static_cast<int>(std::lround(layout.width(splash.lane) * 1.20 * size)));
            const int left = layout.left(splash.lane) + (layout.width(splash.lane) - 2) / 2 - extent / 2;
            const int top = judgeY + 1 - extent / 2;
            const size_t frame = std::min<size_t>(spriteFrameCount - 1, static_cast<size_t>(age / 0.18 * spriteFrameCount));
            draw(sprites_[frame].data(), spriteWidth, spriteHeight, left, top, extent, extent,
                (1ull << 63) | frame, static_cast<float>(opacity(age)));
        }
    }
    void draw(uint32_t* pixels, int width, int height, int fieldX, int laneWidth, int judgeY, double time, int scratchWidth = 0) const {
        if (!pixels || width <= 0 || height <= 0) return;
        const LaneLayout layout{ fieldX, laneWidth, scratchWidth > 0 ? scratchWidth : laneWidth,keys_,players_,popn_ };
        for (const auto& splash : splashes_) {
            if (splash.born < 0) continue;
            const double age = time - splash.born, size = scale(age);
            if (size <= 0) continue;
            const int targetWidth = std::max(1, static_cast<int>(std::lround(layout.width(splash.lane) * 1.20 * size)));
            const int targetHeight = targetWidth;
            const int left = layout.left(splash.lane) + (layout.width(splash.lane) - 2) / 2 - targetWidth / 2;
            const int top = judgeY + 1 - targetHeight / 2;
            const unsigned fade = static_cast<unsigned>(255 * opacity(age));
            const size_t frameIndex = std::min<size_t>(spriteFrameCount - 1,
                static_cast<size_t>(age / 0.18 * spriteFrameCount));
            const auto& sprite = sprites_[frameIndex];
            for (int y = std::max(0, top); y < std::min(height, top + targetHeight); ++y) {
                const int sy = (y - top) * spriteHeight / targetHeight;
                auto* output = pixels + static_cast<size_t>(y) * width;
                for (int x = std::max(0, left); x < std::min(width, left + targetWidth); ++x) {
                    const uint32_t color = sprite[static_cast<size_t>(sy) * spriteWidth + (x - left) * spriteWidth / targetWidth];
                    const unsigned alpha = ((color >> 24) * fade + 127) / 255;
                    if (!alpha) continue;
                    const uint32_t before = output[x];
                    const unsigned inverse = 255 - alpha;
                    const unsigned r = (((color >> 16) & 255) * alpha + ((before >> 16) & 255) * inverse + 127) / 255;
                    const unsigned g = (((color >> 8) & 255) * alpha + ((before >> 8) & 255) * inverse + 127) / 255;
                    const unsigned b = ((color & 255) * alpha + (before & 255) * inverse + 127) / 255;
                    const unsigned combinedAlpha = alpha + ((before >> 24) * inverse + 127) / 255;
                    output[x] = (combinedAlpha << 24) | (r << 16) | (g << 8) | b;
                }
            }
        }
    }
};
