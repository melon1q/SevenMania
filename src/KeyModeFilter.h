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

inline KeyModeFilter keyModeFilterFromInt(int v) noexcept {
    return v >= 0 && v <= static_cast<int>(KeyModeFilter::All) ? static_cast<KeyModeFilter>(v) : KeyModeFilter::Key7;
}
}
