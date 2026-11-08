#pragma once
// Smallest rectangle that differs between two 32-bit pixel images, so GPU uploads
// copy only what changed (e.g. the BGA viewport instead of the whole backdrop).
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace dirtyrect {

// width/height in pixels; pitches in pixels. Returns an empty RECT when identical.
inline RECT find(const uint32_t* previous, size_t previousPitch, const uint32_t* current, size_t currentPitch,
    int width, int height) {
    if (!previous || !current || width <= 0 || height <= 0) return {0, 0, width, height};
    const auto row = [&](const uint32_t* image, size_t pitch, int y) { return image + static_cast<size_t>(y) * pitch; };
    const size_t bytes = static_cast<size_t>(width) * 4;
    int top = 0, bottom = height;
    while (top < bottom && std::memcmp(row(previous, previousPitch, top), row(current, currentPitch, top), bytes) == 0) ++top;
    if (top == bottom) return {};
    while (bottom > top && std::memcmp(row(previous, previousPitch, bottom - 1), row(current, currentPitch, bottom - 1), bytes) == 0) --bottom;
    int left = width, right = 0;
    for (int y = top; y < bottom; ++y) {
        const uint32_t* a = row(previous, previousPitch, y);
        const uint32_t* b = row(current, currentPitch, y);
        int x = 0;
        while (x < left && a[x] == b[x]) ++x;
        if (x < left) left = x;
        int r = width;
        while (r > right && a[r - 1] == b[r - 1]) --r;
        if (r > right) right = r;
        if (left == 0 && right == width) break; // already full width
    }
    if (left >= right) return {};
    return {left, top, right, bottom};
}

// Full-width band of differing rows (left = 0, right = width). Empty when identical.
inline RECT rows(const uint32_t* previous, size_t previousPitch, const uint32_t* current, size_t currentPitch,
    int width, int height) {
    if (!previous || !current || width <= 0 || height <= 0) return {0, 0, width, height};
    const size_t bytes = static_cast<size_t>(width) * 4;
    const auto same = [&](int y) { return std::memcmp(previous + y * previousPitch, current + y * currentPitch, bytes) == 0; };
    int top = 0, bottom = height;
    while (top < bottom && same(top)) ++top;
    if (top == bottom) return {};
    while (bottom > top && same(bottom - 1)) --bottom;
    return {0, top, width, bottom};
}

inline bool same(const uint32_t* previous, size_t previousPitch, const uint32_t* current, size_t currentPitch,
    int width, int height) {
    const size_t bytes = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height; ++y)
        if (std::memcmp(previous + y * previousPitch, current + y * currentPitch, bytes) != 0) return false;
    return true;
}

inline bool empty(const RECT& r) { return r.right <= r.left || r.bottom <= r.top; }
inline uint64_t bytes(const RECT& r) { return empty(r) ? 0 : uint64_t(r.right - r.left) * uint64_t(r.bottom - r.top) * 4; }

} // namespace dirtyrect
