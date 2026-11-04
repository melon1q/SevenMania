#pragma once
#include "SkinPackage.h"
#include <algorithm>
#include <cmath>

namespace sevenmania::skin {
// Reference slot IDs are semantic: 0=speed, 1=judgement, 2=gauge.
// Preserve the selection area's origin/scale, derive its internal layout from play.
inline void synchronizeSelectionSlots(SkinPackage& package) {
    const auto select=package.screens.find(ScreenKind::Select),play=package.screens.find(ScreenKind::Play);
    if(select==package.screens.end()||play==package.screens.end())return;
    auto find=[](auto& elements,const std::string& id)->SkinElement* {
        const auto i=std::find_if(elements.begin(),elements.end(),[&](auto& e){return e.id==id;});return i==elements.end()?nullptr:&*i;
    };
    auto& target=select->second.elements;auto& source=play->second.elements;
    auto* anchor=find(target,"slot-0");auto* first=find(source,"gear-icon-0");
    if(!anchor||!first||!find(source,"gear-judgement-mode")||first->width<=0||first->height<=0)return;
    const double originX=anchor->x,originY=anchor->y,sx=anchor->width/first->width,sy=anchor->height/first->height;
    if(!std::isfinite(sx)||!std::isfinite(sy)||sx<=0||sy<=0)return;
    const auto x=[](const SkinElement& e){return e.extensions[L"gearLayouts"][L"center"][L"x"].num(e.x);};
    const auto y=[](const SkinElement& e){return e.extensions[L"gearLayouts"][L"center"][L"y"].num(e.y);};
    const double baseX=x(*first),baseY=y(*first);
    // Missing reference elements leave partial/custom skins intact.
    const int semantic[]={0,2,1};const char* values[]={"gear-speed-reference","gear-gauge-mode","gear-judgement-mode"};
    for(int slot=0;slot<3;++slot){
        auto* image=find(source,"gear-icon-"+std::to_string(slot));
        auto* destination=find(target,"slot-"+std::to_string(semantic[slot]));
        if(!image||!destination)continue;
        destination->x=originX+(x(*image)-baseX)*sx;destination->y=originY+(y(*image)-baseY)*sy;
        destination->width=image->width*sx;destination->height=image->height*sy;
        destination->resource=image->resource;destination->tint=image->tint;destination->source=image->source;destination->opacity=image->opacity;destination->visible=image->visible;destination->rotation=image->rotation;
        const auto copyText=[&](SkinElement& out,const SkinElement& in){
            out.x=originX+(x(in)-baseX)*sx;out.y=originY+(y(in)-baseY)*sy;
            out.width=in.width*sx;out.height=in.height*sy;out.textStyle=in.textStyle;
            out.textStyle.fontSize=std::clamp(std::round(in.textStyle.fontSize*std::min(sx,sy)),1.0,256.0);out.tint=in.tint;out.opacity=in.opacity;out.visible=in.visible;
        };
        if(auto* value=find(target,"slot-value-"+std::to_string(semantic[slot])))if(auto* model=find(source,values[slot]))copyText(*value,*model);
        if(auto* label=find(target,"slot-label-"+std::to_string(semantic[slot]))) {
            if(slot==2){if(auto* model=find(source,"gear-judgement-label"))copyText(*label,*model);}
            else label->visible=false;
        }
    }
}
}
