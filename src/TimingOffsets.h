#pragma once
#include <algorithm>

namespace bms {
// User-facing timing corrections in real-time milliseconds.
//   audio   (A): audible output delay. Shifts judgement, notes and BGA together.
//   display (D): visual pipeline delay. Shifts only the note scroll position.
//   judge   (J): player/input bias. Shifts only the judgement time (auto-adjust target).
// Chart-time seconds = real ms / 1000 * playback rate.
struct TimingOffsets {
    static constexpr int limitMs = 200;
    static constexpr int judgeLimitMs = 100;
    int audioMs{};
    int displayMs{};
    int judgeMs{};
};
inline int clampOffsetMs(int value, int limit = TimingOffsets::limitMs) { return std::clamp(value, -limit, limit); }
inline double offsetChartSeconds(int ms, double rate) { return static_cast<double>(ms) / 1000.0 * (rate > 0 ? rate : 1.0); }
// The audio clock reports the sample reaching the device; "heard" is what the player perceives.
inline double heardSeconds(double clock, const TimingOffsets& o, double rate) { return clock - offsetChartSeconds(o.audioMs, rate); }
inline double judgeSeconds(double heard, const TimingOffsets& o, double rate) { return heard - offsetChartSeconds(o.judgeMs, rate); }
inline double drawSeconds(double heard, const TimingOffsets& o, double rate) { return heard + offsetChartSeconds(o.displayMs, rate); }
} // namespace bms
