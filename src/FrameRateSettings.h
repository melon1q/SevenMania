#pragma once
#include <array>
#include <algorithm>

inline constexpr int vsyncFps = -1;
inline constexpr int unlimitedFpsLimit = 1500;
inline constexpr std::array<int, 10> frameRateValues{60, 144, 240, 400, 0, vsyncFps, 600, 800, 1000, -2};
inline constexpr std::array<const wchar_t*, 10> frameRateLabels{
    L"60 FPS", L"144 FPS", L"240 FPS", L"400 FPS", L"무제한 (최대 1500 FPS)", L"VSync", L"600 FPS", L"800 FPS", L"1000 FPS", L"사용자 설정"};
inline int frameRateIndex(int fps) {
    const auto found = std::find(frameRateValues.begin(), frameRateValues.end(), fps);
    return found == frameRateValues.end() ? 9 : static_cast<int>(found - frameRateValues.begin());
}
