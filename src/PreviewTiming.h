#pragma once
#include "BmsParser.h"
#include <array>
#include <algorithm>
#include <cmath>

namespace bms {
inline size_t sharePreviewTiming(std::vector<Chart>& charts, const std::vector<size_t>& indices) {
    if (indices.empty()) return charts.size();
    size_t representative = indices.front();
    for (const auto index : indices) {
        if (charts[index].previewStartDefined && (charts[index].durationSeconds <= 0 || charts[index].previewStartSeconds < charts[index].durationSeconds)) {
            representative = index; break;
        }
    }
    const auto start = charts[representative].previewStartSeconds;
    const auto length = charts[representative].previewDurationSeconds;
    for (const auto index : indices) { charts[index].previewStartSeconds = start; charts[index].previewDurationSeconds = length; }
    return representative;
}
// Prefer a dense musical section, weighted away from the intro and ending.
// Explicit chart timing and pre-edited preview files take priority over this estimate.
inline double automaticPreviewStart(const Chart& chart) {
    double length = 0;
    for (const auto& note : chart.notes) length = std::max(length, note.timeSeconds + note.lengthSeconds);
    if (!std::isfinite(length) || length <= chart.previewDurationSeconds) return 0;
    std::array<unsigned, 100> bins{};
    for (const auto& note : chart.notes) {
        if (!std::isfinite(note.timeSeconds) || note.timeSeconds < 0) continue;
        ++bins[static_cast<size_t>(std::clamp(static_cast<int>(note.timeSeconds / length * 100), 0, 99))];
    }
    double best = -1, start = 0;
    for (size_t i = 1; i < bins.size(); ++i) {
        const double relative = (static_cast<double>(i) - 50) / 50;
        const double score = bins[i] * std::sqrt(std::max(0.0, 1 - relative * relative));
        if (score > best) { best = score; start = length * (i - 1) / 100; }
    }
    return std::clamp(start, 0.0, std::max(0.0, length - std::min(chart.previewDurationSeconds, length)));
}
}
