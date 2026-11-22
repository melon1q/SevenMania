#pragma once
// Score target the play screen races against (LR2 select TARGET, button 77 and string 1; DEFAULT TARGET,
// button 76 and number 13). The target feeds LR2 play numbers 151 / 108 and bar graphs 14 / 15.
#include <algorithm>
#include <optional>

namespace bms {
enum class TargetKind : int { MyBest, RankAAA, RankAA, RankA, Default, Count };
inline constexpr int targetKindCount = static_cast<int>(TargetKind::Count);
inline constexpr int minDefaultTargetRate = 0, maxDefaultTargetRate = 100, initialDefaultTargetRate = 50;

inline TargetKind targetKindFromInt(int value) noexcept {
    return value >= 0 && value < targetKindCount ? static_cast<TargetKind>(value) : TargetKind::MyBest;
}
inline TargetKind nextTargetKind(TargetKind kind, bool forward) noexcept {
    const int index = static_cast<int>(kind);
    return static_cast<TargetKind>((index + (forward ? 1 : targetKindCount - 1)) % targetKindCount);
}

// EX score of the target for a chart of notes notes (2 points a note). MY BEST is the best EX score so
// far (0 without one); the ranks are the LR2 rank borders (AAA 8/9, AA 7/9, A 6/9 of the maximum) and
// DEFAULT is rate percent of the maximum, both rounded up so reaching the target earns the rank.
inline int targetExScore(TargetKind kind, std::optional<int> bestExScore, int notes, int defaultRate) noexcept {
    const long long maximum = 2LL * std::max(0, notes);
    const auto share = [maximum](long long numerator, long long denominator) {
        return static_cast<int>((maximum * numerator + denominator - 1) / denominator);
    };
    switch (kind) {
    case TargetKind::RankAAA: return share(8, 9);
    case TargetKind::RankAA: return share(7, 9);
    case TargetKind::RankA: return share(6, 9);
    case TargetKind::Default: return share(std::clamp(defaultRate, minDefaultTargetRate, maxDefaultTargetRate), 100);
    default: return static_cast<int>(std::clamp<long long>(bestExScore.value_or(0), 0, maximum));
    }
}
} // namespace bms
