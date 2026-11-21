// Resolution-independent layout math (src/ResolutionScale.h) and skin anchor inference (src/skin/SkinAnchors.h).
#include "../src/ResolutionScale.h"
#include "../src/skin/SkinAnchors.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
int checks = 0;
void require(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) <= tolerance; }
using bms::ui::Anchor;

void viewportCases() {
    struct Case { double width, height, scale, extraX, extraY; };
    const Case cases[]{
        {1920, 1080, 1.0, 0, 0},
        {1280, 720, 2.0 / 3.0, 0, 0},
        {2560, 1440, 4.0 / 3.0, 0, 0},
        {3840, 2160, 2.0, 0, 0},
        {2560, 1080, 1.0, 640, 0},       // 21:9: height based, extra width
        {3440, 1440, 4.0 / 3.0, 880, 0},
        {1440, 1080, 0.75, 0, 270},      // 4:3: limited by width, extra height
        {1920, 1200, 1.0, 0, 120},       // 16:10
    };
    for (const auto& c : cases) {
        const auto v = bms::ui::viewport(c.width, c.height);
        require(near(v.scale, c.scale) && near(v.extraX, c.extraX, 1e-6) && near(v.extraY, c.extraY, 1e-6),
            "viewport " + std::to_string(int(c.width)) + "x" + std::to_string(int(c.height)));
    }
    const auto invalid = bms::ui::viewport(0, std::nan(""));
    require(near(invalid.scale, 1.0), "invalid viewport falls back to the design size");
}

void mappingCases() {
    const auto wide = bms::ui::viewport(2560, 1080);
    const bms::ui::Rect box{100, 200, 300, 100};
    auto r = bms::ui::mapRect(wide, box, {Anchor::Start, Anchor::Center});
    require(near(r.x, 100) && near(r.width, 300), "start anchor keeps the left edge");
    r = bms::ui::mapRect(wide, box, {Anchor::Center, Anchor::Center});
    require(near(r.x, 420), "centre anchor takes half the extra width");
    r = bms::ui::mapRect(wide, box, {Anchor::End, Anchor::Center});
    require(near(r.x, 740), "end anchor takes all the extra width");
    r = bms::ui::mapRect(wide, {0, 0, 1920, 1080}, {Anchor::Stretch, Anchor::Stretch});
    require(near(r.x, 0) && near(r.width, 2560) && near(r.height, 1080), "stretch spans the window");
    const auto tall = bms::ui::viewport(1440, 1080);
    r = bms::ui::mapRect(tall, box, {Anchor::Start, Anchor::Center});
    require(near(r.x, 75) && near(r.y, 150 + 135) && near(r.width, 225), "4:3 scales by width and centres vertically");
    double x = r.x + r.width / 2, y = r.y + r.height / 2;
    bms::ui::unmapPoint(tall, box, {Anchor::Start, Anchor::Center}, x, y);
    require(near(x, 250, 1e-6) && near(y, 250, 1e-6), "hit testing undoes the mapping");
    x = 1280; y = 540;
    bms::ui::unmapPoint(wide, {0, 0, 1920, 1080}, {Anchor::Stretch, Anchor::Stretch}, x, y);
    require(near(x, 960, 1e-6) && near(y, 540, 1e-6), "stretched hit testing maps proportionally");
}

