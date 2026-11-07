#pragma once
#include "BmsParser.h"

namespace bms {
// Parsing remains format-compatible; the shipping player currently admits only 7+1.
inline bool gameplayModeAllowed(const Chart& chart) noexcept {
    return chart.mode==ChartMode::SevenKey;
}
inline std::wstring gameplayModeBlockReason(const Chart& chart) {
    return L"현재는 7+1키 채보만 플레이할 수 있습니다. 이 채보: "+BmsParser::modeLabel(chart.mode);
}
}
