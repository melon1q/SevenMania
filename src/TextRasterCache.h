#pragma once
#include <windows.h>
#include "PerformanceTrace.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <memory>

// Cache GDI glyphs with the same 24-offset thick outline; compose premultiplied
// sprites on subsequent frames, including the existing size animation.
class TextRasterCache {
    struct Sprite {
        std::wstring text; HFONT font{}; COLORREF color{};
        int width{}, height{}; uint64_t used{};
        bool outlined{}, leftAligned{};
        uint64_t id{};
        std::vector<uint32_t> pixels;
    };
    struct Surface {
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP bitmap{}; HGDIOBJ original{}; uint32_t* pixels{};
        Surface(int width, int height) {
            BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
            bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
            if (bitmap) original = SelectObject(dc, bitmap);
        }
        ~Surface() { if (original) SelectObject(dc, original); if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc); }
    };
    HDC measure_{};
    std::unique_ptr<Surface> black_, white_;
    int surfaceWidth_{},surfaceHeight_{};
    struct Axis { int first{}, second{}; unsigned weight{}; };
    std::vector<Sprite> sprites_;
    std::vector<Axis> xAxis_, yAxis_;
    uint64_t clock_{};
    uint64_t nextSpriteId_{};
    static uint32_t mix(uint32_t a, uint32_t b, unsigned weight) {
        const unsigned inverse = 256 - weight;
        return (((((a & 0x00ff00ffu) * inverse + (b & 0x00ff00ffu) * weight) >> 8) & 0x00ff00ffu)) |
            ((((((a >> 8) & 0x00ff00ffu) * inverse + ((b >> 8) & 0x00ff00ffu) * weight) >> 8) & 0x00ff00ffu) << 8);
    }
    static void axis(std::vector<Axis>& output, int source, int destination) {
        output.resize(static_cast<size_t>(destination));
        for (int i = 0; i < destination; ++i) {
            const double p = std::clamp((i + 0.5) * source / destination - 0.5, 0.0, source - 1.0);
            const int first = static_cast<int>(p);
            output[static_cast<size_t>(i)] = {first, std::min(first + 1, source - 1), static_cast<unsigned>(std::lround((p - first) * 256))};
        }
    }
    static void blend(uint32_t& target, uint32_t source) {
        const unsigned alpha = source >> 24;
        if (alpha == 0) return;
        if (alpha == 255) { target = source; return; }
        const unsigned inverse = 255 - alpha;
        const unsigned r = ((source >> 16) & 255) + (((target >> 16) & 255) * inverse + 127) / 255;
        const unsigned g = ((source >> 8) & 255) + (((target >> 8) & 255) * inverse + 127) / 255;
        const unsigned b = (source & 255) + ((target & 255) * inverse + 127) / 255;
        const unsigned combinedAlpha = alpha + ((target >> 24) * inverse + 127) / 255;
        target = (combinedAlpha << 24) | (std::min(r, 255u) << 16) | (std::min(g, 255u) << 8) | std::min(b, 255u);
    }
