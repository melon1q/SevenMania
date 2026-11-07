#pragma once
#include "BmsParser.h"
#include "VideoFrameScaler.h"
#include "ChartRules.h"
#include "ChartOptions.h"
#include "PerformanceTrace.h"
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>
#include <emmintrin.h>
#include <memory>

// Decode and resize each asset once, then compose premultiplied pixels without
// GDI+'s per-event full-screen software DrawImage resampling.
class BgaRaster {
    friend class AsyncBgaRaster;
    struct Image {
        int width{}, height{};
        bool opaque = true;
        std::vector<uint32_t> pixels;
        unsigned long long lastUsed{};
    };
    struct Scaled : Image { int x{}, y{}; };
    std::unordered_map<std::wstring, Image> sources_;
    std::unordered_map<std::wstring, std::shared_ptr<Scaled>> scaled_;
    size_t sourceBytes_{}, scaledBytes_{};
    int width_{}, height_{};
    unsigned long long clock_{};
    bool backgroundDecode_{};
    static constexpr size_t sourceLimit = 64ull * 1024 * 1024;
    static constexpr size_t scaledLimit = 256ull * 1024 * 1024;

    static const Image& cacheImage(const Image& image) { return image; }
    static const Image& cacheImage(const std::shared_ptr<Scaled>& image) { return *image; }
    template<class Cache> static void makeRoom(Cache& cache, size_t& bytes, size_t needed, size_t limit) {
        while (!cache.empty() && bytes + needed > limit) {
            const auto oldest = std::min_element(cache.begin(), cache.end(),
                [](const auto& a, const auto& b) { return cacheImage(a.second).lastUsed < cacheImage(b.second).lastUsed; });
            bytes -= cacheImage(oldest->second).pixels.size() * sizeof(uint32_t);
            cache.erase(oldest);
        }
    }
    Image* source(const std::filesystem::path& path) {
        if(backgroundDecode_)return decodeSource(path);
        BMS_PERF_SCOPE(decodeMs);
        return decodeSource(path);
    }
    Image* decodeSource(const std::filesystem::path& path) {
        const auto extension = bms::toLower(path.extension().wstring());
        if (extension == L".mp4" || extension == L".mpeg" || extension == L".mpg" || extension == L".wmv" || extension == L".avi" || extension == L".mov" || extension == L".m4v" || extension==L".m1v" || extension==L".m2v") return nullptr;
        const auto key = path.wstring();
        if (auto found = sources_.find(key); found != sources_.end()) {
            found->second.lastUsed = ++clock_; return &found->second;
        }
        Gdiplus::Bitmap file(path.c_str(), FALSE);
        const int width = static_cast<int>(file.GetWidth()), height = static_cast<int>(file.GetHeight());
        if (file.GetLastStatus() != Gdiplus::Ok || width <= 0 || height <= 0 ||
            static_cast<uint64_t>(width) * height * 4 > sourceLimit) return nullptr;
        std::unique_ptr<Gdiplus::Bitmap> decoded(file.Clone(0, 0, width, height, PixelFormat32bppPARGB));
        if (!decoded || decoded->GetLastStatus() != Gdiplus::Ok) return nullptr;
        Gdiplus::BitmapData data{};
        const Gdiplus::Rect rect(0, 0, width, height);
        if (decoded->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &data) != Gdiplus::Ok) return nullptr;
        Image image; image.width = width; image.height = height;
        image.pixels.resize(static_cast<size_t>(width) * height);
        for (int y = 0; y < height; ++y) {
            const auto* row = reinterpret_cast<const uint32_t*>(static_cast<const BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride);
            std::copy_n(row, width, image.pixels.data() + static_cast<size_t>(y) * width);
        }
        decoded->UnlockBits(&data);
        image.opaque = std::all_of(image.pixels.begin(), image.pixels.end(), [](uint32_t pixel) { return (pixel >> 24) == 255; });
        image.lastUsed = ++clock_;
        const size_t bytes = image.pixels.size() * 4;
        makeRoom(sources_, sourceBytes_, bytes, sourceLimit);
        sourceBytes_ += bytes;
        return &sources_.emplace(key, std::move(image)).first->second;
    }
    static uint32_t mix(uint32_t a, uint32_t b, unsigned weight) {
        const unsigned inverse = 256 - weight;
        const uint32_t rb = (((a & 0x00ff00ffu) * inverse + (b & 0x00ff00ffu) * weight) >> 8) & 0x00ff00ffu;
        const uint32_t ga = ((((a >> 8) & 0x00ff00ffu) * inverse + ((b >> 8) & 0x00ff00ffu) * weight) >> 8) & 0x00ff00ffu;
        return rb | (ga << 8);
    }
    struct Axis { int first{}, second{}; unsigned weight{}; };
    static std::vector<Axis> axis(int sourceLength, int targetLength) {
        std::vector<Axis> result(static_cast<size_t>(targetLength));
        for (int i = 0; i < targetLength; ++i) {
            const double position = std::clamp((i + 0.5) * sourceLength / targetLength - 0.5, 0.0, static_cast<double>(sourceLength - 1));
            const int first = static_cast<int>(position);
            result[static_cast<size_t>(i)] = { first, std::min(first + 1, sourceLength - 1),
                static_cast<unsigned>(std::lround((position - first) * 256)) };
        }
        return result;
    }
    static void resize(const Image& sourceImage, int sx, int sy, int sw, int sh, Scaled& result) {
        result.pixels.resize(static_cast<size_t>(result.width) * result.height);
        const auto xAxis = axis(sw, result.width), yAxis = axis(sh, result.height);
        std::vector<uint32_t> firstRow(static_cast<size_t>(result.width)), secondRow(firstRow.size());
        int previousFirst = -1, previousSecond = -1;
        const auto horizontal = [&](int y, std::vector<uint32_t>& row) {
            const auto* input = sourceImage.pixels.data() + static_cast<size_t>(sy + y) * sourceImage.width + sx;
            for (int x = 0; x < result.width; ++x) {
                const auto& position = xAxis[static_cast<size_t>(x)];
                row[static_cast<size_t>(x)] = mix(input[position.first], input[position.second], position.weight);
            }
        };
        for (int y = 0; y < result.height; ++y) {
            const auto& position = yAxis[static_cast<size_t>(y)];
            if (position.first != previousFirst) {
                if (position.first == previousSecond) firstRow.swap(secondRow);
                else horizontal(position.first, firstRow);
                previousFirst = position.first;
                previousSecond = -1;
            }
            if (position.second != previousSecond) { horizontal(position.second, secondRow); previousSecond = position.second; }
            auto* output = result.pixels.data() + static_cast<size_t>(y) * result.width;
            for (int x = 0; x < result.width; ++x)
                output[x] = mix(firstRow[static_cast<size_t>(x)], secondRow[static_cast<size_t>(x)], position.weight);
        }
    }
    Scaled* scaled(const bms::Chart& chart, const std::wstring& imageId, bool blackKey = false, const bms::BgaColor* layerColor = nullptr) {
        const auto id=bms::assetId(chart,imageId);
        const auto definedColor=chart.bitmapColors.find(id);
        const auto* color=layerColor?layerColor:(definedColor==chart.bitmapColors.end()?nullptr:&definedColor->second);
        if(color)blackKey=true;
        const auto key=id+(blackKey?L"|black":L"")+(color?L"|"+std::to_wstring(color->red)+L","+std::to_wstring(color->green)+L","+std::to_wstring(color->blue):L"");
        if (auto found = scaled_.find(key); found != scaled_.end()) {
            found->second->lastUsed = ++clock_; return found->second.get();
        }
        const auto clip = chart.bgaClips.find(id);
        const auto bitmapId = clip == chart.bgaClips.end() ? id : bms::assetId(chart,clip->second.bitmapId);
        const auto file = chart.bitmaps.find(bitmapId);
        if (file == chart.bitmaps.end()) return nullptr;
        const Image* image = source(file->second);
        if (!image) return nullptr;
        int sx = 0, sy = 0, sw = image->width, sh = image->height;
        Scaled result; result.width = width_; result.height = height_; result.opaque = image->opaque;
        if (clip != chart.bgaClips.end() && clip->second.cropped) {
            const auto& crop = clip->second;
            sx = std::clamp(crop.x, 0, image->width); sy = std::clamp(crop.y, 0, image->height);
            sw=std::clamp(crop.width-std::max(0,-crop.x),0,image->width-sx);sh=std::clamp(crop.height-std::max(0,-crop.y),0,image->height-sy);
            if (sw <= 0 || sh <= 0) return nullptr;
            result.x = static_cast<int>(std::lround((crop.destinationX+std::max(0,-crop.x)) * width_ / 256.0));
            result.y = static_cast<int>(std::lround((crop.destinationY+std::max(0,-crop.y)) * height_ / 256.0));
            result.width = std::max(1, static_cast<int>(std::lround(sw * width_ / 256.0)));
            result.height = std::max(1, static_cast<int>(std::lround(sh * height_ / 256.0)));
        }
        if(clip==chart.bgaClips.end()) {
            const int mode=bms::forcedOptions(chart).bgaMode;
            if(mode>0) {
                const double scale=mode==2?.8:1;
                result.width=std::max(1,static_cast<int>(image->width*scale));result.height=std::max(1,static_cast<int>(image->height*scale));
                result.x=(width_-result.width)/2;result.y=0;
            }
        }
        const uint64_t bytes = static_cast<uint64_t>(result.width) * result.height * 4;
        if (bytes > scaledLimit) return nullptr;
        if(blackKey) {
            Image transparent=*image;transparent.opaque=false;
            for(auto& pixel:transparent.pixels)if(
                int((pixel>>16)&255)<=(color?color->red:0) && int((pixel>>8)&255)<=(color?color->green:0) && int(pixel&255)<=(color?color->blue:0))pixel=0;
            resize(transparent,sx,sy,sw,sh,result);
            result.opaque = std::all_of(result.pixels.begin(),result.pixels.end(),
                [](uint32_t pixel) { return (pixel >> 24) == 255; });
        } else resize(*image,sx,sy,sw,sh,result);
        result.lastUsed = ++clock_;
        makeRoom(scaled_, scaledBytes_, static_cast<size_t>(bytes), scaledLimit);
        scaledBytes_ += static_cast<size_t>(bytes);
        return scaled_.emplace(key, std::make_shared<Scaled>(std::move(result))).first->second.get();
    }
