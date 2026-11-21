#pragma once
#include "SkinPackage.h"
#include "SkinData.h"
#include "GearMode.h"
#include <d2d1.h>
#include <array>
#include <memory>
#include <string>

namespace sevenmania::skin {
// suppressNativeGear: the skin has no gear for this key mode; hide its (7K) native gear parts instead of
// drawing them unattached, so the engine deck is shown alone.
struct SkinGearState { bool active{}; bool suppressNativeGear{}; int mode{}; int position=1; bool mirrorUi{}; std::uint32_t judgementColor=RGB(74,222,128); double judgementScale=1; double centerX{},lineY{},gauge=20,speed=1,rotation{};
    // Window pixels per gear design pixel (engine deck width / FHD deck width); 1 keeps the FHD size.
    double scale=1; bool keyGuide=true; std::array<bool,16> pressed{};
    // UTF-8 key names per lane (parser numbering) shown by gear-key-guide elements; null keeps the skin text.
    std::shared_ptr<const std::array<std::string,16>> keyLabels; };
struct SkinRenderStats {
    std::size_t decodedImages{}, uploadedBitmaps{}, imageLayers{}, skippedLayers{};
    std::uint64_t bitmapBytes{};
    std::size_t textLayers{}, textLayouts{}, textUploads{}, textChanges{};
    std::size_t hudAtlases{},hudFontGlyphs{},hudUpdates{},hudDrawCalls{};
};
// Uses the engine's existing D3D11/Direct2D bitmap rendering path.
// One owner thread, same as the supplied target. No BeginDraw/EndDraw or Present here.
class SkinRenderer {
public:
    explicit SkinRenderer(Limits limits = {});
    ~SkinRenderer();
    SkinRenderer(const SkinRenderer&) = delete;
    SkinRenderer& operator=(const SkinRenderer&) = delete;
    // Transactional: failure retains the previously prepared screen.
    // Call before entering a screen, and again after render-target/device loss.
    std::optional<SkinError> prepare(std::shared_ptr<const SkinPackage> package,
        ScreenKind screen, ID2D1RenderTarget* target) noexcept;
    // width/height are target DIPs (pixels at 96 DPI). Caller owns draw transaction.
    // Height-based scaling with per-element screen anchors (ResolutionScale.h, SkinAnchors.h);
    // preserves target transform, performs no I/O/decoding/allocation.
    bool draw(float width, float height, bool foregroundOnly=false, bool skipForeground=false) const noexcept;
    void emit(std::string_view event) noexcept;
    // Freezes/resumes the animation clock (game pause). Keyframe tracks keep their progress.
    void setAnimationPaused(bool paused) noexcept;
    void setGearState(const SkinGearState& state) noexcept;
    bool animating() const noexcept;
    bool scrolling() const noexcept;
    void setJudgementOffset(double offset) noexcept;
    // Rasterize judgement/number glyphs for every text layer before gameplay starts.
    void prewarmText() noexcept;
    void setJudgementRainbow(bool enabled) noexcept;
    void setJudgementAnchor(double centerX,double lineY,double width,double height) noexcept;
    bool hasBox(std::string_view mode) const noexcept;
    // True if any native gear part carries geometry for this key mode (0 = 7K is always supported).
    bool supportsGearMode(int mode) const noexcept;
    bool hasKeyGuides() const noexcept;
    bool coversLegacySlots(int gearPosition,double left,double top,double right,double bottom) const noexcept;
    bool updateBox(std::string_view mode,const std::uint32_t* pixels,unsigned width,unsigned height) noexcept;
    bool editGeometry(std::string_view id,double x,double y,double width,double height) noexcept;
    // Capture/update before BeginDraw. nullptr or another screen clears dynamic values.
    std::optional<SkinError> update(const SkinDataSnapshot* snapshot) noexcept;
    std::span<const BindingDiagnostic> diagnostics() const noexcept;
    std::string_view displayText(std::string_view elementId) const noexcept;
    void unload() noexcept;
    SkinRenderStats stats() const noexcept;
private:
    struct Impl;
    Limits limits_;
    std::unique_ptr<Impl> impl_;
};
}