void engineHelpers() {
    require(bms::ui::scaled(60, 2.0 / 3.0) == 40 && bms::ui::scaled(108, 2.0 / 3.0) == 72, "lane widths at 720p");
    require(bms::ui::scaled(36, 2.0) == 72 && bms::ui::scaled(1, 0.4) == 1 && bms::ui::scaled(0, 3) == 0, "pixel scaling keeps 1px minimum");
    require(bms::ui::scaled(-30, 0.5) == -15, "negative offsets scale");
    require(near(bms::ui::rasterScale(1.0), 1.0) && near(bms::ui::rasterScale(2.0 / 3.0), 0.75) && near(bms::ui::rasterScale(4.0 / 3.0), 1.5) &&
        near(bms::ui::rasterScale(0.1), 0.5) && near(bms::ui::rasterScale(9), 4.0), "raster density steps");
    // Lane length = 2/3 of the window height, so the scroll scale must follow height even when the layout
    // scale is width-limited (5:4, 4:3): travel time (lane length / speed) has to equal FHD's.
    for (const auto size : {std::pair{1920.0, 1080.0}, {1280.0, 720.0}, {1280.0, 1024.0}, {1440.0, 1080.0}, {2560.0, 1080.0}, {3840.0, 2160.0}}) {
        const double laneLength = size.second * 2.0 / 3.0, travel = laneLength / (270.0 * bms::ui::scrollScale(size.second));
        require(near(travel, 720.0 / 270.0, 1e-9), "note travel time matches FHD at " + std::to_string(int(size.first)) + "x" + std::to_string(int(size.second)));
    }
    require(near(bms::ui::scrollScale(0), 1.0) && near(bms::ui::scrollScale(std::nan("")), 1.0), "invalid height falls back to FHD speed");
}

void inferenceCases() {
    require(bms::ui::inferAxis(0, 1920, 1920) == Anchor::Stretch, "full width stretches");
    require(bms::ui::inferAxis(24, 1212, 1920) == Anchor::Start, "left panel sticks left");
    require(bms::ui::inferAxis(510, 300, 1920) == Anchor::Start, "value beside a left label sticks left too");
    require(bms::ui::inferAxis(660, 600, 1920) == Anchor::Center, "centred element stays centred");
    require(bms::ui::inferAxis(1600, 300, 1920) == Anchor::End, "right element sticks right");
    require(bms::ui::inferVerticalAxis(806, 100, 1080) == Anchor::Center && bms::ui::inferVerticalAxis(0, 1080, 1080) == Anchor::Stretch,
        "vertical inference centres all but full-height spans");
}

void skinAnchorCases() {
    using namespace sevenmania::skin;
    const auto element = [](const char* id, double x, double y, double w, double h) {
        SkinElement e; e.id = id; e.kind = ElementKind::Box; e.x = x; e.y = y; e.width = w; e.height = h; return e;
    };
    std::vector<SkinElement> elements{
        element("background", 0, 0, 1920, 1080),
        element("panel", 1500, 100, 400, 300),
        element("panel-child", 1520, 120, 100, 50),      // follows the panel even though it is in the right half anyway
        element("center-panel", 700, 300, 520, 400),
        element("center-child", 710, 310, 100, 50),      // left of centre, but inherits the centred panel
        element("explicit", 1700, 900, 100, 100),
    };
    elements[5].extensions.type = bms::JsonValue::Type::Object;
    auto& anchor = elements[5].extensions.object[L"anchor"]; anchor.type = bms::JsonValue::Type::String; anchor.string = L"bottom-left";
    const auto anchors = computeAnchors(elements, Resolution{1920, 1080});
    require(anchors[0].x == Anchor::Stretch && anchors[0].y == Anchor::Stretch, "background stretches");
    require(anchors[1].x == Anchor::End && anchors[2].x == Anchor::End, "panel and child stick right");
    require(anchors[3].x == Anchor::Center && anchors[4].x == Anchor::Center, "child inherits the centred container");
    require(anchors[5].x == Anchor::Start && anchors[5].y == Anchor::End, "explicit anchor wins");
    SkinElement text = element("title", 24, 90, 1900, 200); text.kind = ElementKind::Text; text.textStyle.align = TextAlign::Left;
    const auto textAnchors = computeAnchors({text}, Resolution{1920, 1080});
    require(textAnchors[0].x == Anchor::Start, "full-width left-aligned text sticks to its alignment edge");
}
}

int main() {
    try {
        viewportCases();
        mappingCases();
        engineHelpers();
        inferenceCases();
        skinAnchorCases();
        std::cout << "Resolution scale checks: " << checks << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
