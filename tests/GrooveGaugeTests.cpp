#include "GrooveGauge.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>

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
        gauge.setCompatibilityEnabled(true);
        gauge.configure(1000, GrooveGaugeMode::Normal, 300);
        require(gauge.compatibilityActive(), "compatibility test mode should activate");
        gauge.apply(TimingJudgement::PGreat);
        expectNear(gauge.value(), 20.3, "compatibility NORMAL uses TOTAL");
        gauge.apply(TimingJudgement::Bad);
        expectNear(gauge.value(), 17.3, "compatibility NORMAL BAD loses 3");
        gauge.damage(100);expectNear(gauge.value(), 2, "compatibility NORMAL floors at 2");
        gauge.configure(1000, GrooveGaugeMode::Hard, 300);
        for(int i=0;i<10;++i)gauge.apply(TimingJudgement::Poor);
        expectNear(gauge.value(), 14, "compatibility HARD GUTS applies before damage");
        gauge.holdTick(false);expectNear(gauge.value(), 12.75, "HCN damage uses half BAD and GUTS");
        for(int i=0;i<20;++i)gauge.apply(TimingJudgement::Poor);
        require(gauge.depleted() && !gauge.clearConditionMet(), "compatibility HARD zero must fail");
        gauge.apply(TimingJudgement::PGreat);require(gauge.depleted(), "compatibility HARD must not revive");
        gauge.reset();expectNear(gauge.value(),100, "compatibility reset must reset bank");
        gauge.configureAuthored(L"normal",L"off");
        require(!gauge.compatibilityActive(), "authored gauge must retain its own rules");
        gauge.configure(0, GrooveGaugeMode::Normal, 300);
        require(!gauge.compatibilityActive(), "zero notes must safely fall back");
        using sevenmania::independent_gauge::Kind;
        using AutoShiftMode=sevenmania::independent_gauge::Mode;
        for(const auto& fixture : std::array<std::pair<Kind,double>,6>{{
            {Kind::AssistEasy,18.5},{Kind::Easy,18.5},{Kind::Normal,17},
            {Kind::Hard,95},{Kind::ExHard,92},{Kind::Hazard,0}}}) {
            gauge.setCompatibilityTrialOptions(fixture.first,AutoShiftMode::None,Kind::AssistEasy);
            gauge.configure(1000,GrooveGaugeMode::Normal,300);
            require(gauge.compatibilityKind()==fixture.first,"selected trial gauge mismatch");
            gauge.apply(TimingJudgement::Bad);
            expectNear(gauge.value(),fixture.second,"trial gauge BAD value");
            require(gauge.depleted()==(fixture.first==Kind::Hazard),"trial gauge failure mismatch");
        }
        gauge.setCompatibilityTrialOptions(Kind::Hard,AutoShiftMode::SurvivalToGroove,Kind::AssistEasy);
        gauge.configure(1000,GrooveGaugeMode::Normal,300);
        for(int i=0;i<30;++i)gauge.apply(TimingJudgement::Poor);
        require(gauge.compatibilityKind()==Kind::Normal && !gauge.depleted(), "auto shift should continue with normal gauge");
        expectNear(gauge.value(),2,"normal gauge must be updated from the beginning");
        gauge.setCompatibilityTrialOptions(Kind::Hazard,AutoShiftMode::Continue,Kind::AssistEasy);
        gauge.configure(1000,GrooveGaugeMode::Normal,300);gauge.apply(TimingJudgement::Bad);
        require(!gauge.depleted() && !gauge.clearConditionMet() && gauge.value()==0,"continue mode must not fail or revive hazard");
        gauge.setCompatibilityTrialOptions(Kind::Normal,AutoShiftMode::BestClear,Kind::Normal);
        gauge.configure(1000,GrooveGaugeMode::Normal,300);
        require(gauge.compatibilityKind()==Kind::Hazard,"best clear must choose highest qualified gauge");
        gauge.apply(TimingJudgement::Bad);
        require(gauge.compatibilityKind()==Kind::ExHard,"best clear must update display after hazard fails");
        gauge.setCompatibilityTrialOptions(Kind::ExHard,AutoShiftMode::SelectToUnder,Kind::Easy);
        gauge.configure(1000,GrooveGaugeMode::Normal,300);
        for(int i=0;i<7;++i)gauge.apply(TimingJudgement::Poor);
        require(gauge.compatibilityKind()==Kind::Hard && !gauge.depleted(),"select-to-under must use the surviving HARD gauge");
        expectNear(gauge.value(),32,"shifted HARD must retain its accumulated value");
        gauge.setCompatibilityTrialOptions(Kind::Normal,AutoShiftMode::None,Kind::AssistEasy,sevenmania::independent_gauge::Profile::BeatorajaLR2);
        gauge.configure(1000,GrooveGaugeMode::Normal,300);gauge.apply(TimingJudgement::Bad);
        expectNear(gauge.value(),16,"LR2 trial must use LR2 NORMAL BAD damage");
        gauge.setCompatibilityTrialOptions(Kind::Hard,AutoShiftMode::None,Kind::AssistEasy,sevenmania::independent_gauge::Profile::LR2oraja);
        gauge.configure(1000,GrooveGaugeMode::Normal,300);gauge.damage(69);gauge.apply(TimingJudgement::Bad);
        expectNear(gauge.value(),27.4,"LR2oraja HARD must reduce damage below 32 percent");
        gauge.configure(1000,GrooveGaugeMode::Normal,300);gauge.damage(98);
        require(!gauge.depleted() && gauge.value()==2,"LR2oraja HARD must survive at exactly 2 percent");
        gauge.damage(.1);require(gauge.depleted() && gauge.value()==0,"LR2oraja HARD must fail below 2 percent");
        gauge.setCompatibilityTrialOptions(std::nullopt,AutoShiftMode::None,Kind::AssistEasy);
        gauge.setCompatibilityEnabled(false);gauge.configure(200, GrooveGaugeMode::Normal);
        gauge.apply(TimingJudgement::Bad);expectNear(gauge.value(),18, "normal mode must retain legacy rules");
        std::cout << "Groove gauge: legacy and experimental compatibility paths passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
