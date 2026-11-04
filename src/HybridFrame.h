#pragma once
#include <windows.h>
#include <algorithm>
#include <vector>

#ifdef BMS_HYBRID_RENDERER
inline constexpr bool hybridRendererEnabled = true;
#else
inline constexpr bool hybridRendererEnabled = false;
#endif

// CPU updates the gear and small HUD areas; the GPU retains the full backdrop.
inline std::vector<RECT> hybridCpuRegions(int width, int height, int gearX, int gearWidth) {
    std::vector<RECT> result;
    for (RECT rect : {RECT{gearX, 0, gearX + gearWidth, height},
        RECT{0, std::max(0, height - 96), std::min(width, 420), height},
        RECT{std::max(0, width - 450), 0, width, std::min(height, 200)}}) {
        rect.left = std::clamp<LONG>(rect.left, 0, width); rect.right = std::clamp<LONG>(rect.right, 0, width);
        rect.top = std::clamp<LONG>(rect.top, 0, height); rect.bottom = std::clamp<LONG>(rect.bottom, 0, height);
        if (rect.left < rect.right && rect.top < rect.bottom) result.push_back(rect);
    }
    return result;
}
