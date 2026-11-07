#pragma once
#include "SkinInternal.h"
#include <algorithm>
#include <cmath>
namespace sevenmania::skin {
inline constexpr const char* entrancePresets[]{"None","Zoom In","Zoom Out","Drop In","Bounce In","Rotate In","Wipe In","Blur In","Elastic In"};
inline constexpr int gameplayEntranceDurationMs=3000;
inline constexpr double gameplayEntranceDurationSeconds=gameplayEntranceDurationMs/1000.0;
struct EntranceFrame {double scale=1,y=0,rotation=0,opacity=1,wipe=1,blur=0;};
struct Entrance {
 int preset{};double duration=.6,delay{};
 static Entrance parse(const bms::JsonValue& extensions){
  Entrance result;const auto& j=extensions[L"entrance"];if(j.type==bms::JsonValue::Type::Null)return result;
  if(j.type!=bms::JsonValue::Type::Object)detail::fail(ErrorCode::InvalidModel,"entrance","Expected preset object");
  const auto name=detail::utf8(j[L"preset"].str(L"None"));bool found=false;
  for(int i=0;i<9;++i)if(name==entrancePresets[i]){result.preset=i;found=true;break;}
  if(!found)detail::fail(ErrorCode::InvalidModel,"entrance","Unknown entrance preset");
  result.duration=j[L"duration"].num(.6);result.delay=j[L"delay"].num(0);
  if(!std::isfinite(result.duration)||!std::isfinite(result.delay)||result.duration<=0||result.duration>10||result.delay<0||result.delay>10)detail::fail(ErrorCode::InvalidModel,"entrance","Invalid timing");return result;
 }
 EntranceFrame sample(double age,double height)const noexcept {
  EntranceFrame f;if(!preset||age>=delay+duration)return f;
  const double p=std::clamp((age-delay)/duration,0.,1.),q=1-std::pow(1-p,3);f.opacity=q;
  if(preset==1)f.scale=.65+.35*q;
  else if(preset==2)f.scale=1.4-.4*q;
  else if(preset==3)f.y=-std::max(64.,height*.6)*(1-q);
  else if(preset==4){double t=p,b;if(t<1/2.75)b=7.5625*t*t;else if(t<2/2.75){t-=1.5/2.75;b=7.5625*t*t+.75;}else if(t<2.5/2.75){t-=2.25/2.75;b=7.5625*t*t+.9375;}else{t-=2.625/2.75;b=7.5625*t*t+.984375;}f.y=-std::max(64.,height*.6)*(1-b);}
  else if(preset==5)f.rotation=-45*(1-q);
  else if(preset==6)f.wipe=q;
  else if(preset==7)f.blur=12*(1-q);
  else if(preset==8){const double elastic=p==0?0:p==1?1:std::pow(2.,-10*p)*std::sin((p*10-.75)*(2*3.141592653589793/3))+1;f.scale=std::max(.01,.25+.75*elastic);}
  return f;
 }
};
}
