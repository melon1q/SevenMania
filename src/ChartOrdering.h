#pragma once

#include "BmsParser.h"

#include <cmath>
#include <optional>
#include <string>

namespace bms {

inline std::optional<double> numericPlayLevel(const Chart& chart) {
    if (chart.playLevel.empty()) return std::nullopt;
    try {
        size_t parsedCharacters = 0;
        const double level = std::stod(chart.playLevel, &parsedCharacters);
        if (parsedCharacters == chart.playLevel.size() && std::isfinite(level)) return level;
    } catch (...) {}
    return std::nullopt;
}

// Negative means left should be listed first. Charts without an exact numeric
// #PLAYLEVEL follow ranked charts; no difficulty is inferred or synthesized.
inline int comparePlayLevelAscending(const Chart& left, const Chart& right) {
    const auto levelLeft = numericPlayLevel(left);
    const auto levelRight = numericPlayLevel(right);
    if (levelLeft && levelRight) return (*levelLeft > *levelRight) - (*levelLeft < *levelRight);
    if (levelLeft.has_value() != levelRight.has_value()) return levelLeft ? -1 : 1;
    return 0;
}

} // namespace bms
