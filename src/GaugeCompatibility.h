// SPDX-License-Identifier: GPL-3.0-only
// Compatibility adaptation of beatoraja GaugeProperty and GrooveGauge
// by exch and beatoraja contributors. Modified for SevenMania, 2026-11-10.
// See docs/licenses/beatoraja-GPL-3.0.txt and the accompanying report.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// Display-independent 7-key gauge calculator. Values and event order are
// documented in docs/GaugeCompatibility-2026-11-10.md. No game state writes.
namespace sevenmania::gauge {
enum class Profile { SevenKeys, BeatorajaLR2 };
enum class Kind : unsigned { AssistEasy, Easy, Normal, Hard, ExHard, Hazard, Class, ExClass, ExHardClass, Count };
enum class Event : unsigned { PGreat, Great, Good, Bad, Poor, EmptyPoor };
struct Definition { float minimum, initial, border; std::array<float,6> delta; };
inline constexpr std::array<Definition,9> seven{{
 {2,20,60,{1,1,.5f,-1.5f,-3,-.5f}},{2,20,80,{1,1,.5f,-1.5f,-4.5f,-1}},
 {2,20,80,{1,1,.5f,-3,-6,-2}},{0,100,0,{.15f,.12f,.03f,-5,-10,-5}},
 {0,100,0,{.15f,.06f,0,-8,-16,-8}},{0,100,0,{.15f,.06f,0,-100,-100,-10}},
 {0,100,0,{.15f,.12f,.06f,-1.5f,-3,-1.5f}},{0,100,0,{.15f,.12f,.03f,-3,-6,-3}},
 {0,100,0,{.15f,.06f,0,-5,-10,-5}}
}};
inline constexpr std::array<Definition,9> lr2{{
 {2,20,60,{1.2f,1.2f,.6f,-3.2f,-4.8f,-1.6f}},{2,20,80,{1.2f,1.2f,.6f,-3.2f,-4.8f,-1.6f}},
 {2,20,80,{1,1,.5f,-4,-6,-2}},{0,100,0,{.1f,.1f,.05f,-6,-10,-2}},
 {0,100,0,{.1f,.1f,.05f,-12,-20,-2}},{0,100,0,{.15f,.06f,0,-100,-100,-10}},
 {0,100,0,{.1f,.1f,.05f,-2,-3,-2}},{0,100,0,{.1f,.1f,.05f,-6,-10,-2}},
 {0,100,0,{.1f,.1f,.05f,-12,-20,-2}}
}};
class Bank {
 Profile profile_{};std::array<Definition,9> def_{};std::array<float,9> values_{};
 static float damageFactor(double total,std::size_t notes) noexcept {
  constexpr std::array<double,10> threshold{240,230,210,200,180,160,150,130,120,0};
  constexpr std::array<float,10> scale{1,1.11f,1.25f,1.5f,1.666f,2,2.5f,3.333f,5,10};
  std::size_t index=0;while(index+1<threshold.size()&&total<threshold[index])++index;
  float countFactor=1,step=.002f;int remaining=1000;
  while(remaining>static_cast<int>(std::min<std::size_t>(notes,1000))||remaining>1){countFactor+=step*float(remaining-std::max<int>(static_cast<int>(std::min<std::size_t>(notes,1000)),remaining/2));remaining/=2;step*=2;}
  return std::max(scale[index],countFactor);
 }
public:
 // Returns false for invalid charts; avoids division by zero and inventing a TOTAL.
 bool configure(Profile profile,double total,std::size_t notes) noexcept {
  if(!std::isfinite(total)||total<=0||notes==0){def_={};values_={};return false;}
  profile_=profile;def_=profile==Profile::SevenKeys?seven:lr2;
  const float cap=static_cast<float>(std::clamp((2*total-320)/notes,0.,static_cast<double>(.15f)));
  const float damage=profile==Profile::BeatorajaLR2?damageFactor(total,notes):1;
  for(std::size_t k=0;k<def_.size();++k){
   for(float& value:def_[k].delta){if(k<=2&&value>0)value=static_cast<float>(static_cast<double>(value)*total/notes);else if(profile==Profile::SevenKeys&&(k==3||k==4)&&value>0)value*=cap/.15f;else if(profile==Profile::BeatorajaLR2&&(k==3||k==4)&&value<0)value*=damage;}
   values_[k]=def_[k].initial;
  }return true;
 }
 void apply(Event event,float rate=1) noexcept {
  const auto j=static_cast<unsigned>(event);if(j>=6||!std::isfinite(rate)||rate<0)return;
  for(std::size_t k=0;k<values_.size();++k){if(values_[k]<=0)continue;float change=def_[k].delta[j]*rate;
   if(change<0){const float v=values_[k];if(profile_==Profile::SevenKeys&&k==3){if(v<10)change*=.4f;else if(v<20)change*=.5f;else if(v<30)change*=.6f;else if(v<40)change*=.7f;else if(v<50)change*=.8f;}
    else if(profile_==Profile::SevenKeys&&k==6){if(v<5)change*=.4f;else if(v<10)change*=.5f;else if(v<15)change*=.6f;else if(v<20)change*=.7f;else if(v<25)change*=.8f;}
    else if(profile_==Profile::BeatorajaLR2&&(k==3||k==6||k==7)&&v<30)change*=.6f;}
   values_[k]=std::clamp(values_[k]+change,def_[k].minimum,100.f);
  }
 }
 // Mine damage bypasses judgement modifiers and GUTS (GrooveGauge.addValue).
 void mineDamage(float damage) noexcept {
  if(!std::isfinite(damage)||damage<0)return;
  for(std::size_t k=0;k<values_.size();++k)
   if(values_[k]>0)values_[k]=std::clamp(values_[k]-damage,def_[k].minimum,100.f);
 }
 // A single HCN body tick; scheduling is the responsibility of the caller.
 void hcnTick(bool held) noexcept {apply(held?Event::Great:Event::Bad,.5f);}
 void setValue(Kind kind,float value) noexcept {const auto k=static_cast<unsigned>(kind);if(k<values_.size()&&values_[k]>0&&std::isfinite(value))values_[k]=std::clamp(value,def_[k].minimum,100.f);}
 float value(Kind kind) const noexcept{return values_[static_cast<unsigned>(kind)];}
 bool qualified(Kind kind) const noexcept{auto k=static_cast<unsigned>(kind);return values_[k]>0&&values_[k]>=def_[k].border;}
};
}
