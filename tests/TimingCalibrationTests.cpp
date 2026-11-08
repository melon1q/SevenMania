#include "TimingCalibration.h"
#include "TimingOffsets.h"

#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static bms::AutoJudgeAdjuster filled(double bias, double sigma, int n, unsigned seed = 7) {
    std::mt19937 rng(seed); std::normal_distribution<double> noise(bias, sigma);
    bms::AutoJudgeAdjuster a; for (int i = 0; i < n; ++i) a.feed(noise(rng)); return a;
}

int main() {
    try {
        // Offsets: sign conventions and rate scaling.
        bms::TimingOffsets o; o.audioMs = 40; o.displayMs = 10; o.judgeMs = -5;
        require(std::abs(bms::heardSeconds(1.0, o, 1.0) - 0.96) < 1e-9, "audio latency delays the heard clock");
        require(std::abs(bms::heardSeconds(1.0, o, 2.0) - 0.92) < 1e-9, "offset scales with playback rate");
        require(std::abs(bms::judgeSeconds(1.0, o, 1.0) - 1.005) < 1e-9, "judge offset only moves judgement");
        require(std::abs(bms::drawSeconds(1.0, o, 1.0) - 1.01) < 1e-9, "display offset only moves drawing");
        require(bms::clampOffsetMs(999) == 200 && bms::clampOffsetMs(-999) == -200, "offset clamp");

        // Not enough data: no estimate.
        require(!filled(10, 5, 29).suggestion(0), "needs at least 30 samples");

        // Consistent late bias is found, with the right sign.
        auto late = filled(12, 6, 100);
        auto target = late.suggestion(0);
        require(target && *target >= 10 && *target <= 14, "late bias estimated");
        auto early = filled(-9, 6, 100);
        target = early.suggestion(0);
        require(target && *target <= -7 && *target >= -11, "early bias estimated");

        // Small bias inside the deadband is ignored.
        require(!filled(1.0, 6, 100).suggestion(0), "deadband keeps small bias");

        // Already corrected: judge offset matches bias, so nothing to change (no feedback loop).
        require(!filled(12, 6, 100).suggestion(12), "no change once corrected");

        // Outliers (accidental presses) do not move the estimate much.
        bms::AutoJudgeAdjuster noisy = filled(8, 4, 90);
        for (int i = 0; i < 10; ++i) noisy.feed(i % 2 ? 180.0 : -170.0);
        target = noisy.suggestion(0);
        require(target && *target >= 6 && *target <= 10, "outliers rejected");

        // Realtime stepping is damped to 2 ms per update and respects the interval.
        auto stepper = filled(30, 3, 100);
        int judge = 0;
        require(stepper.step(judge) == 2, "step capped at 2ms");
        require(stepper.step(2) == 2, "no second step before the update interval");
        for (int i = 0; i < 20; ++i) stepper.feed(30.0);
        require(stepper.step(2) == 4, "next step after interval");

        // Persistence check: bias that only appears in the newer half is not applied.
        bms::AutoJudgeAdjuster shifting;
        for (int i = 0; i < 50; ++i) shifting.feed(0.0);
        for (int i = 0; i < 50; ++i) shifting.feed(9.0);
        require(!shifting.suggestion(0), "inconsistent halves are not applied");

        // Reset clears history.
        late.reset(); require(!late.suggestion(0) && late.samples() == 0, "reset clears");
        std::cout << "TimingCalibrationTests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
}
