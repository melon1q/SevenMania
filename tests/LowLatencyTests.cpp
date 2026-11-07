#include "LowLatency.h"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <utility>
void require(bool v, const char* message) { if (!v) throw std::runtime_error(message); }
struct State {
    int probes{}, releases{}, waits{}, configurations{};
    bool supported=true, enabled{}, failConfigure{}, failSleep{}, failMarker{};
    uint32_t interval{};
    std::vector<std::pair<uint64_t, latency::Marker>> markers;
};
class ProbeBackend final : public latency::Backend {
    State& s_;
public:
    ProbeBackend(State& s): s_(s) {}
    latency::Capability probe(latency::Device) noexcept override { ++s_.probes; return {true,s_.supported}; }
    bool configure(bool on, uint32_t interval) noexcept override { ++s_.configurations; s_.enabled=on; s_.interval=interval; return !s_.failConfigure; }
    bool sleep() noexcept override { ++s_.waits; return !s_.failSleep; }
    bool enabledInDriver() noexcept override { return s_.enabled; }
    bool marker(uint64_t id, latency::Marker m) noexcept override { s_.markers.emplace_back(id,m); return !s_.failMarker; }
    void release() noexcept override { ++s_.releases; s_.enabled=false; }
};
int main() {
    using namespace latency;
    try {
        State s;
        Controller c;
        require(c.active()==Mode::Off && c.waitOwner()==WaitOwner::ExistingFramePacer, "default owner changed");
        require(!c.request(Mode::Reflex) && !c.beginFrame(), "missing SDK activated");
        c.install(Mode::Reflex,std::make_unique<ProbeBackend>(s));
        int device{}; c.attach({&device,0x10de});
        require(c.selectable(Mode::Reflex) && !c.selectable(Mode::AntiLag2), "vendor capability leaked");
        require(c.beginFrame(), "OFF markers unavailable");
        require(!c.beginFrame()&&s.waits==1,"OFF duplicated SDK sleep");
        c.renderStart(); c.renderEnd(); c.presentStart(); c.presentEnd(); c.endFrame();
        require(s.waits==1, "OFF skipped SDK sleep");
        const std::vector<Marker> expected{Marker::SimulationStart,Marker::InputSample,Marker::SimulationEnd,
            Marker::RenderStart,Marker::RenderEnd,Marker::PresentStart,Marker::PresentEnd};
        require(s.markers.size()==expected.size(), "marker count incorrect");
        for (size_t i=0;i<expected.size();++i) require(s.markers[i].first==1 && s.markers[i].second==expected[i], "marker ID/order mismatch");
        require(c.request(Mode::Reflex,667) && s.enabled && s.interval==667 && c.waitOwner()==WaitOwner::Reflex, "Reflex did not take wait ownership");
        const int configurations=s.configurations;
        require(c.request(Mode::Reflex,667) && s.configurations==configurations, "unchanged settings reconfigured SDK");
        require(c.beginFrame() && !c.beginFrame() && s.waits==2, "duplicate Sleep within frame");
        c.renderStart(); c.renderEnd(); c.endFrame(); // busy presentation queue: no Present/retry/sleep.
        require(s.markers.back().second==Marker::RenderEnd, "deferred frame fabricated Present");
        require(c.beginFrame() && c.frameId()==3 && s.waits==3, "deferred frame reused ID");
        c.endFrame();
        require(c.request(Mode::Off) && !s.enabled && c.waitOwner()==WaitOwner::ExistingFramePacer, "OFF did not restore pacing");
        require(c.request(Mode::Reflex,4167), "could not re-enable");
        s.failSleep=true; require(!c.beginFrame() && c.status(Mode::Reflex)==Status::SessionDisabled && c.active()==Mode::Off && !s.enabled, "Sleep failure did not restore default path");
        const int probes=s.probes; c.attach({&device,0x10de});
        require(s.probes==probes && !c.request(Mode::Reflex), "failed session retried after device recreation");
        for (int failure=0;failure<2;++failure) {
            State f; Controller broken; broken.install(Mode::Reflex,std::make_unique<ProbeBackend>(f)); broken.attach({&device,0x10de});
            f.failConfigure=failure==0; f.failMarker=failure==1;
            if (!failure) require(!broken.request(Mode::Reflex,667), "configuration failure ignored");
            else require(!broken.beginFrame(), "marker failure ignored");
            require(broken.status(Mode::Reflex)==Status::SessionDisabled && broken.waitOwner()==WaitOwner::ExistingFramePacer, "failure retained wait ownership");
        }
        {State failed;Controller fallback;fallback.install(Mode::Reflex,std::make_unique<ProbeBackend>(failed));fallback.attach({&device,0x10de});failed.failSleep=true;
        require(!fallback.beginFrame()&&fallback.status(Mode::Reflex)==Status::SessionDisabled&&fallback.waitOwner()==WaitOwner::ExistingFramePacer,"OFF SDK Sleep failure did not fall back safely");}
        // Stage 5: FPS changes never add a second wait; OFF restores ordinary pacing.
        State recovery; Controller recovered;
        recovered.install(Mode::Reflex, std::make_unique<ProbeBackend>(recovery));
        recovered.attach({&device,0x10de});
        for (const uint32_t interval : {667u,4167u,667u}) {
            require(recovered.request(Mode::Reflex, interval), "FPS change rejected");
            const int waits = recovery.waits;
            require(recovered.beginFrame() && recovery.waits == waits+1, "FPS change duplicated/skipped SDK wait");
            recovered.endFrame();
            require(recovered.request(Mode::Off) && recovery.interval == 0, "OFF retained SDK FPS cap");
            require(recovered.beginFrame() && recovery.waits == waits+2, "OFF skipped SDK sleep");
            recovered.endFrame();
        }
        // Stage 6: pause/OFF, resume/ON, device teardown, recreation, then CPU fallback.
        require(recovered.request(Mode::Reflex,4167) && recovered.beginFrame(), "resume setup failed");
        recovered.endFrame();
        const auto beforeReset = recovered.frameId();
        recovered.detach();
        require(!recovery.enabled && recovered.active()==Mode::Off &&
            recovered.waitOwner()==WaitOwner::ExistingFramePacer, "device teardown retained Reflex");
        recovered.attach({&device,0x10de});
        require(recovered.request(Mode::Reflex,4167) && recovered.beginFrame() &&
            recovered.frameId()>beforeReset, "device recreation lost monotonic markers/resume");
        recovered.endFrame();
        recovery.supported=false;
        recovered.attach({&device,0x8086});
        require(!recovered.selectable(Mode::Reflex) && !recovered.request(Mode::Reflex) &&
            recovered.waitOwner()==WaitOwner::ExistingFramePacer, "unsupported replacement device retained SDK wait");
        std::cout << "Reflex marker IDs/order, OFF baseline, single Sleep, deferred frames, cached configuration and session fallback passed.\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
