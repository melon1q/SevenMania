#pragma once

#include "BmsParser.h"
#include <algorithm>
#include <iterator>

namespace bms {

// Position in equivalent seconds at the chart's initial BPM. Only the visual
// scroll uses this clock; judgement, audio and movies keep real chart time.
inline double scrollPositionSeconds(const Chart& chart, double chartSeconds) {
    if (chart.scroll.empty()) return chartSeconds;
    const auto next = std::upper_bound(chart.scroll.begin(), chart.scroll.end(), chartSeconds,
        [](double time, const ScrollEvent& event) { return time < event.timeSeconds; });
    const auto& event = next == chart.scroll.begin() ? chart.scroll.front() : *std::prev(next);
    return event.positionSeconds + (chartSeconds - event.timeSeconds) * event.speed + 0.5 * (chartSeconds-event.timeSeconds)*(chartSeconds-event.timeSeconds)*event.acceleration;
}

} // namespace bms