public:
    void reset() { sources_.clear(); scaled_.clear(); sourceBytes_ = scaledBytes_ = 0; width_ = height_ = 0; }
    void canvas(int width, int height) {
        if (width_ != width || height_ != height) { scaled_.clear(); scaledBytes_ = 0; width_ = width; height_ = height; }
    }
    bool draw(const bms::Chart& chart, const std::wstring& imageId, uint32_t* target, unsigned opacity = 255, bool blackKey = false, const bms::BgaColor* color = nullptr) {
        if (imageId.empty()) return false;
        if (opacity == 0) return true;
        opacity = std::min(opacity, 255u);
        const auto defined=chart.bitmapColors.find(bms::assetId(chart,imageId));
        if(defined!=chart.bitmapColors.end())opacity=opacity*defined->second.alpha/255;
        if(color)opacity=opacity*color->alpha/255;
        try {
        const Scaled* image = scaled(chart, imageId, blackKey, color);
        if (!image) return false;
        const int left = std::max(0, image->x), top = std::max(0, image->y);
        const int right = std::min(width_, image->x + image->width), bottom = std::min(height_, image->y + image->height);
        if (left >= right || top >= bottom) return true;
        for (int y = top; y < bottom; ++y) {
            const auto* input = image->pixels.data() + static_cast<size_t>(y - image->y) * image->width + left - image->x;
            auto* output = target + static_cast<size_t>(y) * width_ + left;
            if (image->opaque && opacity == 255) std::copy_n(input, right - left, output);
            else for (int x = 0; x < right - left; ++x) {
                const uint32_t pixel = input[x];
                const unsigned alpha = ((pixel >> 24) * opacity + 127) / 255;
                if (alpha == 255) output[x] = pixel;
                else if (alpha != 0) {
                    const auto previous = output[x]; const unsigned inverse = 255 - alpha;
                    const unsigned r = ((((pixel >> 16) & 255) * opacity + 127) / 255) + (((previous >> 16) & 255) * inverse + 127) / 255;
                    const unsigned g = ((((pixel >> 8) & 255) * opacity + 127) / 255) + (((previous >> 8) & 255) * inverse + 127) / 255;
                    const unsigned b = (((pixel & 255) * opacity + 127) / 255) + ((previous & 255) * inverse + 127) / 255;
                    output[x] = 0xff000000u | (r << 16) | (g << 8) | b;
                }
            }
        }
        return true;
        } catch (...) {
            // BGA is optional; malformed images must not end audio/gameplay.
            return false;
        }
    }
    bool coversCanvas(const bms::Chart& chart, const std::wstring& imageId) {
        if (imageId.empty()) return false;
        try {
            const auto* image = scaled(chart, imageId);
            return image && image->opaque && image->x <= 0 && image->y <= 0 &&
                image->x + image->width >= width_ && image->y + image->height >= height_;
        } catch (...) { return false; }
    }
    static bool scaleVideoFrame(int sourceWidth, int sourceHeight, const std::vector<uint32_t>& pixels, int targetWidth, int targetHeight, std::vector<uint32_t>& output) {
        VideoFrameScaler scaler;
        return scaler.scale(sourceWidth, sourceHeight, pixels, targetWidth, targetHeight, output);
    }
    bool drawVideoFrame(int sourceWidth, int sourceHeight, const std::vector<uint32_t>& pixels, uint32_t* target) {
        std::vector<uint32_t> scaledFrame;
        if (!scaleVideoFrame(sourceWidth, sourceHeight, pixels, width_, height_, scaledFrame)) return false;
        std::copy(scaledFrame.begin(), scaledFrame.end(), target);
        return true;
    }
    static void tint(uint32_t* pixels, int width, int height, RECT rect, uint32_t color, unsigned alpha) {
        if (!pixels || width <= 0 || height <= 0 || alpha == 0) return;
        rect.left = std::clamp(static_cast<int>(rect.left), 0, width); rect.right = std::clamp(static_cast<int>(rect.right), 0, width);
        rect.top = std::clamp(static_cast<int>(rect.top), 0, height); rect.bottom = std::clamp(static_cast<int>(rect.bottom), 0, height);
        if (rect.left >= rect.right || rect.top >= rect.bottom) return;
        const unsigned weight = (alpha * 256 + 127) / 255;
        const bool black = (color & 0xffffffu) == 0;
        const auto zero = _mm_setzero_si128();
        const auto inverse = _mm_set1_epi16(static_cast<short>(256 - weight));
        const short alphaContribution = static_cast<short>((color >> 24) * weight);
        const auto addAlpha = _mm_set_epi16(alphaContribution, 0, 0, 0, alphaContribution, 0, 0, 0);
        for (int y = rect.top; y < rect.bottom; ++y) {
            auto* row = pixels + static_cast<size_t>(y) * width;
            if (alpha == 255) { std::fill(row + rect.left, row + rect.right, color); continue; }
            int x = rect.left;
            if (black) for (; x + 4 <= rect.right; x += 4) {
                const auto input = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + x));
                const auto low = _mm_srli_epi16(_mm_add_epi16(_mm_mullo_epi16(_mm_unpacklo_epi8(input, zero), inverse), addAlpha), 8);
                const auto high = _mm_srli_epi16(_mm_add_epi16(_mm_mullo_epi16(_mm_unpackhi_epi8(input, zero), inverse), addAlpha), 8);
                _mm_storeu_si128(reinterpret_cast<__m128i*>(row + x), _mm_packus_epi16(low, high));
            }
            for (; x < rect.right; ++x) row[x] = mix(row[x], color, weight);
        }
    }
};
