#pragma once
#include <array>
#include <algorithm>

inline constexpr int vsyncFps = -1;
inline constexpr int unlimitedFpsLimit = 1500;
// Test-only: no frame pacing at all (no 1500 Hz ceiling).
inline constexpr int uncappedFps = -3;
inline constexpr bool validFpsSetting(int fps) { return fps == vsyncFps || fps == uncappedFps || (fps >= 0 && fps <= unlimitedFpsLimit); }
inline constexpr std::array<int, 11> frameRateValues{60, 144, 240, 400, 0, vsyncFps, 600, 800, 1000, -2, uncappedFps};
inline constexpr std::array<const wchar_t*, 11> frameRateLabels{
    L"60 FPS", L"144 FPS", L"240 FPS", L"400 FPS", L"무제한 (최대 1500 FPS)", L"VSync", L"600 FPS", L"800 FPS", L"1000 FPS", L"사용자 설정", L"완전 무제한 (테스트)"};
inline int frameRateIndex(int fps) {
    const auto found = std::find(frameRateValues.begin(), frameRateValues.end(), fps);
    return found == frameRateValues.end() ? 9 : static_cast<int>(found - frameRateValues.begin());
}
