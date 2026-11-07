#pragma once

#include <cmath>

enum class TimingJudgement { PGreat, Great, Good, Bad, Poor };
enum class JudgementProfile : int { Basic, Easy, Normal, Hard };

struct JudgementWindows {
    double pGreatMs, greatMs, goodMs, badMs;
};

inline constexpr JudgementWindows judgementWindows(JudgementProfile profile) {
    switch (profile) {
    case JudgementProfile::Hard: return { 12.50, 25.00, 90.00, 200.00 };
    case JudgementProfile::Easy: return { 21.00, 45.00, 150.00, 275.00 };
    case JudgementProfile::Basic: return { 25.00, 55.00, 180.00, 300.00 };
    case JudgementProfile::Normal: return { 16.67, 33.33, 116.67, 250.00 };
    }
    return { 16.67, 33.33, 116.67, 250.00 };
}

inline constexpr double JUDGEMENT_PGREAT_SECONDS = 1.0 / 60.0;
inline constexpr double JUDGEMENT_GREAT_SECONDS = 1.0 / 30.0;
inline constexpr double JUDGEMENT_GOOD_SECONDS = 7.0 / 60.0;
inline constexpr double JUDGEMENT_BAD_SECONDS = 0.25;
inline constexpr double LONG_NOTE_RELEASE_WINDOW_SECONDS = 7.0 / 60.0;
inline constexpr double JUDGEMENT_WINDOW_SECONDS = JUDGEMENT_BAD_SECONDS;

inline TimingJudgement judgeTimingOffset(double offsetSeconds, JudgementProfile profile = JudgementProfile::Normal) {
    const double offset = std::abs(offsetSeconds);
    const auto windows = judgementWindows(profile);
    if (offset * 1000.0 <= windows.pGreatMs) return TimingJudgement::PGreat;
    if (offset * 1000.0 <= windows.greatMs) return TimingJudgement::Great;
    if (offset * 1000.0 <= windows.goodMs) return TimingJudgement::Good;
    if (offset * 1000.0 <= windows.badMs) return TimingJudgement::Bad;
    return TimingJudgement::Poor;
}
