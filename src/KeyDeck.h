#pragma once
#include "LaneLayout.h"
#include "TurntableImage.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

class ScratchDisc {
    double angle_{}, lastTime_{};
    bool stopped_{};
public:
    void reset() { angle_ = lastTime_ = 0; stopped_ = false; }
    void update(double time, bool pressed) {
        constexpr double pi = 3.141592653589793;
        if (!stopped_) angle_ = std::fmod(angle_ + std::max(0.0, time - lastTime_) * pi, 2 * pi);
        lastTime_ = time;
        stopped_ = pressed;
    }
    double angle() const { return angle_; }
};

// Procedural controller panel; direct pixels keep high-FPS rendering inexpensive.
class KeyDeck {
    static void drawScratchRim(uint32_t* pixels, int width, int height, int centerX, int centerY, bool pressed) {
        constexpr double outer = 46.0;
        for (int y = std::max(0, centerY - 47); y <= std::min(height - 1, centerY + 47); ++y) {
            auto* row = pixels + static_cast<size_t>(y) * width;
            for (int x = std::max(0, centerX - 47); x <= std::min(width - 1, centerX + 47); ++x) {
                const double radius = static_cast<double>((x - centerX) * (x - centerX) + (y - centerY) * (y - centerY));
                uint32_t color{};
                if (radius >= 45.0 * 45.0 && radius <= outer * outer) color = 0xff31343du;
                else if (radius >= 43.0 * 43.0 && radius < 45.0 * 45.0) color = pressed ? 0xff747d89u : 0xffaeb7c3u;
                else if (radius >= 41.0 * 41.0 && radius < 43.0 * 43.0) color = 0xffc1c8d2u;
                else if (radius >= 39.0 * 39.0 && radius < 41.0 * 41.0) color = 0xff626a77u;
                else continue;
                row[x] = color;
            }
        }
    }

    static uint32_t keyImagePixel(const TurntableImage& image, int x, int y, int destinationWidth, int destinationHeight, bool pressed) {
        const int sourceX = std::clamp(x * image.width() / destinationWidth, 0, image.width() - 1);
        const int sourceY = std::clamp(y * image.height() / destinationHeight, 0, image.height() - 1);
        const uint32_t source = image.pixels()[static_cast<size_t>(sourceY) * image.width() + sourceX];
        const unsigned alpha = source >> 24;
        unsigned red = (source >> 16) & 255;
        unsigned green = (source >> 8) & 255;
        unsigned blue = source & 255;
        if (pressed) {
            red = red * 3 / 5;
            green = green * 3 / 5;
            blue = blue * 3 / 5;
        }
        // The deck background is black, so alpha can be applied directly to the image color.
        red = red * alpha / 255;
        green = green * alpha / 255;
        blue = blue * alpha / 255;
        return 0xff000000u | (red << 16) | (green << 8) | blue;
    }

