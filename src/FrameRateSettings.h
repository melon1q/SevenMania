#pragma once
#include <array>
#include <algorithm>

inline constexpr int vsyncFps = -1;
inline constexpr int unlimitedFpsLimit = 1500;
inline constexpr std::array<int, 6> frameRateValues{60, 144, 240, 400, 0, vsyncFps};
inline constexpr std::array<const wchar_t*, 6> frameRateLabels{
    L"60 FPS", L"144 FPS", L"240 FPS", L"400 FPS", L"무제한 (최대 1500 FPS)", L"VSync"};
inline int frameRateIndex(int fps) {
    const auto found = std::find(frameRateValues.begin(), frameRateValues.end(), fps);
    return found == frameRateValues.end() ? 2 : static_cast<int>(found - frameRateValues.begin());
}
