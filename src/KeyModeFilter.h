#pragma once
#include "BmsParser.h"

#include <string>

namespace bms {
// Song-select key mode filter. Tab cycles Key7 -> Key5 -> Key14 -> Key10 -> Pms9 -> All.
enum class KeyModeFilter { Key7, Key5, Key14, Key10, Pms9, All };

inline KeyModeFilter nextKeyModeFilter(KeyModeFilter f) noexcept {
    switch (f) {
    case KeyModeFilter::Key7: return KeyModeFilter::Key5;
    case KeyModeFilter::Key5: return KeyModeFilter::Key14;
    case KeyModeFilter::Key14: return KeyModeFilter::Key10;
    case KeyModeFilter::Key10: return KeyModeFilter::Pms9;
    case KeyModeFilter::Pms9: return KeyModeFilter::All;
    default: return KeyModeFilter::Key7;
    }
}

inline bool keyModeFilterMatches(KeyModeFilter f, ChartMode m) noexcept {
    if (f == KeyModeFilter::All) return true;
    if (m == ChartMode::Unknown) m = ChartMode::SevenKey;
    switch (f) {
    case KeyModeFilter::Key7: return m == ChartMode::SevenKey;
    case KeyModeFilter::Key5: return m == ChartMode::FiveKey;
    case KeyModeFilter::Key14: return m == ChartMode::FourteenKey;
    case KeyModeFilter::Key10: return m == ChartMode::TenKey;
    case KeyModeFilter::Pms9: return m == ChartMode::PopnNine;
    default: return true;
    }
}

inline const wchar_t* keyModeFilterLabel(KeyModeFilter f) noexcept {
    switch (f) {
    case KeyModeFilter::Key7: return L"7K";
    case KeyModeFilter::Key5: return L"5K";
    case KeyModeFilter::Key14: return L"14K";
    case KeyModeFilter::Key10: return L"10K";
    case KeyModeFilter::Pms9: return L"PMS";
    default: return L"ALL";
    }
}

// Song-select difficulty filter (LR2 select button 10): 0 shows every chart, 1-5 only charts whose
// #DIFFICULTY is BEGINNER, NORMAL, HYPER, ANOTHER or INSANE. Charts without a difficulty show only in 0.
inline constexpr int difficultyFilterCount = 6;
inline int difficultyFilterFromInt(int v) noexcept { return v >= 0 && v < difficultyFilterCount ? v : 0; }
inline int nextDifficultyFilter(int f, bool forward) noexcept {
    return (difficultyFilterFromInt(f) + (forward ? 1 : difficultyFilterCount - 1)) % difficultyFilterCount;
}
inline bool difficultyFilterMatches(int f, int difficulty) noexcept { return f <= 0 || f >= difficultyFilterCount || difficulty == f; }
inline const wchar_t* difficultyFilterLabel(int f) noexcept {
    static constexpr const wchar_t* names[] = {L"ALL", L"BEGINNER", L"NORMAL", L"HYPER", L"ANOTHER", L"INSANE"};
    return names[difficultyFilterFromInt(f)];
}

inline KeyModeFilter keyModeFilterFromInt(int v) noexcept {
    return v >= 0 && v <= static_cast<int>(KeyModeFilter::All) ? static_cast<KeyModeFilter>(v) : KeyModeFilter::Key7;
}
}