public:
    ~TextRasterCache() { if(measure_) DeleteDC(measure_); }
    using GpuDraw = std::function<bool(const uint32_t*, int, int, int, int, int, int, uint64_t)>;
    void clear() { std::vector<Sprite>().swap(sprites_); std::vector<Axis>().swap(xAxis_); std::vector<Axis>().swap(yAxis_); clock_ = 0; }
    template<class Renderer> bool draw(uint32_t* target, int width, int height, const std::wstring& text, RECT rect,
        HFONT font, COLORREF color, double scale, Renderer render, bool outlined = true, const GpuDraw& gpu = {}, bool leftAligned = false, bool fitWidth = false) {
        if ((!target && !gpu) || text.empty() || width <= 0 || height <= 0 || scale <= 0) return false;
        auto found = std::find_if(sprites_.begin(), sprites_.end(), [&](const Sprite& item) { return item.text == text && item.font == font && item.color == color && item.outlined == outlined && item.leftAligned == leftAligned; });
        if (found == sprites_.end()) {
            BMS_PERF_COUNT(textMisses);
            if(!measure_) measure_=CreateCompatibleDC(nullptr);
            HDC measure=measure_; if(!measure) return false;
            const auto oldFont=font?SelectObject(measure,font):nullptr;
            SIZE size{}; TEXTMETRICW metrics{};
            GetTextExtentPoint32W(measure, text.c_str(), static_cast<int>(text.size()), &size); GetTextMetricsW(measure, &metrics);
            if(oldFont) SelectObject(measure,oldFont);
            Sprite sprite; sprite.text = text; sprite.font = font; sprite.color = color; sprite.outlined = outlined; sprite.leftAligned = leftAligned; sprite.id = ++nextSpriteId_;
            sprite.width = std::max(1L, size.cx) + 16; sprite.height = std::max(1L, metrics.tmHeight) + 16;
            if (sprite.width > 4096 || sprite.height > 512) return false;
            if(!black_ || !white_ || surfaceWidth_!=sprite.width || surfaceHeight_!=sprite.height) {
                auto black = std::make_unique<Surface>(sprite.width, sprite.height);
                auto white = std::make_unique<Surface>(sprite.width, sprite.height);
                if (!black->dc || !white->dc || !black->pixels || !white->pixels) return false;
                black_ = std::move(black);
                white_ = std::move(white);
                surfaceWidth_=sprite.width;surfaceHeight_=sprite.height;
            }
            auto& black=*black_;auto& white=*white_;
            if (!black.dc || !white.dc || !black.pixels || !white.pixels) return false;
            const size_t count = static_cast<size_t>(sprite.width) * sprite.height;
            std::fill_n(black.pixels, count, 0xff000000u); std::fill_n(white.pixels, count, 0xffffffffu);
            const RECT bounds{0, 0, sprite.width, sprite.height};
            render(black.dc, text, bounds, font, color); render(white.dc, text, bounds, font, color); GdiFlush();
            sprite.pixels.resize(count);
            for (size_t i = 0; i < count; ++i) {
                const uint32_t a = black.pixels[i], b = white.pixels[i];
                const int difference = std::min({std::clamp(int((b >> 16) & 255) - int((a >> 16) & 255), 0, 255),
                    std::clamp(int((b >> 8) & 255) - int((a >> 8) & 255), 0, 255), std::clamp(int(b & 255) - int(a & 255), 0, 255)});
                sprite.pixels[i] = (static_cast<uint32_t>(255 - difference) << 24) | (a & 0xffffffu);
            }
            // Preoutlined HUD sprites need no additional outline pass.
            if (outlined) {
            constexpr POINT offsets[24]{{-2,0},{2,0},{0,-2},{0,2},{-1,-1},{1,-1},{-1,1},{1,1},
                {-4,0},{4,0},{0,-4},{0,4},{-3,-3},{3,-3},{-3,3},{3,3},
                {-6,0},{6,0},{0,-6},{0,6},{-5,-5},{5,-5},{-5,5},{5,5}};
            std::vector<unsigned char> outline(count, 0);
            for (int y = 0; y < sprite.height; ++y) for (int x = 0; x < sprite.width; ++x) {
                const unsigned alpha = sprite.pixels[static_cast<size_t>(y) * sprite.width + x] >> 24;
                if (!alpha || !outlined) continue;
                for (const auto& offset : offsets) {
                    const int xx = x + offset.x, yy = y + offset.y;
                    if (xx < 0 || yy < 0 || xx >= sprite.width || yy >= sprite.height) continue;
                    auto& coverage = outline[static_cast<size_t>(yy) * sprite.width + xx];
                    if (coverage != 255) coverage = static_cast<unsigned char>(coverage + ((255 - coverage) * alpha + 127) / 255);
                }
            }
            for (size_t i = 0; i < count; ++i) {
                const uint32_t body = sprite.pixels[i]; const unsigned inverse = 255 - (body >> 24);
                const unsigned alpha = (body >> 24) + (outline[i] * inverse + 127) / 255;
                const unsigned r = ((body >> 16) & 255) + ((GetRValue(color) / 3 * outline[i] + 127) / 255 * inverse + 127) / 255;
                const unsigned g = ((body >> 8) & 255) + ((GetGValue(color) / 3 * outline[i] + 127) / 255 * inverse + 127) / 255;
                const unsigned b = (body & 255) + ((GetBValue(color) / 3 * outline[i] + 127) / 255 * inverse + 127) / 255;
                sprite.pixels[i] = (alpha << 24) | (std::min(r, 255u) << 16) | (std::min(g, 255u) << 8) | std::min(b, 255u);
            }
            }
            if (sprites_.size() == 32) sprites_.erase(std::min_element(sprites_.begin(), sprites_.end(), [](const Sprite& a, const Sprite& b) { return a.used < b.used; }));
            sprites_.push_back(std::move(sprite)); found = std::prev(sprites_.end());
        }
        found->used = ++clock_;
        if (fitWidth) scale = std::min(scale, std::max(1L, rect.right - rect.left) / static_cast<double>(found->width));
        const int scaledWidth = std::max(1, static_cast<int>(std::lround(found->width * scale)));
        const int scaledHeight = std::max(1, static_cast<int>(std::lround(found->height * scale)));
        const int left = leftAligned ? rect.left : (rect.left + rect.right - scaledWidth) / 2;
        const int top = (rect.top + rect.bottom - scaledHeight) / 2;
        if (gpu && gpu(found->pixels.data(), found->width, found->height, left, top, scaledWidth, scaledHeight, found->id)) return true;
        if (!target) return false;
        const int fromX = std::max(0, left), toX = std::min(width, left + scaledWidth);
        const int fromY = std::max(0, top), toY = std::min(height, top + scaledHeight);
        const bool unscaled = scaledWidth == found->width && scaledHeight == found->height;
        if (!unscaled) { axis(xAxis_, found->width, scaledWidth); axis(yAxis_, found->height, scaledHeight); }
        for (int y = fromY; y < toY; ++y) {
            auto* row = target + static_cast<size_t>(y) * width;
            for (int x = fromX; x < toX; ++x) {
                uint32_t pixel;
                if (unscaled) pixel = found->pixels[static_cast<size_t>(y - top) * found->width + x - left];
                else {
                    const auto& xx = xAxis_[static_cast<size_t>(x - left)]; const auto& yy = yAxis_[static_cast<size_t>(y - top)];
                    const auto* first = found->pixels.data() + static_cast<size_t>(yy.first) * found->width;
                    const auto* second = found->pixels.data() + static_cast<size_t>(yy.second) * found->width;
                    pixel = mix(mix(first[xx.first], first[xx.second], xx.weight), mix(second[xx.first], second[xx.second], xx.weight), yy.weight);
                }
                blend(row[x], pixel);
            }
        }
        return true;
    }
};
