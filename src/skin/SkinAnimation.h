#pragma once
#include "SkinInternal.h"
#include <cmath>
#include <algorithm>
#include <chrono>
namespace sevenmania::skin {
struct AnimationTrack {
 std::string id,property,trigger="screen.enter",easing="linear",finish="hold",policy="restart";
 double duration=1,start=0;bool loop=false,active=false;
 std::vector<std::pair<double,double>> keys;
 double sample(double now) const noexcept {double t=std::max(0.0,now-start);if(loop)t=std::fmod(t,duration);else t=std::min(t,duration);auto it=std::upper_bound(keys.begin(),keys.end(),t,[](double v,const auto& k){return v<k.first;});if(it==keys.begin())return keys.front().second;if(it==keys.end())return keys.back().second;auto a=*(it-1),b=*it;double f=(t-a.first)/(b.first-a.first);if(easing=="ease-in")f*=f;else if(easing=="ease-out")f=1-(1-f)*(1-f);else if(easing=="ease-in-out")f=f*f*(3-2*f);return a.second+(b.second-a.second)*f;}
 bool running(double now)const noexcept{return active&&(loop||finish=="hold"||now-start<duration);}
};
inline std::vector<AnimationTrack> parseAnimations(const bms::JsonValue& extensions){
 using T=bms::JsonValue::Type;const auto& list=extensions[L"animations"];std::vector<AnimationTrack> out;if(list.type==T::Null)return out;
 if(list.type!=T::Array||list.array.size()>64)detail::fail(ErrorCode::InvalidModel,"animations","Expected bounded track array");
 for(auto& j:list.array){AnimationTrack t;t.id=detail::utf8(j[L"id"].str());t.property=detail::utf8(j[L"property"].str());
 if(t.id.empty()||std::any_of(out.begin(),out.end(),[&](auto& v){return v.id==t.id;}))detail::fail(ErrorCode::InvalidModel,"animations","Missing/duplicate track ID");
 if(t.property!="x"&&t.property!="y"&&t.property!="scale"&&t.property!="rotation"&&t.property!="opacity")detail::fail(ErrorCode::InvalidModel,t.id,"Unsupported animated property");
 t.duration=j[L"duration"].num(-1);if(!std::isfinite(t.duration)||t.duration<=0||t.duration>3600)detail::fail(ErrorCode::InvalidModel,t.id,"Invalid duration");
 t.trigger=detail::utf8(j[L"trigger"].str(L"screen.enter"));t.easing=detail::utf8(j[L"easing"].str(L"linear"));t.finish=detail::utf8(j[L"finish"].str(L"hold"));t.policy=detail::utf8(j[L"policy"].str(L"restart"));t.loop=j[L"loop"].boolean;
 if(t.easing!="linear"&&t.easing!="ease-in"&&t.easing!="ease-out"&&t.easing!="ease-in-out")detail::fail(ErrorCode::InvalidModel,t.id,"Unknown easing");
 if(t.finish!="hold"&&t.finish!="restore")detail::fail(ErrorCode::InvalidModel,t.id,"Unknown completion policy");if(t.policy!="restart"&&t.policy!="ignore")detail::fail(ErrorCode::InvalidModel,t.id,"Unknown retrigger policy");
 auto& keys=j[L"keys"];if(keys.type!=T::Array||keys.array.empty()||keys.array.size()>256)detail::fail(ErrorCode::InvalidModel,t.id,"Invalid keyframes");
 for(auto& k:keys.array){double time=k[L"time"].num(-1),value=k[L"value"].num(NAN);if(!std::isfinite(time)||!std::isfinite(value)||time<0||time>t.duration||std::abs(value)>1000000||(!t.keys.empty()&&time<=t.keys.back().first)||(t.property=="scale"&&value<=0)||(t.property=="opacity"&&(value<0||value>1)))detail::fail(ErrorCode::InvalidModel,t.id,"Invalid keyframe");t.keys.emplace_back(time,value);}t.active=t.trigger=="screen.enter";out.push_back(std::move(t));}
 return out;
}
}
