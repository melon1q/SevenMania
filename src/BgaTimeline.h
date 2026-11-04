#pragma once

#include "BmsParser.h"
#include "ChartRules.h"

#include <algorithm>
#include <limits>

namespace bms {

// Only the standalone video has a local clock. BMS image events already use
// chart time and must never have this offset applied to them a second time.
inline double bgaVideoStartSeconds(const Chart& chart) {
    double first = std::numeric_limits<double>::infinity();
    if (!chart.fallbackVideo.empty()) {
        auto moviePath = chart.fallbackVideo.lexically_normal();
        moviePath.replace_extension();
        const auto movie = toLower(moviePath.generic_wstring());
        for (const auto& event : chart.bga) {
            if (event.layer == BgaLayer::Poor) continue;
            const auto asset = chart.bitmaps.find(assetId(chart,event.imageId));
            if (asset == chart.bitmaps.end()) continue;
            const auto extension = toLower(asset->second.extension().wstring());
            if (extension != L".mp4" && extension != L".m4v" && extension != L".mov" && extension != L".wmv" &&
                extension != L".avi" && extension != L".mpeg" && extension != L".mpg") continue;
            auto declaredPath = asset->second.lexically_normal();
            declaredPath.replace_extension();
            // An MP4 conversion of the declared MPEG is the same BGA and must
            // inherit its authored start event instead of falling back to BGM.
            if (toLower(declaredPath.generic_wstring()) == movie)
                first = std::min(first, event.timeSeconds);
        }
        if (first != std::numeric_limits<double>::infinity()) return std::max(0.0, first);
    }
    // An undeclared fallback follows the first sound, including a keysound
    // before the first BGM event. Missing/undefined samples are not audible.
    for (const auto& event : chart.bgm)
        if (chart.samples.empty() || chart.samples.contains(event.sampleId)) first = std::min(first, event.timeSeconds);
    for (const auto& note : chart.notes)
        if (chart.samples.empty() || chart.samples.contains(note.sampleId)) first = std::min(first, note.timeSeconds);
    return first == std::numeric_limits<double>::infinity() ? 0.0 : std::max(0.0, first);
}

inline double bgaTimelineSeconds(const Chart& chart, double chartSeconds) {
    return chartSeconds - bgaVideoStartSeconds(chart);
}

} // namespace bms
