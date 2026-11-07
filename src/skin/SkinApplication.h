#pragma once
#include "SkinRenderer.h"
#include <windows.h>

namespace sevenmania::skin {
// UI-thread host for one externally selected package. Existing game UI stays active.
class SkinApplication {
public:
    SkinApplication();
    ~SkinApplication();
    // Validate/prepare all declared screens before replacing the old selection.
    std::optional<SkinError> load(const std::filesystem::path& file,int width,int height) noexcept;
    void clear() noexcept;
    bool active() const noexcept;
    bool animating() const noexcept;
    bool scrolling() const noexcept;
    void setJudgementOffset(double offset) noexcept;
    void setJudgementRainbow(bool enabled) noexcept;
    void setJudgementAnchor(double x,double y,double width,double height) noexcept;
    bool hasBox(std::string_view mode,ScreenKind screen=ScreenKind::Select) const noexcept;
    void updateBox(std::string_view mode,const std::uint32_t* pixels,unsigned width,unsigned height,ScreenKind screen=ScreenKind::Select) noexcept;
    void emit(std::string_view event) noexcept;
    void enterScreen(ScreenKind screen) noexcept;
    void setGearState(const SkinGearState& state) noexcept;
    const SkinManifest* manifest() const noexcept;
    const SkinScreen* screenLayout(ScreenKind screen) const noexcept;
    bool supports(ScreenKind screen) const noexcept;
    bool ownsJudgement() const noexcept;
    bool needsJudgementText() const noexcept;
    bool hasImageHud() const noexcept;
    bool coversLegacySlots(double left,double top,double right,double bottom) const noexcept;
    SkinRenderStats gpuStats() const noexcept;
    std::span<const BindingDiagnostic> diagnostics() const noexcept;
    // -1: package mode, 0: font, 1: image. Rebuild only while changing settings.
    std::optional<SkinError> setHudModes(int judgement,int combo) noexcept;
    // Reference select slots: speed=0, judgement=1, gauge=2; -1 outside visible slots.
    int selectShortcutAt(double x,double y,int width,int height) const noexcept;
    // GDI paths: transparent cached overlay, readback only on changed content/size/screen.
    bool drawDc(HDC destination,int width,int height,const SkinDataSnapshot& snapshot) noexcept;
    // GPU gameplay path: update before BeginDraw; draw inside the presenter's draw transaction.
    bool prepareGpu(ID2D1RenderTarget* target,const SkinDataSnapshot& snapshot) noexcept;
    bool drawGpu(float width,float height) const noexcept;
    bool drawGpuForeground(float width,float height) const noexcept;
    void releaseGpu() noexcept;
    const std::optional<SkinError>& error() const noexcept {return error_;}
    std::uint64_t readbacks() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::optional<SkinError> error_;
    int judgementMode_=-1,comboMode_=-1;
};
}
