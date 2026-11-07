#pragma once
#include "Judgement.h"
#include <array>

enum class AccuracyLimit : int { None, BelowPGreat, BelowGreat, BelowGood, BelowBad };
inline constexpr std::array<const wchar_t*,5> accuracyLimitLabels{
    L"없음",L"PGREAT 미만",L"GREAT 미만",L"GOOD 미만",L"BAD 미만"
};
// Equality is allowed: BelowGreat rejects GOOD/BAD/POOR, not GREAT.
inline constexpr bool belowAccuracyLimit(AccuracyLimit limit,TimingJudgement grade) noexcept {
    const int setting=static_cast<int>(limit),rank=static_cast<int>(grade);
    return setting>=1&&setting<=4&&rank>=setting&&rank<=4;
}
