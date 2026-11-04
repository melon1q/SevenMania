#include "GrooveGauge.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void expectNear(double actual, double expected, const char* message) {
    require(std::abs(actual - expected) < 0.00001, message);
}

// Exercise runtime judgement dispatch; MSVC optimization of the fully constant fixture crashes CL.exe.
__declspec(noinline) void applyJudgement(GrooveGauge& gauge, TimingJudgement judgement) { gauge.apply(judgement); }

int main() {
    try {
        GrooveGauge gauge;
        expectNear(gauge.value(), 20.0, "normal starts at 20 percent");
        gauge.configure(200, GrooveGaugeMode::Normal);
        applyJudgement(gauge, TimingJudgement::Great);
        expectNear(gauge.value(), 21.0, "normal GREAT adds 200/N");
        applyJudgement(gauge, TimingJudgement::PGreat);
        expectNear(gauge.value(), 22.0, "normal PGREAT adds 200/N");
        applyJudgement(gauge, TimingJudgement::Good);
        expectNear(gauge.value(), 22.5, "normal GOOD adds half gain");
        applyJudgement(gauge, TimingJudgement::Bad);
        expectNear(gauge.value(), 20.5, "normal BAD loses 2 percent");
        applyJudgement(gauge, TimingJudgement::Poor);
        expectNear(gauge.value(), 14.5, "normal POOR loses 6 percent");
        require(!gauge.depleted() && !gauge.clearConditionMet(), "normal has no gauge-failure condition and needs 80 percent to clear");
        gauge.configure(200, GrooveGaugeMode::Easy);
        expectNear(gauge.value(), 20.0, "easy starts at 20 percent");
        applyJudgement(gauge, TimingJudgement::Good);
        expectNear(gauge.value(), 20.5, "easy GOOD adds half gain");
        applyJudgement(gauge, TimingJudgement::Bad);
        expectNear(gauge.value(), 18.9, "easy BAD loses 1.6 percent");
        applyJudgement(gauge, TimingJudgement::Poor);
        expectNear(gauge.value(), 14.1, "easy POOR loses 4.8 percent");
        gauge.configure(500, GrooveGaugeMode::Hard);
        expectNear(gauge.value(), 100.0, "hard starts full");
        applyJudgement(gauge, TimingJudgement::Great);
        expectNear(gauge.value(), 100.0, "hard GREAT gain is capped");
        applyJudgement(gauge, TimingJudgement::Good);
        expectNear(gauge.value(), 100.0, "hard GOOD does not change gauge");
        applyJudgement(gauge, TimingJudgement::Bad);
        expectNear(gauge.value(), 95.0, "hard BAD loses 5 percent");
        applyJudgement(gauge, TimingJudgement::Great);
        expectNear(gauge.value(), 95.16, "hard GREAT adds 0.16 percent");
        applyJudgement(gauge, TimingJudgement::Poor);
        expectNear(gauge.value(), 86.16, "hard POOR loses 9 percent");
        require(!gauge.depleted() && gauge.clearConditionMet(), "hard clears when gauge survives above zero");
        for (int i = 0; i < 10; ++i) applyJudgement(gauge, TimingJudgement::Poor);
        expectNear(gauge.value(), 0.0, "hard gauge floors at zero");
        require(gauge.depleted() && !gauge.clearConditionMet(), "hard zero gauge must fail");
        applyJudgement(gauge, TimingJudgement::PGreat);
        require(gauge.depleted(), "hard empty gauge must not revive past zero");
        gauge.reset();
        expectNear(gauge.value(), 100.0, "reset gauge");
        gauge.configure(10, GrooveGaugeMode::Easy);
        for (int i = 0; i < 3; ++i) applyJudgement(gauge, TimingJudgement::Great);
        require(gauge.clearConditionMet(), "normal/easy clear threshold is 80 percent");
        for (int i = 0; i < 20; ++i) applyJudgement(gauge, TimingJudgement::PGreat);
        expectNear(gauge.value(), 100.0, "gauge is capped at 100 percent");
        std::cout << "Groove gauge: normal/easy/hard formulas, clear and failure rules passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
