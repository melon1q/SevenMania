#pragma once
#include "ChartRules.h"
#include <array>

class BgaSwitchState {
    std::wstring id_;double started_=-1;
public:
    void reset(){id_.clear();started_=-1;}
    void press(const bms::Chart& chart,int lane,double time){
        const auto next=std::upper_bound(chart.switchEvents.begin(),chart.switchEvents.end(),time,[](double t,const bms::SwitchEvent&e){return t<e.timeSeconds;});
        if(next==chart.switchEvents.begin())return;
        const auto& id=std::prev(next)->id;const auto definition=chart.switchAnimations.find(id);
        if(definition!=chart.switchAnimations.end() && definition->second.lane==lane){id_=id;started_=time;}
    }
    struct Frame {std::wstring id;bms::BgaColor color;};
    Frame frame(const bms::Chart& chart,double time,const std::array<bool,16>& pressed)const{
        const auto it=chart.switchAnimations.find(id_);if(it==chart.switchAnimations.end() || started_<0)return {};
        const auto& a=it->second;const double elapsed=(time-started_)*1000;
        if(elapsed<0 || a.frames.empty() || (a.durationMilliseconds>0 && elapsed>=a.durationMilliseconds) ||
            (a.durationMilliseconds==0 && !pressed[a.lane]))return {};
        size_t index=static_cast<size_t>(elapsed/a.frameMilliseconds);
        index=a.loop?index%a.frames.size():std::min(index,a.frames.size()-1);
        return {a.frames[index],a.color};
    }
};
