// SPDX-License-Identifier: GPL-3.0-only
// Adapted from beatoraja BMSPlayer.java by exch and contributors, 2026-11-11.
#pragma once
#include "GaugeCompatibility.h"
#include <algorithm>

namespace sevenmania::gauge {
enum class AutoShiftMode { None, Continue, SurvivalToGroove, BestClear, SelectToUnder };

// Updates only the selected display gauge and stage-failure signal. The Bank
// must already have received each event for all gauges before update().
class AutoShift {
 Kind selected_{Kind::Normal};
 Kind current_{Kind::Normal};
 Kind bottom_{Kind::AssistEasy};
 AutoShiftMode mode_{AutoShiftMode::None};
 bool failed_{};
 static constexpr unsigned index(Kind value) noexcept { return static_cast<unsigned>(value); }
public:
 bool configure(Kind selected, AutoShiftMode mode, Kind bottom) noexcept {
  if(index(selected)>=index(Kind::Count) || index(bottom)>index(Kind::Hazard) ||
     static_cast<unsigned>(mode)>static_cast<unsigned>(AutoShiftMode::SelectToUnder))return false;
  selected_=current_=selected;bottom_=bottom;mode_=mode;failed_=false;return true;
 }
 void update(const Bank& bank) noexcept {
  if(failed_)return;
  const bool course=index(current_)>=index(Kind::Class);
  if(mode_==AutoShiftMode::BestClear || mode_==AutoShiftMode::SelectToUnder){
   unsigned first=course?index(Kind::Class):std::min(index(current_),index(bottom_));
   unsigned last=mode_==AutoShiftMode::BestClear?(course?index(Kind::ExHardClass):index(Kind::Hazard)):index(selected_);
   unsigned next=first;
   for(unsigned i=first;i<=last;++i)if(bank.qualified(static_cast<Kind>(i)))next=i;
   current_=static_cast<Kind>(next);
  } else if(bank.value(current_)<=0){
   if(mode_==AutoShiftMode::None)failed_=true;
   else if(mode_==AutoShiftMode::SurvivalToGroove && !course)current_=Kind::Normal;
  }
 }
 Kind current() const noexcept {return current_;}
 bool changed() const noexcept {return current_!=selected_;}
 bool failed() const noexcept {return failed_;}
};
}
