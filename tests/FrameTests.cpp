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
};

int main() {
    const bool resolution = timeBeginPeriod(1) == TIMERR_NOERROR;
    int result = 0;
    try {
        FramePacerTestAccess::verifyExpensiveUnlimitedFrame();
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