    struct KeyFaceCache {
        const TurntableImage* image{};
        uint64_t generation{};
        int width{};
        std::array<std::vector<uint32_t>, 2> states;
        bool prepare(const TurntableImage* source, int faceWidth) {
            if (!source || !source->loaded() || faceWidth <= 0) return false;
            if (image == source && generation == source->generation() && width == faceWidth) return true;
            image = source; generation = source->generation(); width = faceWidth;
            for (size_t state = 0; state < states.size(); ++state) {
                states[state].resize(static_cast<size_t>(width) * 63);
                for (int y = 0; y < 63; ++y) for (int x = 0; x < width; ++x)
                    states[state][static_cast<size_t>(y) * width + x] = keyImagePixel(*source, x, y, width, 63, state != 0);
            }
            return true;
        }
    };

public:
    static void drawCached(uint32_t* pixels, int width, int height, int fieldX, int laneWidth, int judgeY,
        const std::array<bool, 8>& pressed, double angle, int scratchWidth,
        const TurntableImage* turntable, const TurntableImage* black, const TurntableImage* white) {
        if (!pixels || width <= 0 || height <= 0 || laneWidth <= 0) return;
        struct Cache {
            std::vector<uint32_t> pixels;
            std::array<bool, 8> pressed{};
            std::array<const TurntableImage*, 3> images{};
            std::array<uint64_t, 3> generations{};
            int keyWidth{}, scratchWidth{}, rotation{};
        };
        static thread_local Cache cache;
        const LaneLayout layout{0, laneWidth, scratchWidth};
        constexpr double pi = 3.141592653589793;
        const int rotation = static_cast<int>(std::lround(angle * 360.0 / pi)); // half-degree steps
        const std::array<const TurntableImage*, 3> images{turntable, black, white};
        const std::array<uint64_t, 3> generations{turntable ? turntable->generation() : 0,
            black ? black->generation() : 0, white ? white->generation() : 0};
        if (cache.pixels.empty() || cache.keyWidth != laneWidth || cache.scratchWidth != scratchWidth ||
            cache.pressed != pressed || cache.rotation != rotation || cache.images != images || cache.generations != generations) {
            cache.pixels.assign(static_cast<size_t>(layout.totalWidth()) * 113, 0);
            draw(cache.pixels.data(), layout.totalWidth(), 113, 0, laneWidth, 1, pressed,
                rotation * pi / 360.0, scratchWidth, turntable, black, white);
            cache.keyWidth = laneWidth; cache.scratchWidth = scratchWidth; cache.pressed = pressed;
            cache.rotation = rotation; cache.images = images; cache.generations = generations;
        }
        const int left = std::max(0, fieldX), right = std::min(width, fieldX + layout.totalWidth()), top = judgeY + 4;
        if (left >= right) return;
        // The scratch rim reaches five pixels above the key panel. Preserve the
        // judgement line behind transparent pixels in that small margin.
        for (int y = std::max(0, top - 5); y < std::min(height, top); ++y)
            for (int x = left; x < right; ++x) {
                const auto pixel = cache.pixels[static_cast<size_t>(y - top + 5) * layout.totalWidth() + x - fieldX];
                if (pixel >> 24) pixels[static_cast<size_t>(y) * width + x] = pixel;
            }
        for (int y = std::max(0, top); y < std::min(height, top + 108); ++y)
            std::copy_n(cache.pixels.data() + static_cast<size_t>(y - top + 5) * layout.totalWidth() + left - fieldX,
                right - left, pixels + static_cast<size_t>(y) * width + left);
        for (int y = std::max(0, top + 108); y < height; ++y)
            std::fill_n(pixels + static_cast<size_t>(y) * width + left, right - left, 0xff000000u);
    }
    static void draw(uint32_t* pixels, int width, int height, int fieldX, int laneWidth, int judgeY, const std::array<bool, 8>& pressed, double discAngle = 0, int scratchWidth = 0, const TurntableImage* turntable = nullptr, const TurntableImage* blackKeyImage = nullptr, const TurntableImage* whiteKeyImage = nullptr) {
        if (!pixels || width <= 0 || height <= 0 || laneWidth <= 0) return;
        const int panelTop = judgeY + 4;
        const LaneLayout layout{ fieldX, laneWidth, scratchWidth > 0 ? scratchWidth : laneWidth };
        // Image scaling and pressed-state tinting are invariant between frames.
        static thread_local KeyFaceCache whiteFace, blackFace;
        const bool hasWhiteFace = whiteFace.prepare(whiteKeyImage, layout.width(1) - 8);
        const bool hasBlackFace = blackFace.prepare(blackKeyImage, layout.width(2) - 14);
        const int left = std::max(0, fieldX), right = std::min(width, fieldX + layout.totalWidth());
        if (left >= right) return;
        for (int y = std::max(0, panelTop + 83); y < height; ++y)
            std::fill_n(pixels + static_cast<size_t>(y) * width + left, right - left, 0xff000000u);
        const double cosine = std::cos(discAngle), sine = std::sin(discAngle);
        for (int y = std::max(0, panelTop); y < std::min(height, panelTop + 108); ++y) {
            const int py = y - panelTop;
            for (int lane = 0; lane < 8; ++lane) {
                const int laneLeft = layout.left(lane), laneRight = layout.right(lane);
                const int thisLaneWidth = layout.width(lane);
                for (int x = std::max(0, laneLeft); x < std::min(width, laneRight); ++x) {
                const int px = x - laneLeft;
                uint32_t color = 0xff000000u;
                if (lane == 0) {
                    if (!turntable || !turntable->loaded()) {
                        const double dx = px - (layout.scratchWidth - 1) / 2.0, dy = py - 42.0;
                        const double radius = std::hypot(dx, dy);
                        const double outer = std::min(40.0, layout.scratchWidth * 0.44);
                        if (radius < outer) color = 0xffc4ced4u;
                        if (radius < outer - 2) color = pressed[0] ? 0xff38bdf8u : 0xff698b70u;
                        if (radius < outer - 4) color = 0xff10151cu;
                        if (radius < outer - 8 && radius > outer - 10) color = 0xff353d48u;
                        const double hub = outer * 0.36;
                        if (radius < hub) color = pressed[0] ? 0xff72dcffu : 0xff344eabu;
                        if (radius < hub - 1) color = pressed[0] ? 0xff38bdf8u : 0xff293a78u;
                        if (radius > hub + 2 && radius < outer - 6 && dx * cosine + dy * sine > 0 && std::abs(-dx * sine + dy * cosine) < 1.3) color = 0xffaebdccu;
                    }
                } else {
                    const bool keyPressed = pressed[static_cast<size_t>(lane)];
                    const bool white = lane % 2 != 0;
                    const int keyInset = white ? 4 : 7;
                    if (px >= keyInset && px < thisLaneWidth - keyInset && py >= 7 && py < 70) {
                        const int faceWidth = thisLaneWidth - keyInset * 2;
                        if (white ? hasWhiteFace : hasBlackFace) {
                            const auto& face = white ? whiteFace : blackFace;
                            color = face.states[keyPressed ? 1 : 0][static_cast<size_t>(py - 7) * faceWidth + px - keyInset];
                        } else {
                        int shade{};
                        if (white) {
                            const int start = keyPressed ? 198 : 244;
                            const int bottom = keyPressed ? 92 : 112;
                            const int blend = std::clamp(py - 62, 0, 7);
                            shade = start + (bottom - start) * blend / 7;
                        } else {
                            const int topShade = keyPressed ? 88 : 68;
                            const int bottomShade = keyPressed ? 8 : 0;
                            shade = topShade + (bottomShade - topShade) * (py - 7) / 62;
                        }
                        const uint32_t face = 0xff000000u | (shade << 16) | (shade << 8) | shade;
                        color = face;
                        if (px == keyInset || px == thisLaneWidth - keyInset - 1)
                            color = white ? 0xffb0b0b0u : face;
                        if (py == 7) color = white ? 0xffb0b0b0u : 0xff464646u;
                        if (py == 69) color = white ? 0xff5a5a5au : 0xff000000u;
                        }
                    }
                }
                if (lane == 0 && pressed[0] && py >= 5 && py < 82) {
                    color = 0xff000000u | (((color >> 16) & 255) / 2 << 16) |
                        (((color >> 8) & 255) / 2 << 8) | ((color & 255) / 2);
                }
                pixels[static_cast<size_t>(y) * width + x] = color;
                }
            }
        }
        if (turntable && turntable->loaded()) {
            const int centerX = fieldX + layout.scratchWidth / 2;
            const int centerY = panelTop + 42;
            drawScratchRim(pixels, width, height, centerX, centerY, pressed[0]);
            turntable->draw(pixels, width, height, centerX, centerY, discAngle, 80);
        }
    }
};
