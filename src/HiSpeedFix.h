#pragma once

#include "BmsParser.h"
#include "ScrollTimeline.h"
#include <algorithm>
#include <map>

namespace bms {

// HI-SPEED FIX. The scroll clock already runs in seconds at the chart's reference BPM (#BASEBPM, else the
// start BPM), so without a fix a chart scrolls at the same speed at its start BPM whatever that BPM is.
// Start/Max/Main/Min pick another BPM to scroll at that speed; Constant ignores BPM changes, stops,
// #SCROLL and #SPEED, so notes move at one speed for the whole song.
enum class HiSpeedFix { Off, Start, Max, Main, Min, Constant };
constexpr int hiSpeedFixCount = 6;

inline double scrollReferenceBpm(const Chart& chart) {
    return chart.referenceBpm > 0 ? chart.referenceBpm : chart.initialBpm;
}

// BPM the chart spends the most time at, up to its last note; the start BPM for a chart without tempo events.
inline double mainBpm(const Chart& chart) {
    double end = 0;
    for (const auto& note : chart.notes) end = std::max(end, note.timeSeconds + std::max(0.0, note.lengthSeconds));
    std::map<double, double> seconds;
    double bpm = chart.initialBpm, from = 0;
    for (const auto& tempo : chart.tempos) {
        if (tempo.timeSeconds > from) {
            seconds[bpm] += std::min(tempo.timeSeconds, std::max(end, from)) - from;
            from = tempo.timeSeconds;
        }
        bpm = tempo.bpm;
    }
    if (end > from) seconds[bpm] += end - from;
    double best = chart.initialBpm, longest = -1;
    for (const auto& [value, duration] : seconds)
        if (value > 0 && duration > longest) { best = value; longest = duration; }
    return best;
}

// The BPM a fix holds at the base speed; 0 for Off and Constant.
inline double hiSpeedFixBpm(const Chart& chart, HiSpeedFix fix) {
    double low = chart.initialBpm, high = chart.initialBpm;
    for (const auto& tempo : chart.tempos)
        if (tempo.bpm > 0) { low = std::min(low, tempo.bpm); high = std::max(high, tempo.bpm); }
    switch (fix) {
    case HiSpeedFix::Start: return chart.initialBpm;
    case HiSpeedFix::Max: return high;
    case HiSpeedFix::Main: return mainBpm(chart);
    case HiSpeedFix::Min: return low;
    default: return 0;
    }
}

// Multiplier on the hi-speed: 1 for Off and Constant.
inline double hiSpeedFixScale(const Chart& chart, HiSpeedFix fix) {
    const double fixed = hiSpeedFixBpm(chart, fix), reference = scrollReferenceBpm(chart);
    return fixed > 0 && reference > 0 ? reference / fixed : 1.0;
}

// Scroll position of a chart time: plain chart time under Constant.
inline double fixedScrollPosition(const Chart& chart, double chartSeconds, HiSpeedFix fix) {
    return fix == HiSpeedFix::Constant ? chartSeconds : scrollPositionSeconds(chart, chartSeconds);
}

} // namespace bms
