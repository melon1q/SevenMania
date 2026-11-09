#pragma once
#include "BmsParser.h"

namespace bms {
// Every parsed key mode is playable; Unknown/Other have no lane layout.
inline bool gameplayModeAllowed(const Chart& chart) noexcept {
    return chart.mode!=ChartMode::Unknown&&chart.mode!=ChartMode::Other;
}
inline std::wstring gameplayModeBlockReason(const Chart& chart) {
    return L"지원하지 않는 키 모드입니다: "+BmsParser::modeLabel(chart.mode);
}
}
