#include "GaugeCompatibility.h"
#include "GaugeAutoShift.h"
#include "GaugeTrialOptions.h"
#include <stdexcept>
#define VERIFY(condition) do { if(!(condition)) throw std::runtime_error(#condition); } while(false)
#include <cmath>
#include <iostream>
using namespace sevenmania::gauge;
int main(){Bank b;VERIFY(!b.configure(Profile::SevenKeys,200,0));VERIFY(!b.configure(Profile::SevenKeys,-1,1000));VERIFY(b.configure(Profile::SevenKeys,300,1000));
 VERIFY(b.value(Kind::Normal)==20);b.apply(Event::PGreat);VERIFY(std::abs(b.value(Kind::Normal)-20.3f)<.00001f);
 b.apply(Event::EmptyPoor);VERIFY(std::abs(b.value(Kind::Normal)-18.3f)<.00001f);b.apply(Event::Poor);VERIFY(std::abs(b.value(Kind::Normal)-12.3f)<.00001f);
 for(int i=0;i<100;++i)b.apply(Event::Poor);VERIFY(b.value(Kind::Normal)==2);VERIFY(b.value(Kind::Hard)==0);b.apply(Event::PGreat);VERIFY(b.value(Kind::Hard)==0);
 VERIFY(b.configure(Profile::SevenKeys,300,1000));for(int i=0;i<1000;++i)b.apply(Event::PGreat);VERIFY(b.value(Kind::Normal)==100&&b.qualified(Kind::Normal));
 struct Boundary { float initial; float factor; };
 constexpr Boundary boundaries[] = {{9.99f,.4f},{10.f,.5f},{19.99f,.5f},{20.f,.6f},
   {29.99f,.6f},{30.f,.7f},{39.99f,.7f},{40.f,.8f},{49.99f,.8f},{50.f,1.f}};
 for(const auto& boundary : boundaries) {
  VERIFY(b.configure(Profile::SevenKeys,300,1000));
  b.setValue(Kind::Hard,boundary.initial);
  b.apply(Event::Bad);
  VERIFY(std::abs(b.value(Kind::Hard)-(boundary.initial-5.f*boundary.factor))<.00001f);
 }
 VERIFY(b.configure(Profile::BeatorajaLR2,300,1000));b.apply(Event::Bad);VERIFY(std::abs(b.value(Kind::Normal)-16)<.00001f);VERIFY(b.value(Kind::Hard)<100);
 // HCN body ticks are GREAT/BAD at half rate, not PGREAT/POOR.
 VERIFY(b.configure(Profile::SevenKeys,300,1000));b.setValue(Kind::Hard,50);b.hcnTick(true);
 VERIFY(std::abs(b.value(Kind::Hard)-50.06f)<.00001f);
 b.hcnTick(false);VERIFY(std::abs(b.value(Kind::Hard)-47.56f)<.00001f);
 // Mines subtract their own damage without HARD GUTS; each gauge is updated.
 VERIFY(b.configure(Profile::SevenKeys,300,1000));b.setValue(Kind::Hard,10);
 b.mineDamage(5);VERIFY(b.value(Kind::Hard)==5);VERIFY(b.value(Kind::Normal)==15);
 b.mineDamage(100);VERIFY(b.value(Kind::Hard)==0 && b.value(Kind::Normal)==2);
 b.mineDamage(1);VERIFY(b.value(Kind::Hard)==0);
 VERIFY(!b.configure(Profile::SevenKeys,300,0));VERIFY(b.value(Kind::Normal)==0);

 AutoShift shift;
 VERIFY(b.configure(Profile::SevenKeys,300,1000));
 VERIFY(shift.configure(Kind::Hard,AutoShiftMode::None,Kind::Normal));
 b.setValue(Kind::Hard,1);b.apply(Event::Poor);shift.update(b);VERIFY(shift.failed());
 VERIFY(b.configure(Profile::SevenKeys,300,1000));
 VERIFY(shift.configure(Kind::Hard,AutoShiftMode::Continue,Kind::Normal));
 b.setValue(Kind::Hard,1);b.apply(Event::Poor);shift.update(b);
 VERIFY(!shift.failed() && shift.current()==Kind::Hard);
 VERIFY(b.configure(Profile::SevenKeys,300,1000));
 VERIFY(shift.configure(Kind::Hard,AutoShiftMode::SurvivalToGroove,Kind::Normal));
 b.setValue(Kind::Hard,1);b.apply(Event::Poor);shift.update(b);
 VERIFY(!shift.failed() && shift.current()==Kind::Normal && shift.changed());
 VERIFY(b.configure(Profile::SevenKeys,300,1000));
 VERIFY(shift.configure(Kind::Normal,AutoShiftMode::BestClear,Kind::Easy));
 shift.update(b);VERIFY(shift.current()==Kind::Hazard);
 b.setValue(Kind::Hazard,0);b.setValue(Kind::ExHard,0);shift.update(b);VERIFY(shift.current()==Kind::Hard);
 VERIFY(b.configure(Profile::SevenKeys,300,1000));
 VERIFY(shift.configure(Kind::Normal,AutoShiftMode::SelectToUnder,Kind::Easy));
 shift.update(b);VERIFY(shift.current()==Kind::Easy);
 b.setValue(Kind::Normal,80);shift.update(b);VERIFY(shift.current()==Kind::Normal);
 VERIFY(b.configure(Profile::SevenKeys,300,1000));
 VERIFY(shift.configure(Kind::ExClass,AutoShiftMode::SurvivalToGroove,Kind::Normal));
 b.setValue(Kind::ExClass,0);shift.update(b);VERIFY(shift.current()==Kind::ExClass);
 VERIFY(b.configure(Profile::SevenKeys,300,1000));
 VERIFY(shift.configure(Kind::Hard,AutoShiftMode::BestClear,Kind::Normal));
 b.setValue(Kind::Hard,0);b.setValue(Kind::ExHard,0);b.setValue(Kind::Hazard,0);
 shift.update(b);VERIFY(shift.current()==Kind::Normal);
 VERIFY(b.configure(Profile::SevenKeys,300,1000));
 VERIFY(shift.configure(Kind::ExClass,AutoShiftMode::SelectToUnder,Kind::Normal));
 shift.update(b);VERIFY(shift.current()==Kind::ExClass);
 b.setValue(Kind::ExClass,0);shift.update(b);VERIFY(shift.current()==Kind::Class);
 for(int play=0;play<4;++play){
  VERIFY(b.configure(Profile::SevenKeys,300,1000));
  VERIFY(shift.configure(Kind::Hard,AutoShiftMode::None,Kind::Normal));
  b.setValue(Kind::Hard,1);b.apply(Event::Poor);shift.update(b);VERIFY(shift.failed());
  VERIFY(b.configure(Profile::BeatorajaLR2,300,1000));
  VERIFY(shift.configure(Kind::Normal,AutoShiftMode::None,Kind::Normal));
  VERIFY(!shift.failed() && shift.current()==Kind::Normal && b.value(Kind::Normal)==20);
 }
 VERIFY(!shift.configure(Kind::Count,AutoShiftMode::None,Kind::Normal));
 sevenmania::gauge_trial::TrialOptions options;
 using sevenmania::gauge_trial::TrialOptionResult;
 using sevenmania::gauge_trial::parseTrialOption;
 VERIFY(parseTrialOption(L"--gauge-kind=ex-hard",options)==TrialOptionResult::Applied && options.selected==sevenmania::independent_gauge::Kind::ExHard);
 VERIFY(parseTrialOption(L"--gauge-bottom=easy",options)==TrialOptionResult::Applied && options.bottom==sevenmania::independent_gauge::Kind::Easy);
 VERIFY(parseTrialOption(L"--gauge-auto-shift=survival-to-groove",options)==TrialOptionResult::Applied && options.shift==sevenmania::independent_gauge::Mode::SurvivalToGroove);
 VERIFY(parseTrialOption(L"--gauge-profile=lr2",options)==TrialOptionResult::Applied && options.profile==sevenmania::independent_gauge::Profile::BeatorajaLR2);
 VERIFY(parseTrialOption(L"--gauge-profile=lr2oraja",options)==TrialOptionResult::Applied && options.profile==sevenmania::independent_gauge::Profile::LR2oraja);
 VERIFY(parseTrialOption(L"--gauge-profile=unknown",options)==TrialOptionResult::Invalid && options.profile==sevenmania::independent_gauge::Profile::LR2oraja);
 VERIFY(parseTrialOption(L"--gauge-kind=unknown",options)==TrialOptionResult::Invalid && options.selected==sevenmania::independent_gauge::Kind::ExHard);
 VERIFY(parseTrialOption(L"--unrelated",options)==TrialOptionResult::Unrecognized);
 std::cout<<"PASS: gauge events, HCN, mine damage and auto shift\n";}
