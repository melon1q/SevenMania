#pragma once
#include "BmsParser.h"
#include "ChartRules.h"
#include "JsonValue.h"
#include "PreviewTiming.h"
#include <map>
#include <set>
#include <tuple>

namespace bms {
inline ParseResult parseBmson(const std::filesystem::path& path,const std::wstring& text) {
    ParseResult result;auto& c=result.chart;c.path=path;c.bmson=true;c.objectBase=62;c.title=path.stem().wstring();
    try {
        const auto root=JsonReader(text).read();
        if(root.type!=JsonValue::Type::Object)throw std::runtime_error("bmson root must be an object");
        if(root.object.contains(L"version") && root[L"version"].type==JsonValue::Type::Null)throw std::runtime_error("null bmson version");
        const bool legacy=!root.object.contains(L"version");
        if(!legacy && root[L"version"].str()!=L"1.0.0")throw std::runtime_error("unsupported bmson version");
        const auto& info=root[L"info"];
        const auto field=[&](const JsonValue& object,const wchar_t* current,const wchar_t* old)->const JsonValue& {
            const auto& v=object[current];return v.type!=JsonValue::Type::Null?v:object[old];
        };
        c.title=info[L"title"].str(c.title);c.subtitle=info[L"subtitle"].str();c.artist=info[L"artist"].str();c.genre=info[L"genre"].str();
        for(const auto& a:info[L"subartists"].array){if(!c.subartist.empty())c.subartist+=L" / ";c.subartist+=a.str();}
        c.playLevel=std::to_wstring(static_cast<int>(info[L"level"].num()));c.initialBpm=field(info,L"init_bpm",L"initBPM").num(120);
        if(c.initialBpm<=0)throw std::runtime_error("invalid initial BPM");
        const double resolution=info[L"resolution"].num(240);if(resolution<=0)throw std::runtime_error("invalid resolution");
        c.extendedRank=field(info,L"judge_rank",L"judgeRank").num(100);
        const double relativeTotal=info[L"total"].num(100);
        const auto hint=info[L"mode_hint"].str(L"beat-7k");
        if(hint==L"beat-5k")c.mode=ChartMode::FiveKey;else if(hint==L"beat-10k")c.mode=ChartMode::TenKey;
        else if(hint==L"beat-14k")c.mode=ChartMode::FourteenKey;else if(hint==L"popn-9k")c.mode=ChartMode::PopnNine;
        else if(hint==L"beat-7k" || hint.empty())c.mode=ChartMode::SevenKey;else throw std::runtime_error("unsupported bmson mode");
        int lnmode=static_cast<int>(info[L"ln_type"].num(1));c.longNoteMode=static_cast<LongNoteMode>(std::clamp(lnmode,1,3));
        const auto asset=[&](const std::wstring& name) {
            if(name.empty())return std::filesystem::path{};
            auto file=(path.parent_path()/name).lexically_normal();
            if(readableAssetExists(file))return std::filesystem::absolute(file);
            for(const auto* ext:{L".wav",L".ogg",L".mp3",L".flac",L".m4a",L".png",L".bmp",L".jpg",L".mp4",L".mpeg",L".avi"}) {
                auto p=file;p.replace_extension(ext);if(readableAssetExists(p))return std::filesystem::absolute(p);
            }return std::filesystem::absolute(file);
        };
        c.titleImage=asset(info[L"eyecatch_image"].str());c.finishImage=asset(info[L"back_image"].str());c.banner=asset(info[L"banner_image"].str());c.previewAudio=asset(info[L"preview_music"].str());
        if(!c.previewAudio.empty())c.previewAudioCandidates.push_back(c.previewAudio);
        if(!c.titleImage.empty())c.bitmaps[L"__titlecard"]=c.titleImage;if(!c.finishImage.empty())c.bitmaps[L"__finishcard"]=c.finishImage;
        struct Pulse {double bpm{},stop{},scroll=1;bool hasScroll{};};std::map<double,Pulse> pulses;
        pulses[0].bpm=c.initialBpm;
        for(const auto& e:field(root,L"bpm_events",L"bpmNotes").array){const double bpm=field(e,L"bpm",L"v").num();if(bpm<=0)throw std::runtime_error("invalid bmson BPM");pulses[e[L"y"].num()].bpm=bpm;}
        for(const auto& e:field(root,L"stop_events",L"stopNotes").array)pulses[e[L"y"].num()].stop=field(e,L"duration",L"v").num();
        for(const auto& e:root[L"scroll_events"].array){auto& p=pulses[e[L"y"].num()];p.scroll=e[L"rate"].num(1);p.hasScroll=true;}
        struct Clock {double pulse{},begin{},after{},bpm{};};std::vector<Clock> clock;
        double now=0,previous=0,bpm=c.initialBpm,scroll=1,position=0;
        for(const auto& [y,p]:pulses){
            if(y<0 || p.stop<0)throw std::runtime_error("negative bmson pulse/stop");
            const double dt=(y-previous)*60/bpm/resolution;now+=dt;position+=dt*bpm/c.initialBpm*scroll;previous=y;
            if(p.bpm>0)bpm=p.bpm;if(p.hasScroll)scroll=p.scroll;
            c.tempos.push_back({now,bpm});c.scroll.push_back({now,position,bpm/c.initialBpm*scroll});
            if(scroll<0)c.nonMonotonicScroll=true;
            const double begin=now;
            if(p.stop>0){c.scroll.back().speed=0;now+=p.stop*60/bpm/resolution;c.scroll.push_back({now,position,bpm/c.initialBpm*scroll});}
            clock.push_back({y,begin,now,bpm});
        }
        const auto timeAt=[&](double y){
            if(y<0 || !std::isfinite(y))throw std::runtime_error("invalid bmson pulse");
            const auto next=std::lower_bound(clock.begin(),clock.end(),y,[](const Clock& p,double value){return p.pulse<value;});
            if(next!=clock.end() && next->pulse==y)return next->begin;
            const auto& p=next==clock.begin()?clock.front():*std::prev(next);
            return p.after+(y-p.pulse)*60/p.bpm/resolution;
        };
        const auto lane=[&](int x){
            if(x<=0)return -1;
            if(c.mode==ChartMode::PopnNine)return x<=9?x-1:-1;
            const int keys=c.mode==ChartMode::FiveKey || c.mode==ChartMode::TenKey?5:7;
            const int side=(x-1)/(keys+1),local=(x-1)%(keys+1)+1;
            if(side>=(c.mode==ChartMode::TenKey || c.mode==ChartMode::FourteenKey?2:1))return -1;
            return side*8+(local==keys+1?0:local);
        };
        std::map<std::pair<double,int>,size_t> merged;
        size_t channelIndex=0;
        for(const auto& channel:field(root,L"sound_channels",L"soundChannel").array){
            const auto source=L"source-"+std::to_wstring(channelIndex++);const auto file=asset(channel[L"name"].str());
            if(readableAssetExists(file))c.samples[source]=file;else c.missingSampleFiles.push_back(file);
            std::map<double,std::vector<const JsonValue*>> notes;
            for(const auto& n:channel[L"notes"].array)notes[n[L"y"].num()].push_back(&n);
            double restart=0;size_t sliceIndex=0;
            for(auto it=notes.begin();it!=notes.end();++it){
                const double y=it->first,t=timeAt(y);const auto next=std::next(it);
                bool continuation=true;for(const auto* n:it->second)if(!(*n)[L"c"].boolean)continuation=false;
                if(!continuation)restart=t;
                const auto id=source+L"-slice-"+std::to_wstring(sliceIndex++);
                c.sampleSlices[id]={source,std::max(0.0,t-restart),next==notes.end()?-1:timeAt(next->first)-t};
                const bool background=std::any_of(it->second.begin(),it->second.end(),[](const auto* n){return (*n)[L"x"].num()<=0;});
                for(const auto* n:it->second){
                    const int x=static_cast<int>((*n)[L"x"].num());const int l=lane(x);
                    if(x<=0)continue;if(l<0){result.errors.push_back(L"범위를 벗어난 bmson 레인");continue;}
                    const double length=(*n)[L"l"].num();if(length<0)throw std::runtime_error("negative bmson LN length");
                    const auto key=std::make_pair(y,l);const auto existing=merged.find(key);
                    if(existing==merged.end()){
                        merged[key]=c.notes.size();c.notes.push_back({l,t,timeAt(y+length)-t,background?L"":id,L"",c.longNoteMode});
                    } else {
                        auto& note=c.notes[existing->second];
                        if(background){note.lengthSeconds=std::max(note.lengthSeconds,timeAt(y+length)-t);continue;}
                        if(note.sampleId.empty()){note.sampleId=id;note.lengthSeconds=std::max(note.lengthSeconds,timeAt(y+length)-t);continue;}
                        auto& group=c.sampleGroups[note.sampleId];
                        if(group.empty()){const auto old=note.sampleId;note.sampleId=L"group-"+std::to_wstring(existing->second);c.sampleGroups[note.sampleId]={old,id};c.sampleGroups.erase(old);}
                        else if(std::find(group.begin(),group.end(),id)==group.end())group.push_back(id);
                        note.lengthSeconds=std::max(note.lengthSeconds,timeAt(y+length)-t);
                    }
                }
                if(background)c.bgm.push_back({t,id});
            }
        }
        for(const auto& channel:root[L"key_channels"].array) {
            const auto id=L"hidden-"+std::to_wstring(channelIndex++);const auto file=asset(channel[L"name"].str());
            if(readableAssetExists(file))c.samples[id]=file;else c.missingSampleFiles.push_back(file);
            for(const auto& n:channel[L"notes"].array){const int l=lane(static_cast<int>(n[L"x"].num()));if(l>=0)c.invisible.push_back({l,timeAt(n[L"y"].num()),id});}
        }
        std::stable_sort(c.invisible.begin(),c.invisible.end(),[](const HiddenNote&a,const HiddenNote&b){return a.timeSeconds<b.timeSeconds;});
        for(const auto& channel:root[L"mine_channels"].array)for(const auto& n:channel[L"notes"].array){const int l=lane(static_cast<int>(n[L"x"].num()));if(l>=0)c.mines.push_back({l,timeAt(n[L"y"].num()),n[L"damage"].num()});}
        const auto& bga=root[L"bga"];
        for(const auto& e:field(bga,L"bga_header",L"bgaHeader").array)c.bitmaps[std::to_wstring(static_cast<int>(field(e,L"id",L"ID").num()))]=asset(e[L"name"].str());
        for(const auto& [modern,old,layer]:{std::tuple{L"bga_events",L"bgaNotes",BgaLayer::Base},std::tuple{L"layer_events",L"layerNotes",BgaLayer::Layer},std::tuple{L"poor_events",L"poorNotes",BgaLayer::Poor}})
            for(const auto& e:field(bga,modern,old).array)c.bga.push_back({timeAt(e[L"y"].num()),layer,std::to_wstring(static_cast<int>(field(e,L"id",L"ID").num()))});
        for(const auto& event:c.bga) {
            const auto image=c.bitmaps.find(event.imageId);
            if(image==c.bitmaps.end() || !readableAssetExists(image->second))result.errors.push_back(L"bmson BGA 파일을 찾을 수 없습니다: "+event.imageId);
        }
        std::stable_sort(c.notes.begin(),c.notes.end(),[](const Note&a,const Note&b){return a.timeSeconds<b.timeSeconds;});
        std::stable_sort(c.bgm.begin(),c.bgm.end(),[](const BgmEvent&a,const BgmEvent&b){return a.timeSeconds<b.timeSeconds;});
        std::stable_sort(c.bga.begin(),c.bga.end(),[](const BgaEvent&a,const BgaEvent&b){return a.timeSeconds<b.timeSeconds;});
        std::stable_sort(c.mines.begin(),c.mines.end(),[](const MineNote&a,const MineNote&b){return a.timeSeconds<b.timeSeconds;});
        c.total=legacy && relativeTotal>0?relativeTotal:chartTotal(c,judgementNoteCount(c))*(relativeTotal>0?relativeTotal/100.0:1);
        for(const auto& n:c.notes)c.durationSeconds=std::max(c.durationSeconds,n.timeSeconds+n.lengthSeconds);
        for(const auto& e:c.bgm)c.durationSeconds=std::max(c.durationSeconds,e.timeSeconds);
        c.playable=!c.notes.empty();c.partialLoad=!result.errors.empty() || !c.missingSampleFiles.empty();
        if(c.partialLoad)c.partialLoadReason=L"bmson 자산 또는 일부 노트를 읽지 못했습니다.";
        if(!c.playable)c.blockReason=L"플레이 가능한 bmson 노트가 없습니다.";
        c.previewStartSeconds=automaticPreviewStart(c);
    } catch(const std::exception&){c.playable=false;c.blockReason=L"bmson JSON/시간/모드 정의가 올바르지 않습니다.";result.errors.push_back(c.blockReason);}
    return result;
}
} // namespace bms
