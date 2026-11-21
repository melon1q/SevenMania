#pragma once
// Screen anchors of skin elements for resolution-independent layout (see ResolutionScale.h).
// Explicit: extensions.anchor = "left" | "center" | "right" | "stretch" | "top-left" | ... |
//           { "x": "left|center|right|stretch", "y": "top|center|bottom|stretch" }.
// Implicit: an element fully inside an earlier (lower z) element inherits that container's anchor,
// so a panel and its contents move together; otherwise bms::ui::inferAxis/inferVerticalAxis decide
// (centred elements stay centred, others stick to their half; full-size spans stretch).
#include "SkinPackage.h"
#include "../ResolutionScale.h"
#include <optional>
#include <string>
#include <vector>

namespace sevenmania::skin {
namespace anchors_detail {
inline std::optional<bms::ui::Anchor> parseToken(const std::wstring& token, bool horizontal) {
    using bms::ui::Anchor;
    if (token == L"center" || token == L"middle") return Anchor::Center;
    if (token == L"stretch" || token == L"fill") return Anchor::Stretch;
    if (horizontal ? (token == L"left" || token == L"start") : (token == L"top" || token == L"start")) return Anchor::Start;
    if (horizontal ? (token == L"right" || token == L"end") : (token == L"bottom" || token == L"end")) return Anchor::End;
    return std::nullopt;
}
struct Explicit { std::optional<bms::ui::Anchor> x, y; };
inline Explicit parseExplicit(const SkinElement& e) {
    Explicit out;
    const auto& value = e.extensions[L"anchor"];
    if (value.type == bms::JsonValue::Type::Object) {
        out.x = parseToken(value[L"x"].str(), true);
        out.y = parseToken(value[L"y"].str(), false);
    } else if (value.type == bms::JsonValue::Type::String) {
        const auto& text = value.string;
        const auto dash = text.find(L'-');
        if (dash == std::wstring::npos) {
            // A single horizontal word ("left") fixes x; "top"/"bottom" fix y; "center"/"stretch" fix both.
            if (auto h = parseToken(text, true); h && (text == L"left" || text == L"right" || text == L"start" || text == L"end")) out.x = h;
            else if (auto v = parseToken(text, false); v && (text == L"top" || text == L"bottom")) out.y = v;
            else if (auto both = parseToken(text, true)) out.x = out.y = both;
        } else {
            out.y = parseToken(text.substr(0, dash), false);
            out.x = parseToken(text.substr(dash + 1), true);
        }
    }
    return out;
}
inline bool contains(const SkinElement& outer, const SkinElement& inner) {
    constexpr double slack = 0.5;
    return outer.width > 0 && outer.height > 0 && inner.x >= outer.x - slack && inner.y >= outer.y - slack &&
        inner.x + inner.width <= outer.x + outer.width + slack && inner.y + inner.height <= outer.y + outer.height + slack &&
        outer.width * outer.height > inner.width * inner.height;
}
// Text never stretches: a full-width text box keeps the edge its alignment points to.
inline bms::ui::Anchor textAxis(bms::ui::Anchor anchor, TextAlign align) {
    using bms::ui::Anchor;
    if (anchor != Anchor::Stretch) return anchor;
    return align == TextAlign::Left ? Anchor::Start : align == TextAlign::Right ? Anchor::End : Anchor::Center;
}
inline bms::ui::Anchor textAxis(bms::ui::Anchor anchor, TextVerticalAlign align) {
    using bms::ui::Anchor;
    if (anchor != Anchor::Stretch) return anchor;
    return align == TextVerticalAlign::Top ? Anchor::Start : align == TextVerticalAlign::Bottom ? Anchor::End : Anchor::Center;
}
}

// One entry per element, in element order (elements are z-sorted by the loader).
inline std::vector<bms::ui::Anchors> computeAnchors(const std::vector<SkinElement>& elements, Resolution resolution) {
    using namespace anchors_detail;
    using bms::ui::Anchor;
    std::vector<bms::ui::Anchors> out(elements.size());
    for (std::size_t i = 0; i < elements.size(); ++i) {
        const auto& e = elements[i];
        const auto explicitAnchor = parseExplicit(e);
        bms::ui::Anchors own{bms::ui::inferAxis(e.x, e.width, resolution.width), bms::ui::inferVerticalAxis(e.y, e.height, resolution.height)};
        // Smallest earlier container that does not stretch on that axis.
        std::optional<Anchor> inheritedX, inheritedY;
        double bestX = 0, bestY = 0;
        for (std::size_t j = 0; j < i; ++j) {
            const auto& outer = elements[j];
            if (!contains(outer, e)) continue;
            const double area = outer.width * outer.height;
            if (out[j].x != Anchor::Stretch && (!inheritedX || area < bestX)) {inheritedX = out[j].x; bestX = area;}
            if (out[j].y != Anchor::Stretch && (!inheritedY || area < bestY)) {inheritedY = out[j].y; bestY = area;}
        }
        bms::ui::Anchors anchors{explicitAnchor.x.value_or(inheritedX.value_or(own.x)), explicitAnchor.y.value_or(inheritedY.value_or(own.y))};
        if (e.kind == ElementKind::Text || e.kind == ElementKind::Number) {
            anchors.x = textAxis(anchors.x, e.textStyle.align);
            anchors.y = textAxis(anchors.y, e.textStyle.verticalAlign);
        }
        out[i] = anchors;
    }
    return out;
}
}
