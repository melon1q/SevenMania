#pragma once
// Resolution-independent layout: the skin and engine are authored for a 1920x1080 design space.
// The design space is scaled by the window height (limited by the width on screens narrower than
// 16:9), and the remaining space is distributed by per-axis anchors, so edge-attached elements stay
// on their edge on ultrawide screens instead of being letterboxed.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bms::ui {
inline constexpr double kDesignWidth = 1920.0;
inline constexpr double kDesignHeight = 1080.0;

enum class Anchor : std::uint8_t { Start, Center, End, Stretch };
struct Anchors { Anchor x = Anchor::Center, y = Anchor::Center; };
struct Rect { double x{}, y{}, width{}, height{}; };

struct Viewport {
    double width = kDesignWidth, height = kDesignHeight;
    double designWidth = kDesignWidth, designHeight = kDesignHeight;
    double scale = 1.0;
    // Window space left over after scaling the design space (never negative).
    double extraX = 0.0, extraY = 0.0;
};

inline Viewport viewport(double width, double height, double designWidth = kDesignWidth, double designHeight = kDesignHeight) noexcept {
    Viewport v;
    v.width = std::isfinite(width) && width > 0 ? width : designWidth;
    v.height = std::isfinite(height) && height > 0 ? height : designHeight;
    v.designWidth = designWidth > 0 ? designWidth : kDesignWidth;
    v.designHeight = designHeight > 0 ? designHeight : kDesignHeight;
    v.scale = std::min(v.width / v.designWidth, v.height / v.designHeight);
    if (!(v.scale > 0)) v.scale = 1.0;
    v.extraX = std::max(0.0, v.width - v.designWidth * v.scale);
    v.extraY = std::max(0.0, v.height - v.designHeight * v.scale);
    return v;
}

// Offset added to a scaled design coordinate for the start (and end) edge of an anchored axis.
inline double anchorOffset(Anchor anchor, double extra) noexcept {
    return anchor == Anchor::Center ? extra / 2 : anchor == Anchor::End ? extra : 0.0;
}
inline double anchorEndOffset(Anchor anchor, double extra) noexcept {
    return anchor == Anchor::Stretch ? extra : anchorOffset(anchor, extra);
}

// Window rectangle of a design rectangle. A stretched axis spans the extra space.
inline Rect mapRect(const Viewport& v, const Rect& design, Anchors anchors) noexcept {
    const double left = design.x * v.scale + anchorOffset(anchors.x, v.extraX);
    const double right = (design.x + design.width) * v.scale + anchorEndOffset(anchors.x, v.extraX);
    const double top = design.y * v.scale + anchorOffset(anchors.y, v.extraY);
    const double bottom = (design.y + design.height) * v.scale + anchorEndOffset(anchors.y, v.extraY);
    return {left, top, right - left, bottom - top};
}

// Window point back to design coordinates for an element's rectangle (hit testing).
inline bool unmapPoint(const Viewport& v, const Rect& design, Anchors anchors, double& x, double& y) noexcept {
    const Rect mapped = mapRect(v, design, anchors);
    if (design.width <= 0 || design.height <= 0 || mapped.width <= 0 || mapped.height <= 0) {
        x = (x - anchorOffset(anchors.x, v.extraX)) / v.scale;
        y = (y - anchorOffset(anchors.y, v.extraY)) / v.scale;
        return true;
    }
    x = design.x + (x - mapped.x) * design.width / mapped.width;
    y = design.y + (y - mapped.y) * design.height / mapped.height;
    return true;
}

// Horizontal edge inference: full-width spans stretch; elements centred on the screen (within 1/12 of
// the width) stay centred; everything else sticks to the half it is in, so a label and its value on the
// same side of the screen move together.
inline Anchor inferAxis(double start, double size, double designSize) noexcept {
    if (designSize <= 0) return Anchor::Center;
    if (size >= designSize * 0.95 && start <= designSize * 0.025) return Anchor::Stretch;
    const double center = start + size / 2, middle = designSize / 2;
    if (std::abs(center - middle) <= designSize / 12) return Anchor::Center;
    return center < middle ? Anchor::Start : Anchor::End;
}
// Vertical inference: only full-height spans stretch. Screens narrower than 16:9 keep the design
// space vertically centred so stacked text blocks keep their spacing.
inline Anchor inferVerticalAxis(double start, double size, double designSize) noexcept {
    if (designSize > 0 && size >= designSize * 0.95 && start <= designSize * 0.025) return Anchor::Stretch;
    return Anchor::Center;
}

inline Anchor mirrored(Anchor anchor) noexcept {
    return anchor == Anchor::Start ? Anchor::End : anchor == Anchor::End ? Anchor::Start : anchor;
}

// Engine pixel helper: scales a design length, keeping non-zero lengths at least 1px.
inline int scaled(int designPixels, double scale) noexcept {
    if (designPixels == 0) return 0;
    const long value = std::lround(designPixels * scale);
    if (designPixels > 0) return static_cast<int>(std::max(1L, value));
    return static_cast<int>(std::min(-1L, value));
}
inline double scaled(double designPixels, double scale) noexcept { return designPixels * scale; }

// Text/HUD raster density: quarter steps so small window resizes do not re-rasterize everything.
inline double rasterScale(double scale) noexcept {
    if (!std::isfinite(scale) || scale <= 0) return 1.0;
    return std::clamp(std::ceil(scale * 4 - 1e-6) / 4, 0.5, 4.0);
}

// Note scroll speed scale. The lane runs from the top of the window to a judgement line placed at a
// fixed fraction of the window height, so its length follows the height alone, even on screens
// narrower than 16:9 where the layout scale is limited by width. Scaling the speed by height keeps
// the note travel time identical to FHD at every window size and aspect ratio.
inline double scrollScale(double windowHeight, double designHeight = 1080) noexcept {
    if (!std::isfinite(windowHeight) || windowHeight <= 0 || !std::isfinite(designHeight) || designHeight <= 0) return 1.0;
    return windowHeight / designHeight;
}
}
