#pragma once
#include "HudFrameProbe.h"
#include <array>
#include <cstdint>
#include <memory>
#include <utility>

namespace latency {
enum class Mode { Off, Reflex, AntiLag2 };
enum class Status { Off, SdkMissing, Unsupported, Ready, SessionDisabled };
enum class WaitOwner { ExistingFramePacer, Reflex };
enum class Marker { SimulationStart, SimulationEnd, RenderStart, RenderEnd, PresentStart, PresentEnd, InputSample };
struct Device { void* nativeDevice{}; uint32_t vendorId{}; };
struct Capability { bool sdkLinked{}, supported{}; };
class Backend {
public:
    virtual ~Backend() = default;
    virtual Capability probe(Device) noexcept = 0;
    virtual bool configure(bool enabled, uint32_t intervalUs) noexcept = 0;
    virtual bool sleep() noexcept = 0;
    virtual bool marker(uint64_t frame, Marker marker) noexcept = 0;
    virtual bool enabledInDriver() noexcept = 0;
    virtual void release() noexcept = 0;
};
class Controller {
    std::array<std::unique_ptr<Backend>, 2> backends_;
    std::array<Capability, 2> capabilities_{};
    std::array<bool, 2> failed_{};
    Mode active_ = Mode::Off;
    uint32_t interval_{};
    uint64_t frame_{};
    enum class Phase { Idle, Simulation, Render, Rendered, Present } phase_ = Phase::Idle;
    static int index(Mode mode) noexcept { return static_cast<int>(mode) - 1; }
    bool emit(Marker marker) noexcept {
        // Markers remain available with Reflex OFF for comparable measurements.
        if (status(Mode::Reflex) != Status::Ready) return false;
        HUD_MARKER(marker);
        if (backends_[0]->marker(frame_, marker)) return true;
        failSession(Mode::Reflex); return false;
    }
public:
    ~Controller() { detach(); }
    void install(Mode mode, std::unique_ptr<Backend> backend) noexcept {
        const int i = index(mode); if (i < 0 || i >= 2) return;
        if (backends_[i]) backends_[i]->release();
        backends_[i] = std::move(backend); capabilities_[i] = {};
    }
    void attach(Device device) noexcept {
        detach();
        for (int i = 0; i < 2; ++i)
            if (backends_[i] && !failed_[i] && device.nativeDevice)
                capabilities_[i] = backends_[i]->probe(device);
    }
    void detach() noexcept {
        for (auto& backend : backends_) if (backend) backend->release();
        capabilities_ = {}; active_ = Mode::Off; interval_ = 0; phase_ = Phase::Idle;
    }
    Status status(Mode mode) const noexcept {
        const int i = index(mode);
        if (mode == Mode::Off) return Status::Off;
        if (i < 0 || i >= 2) return Status::Unsupported;
        if (failed_[i]) return Status::SessionDisabled;
        if (!capabilities_[i].sdkLinked) return Status::SdkMissing;
        return capabilities_[i].supported ? Status::Ready : Status::Unsupported;
    }
    bool request(Mode mode, uint32_t intervalUs = 0) noexcept {
        HUD_SCOPE(LatencyRequest);
        if (mode != Mode::Off && (!selectable(mode) || mode != Mode::Reflex)) return false;
        if (mode == active_ && (mode == Mode::Off || intervalUs == interval_)) return true;
        if (status(Mode::Reflex) == Status::Ready && !backends_[0]->configure(mode == Mode::Reflex, mode == Mode::Reflex ? intervalUs : 0)) {
            failSession(Mode::Reflex); return false;
        }
        active_ = mode; interval_ = intervalUs; return true;
    }
    void failSession(Mode mode) noexcept {
        const int i = index(mode); if (i < 0 || i >= 2) return;
        failed_[i] = true; capabilities_[i] = {};
        if (backends_[i]) backends_[i]->release();
        active_ = Mode::Off; phase_ = Phase::Idle;
    }
    bool beginFrame() noexcept {
        if (phase_ != Phase::Idle) return false; // Never sleep twice for a retry/paint.
        if (status(Mode::Reflex) != Status::Ready) return false;
        {HUD_SCOPE(ReflexSleep);if ((!HUD_LEGACY_REFLEX()||active_==Mode::Reflex)&&!backends_[0]->sleep()) { failSession(Mode::Reflex); return false; }}
        ++frame_;
        if (!emit(Marker::SimulationStart)) return false;
        phase_ = Phase::Simulation;
        return emit(Marker::InputSample);
    }
    void renderStart() noexcept {
        if (phase_ != Phase::Simulation) return;
        if (emit(Marker::SimulationEnd) && emit(Marker::RenderStart)) phase_ = Phase::Render;
    }
    void renderEnd() noexcept {
        if (phase_ == Phase::Render && emit(Marker::RenderEnd)) phase_ = Phase::Rendered;
    }
    void presentStart() noexcept {
        if (phase_ == Phase::Rendered && emit(Marker::PresentStart)) phase_ = Phase::Present;
    }
    void presentEnd() noexcept {
        if (phase_ == Phase::Present && emit(Marker::PresentEnd)) phase_ = Phase::Idle;
    }
    void endFrame() noexcept {
        // Deferred frames have no Present markers; never invent a second submission.
        if (phase_ == Phase::Simulation) emit(Marker::SimulationEnd);
        else if (phase_ == Phase::Render) emit(Marker::RenderEnd);
        phase_ = Phase::Idle;
    }
    Mode requested() const noexcept { return active_; }
    Mode active() const noexcept { return active_; }
    WaitOwner waitOwner() const noexcept { return active_ == Mode::Reflex ? WaitOwner::Reflex : WaitOwner::ExistingFramePacer; }
    bool selectable(Mode mode) const noexcept { return mode == Mode::Off || (mode == Mode::Reflex && status(mode) == Status::Ready); }
    uint64_t frameId() const noexcept { return frame_; }
    bool enabledInDriver() noexcept { return status(Mode::Reflex) == Status::Ready && backends_[0]->enabledInDriver(); }
};
}
