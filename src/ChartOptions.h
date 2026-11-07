#pragma once
#include "ChartRules.h"
#include <map>
#include <numeric>
#include <random>
#include <set>

namespace bms {
inline std::wstring normalizeOption(std::wstring value) {
    value=toLower(value);const auto colon=value.find(L':');
    if(colon!=std::wstring::npos) {
        const auto vendor=value.substr(0,colon);if(vendor!=L"774" && vendor!=L"charatbeathdx")return value;
        value=value.substr(colon+1);
    }
    static const std::map<std::wstring,std::wstring> aliases{
        {L"rnd-normal",L"random_off"},{L"rnd-mirror",L"random_mirror"},{L"rnd-random",L"random_random"},{L"rnd-randomplus",L"random_random+"},
        {L"cra-rhs1",L"base-speed_x1.20"},{L"cra-rhs2",L"base-speed_x1.50"},{L"cra-rhs3",L"base-speed_x2.00"},
        {L"cra-hyper",L"notes_double"},{L"cra-cpdouble",L"notes_copy"},{L"cra-normal",L"crazy_off"},
        {L"cra-hard1",L"crazy_good2bad"},{L"cra-hard2",L"crazy_great2bad"},{L"cra-ulthard",L"crazy_helljudge"},{L"cra-2judge",L"crazy_helljudge"},
        {L"cra-accel",L"change-hs_accel"},{L"cra-brake",L"change-hs_brake"},{L"hid-normal",L"hidden_off"},{L"hid-hidjudge",L"crazy_hidjudge"},
        {L"hsp-line",L"change-hs_line"},{L"hsp-nazo",L"change-hs_nazo"},{L"hsp-random",L"change-hs_random"},{L"hsp-combo",L"change-hs_combo-up"},{L"hsp-linenazo",L"change-hs_linenazo"},
        {L"hid-hidden1",L"hidden_hidden"},{L"hid-hidden2",L"hidden_hidden2"},{L"hid-sudden1",L"hidden_sudden"},{L"hid-sudden2",L"hidden_sudden2"},
        {L"hid-hidsud1",L"hidden_hid-sud"},{L"hid-hidsud2",L"hidden_hid-sud2"},{L"hid-hidsud3",L"hidden_hid-sud3"},{L"hid-blink1",L"hidden_blink"},{L"hid-blink2",L"hidden_blink2"},{L"hid-stealth",L"hidden_stealth"},
        {L"cra-xwave1",L"crazy_xwave"},{L"cra-ywave1",L"crazy_ywave"},{L"cra-xywave1",L"crazy_xywave"},{L"cra-3dwave1",L"crazy_3dwave"}
    };
    if(const auto alias=aliases.find(value);alias!=aliases.end())return alias->second;
    if(value.starts_with(L"cra-"))return L"crazy_"+value.substr(4);
    return value;
}
struct ForcedOptions {
    double hiSpeed{},rate=1;
    std::wstring speedEffect=L"off",hidden=L"off",gameLevel=L"normal",extra=L"off";
    std::set<std::wstring,std::less<>> crazy;
    int bgaMode{};bool gaugeAuthored{},manualHiSpeed{};
};
inline ForcedOptions forcedOptions(const Chart& chart) {
    ForcedOptions out;
    for(auto option:chart.options) {
        option=normalizeOption(option);
        try {
            if(option.starts_with(L"hi-speed_x"))out.hiSpeed=std::clamp(std::stod(option.substr(10)),0.25,100.0);
            else if(option.starts_with(L"base-speed_x"))out.rate=std::clamp(std::stod(option.substr(12)),0.1,10.0);
            else if(option==L"hsp-manual")out.manualHiSpeed=true;
            else if(option==L"hsp-change")out.speedEffect=L"off"; // nanasigroove 1.552 profile
            else if(option.starts_with(L"hsp-") && iswdigit(option[4]))out.hiSpeed=std::clamp(std::stod(option.substr(4)),0.25,100.0);
            else if(option.starts_with(L"change-hs_"))out.speedEffect=option.substr(10);
            else if(option.starts_with(L"hidden_"))out.hidden=option.substr(7);
            else if(option.starts_with(L"gamelevel_")){out.gameLevel=option.substr(10);out.gaugeAuthored=true;}
            else if(option.starts_with(L"extra_")){out.extra=option.substr(6);out.gaugeAuthored=true;}
            else if(option.starts_with(L"gtp-")){out.gameLevel=option;out.extra=L"off";out.gaugeAuthored=true;}
            else if(option.starts_with(L"glv-")) {
                const auto v=option.substr(4);out.gaugeAuthored=true;
                if(v==L"veryeasy"){out.gameLevel=L"beginner";out.extra=L"easy";}
                else if(v==L"easy"){out.gameLevel=L"normal";out.extra=L"easy";}
                else if(v==L"normal"){out.gameLevel=L"normal";out.extra=L"off";}
                else if(v==L"hard"){out.gameLevel=L"normal";out.extra=L"hard";}
                else if(v==L"hell"){out.gameLevel=L"danger";out.extra=L"hard";}
                else {out.gameLevel=L"hyper";out.extra=L"hard";}
            }
            else if(option==L"crazy_off")out.crazy.clear();
            else if(option.starts_with(L"crazy_"))out.crazy.insert(option.substr(6));
            else if(option.starts_with(L"bgamode "))out.bgaMode=std::clamp(std::stoi(option.substr(8)),0,2);
        }catch(...){}
    }return out;
}
inline double optionVisualSpeed(const ForcedOptions& options,const Note& note,double time,unsigned combo,double distance) {
    const auto& effect=options.speedEffect;
    const double random=0.5+static_cast<double>((uint64_t(note.timeSeconds*1000000)+note.lane*104729)%1000)/1000;
    if(effect==L"accel")return std::clamp(1.5-distance/1000,0.25,2.0);
    if(effect==L"brake")return std::clamp(0.25+distance/600,0.25,2.0);
    if(effect==L"random")return random;
    if(effect==L"combo-up")return 1+combo/100.0;
    if(effect==L"combo-dn")return 1/(1+combo/100.0);
    if(effect==L"combo-rd")return 1+(random-1)*std::min(3.0,combo/100.0);
    if(effect==L"nazo" || effect==L"nazo2")return 1+std::sin(time*3)*(effect==L"nazo2"?0.85:0.45);
    if(effect==L"line" || effect==L"line2")return 0.5+(note.lane*7%11)/10.0;
    if(effect==L"linenazo")return 1+std::sin(time*3+note.lane)*0.5;
    if(effect==L"warp" || effect==L"s-warp")return 0.5+std::fmod(std::floor(time*2),3.0);
    if(effect==L"lineaccel")return (0.5+(note.lane*7%11)/10.0)*std::clamp(1.5-distance/1000,0.25,2.0);
    if(effect==L"chaos")return random*(1+std::sin(time*3+note.lane)*0.5);
    if(effect==L"random2")return random*(1+std::sin(time*3+random)*0.5);
    return 1;
}
inline double optionVisibility(const ForcedOptions& options,double distance,double time=0) {
    if(options.hidden==L"stealth")return 0;
    if(options.hidden.starts_with(L"hidden"))return std::clamp((distance-(options.hidden==L"hidden3"?300:options.hidden==L"hidden2"?200:100))/80,0.0,1.0);
    if(options.hidden.starts_with(L"sudden"))return std::clamp(((options.hidden==L"sudden3"?200:options.hidden==L"sudden2"?320:450)-distance)/80,0.0,1.0);
    if(options.hidden.starts_with(L"hid-sud")){const double band=options.hidden==L"hid-sud3"?120:options.hidden==L"hid-sud2"?200:300;return std::clamp((distance-100)/50,0.0,1.0)*std::clamp((100+band-distance)/50,0.0,1.0);}
    if(options.hidden==L"mist")return .5;if(options.hidden==L"deepmist")return .15;
    if(options.hidden==L"blink" || options.hidden==L"blink2")return (std::sin(time*(options.hidden==L"blink2"?2:5))+1)/2;
    return 1;
}
// Legacy visual effects have no published numeric amplitudes. Keep their
// deterministic renderer profile here, independent of audio and judgement time.
struct OptionNoteVisual {double distance{},shift{},scale=1;};
inline OptionNoteVisual optionNoteVisual(const ForcedOptions& options,const Note& note,double time,double distance) {
    OptionNoteVisual out{distance};const auto has=[&](const wchar_t* name){return options.crazy.contains(name);};
    const double phase=time*4+note.timeSeconds*2;
    const bool strongX=has(L"xwave2") || has(L"xywave2");
    const bool strongY=has(L"ywave2") || has(L"xywave2");
    if(strongX || has(L"xwave") || has(L"xywave"))out.shift=std::sin(phase)*(strongX?1:.4);
    if(strongY || has(L"ywave") || has(L"xywave"))out.distance*=1+std::sin(phase)*(strongY?.8:.4);
    if(has(L"small"))out.scale*=.5;if(has(L"big"))out.scale*=1.5;
    const double approach=std::clamp(1-std::abs(distance)/700,.05,1.0);
    if(has(L"fadein"))out.scale*=approach;if(has(L"fadeout"))out.scale*=1.05-approach;
    if(has(L"rd-size"))out.scale*=.5+((uint64_t(note.timeSeconds*1000000)+note.lane*104729)%1000)/1000.0;
    if(has(L"3dwave") || has(L"3dwave2"))out.scale*=1+std::sin(phase)*(has(L"3dwave2")?.8:.4);
    return out;
}
inline int optionJudgeLine(const ForcedOptions& options,double time,int normal,int height) {
    int y=options.crazy.contains(L"reverse")?height-normal:normal;
    if(options.crazy.contains(L"linemove") || options.crazy.contains(L"linemove2"))
        y+=static_cast<int>(std::sin(time*2)*(options.crazy.contains(L"linemove2")?100:40));
    return std::clamp(y,40,std::max(40,height-40));
}
inline bool knownOption(const std::wstring& option) {
    if(option.starts_with(L"hi-speed_x") || option.starts_with(L"base-speed_x") || option==L"hsp-manual" || option==L"hsp-change" || (option.starts_with(L"hsp-") && option.size()>4 && iswdigit(option[4])) || option.starts_with(L"bgamode ") || option.starts_with(L"longmode "))return true;
    static const std::map<std::wstring,std::set<std::wstring>> names{
        {L"gtp-",{L"easy",L"normal",L"full",L"extra",L"cool",L"10miss"}},
        {L"glv-",{L"veryeasy",L"easy",L"normal",L"hard",L"veryhard",L"total100",L"abyss",L"hell"}},
        {L"random_",{L"off",L"mirror",L"random",L"random+",L"all",L"all+"}},
        {L"notes_",{L"off",L"double",L"copy",L"dblcopy"}},
        {L"gamelevel_",{L"beginner",L"normal",L"hyper",L"danger"}},
        {L"extra_",{L"off",L"easy",L"hard",L"veryhard"}},
        {L"change-hs_",{L"off",L"accel",L"brake",L"line",L"nazo",L"nazo2",L"random",L"combo-up",L"combo-dn",L"combo-rd",L"linenazo",L"warp",L"s-warp",L"line2",L"lineaccel",L"chaos",L"random2"}},
        {L"hidden_",{L"off",L"hidden",L"hidden2",L"hidden3",L"sudden",L"sudden2",L"sudden3",L"m-sudden",L"hid-sud",L"hid-sud2",L"hid-sud3",L"mist",L"deepmist",L"blink",L"blink2",L"stealth"}},
        {L"crazy_",{L"off",L"star",L"spin",L"clone",L"clone2",L"s-clone",L"linemove",L"linemove2",L"good2bad",L"great2bad",L"helljudge",L"xwave",L"xwave2",L"ywave",L"ywave2",L"xywave",L"xywave2",L"3dwave",L"3dwave2",L"hidjudge",L"dark",L"dark2",L"reverse",L"small",L"big",L"fadein",L"fadeout",L"rd-size",L"addmine"}}
    };
    for(const auto& [prefix,values]:names)if(option.starts_with(prefix))return values.contains(option.substr(prefix.size()));
    return false;
}
inline void applyStaticOptions(Chart& chart,uint64_t seed,std::vector<std::wstring>& errors) {
    std::wstring random=L"off",notes=L"off";
    std::mt19937_64 rng(seed);
    for(auto option:chart.options) {
        const auto full=toLower(option);const auto colon=full.find(L':');
        if(colon!=std::wstring::npos && full.substr(0,colon)!=L"774" && full.substr(0,colon)!=L"charatbeathdx") {errors.push_back(L"알 수 없는 OPTION 네임스페이스: "+option);continue;}
        option=normalizeOption(full);
        if(!knownOption(option))errors.push_back(L"지원되지 않는 구형 OPTION 값: "+option);
        if(option==L"glv-total100")chart.total=100;
        if(option.starts_with(L"random_"))random=option.substr(7);
        else if(option.starts_with(L"notes_"))notes=option.substr(6);
        else if(option.starts_with(L"longmode ")) {
            chart.longNoteMode=option.ends_with(L"1")?LongNoteMode::HCN:LongNoteMode::CN;
            for(auto& n:chart.notes)n.mode=chart.longNoteMode;
        }
    }
    if(notes==L"double" || notes==L"dblcopy") {
        const auto original=chart.notes;
        const int keys=chart.mode==ChartMode::FiveKey || chart.mode==ChartMode::TenKey?5:7;
        for(auto n:original)if(n.lane%8!=0) {n.lane=(n.lane/8)*8+(n.lane%keys)+1;chart.notes.push_back(std::move(n));}
    }
    if((notes==L"copy" || notes==L"dblcopy") && (chart.mode==ChartMode::SevenKey || chart.mode==ChartMode::FiveKey)) {
        const int keys=chart.mode==ChartMode::FiveKey?5:7;const auto original=chart.notes;
        for(auto n:original){n.lane=n.lane==0?8:8+keys+1-n.lane;chart.notes.push_back(std::move(n));}
        chart.mode=keys==5?ChartMode::TenKey:ChartMode::FourteenKey;
    }
    std::array<int,16> map{};std::iota(map.begin(),map.end(),0);
    const auto layout=chartLayout(chart,0,68,104);
    const bool across=random==L"all" || random==L"all+";
    for(int side=0;side<(across?1:layout.players);++side) {
        std::vector<int> keys;
        for(int lane=0;lane<layout.laneCount();++lane)if(layout.width(lane)>0 && (across || layout.popn || lane/8==side) &&
            (layout.popn || lane%8!=0 || random==L"all+" || random==L"random+"))keys.push_back(lane);
        auto shuffled=keys;if(random==L"mirror")std::reverse(shuffled.begin(),shuffled.end());
        else if(random!=L"off")std::shuffle(shuffled.begin(),shuffled.end(),rng);
        for(size_t i=0;i<keys.size();++i)map[keys[i]]=shuffled[i];
    }
    for(auto& n:chart.notes)n.lane=map[n.lane];for(auto& n:chart.invisible)n.lane=map[n.lane];for(auto& n:chart.mines)n.lane=map[n.lane];
    const auto options=forcedOptions(chart);
    chart.halfJudgement=options.crazy.contains(L"helljudge");
    if(options.crazy.contains(L"addmine"))for(const auto& n:chart.notes) {
        std::vector<int> available;for(int lane=0;lane<layout.laneCount();++lane)if(layout.width(lane)>0 && lane!=n.lane)available.push_back(lane);
        if(!available.empty())chart.mines.push_back({available[rng()%available.size()],n.timeSeconds,5});
    }
    std::stable_sort(chart.notes.begin(),chart.notes.end(),[](const Note&a,const Note&b){return a.timeSeconds<b.timeSeconds;});
    std::stable_sort(chart.mines.begin(),chart.mines.end(),[](const MineNote&a,const MineNote&b){return a.timeSeconds<b.timeSeconds;});
}
} // namespace bms
