#pragma once
// Editing model for an element's extensions.animations track list.
// No Win32 here: every mutation is applied to a copy, validated with the same
// parseAnimations() the loader and renderer use, and only then stored.
#include "EditorDocument.h"
#include "skin/SkinAnimation.h"
namespace sevenmania::editor::anim {
inline const std::vector<std::wstring> properties{L"x",L"y",L"scale",L"rotation",L"opacity"};
inline const std::vector<std::wstring> triggers{L"screen.enter",L"judgement",L"combo.break"};
inline const std::vector<std::wstring> easings{L"linear",L"ease-in",L"ease-out",L"ease-in-out"};
inline const std::vector<std::wstring> finishes{L"hold",L"restore"};
inline const std::vector<std::wstring> policies{L"restart",L"ignore"};
constexpr size_t maxTracks=64,maxKeys=256;

struct TrackSettings {
    std::wstring id,property=L"opacity",trigger=L"screen.enter",easing=L"linear",finish=L"hold",policy=L"restart";
    double duration=1;bool loop=false;
};

inline const J& list(const J& element){return element[L"extensions"][L"animations"];}
inline size_t trackCount(const J& element){const auto& l=list(element);return l.type==T::Array?l.array.size():0;}
inline const J& track(const J& element,size_t index){
    if(index>=trackCount(element))throw std::out_of_range("Animation track index");
    return list(element).array[index];
}
inline size_t keyCount(const J& element,size_t index){const auto& k=track(element,index)[L"keys"];return k.type==T::Array?k.array.size():0;}
inline std::pair<double,double> key(const J& element,size_t track_,size_t index){
    const auto& keys=track(element,track_)[L"keys"];
    if(keys.type!=T::Array||index>=keys.array.size())throw std::out_of_range("Animation keyframe index");
    return {keys.array[index][L"time"].num(),keys.array[index][L"value"].num()};
}
inline TrackSettings settings(const J& element,size_t index){
    const auto& t=track(element,index);TrackSettings s;
    s.id=t[L"id"].str();s.property=t[L"property"].str(L"opacity");s.trigger=t[L"trigger"].str(L"screen.enter");
    s.easing=t[L"easing"].str(L"linear");s.finish=t[L"finish"].str(L"hold");s.policy=t[L"policy"].str(L"restart");
    s.duration=t[L"duration"].num(1);s.loop=t[L"loop"].boolean;return s;
}
inline std::vector<skin::AnimationTrack> parsed(const J& element){
    J ext;ext.type=T::Object;ext.object[L"animations"]=list(element);
    return skin::parseAnimations(ext);
}
inline std::wstring uniqueId(const J& element,const std::wstring& base,size_t ignore=size_t(-1)){
    for(unsigned n=0;;++n){
        const auto candidate=n?base+L"-"+std::to_wstring(n+1):base;bool used=false;
        for(size_t i=0;i<trackCount(element);++i)if(i!=ignore&&track(element,i)[L"id"].str()==candidate)used=true;
        if(!used)return candidate;
    }
}
// Runs `change` on a copy of the track array; stores the result only if it validates.
template<class F> void mutate(J& element,F&& change){
    J candidate=list(element);if(candidate.type!=T::Array){candidate=J{};candidate.type=T::Array;}
    change(candidate.array);
    if(candidate.array.size()>maxTracks)throw std::runtime_error("Too many animation tracks");
    J ext;ext.type=T::Object;ext.object[L"animations"]=candidate;(void)skin::parseAnimations(ext); // throws on any invalid track
    auto& target=element.object[L"extensions"];target.type=T::Object;
    if(candidate.array.empty())target.object.erase(L"animations");else target.object[L"animations"]=std::move(candidate);
}
inline J keyJson(double time,double value){J k;k.type=T::Object;k.object={{L"time",num(time)},{L"value",num(value)}};return k;}
inline J trackJson(const TrackSettings& s,const std::vector<std::pair<double,double>>& keys){
    J t;t.type=T::Object;
    t.object={{L"id",str(s.id)},{L"property",str(s.property)},{L"trigger",str(s.trigger)},{L"easing",str(s.easing)},{L"finish",str(s.finish)},{L"policy",str(s.policy)},{L"duration",num(s.duration)},{L"loop",jsonBool(s.loop)}};
    J k;k.type=T::Array;for(auto& [time,value]:keys)k.array.push_back(keyJson(time,value));t.object[L"keys"]=std::move(k);return t;
}
// Natural start/end values for `property` on this element, used for new tracks and when a track's property changes.
inline std::pair<double,double> defaultRange(const J& element,const std::wstring& property){
    if(property==L"opacity")return {0,1};if(property==L"scale")return {1,1.2};if(property==L"rotation")return {0,15};
    const double base=element[property==L"x"?L"x":L"y"].num();return {base,base+100};
}
// New track with a sensible two-key default for `property`; returns its index.
inline size_t addTrack(J& element,const std::wstring& property,const std::wstring& trigger){
    TrackSettings s;s.property=property;s.trigger=trigger;s.duration=1;s.id=uniqueId(element,L"anim-"+property);
    const auto range=defaultRange(element,property);std::vector<std::pair<double,double>> keys{{0,range.first},{1,range.second}};
    size_t index=0;mutate(element,[&](auto& tracks){tracks.push_back(trackJson(s,keys));index=tracks.size()-1;});return index;
}
inline size_t duplicateTrack(J& element,size_t index){
    auto copy=track(element,index);copy.object[L"id"]=str(uniqueId(element,copy[L"id"].str()+L"-copy"));
    size_t result=0;mutate(element,[&](auto& tracks){tracks.insert(tracks.begin()+index+1,std::move(copy));result=index+1;});return result;
}
inline void removeTrack(J& element,size_t index){track(element,index);mutate(element,[&](auto& tracks){tracks.erase(tracks.begin()+index);});}
inline void moveTrack(J& element,size_t index,int direction){
    track(element,index);const long long to=static_cast<long long>(index)+direction;if(to<0||to>=static_cast<long long>(trackCount(element)))return;
    mutate(element,[&](auto& tracks){std::swap(tracks[index],tracks[static_cast<size_t>(to)]);});
}
inline void updateTrack(J& element,size_t index,const TrackSettings& s){
    track(element,index);
    if(uniqueId(element,s.id,index)!=s.id)throw std::runtime_error("Duplicate animation track ID");
    const bool propertyChanged=track(element,index)[L"property"].str()!=s.property;
    const auto range=defaultRange(element,s.property);
    mutate(element,[&](auto& tracks){
        auto& t=tracks[index];
        if(propertyChanged){ // old values belong to another unit (opacity 0..1 is not an x position): respread the keys over the new property's range
            for(auto& k:t.object[L"keys"].array){const double f=std::clamp(k[L"time"].num()/std::max(1e-9,t[L"duration"].num(1)),0.0,1.0);k.object[L"value"]=num(range.first+(range.second-range.first)*f);}
        }
        t.object[L"id"]=str(s.id);t.object[L"property"]=str(s.property);t.object[L"trigger"]=str(s.trigger);t.object[L"easing"]=str(s.easing);
        t.object[L"finish"]=str(s.finish);t.object[L"policy"]=str(s.policy);t.object[L"duration"]=num(s.duration);t.object[L"loop"]=jsonBool(s.loop);
    });
}
// Inserts keeping time order. Returns the new key index.
inline size_t addKey(J& element,size_t index,double time,double value){
    track(element,index);size_t result=0;
    mutate(element,[&](auto& tracks){
        auto& keys=tracks[index].object[L"keys"].array;if(keys.size()>=maxKeys)throw std::runtime_error("Too many keyframes");
        auto at=std::find_if(keys.begin(),keys.end(),[&](const J& k){return k[L"time"].num()>=time;});
        result=static_cast<size_t>(at-keys.begin());keys.insert(at,keyJson(time,value));
    });
    return result;
}
inline size_t setKey(J& element,size_t track_,size_t index,double time,double value){
    keyCount(element,track_);key(element,track_,index);size_t result=0;
    mutate(element,[&](auto& tracks){
        auto& keys=tracks[track_].object[L"keys"].array;keys.erase(keys.begin()+index);
        auto at=std::find_if(keys.begin(),keys.end(),[&](const J& k){return k[L"time"].num()>=time;});
        result=static_cast<size_t>(at-keys.begin());keys.insert(at,keyJson(time,value));
    });
    return result;
}
inline void removeKey(J& element,size_t track_,size_t index){
    key(element,track_,index);
    mutate(element,[&](auto& tracks){auto& keys=tracks[track_].object[L"keys"].array;keys.erase(keys.begin()+index);}); // an empty key list is rejected by the validator
}
// Gives `to` the same tracks as `from`. x/y keyframes are absolute positions, so they are shifted
// by the distance between the two elements and every selected object keeps its own place.
inline void copyTracks(const J& from,J& to){
    const double dx=to[L"x"].num()-from[L"x"].num(),dy=to[L"y"].num()-from[L"y"].num();
    mutate(to,[&](auto& tracks){
        tracks=list(from).type==T::Array?list(from).array:std::vector<J>{};
        for(auto& t:tracks){const auto property=t[L"property"].str();if(property!=L"x"&&property!=L"y")continue;
            for(auto& k:t.object[L"keys"].array)k.object[L"value"]=num(k[L"value"].num()+(property==L"x"?dx:dy));}
    });
}
}
