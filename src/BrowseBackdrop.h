#pragma once
#include "AsyncVideoBga.h"
#include "SelectedBgaMetadata.h"
#include <array>

class BrowseBackdrop {
    BgaRaster raster_;
    SelectedBgaMetadata metadata_;
    AsyncVideoBga video_;
    std::filesystem::path chartPath_, videoPath_;
    std::vector<uint32_t> small_, pixels_;
    std::wstring cacheKey_;
    uint64_t videoGeneration_{}, revision_{};
    static bool movie(const std::filesystem::path& path) {
        const auto extension = bms::toLower(path.extension().wstring());
        return extension == L".mp4" || extension == L".mpeg" || extension == L".mpg" || extension == L".wmv" ||
            extension == L".avi" || extension == L".mov" || extension == L".m4v";
    }
public:
    uint64_t revision() const noexcept { return revision_; }
    void reset() { metadata_.reset(); ++revision_; video_.reset(); raster_.reset(); chartPath_.clear(); videoPath_.clear(); cacheKey_.clear();
        std::vector<uint32_t>().swap(pixels_); std::vector<uint32_t>().swap(small_); }
    const std::vector<uint32_t>& render(const bms::Chart* chart, int mode, const std::filesystem::path& custom,
        int blur, int opacity, int width, int height, double time) {
        if (mode == 0 || width <= 0 || height <= 0) { if (!videoPath_.empty()) reset(); return empty(); }
        const auto chartPath = chart ? chart->path : std::filesystem::path{};
        if (chartPath != chartPath_) { reset(); chartPath_ = chartPath; }
        std::shared_ptr<const bms::Chart> enriched;
        if(chart&&chart->catalogOnly&&mode==1){enriched=metadata_.get(chart->path);if(enriched)chart=enriched.get();}
        bms::Chart imageChart;
        std::array<std::wstring, 4> layers{};
        auto desiredVideo = std::filesystem::path{};
        double videoTime = time;
        if (mode == 3) { imageChart.bitmaps[L"background"] = custom; layers[0] = L"background"; }
        else if (chart && mode == 2) { imageChart.bitmaps[L"background"] = chart->titleImage; layers[0] = L"background"; }
        else if (chart && mode == 1) {
            imageChart.bitmaps = chart->bitmaps;
            imageChart.bgaClips = chart->bgaClips;
            if (movie(chart->fallbackVideo)) desiredVideo = chart->fallbackVideo;
            else if (!chart->fallbackVideo.empty()) { imageChart.bitmaps[L"background"] = chart->fallbackVideo; layers[0] = L"background"; }
            for (const auto& event : chart->bga) {
                if (event.timeSeconds > time) break;
                const int layer = static_cast<int>(event.layer);
                if (layer == 1) continue;
                layers[static_cast<size_t>(layer)] = event.imageId;
                const auto asset = chart->bitmaps.find(event.imageId);
                if (layer == 0 && asset != chart->bitmaps.end() && movie(asset->second)) {
                    desiredVideo = asset->second; videoTime = time - event.timeSeconds;
                }
            }
        } else return empty();
        const int factor = 1 + std::clamp(blur, 0, 100) / 5;
        const int sw = std::max(1, width / factor), sh = std::max(1, height / factor);
        if (desiredVideo != videoPath_) {
            video_.reset(); videoPath_ = desiredVideo;
            if (!videoPath_.empty()) video_.open(videoPath_, sw, sh);
            cacheKey_.clear(); videoGeneration_ = 0;
        }
        if (!videoPath_.empty()) video_.request(videoTime, sw, sh);
        const auto frame = video_.frame();
        std::wstring key = std::to_wstring(mode) + L":" + custom.wstring() + L":" + std::to_wstring(blur) + L":" +
            std::to_wstring(opacity) + L":" + std::to_wstring(width) + L":" + std::to_wstring(height);
        for (const auto& layer : layers) key += L"|" + layer;
        const uint64_t generation = frame ? frame->generation : 0;
        if (key == cacheKey_ && generation == videoGeneration_) return pixels_;
        cacheKey_ = std::move(key); videoGeneration_ = generation; ++revision_;
        small_.assign(static_cast<size_t>(sw) * sh, 0xff404040u);
        raster_.canvas(sw, sh);
        if (frame && frame->width == sw && frame->height == sh) small_ = frame->pixels;
        for (int layer : {0, 2, 3}) if (!layers[layer].empty()) raster_.draw(imageChart, layers[layer], small_.data());
        if (factor == 1) pixels_ = small_;
        else BgaRaster::scaleVideoFrame(sw, sh, small_, width, height, pixels_);
        const unsigned alpha = static_cast<unsigned>(std::clamp(opacity, 0, 100)) * 255 / 100;
        for (auto& pixel : pixels_) {
            const unsigned r = (((pixel >> 16) & 255) * alpha + 64 * (255 - alpha)) / 255;
            const unsigned g = (((pixel >> 8) & 255) * alpha + 64 * (255 - alpha)) / 255;
            const unsigned b = ((pixel & 255) * alpha + 64 * (255 - alpha)) / 255;
            pixel = 0xff000000u | (r << 16) | (g << 8) | b;
        }
        return pixels_;
    }
private:
    static const std::vector<uint32_t>& empty() { static const std::vector<uint32_t> value; return value; }
};
