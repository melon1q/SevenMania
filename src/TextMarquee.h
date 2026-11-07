#pragma once
#include <algorithm>
#include <cmath>

namespace bms::ui {
// Hold at the beginning, move to the last character, hold, then restart.
inline int textMarqueeOffset(double overflow, double seconds) {
    if (overflow <= 0 || !std::isfinite(overflow) || !std::isfinite(seconds)) return 0;
    constexpr double startHold = 1.2, endHold = 1.5, pixelsPerSecond = 48;
    const double travel = overflow / pixelsPerSecond;
    const double phase = std::fmod(std::max(0.0, seconds), startHold + travel + endHold);
    return static_cast<int>(std::clamp((phase - startHold) * pixelsPerSecond, 0.0, overflow));
}
}
