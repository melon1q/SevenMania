#include "FramePacer.h"
#include "AudioPositionClock.h"
#include <mmsystem.h>
#include <iostream>
#include <stdexcept>
#include <cmath>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct FramePacerTestAccess {
    static void verifyExpensiveCappedFrame(int fps){
        FramePacer p;p.reset(fps);p.frameStarted_=std::chrono::steady_clock::now()-std::chrono::milliseconds(3);p.deadline_=p.frameStarted_;p.advance();require(p.due(),"Over-budget capped frame skipped another grid slot");
        p.advance();require(!p.due(),"Capped frame bypassed minimum start interval");
    }
    static void verifyExpensiveUnlimitedFrame() {
        FramePacer pacer;
        pacer.reset(0);
        pacer.frameStarted_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(2);
        pacer.deadline_ = pacer.frameStarted_;
        pacer.advance();
        require(pacer.due(), "unlimited frame already over budget was forced to wait another slot");
        pacer.advance();
        require(!pacer.due(), "cheap unlimited frame bypassed the 1500 FPS ceiling");
        require(pacer.pollBeforeDeadline(), "unlimited sub-millisecond wait fell back to timer sleep");
    }
    static void verifyVblankAlignment() {
        using namespace std::chrono;
        const auto refresh = nanoseconds(4'166'667); const auto lead = microseconds(2000);
        const auto now = steady_clock::now(); const auto vblank = now - microseconds(1000);
        // VSync: start lead before the next vblank, one frame per refresh.
        FramePacer v; v.reset(-1); v.alignToVblank(vblank, refresh, lead); v.advanceToVblank(now);
        auto first = v.deadline_; require(first > now && first + lead - vblank == refresh, "VSync start not lead before next vblank");
        v.advanceToVblank(now); require(v.deadline_ == first + refresh, "early VSync frame ran twice for one vblank");
        // Cap 240 at 240 Hz aligns; cap 120 takes every second vblank; cap 144 keeps the grid.
        FramePacer c; c.reset(240); c.alignToVblank(vblank, refresh, lead); c.advance(); require(c.vblankAligned(), "cap at refresh did not align");
        FramePacer h; h.reset(120); h.alignToVblank(vblank, refresh, lead); h.advance(); require(h.vblankAligned(), "cap at half refresh did not align");
        const auto half = h.deadline_; h.advanceToVblank(steady_clock::now()); require(h.deadline_ - half == refresh * 2, "half-refresh cap did not skip a vblank");
        FramePacer o; o.reset(144); o.alignToVblank(vblank, refresh, lead); o.advance(); require(!o.vblankAligned(), "non-multiple cap was aligned");
        FramePacer f; f.reset(600); f.alignToVblank(vblank, refresh, lead); f.advance(); require(!f.vblankAligned(), "cap above refresh was aligned");
        f.clearVblank(); FramePacer n; n.reset(-1); n.advance(); require(!n.vblankAligned(), "alignment active without vblank data");
        require(c.pollBeforeDeadline() == (c.deadline_ - steady_clock::now() < microseconds(500)), "aligned poll window wrong");
        // Ready-wake fallback: suspended alignment resumes after the requested frames.
        FramePacer s; s.reset(-1); s.alignToVblank(vblank, refresh, lead); s.advance(); require(s.vblankAligned(), "VSync did not align");
        s.suspendVblank(2); s.advance(); require(!s.vblankAligned(), "suspended pacer stayed aligned");
        s.advance(); require(s.vblankAligned(), "pacer did not resume alignment after suspension");
        s.reset(-1); require(s.suspendedFrames() == 0, "reset kept a suspension");
    }
};

int main() {
    const bool resolution = timeBeginPeriod(1) == TIMERR_NOERROR;
    int result = 0;
    try {
        FramePacerTestAccess::verifyExpensiveUnlimitedFrame();
        FramePacerTestAccess::verifyVblankAlignment();
        for(int fps:{600,1000})FramePacerTestAccess::verifyExpensiveCappedFrame(fps);
        AudioPositionClock clock;
        require(clock.update(0.0, 10.0) == 0.0, "clock started before audio output");
        double previous = 0;
        unsigned movingFrames = 0;
        for (int frame = 1; frame <= 240; ++frame) {
            const double wall = frame / 240.0;
            const double raw = std::floor(wall / 0.016) * 0.016;
            const double position = clock.update(raw, 10.0 + wall);
            require(position >= previous, "audio clock moved backwards");
            require(position <= raw + 0.125001, "audio clock ran too far ahead of output");
            if (position > previous) ++movingFrames;
            previous = position;
        }
        require(movingFrames > 220, "240 FPS positions retained coarse driver steps");
        const double stalled = clock.update(1.0, 20.0);
        require(stalled <= 1.125001, "stalled output did not bound interpolation");
        const double bounded = clock.update(1.0, 21.0);
        require(bounded <= 1.125001, "stalled output exceeded interpolation bound");
        require(clock.update(1.0, 22.0) == bounded, "stalled output continued advancing after bound");
        clock.reset();
        require(clock.update(0.0, 30.0) == 0.0, "audio clock reset failed");

        for (double reportInterval : {0.032, 0.064, 0.120}) {
            AudioPositionClock coarseClock;
            double last = 0; unsigned continuous = 0;
            for (int frame = 1; frame <= 800; ++frame) {
                const double wall = frame / 400.0;
                const double raw = std::floor(wall / reportInterval) * reportInterval;
                const double position = coarseClock.update(raw, wall);
                if (last > 0) {
                    const double delta = position - last;
                    require(delta >= 0.0025 * 0.979 && delta <= 0.0025 * 1.021, "coarse driver report causes clock stall or jump");
                    ++continuous;
                }
                last = position;
            }
            require(continuous > 730, "coarse clock retained stepped movement");
        }

        for (int target : {144, 240, 0, vsyncFps}) {
            FramePacer pacer;
            pacer.reset(target);
            const auto start = std::chrono::steady_clock::now();
            unsigned frames = 0;
            double maxGapMs = 0;
            auto previousFrame = start;
            while (std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < 1.5) {
                if (!pacer.due()) {
                    if (pacer.pollBeforeDeadline()) { YieldProcessor(); continue; }
                    HANDLE timer = pacer.arm();
                    require(timer != nullptr, "frame timer could not be armed");
                    require(WaitForSingleObject(timer, 1000) == WAIT_OBJECT_0, "frame timer timeout");
                    continue;
                }
                const auto now = std::chrono::steady_clock::now();
                if (frames > 0) maxGapMs = std::max(maxGapMs, std::chrono::duration<double, std::milli>(now - previousFrame).count());
                previousFrame = now;
                ++frames;
                pacer.advance();
            }
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            const double measured = frames / seconds;
            std::cout << "Target " << target << " FPS: measured " << measured << ", max scheduling gap " << maxGapMs << " ms\n";
            const int limit = target <= 0 ? unlimitedFpsLimit : target;
            if (target > 0) require(measured >= target * 0.85, "frame scheduler is below target (or stuck at 60 FPS)");
            if (target == 0) require(measured >= unlimitedFpsLimit * 0.85, "unlimited scheduler loses throughput while enforcing its ceiling");
            require(frames <= static_cast<unsigned>(std::ceil(seconds * limit)) + 1, "frame scheduler exceeds target/unlimited ceiling");
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    if (resolution) timeEndPeriod(1);
    return result;
}
