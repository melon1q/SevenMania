#pragma once
#include "BmsParser.h"
#include "Judgement.h"
#include "OfficialJudgement.h"
#include "LaneLayout.h"
#include <algorithm>
#include <cmath>

namespace bms {
inline std::wstring assetId(const Chart& chart, const std::wstring& id) { return chart.objectBase==62?id:toLower(id); }
inline double valueAt(const std::vector<ValueEvent>& events, double time, double fallback) {
    const auto next=std::upper_bound(events.begin(),events.end(),time,[](double t,const ValueEvent&e){return t<e.timeSeconds;});
    return next==events.begin()?fallback:std::prev(next)->value;
}
inline double speedAt(const Chart& chart,double time) {
    const auto next=std::upper_bound(chart.speeds.begin(),chart.speeds.end(),time,[](double t,const ValueEvent&e){return t<e.timeSeconds;});
    const double start=next==chart.speeds.begin()?0:std::prev(next)->timeSeconds;
    const double value=next==chart.speeds.begin()?1:std::prev(next)->value;
    if(next==chart.speeds.end() || next->timeSeconds<=start) return value;
    return value+(next->value-value)*std::clamp((time-start)/(next->timeSeconds-start),0.0,1.0);
}
inline double rankPercent(const Chart& chart,double time) {
    // BMS RANK is a category; DEFEXRANK is a percentage, never a category.
    static constexpr double ranks[]{25,50,75,100,125};
    double percent=chart.extendedRank>0?chart.extendedRank*.75:ranks[std::clamp(chart.rank,0,4)];
    return valueAt(chart.judgeRanks,time,percent/.75)*.75;
}
inline JudgementWindows chartJudgementWindows(const Chart& chart,double time,JudgementProfile profile) {
    auto w=judgementWindows(profile);
    const double factor=rankPercent(chart,time)/75.0*(chart.halfJudgement?.5:1);
    w.pGreatMs*=factor;w.greatMs*=factor;w.goodMs*=factor;w.badMs*=factor;
    return w;
}
inline TimingJudgement judgeChartOffset(const Chart& chart,double time,double offset,JudgementProfile profile) {
    const double ms=std::abs(offset)*1000;const auto w=chartJudgementWindows(chart,time,profile);
    if(ms<=w.pGreatMs)return TimingJudgement::PGreat;
    if(ms<=w.greatMs)return TimingJudgement::Great;
    if(ms<=w.goodMs)return TimingJudgement::Good;
    if(ms<=w.badMs)return TimingJudgement::Bad;
    return TimingJudgement::Poor;
}
inline bool usesOfficialJudgement(const Chart& chart) {
    return chart.extendedRank<=0 && chart.judgeRanks.empty() && !chart.halfJudgement;
}
inline bool isScratchLane(const Chart& chart,int lane) {
    return chart.mode!=ChartMode::PopnNine && (lane==0 || lane==8);
}
inline bool usesOfficialLane(const Chart& chart,int lane) {
    return usesOfficialJudgement(chart) && !isScratchLane(chart,lane);
}
inline double laneBadMilliseconds(const Chart& chart,double time,int lane,bool slow,JudgementProfile profile) {
    if(!usesOfficialLane(chart,lane))return chartJudgementWindows(chart,time,profile).badMs;
    const auto& w=officialJudgeWindow(chart.rank);return slow?w.badSlow:w.badFast;
}
inline std::optional<TimingJudgement> judgeLaneOffset(const Chart& chart,double time,int lane,double seconds,JudgementProfile profile) {
    if(usesOfficialLane(chart,lane))return judgeOfficialMilliseconds(seconds*1000,chart.rank);
    const auto grade=judgeChartOffset(chart,time,seconds,profile);
    return grade==TimingJudgement::Poor?std::nullopt:std::optional{grade};
}
inline int nearestLaneNote(const Chart& chart,const std::vector<uint8_t>& hit,int lane,double time,double timingScale,JudgementProfile profile,double start=0) {
    const double slow=laneBadMilliseconds(chart,time,lane,true,profile)/1000*timingScale;
    const double fast=laneBadMilliseconds(chart,time,lane,false,profile)/1000*timingScale;
    auto first=std::lower_bound(chart.notes.begin(),chart.notes.end(),time-slow,[](const Note& n,double t){return n.timeSeconds<t;});
    int nearest=-1;double distance=std::max(slow,fast)+.000001;
    for(auto it=first;it!=chart.notes.end() && it->timeSeconds<=time+fast;++it) {
        const auto i=static_cast<size_t>(it-chart.notes.begin());
        if(i>=hit.size() || hit[i] || it->lane!=lane || it->timeSeconds<start)continue;
        if(!judgeLaneOffset(chart,time,lane,(time-it->timeSeconds)/timingScale,profile))continue;
        const double delta=std::abs(time-it->timeSeconds);
        if(delta<=distance){nearest=static_cast<int>(i);distance=delta;}
    }
    return nearest;
}
inline bool laneNoteExpired(const Chart& chart,const Note& note,double time,double timingScale,JudgementProfile profile) {
    return (time-note.timeSeconds)/timingScale*1000>laneBadMilliseconds(chart,time,note.lane,true,profile);
}
inline size_t judgementNoteCount(const Chart& chart) {
    size_t count=0;for(const auto& n:chart.notes)count+=n.longNote() && n.mode!=LongNoteMode::LN?2:1;return count;
}
inline double chartTotal(const Chart& chart,size_t notes) {
    if(chart.total>0)return chart.total;
    const double n=static_cast<double>(notes);
    return std::max(260.0,7.605*n/(0.01*n+6.5));
}
inline const HiddenNote* hiddenAt(const Chart& chart,int lane,double time) {
    const HiddenNote* last=nullptr;
    for(const auto& n:chart.invisible) {if(n.timeSeconds>time)break;if(n.lane==lane)last=&n;}
    return last;
}
inline std::wstring textAt(const Chart& chart,double time) {
    const auto next=std::upper_bound(chart.texts.begin(),chart.texts.end(),time,[](double t,const TextEvent&e){return t<e.timeSeconds;});
    return next==chart.texts.begin()?L"":std::prev(next)->text;
}
inline LaneLayout chartLayout(const Chart& chart,int x,int width,int scratch) {
    return {x,width,scratch,chart.mode==ChartMode::FiveKey || chart.mode==ChartMode::TenKey?5:7,
        chart.mode==ChartMode::TenKey || chart.mode==ChartMode::FourteenKey?2:1,chart.mode==ChartMode::PopnNine};
}
inline int chartLanes(const Chart& chart) {
    switch(chart.mode) {
    case ChartMode::FiveKey:return 6; case ChartMode::TenKey:return 16;
    case ChartMode::FourteenKey:return 16;case ChartMode::PopnNine:return 9;default:return 8;
    }
}
} // namespace bms
