// 5K / 10K / 14K / PMS skin gear: runtime rendering from extensions.gearModes and the editor's
// per-mode editing. Argument 1: DefaultReference.semaskin.
#include "SkinRenderTestSupport.h"
#include "skin/SkinApplication.h"
#include "editor/GearLayouts.h"
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <filesystem>
#include <iostream>

using namespace skin_test;
namespace ed = sevenmania::editor;

namespace {
struct Spec { int mode; double width; std::vector<int> lanes; };
const Spec specs[] = {
    {1, 428, {1, 2, 3, 4, 5}},
    {2, 554, {1, 2, 3, 4, 5, 9, 10, 11, 12, 13}},
    {3, 718, {1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15}},
    {4, 612, {0, 1, 2, 3, 4, 5, 6, 7, 8}},
};

ed::J* findElement(std::vector<ed::J>& elements, const wchar_t* id) {
    for (auto& e : elements) if (e[L"id"].str() == id) return &e;
    throw std::runtime_error("element missing in default skin");
}

void testRenderer(const std::filesystem::path& skin, const std::filesystem::path& captures) {
    auto loaded = SkinLoader{}.load(skin);
    require(bool(loaded), "default skin load failed");
    Surface surface(1920, 1080, D3D_DRIVER_TYPE_WARP);
    SkinRenderer renderer;
    require(!renderer.prepare(loaded.package, ScreenKind::Play, surface.target.Get()), "play prepare failed");
    require(renderer.supportsGearMode(0), "7K gear must always be supported");
    for (const auto& spec : specs) require(renderer.supportsGearMode(spec.mode), "default skin lacks a key mode gear");

    auto state = [&](int mode) {
        SkinGearState gear;
        gear.active = true; gear.mode = mode; gear.centerX = 960; gear.lineY = 720; gear.position = 1;
        return gear;
    };
    renderer.setGearState(state(0));
    const auto sevenKey = surface.render(renderer);
    if (!captures.empty()) { std::filesystem::create_directories(captures); save(captures / L"gear-7k.png", sevenKey, surface.width, surface.height); }
    const auto brightness = [&](const Bytes& pixels, int x, int y) {
        const auto i = (size_t(y) * surface.width + size_t(x)) * 4;
        return int(pixels[i]) + int(pixels[i + 1]) + int(pixels[i + 2]);
    };
    for (const auto& spec : specs) {
        renderer.setGearState(state(spec.mode));
        const auto idle = surface.render(renderer);
        if (!captures.empty()) {
            auto held = state(spec.mode);
            for (const int lane : spec.lanes) if (lane % 2 == 0) held.pressed[size_t(lane)] = true;
            renderer.setGearState(held);
            save(captures / (L"gear-mode" + std::to_wstring(spec.mode) + L".png"), surface.render(renderer), surface.width, surface.height);
            renderer.setGearState(state(spec.mode));
        }
        require(idle != sevenKey, "key mode gear renders like 7K");
        // The outer border is 6.5px wide just left of the panel, which is centered on the gear.
        const int panelLeft = int(std::lround(960 - spec.width / 2));
        require(brightness(idle, panelLeft - 3, 100) > 450, "gear border missing at the mode's panel edge");
        require(brightness(idle, panelLeft - 12, 100) < 100, "gear is wider than the mode's panel");
        // Every playable lane has a key texture that reacts to its pressed state.
        for (const int lane : spec.lanes) {
            auto pressed = state(spec.mode);
            pressed.pressed[size_t(lane)] = true;
            renderer.setGearState(pressed);
            require(surface.render(renderer) != idle, "key texture did not respond to its lane being pressed");
        }
    }
    // 5K has no 2P keys: pressing a 2P lane must not change anything.
    renderer.setGearState(state(1));
    const auto fiveIdle = surface.render(renderer);
    auto stray = state(1);
    stray.pressed[9] = true;
    renderer.setGearState(stray);
    require(surface.render(renderer) == fiveIdle, "5K reacted to a 2P lane");
    // Different modes never share a rendering.
    renderer.setGearState(state(3));
    const auto dp = surface.render(renderer);
    renderer.setGearState(state(2));
    require(surface.render(renderer) != dp, "10K and 14K render alike");
    // A skin without gear for the mode must not leave its 7K gear floating at the authored position.
    SkinGearState raw;
    renderer.setGearState(raw);
    require(brightness(surface.render(renderer), 200, 760) > 600, "inactive gear should draw the authored 7K key");
    raw.suppressNativeGear = true;
    renderer.setGearState(raw);
    require(brightness(surface.render(renderer), 200, 760) < 100, "suppressed native gear is still drawn");
    // Key guide texts show the lane's bound key name in every mode.
    auto labels = std::make_shared<std::array<std::string, 16>>();
    for (int lane = 0; lane < 16; ++lane) (*labels)[size_t(lane)] = "K" + std::to_string(lane);
    const auto labelled = [&](int mode, const char* id) {
        auto gear = state(mode);
        gear.keyLabels = labels;
        renderer.setGearState(gear);
        require(!renderer.update(nullptr), "label update failed");
        return std::string(renderer.displayText(id));
    };
    require(labelled(0, "gear-key-guide-0") == "K0" && labelled(0, "gear-key-guide-7") == "K7", "7K guides do not show bound keys");
    require(labelled(1, "gear-key-guide-5") == "K5", "5K guide label wrong");
    require(labelled(3, "gear-key-guide-1") == "K1" && labelled(3, "gear-key-guide-p2-0") == "K8" && labelled(3, "gear-key-guide-p2-7") == "K15", "14K guide labels wrong");
    require(labelled(4, "gear-key-guide-1") == "K0" && labelled(4, "gear-key-guide-7") == "K6" &&
            labelled(4, "gear-key-guide-p2-2") == "K7" && labelled(4, "gear-key-guide-p2-3") == "K8", "PMS guide labels wrong");
    renderer.setGearState(state(0));
    require(!renderer.update(nullptr), "state reset failed");
    // The 7K gear is unchanged by the new data (no 2P parts leak in).
    renderer.setGearState(state(0));
    require(surface.render(renderer) == sevenKey, "7K rendering changed");
}

void testApplication(const std::filesystem::path& skin) {
    // The game goes through SkinApplication (runtime copy of the package), not the bare renderer.
    SkinApplication app;
    require(!app.load(skin, 1920, 1080), "SkinApplication load failed");
    require(app.supportsGearMode(0), "7K unsupported by the application");
    require(app.hasKeyGuides(), "application reports no key guide elements");
    for (const auto& spec : specs) require(app.supportsGearMode(spec.mode), "application reports no gear for a key mode");
}

void testEditor(const std::filesystem::path& skin) {
    ed::Document doc;
    doc.open(skin);
    doc.screen = "play";
    require(ed::editorGearMode(doc) == 0, "fresh document should edit 7K");
    const auto get = [&](const wchar_t* id, const wchar_t* key) { return (*findElement(doc.elements(), id))[key]; };
    const auto panelWidth = [&] { return get(L"gear-panel", L"width").num(); };
    const auto panelX = [&] { return get(L"gear-panel", L"x").num(); };
    require(panelWidth() == 556 && panelX() == 64, "7K base geometry changed");
    require(get(L"gear-key-p2-1", L"visible").type == ed::T::Boolean && !get(L"gear-key-p2-1", L"visible").boolean, "2P parts must be hidden in the 7K base");

    ed::switchGearMode(doc, 3);
    require(ed::editorGearMode(doc) == 3 && panelWidth() == 718 && panelX() == -17, "switching to 14K did not apply its geometry");
    require(get(L"gear-key-p2-1", L"visible").boolean, "14K should show 2P keys");
    require(get(L"gear-key-guide-1", L"visible").boolean && get(L"gear-key-guide-p2-1", L"visible").boolean, "14K should show key guides for both sides");
    // Edit the 14K gear, go to 5K and come back: the edit is remembered, 5K is untouched.
    findElement(doc.elements(), L"gear-panel")->object[L"width"] = ed::num(730);
    ed::switchGearMode(doc, 1);
    require(panelWidth() == 428, "5K geometry polluted by the 14K edit");
    ed::switchGearMode(doc, 3);
    require(panelWidth() == 730, "14K edit lost when switching modes");
    ed::switchGearMode(doc, 0);
    require(panelWidth() == 556 && panelX() == 64, "7K geometry not restored");
    ed::switchGearMode(doc, 4);

    // Saving while a non-7K mode is being edited must persist the 7K base and keep the mode data.
    const auto output = std::filesystem::temp_directory_path() / L"SevenManiaGearModeTests.semaskin";
    ed::withBaseGearGeometry(doc, [&] { require(panelWidth() == 556, "base geometry missing while saving"); doc.save(output); });
    require(ed::editorGearMode(doc) == 4 && panelWidth() == 612, "editing mode not restored after saving");
    ed::Document reloaded;
    reloaded.open(output);
    reloaded.screen = "play";
    require(ed::editorGearMode(reloaded) == 0, "saved file must open in 7K");
    require((*findElement(reloaded.elements(), L"gear-panel"))[L"width"].num() == 556, "saved base geometry is not 7K");
    const auto& modes = (*findElement(reloaded.elements(), L"gear-panel"))[L"extensions"][L"gearModes"];
    require(modes[L"14k"][L"width"].num() == 730 && modes[L"pms"][L"width"].num() == 612, "per-mode edits not saved");
    std::error_code ec;
    std::filesystem::remove(output, ec);
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 2) throw std::runtime_error("Usage: SevenManiaGearModeTests DefaultReference.semaskin");
        hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
        testRenderer(argv[1], argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path());
        testApplication(argv[1]);
        testEditor(argv[1]);
        std::cout << "SevenManiaGearModeTests passed (" << checks << " checks)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
}
