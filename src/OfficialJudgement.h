#pragma once
#include "Judgement.h"
#include <array>
#include <optional>
#include <string_view>

namespace bms {
inline constexpr std::string_view officialJudgementId="SM-JUDGE-1";
struct OfficialJudgeWindow { int pgreat,great,good,badFast,badSlow; };
inline constexpr std::array<OfficialJudgeWindow,4> officialJudgeTable{{
    {7,20,39,128,135},{13,30,68,155,170},
    {17,43,106,183,205},{21,60,135,210,240}
}};
inline constexpr const OfficialJudgeWindow& officialJudgeWindow(int rank) {
    return officialJudgeTable[rank>=0&&rank<4?rank:2];
}
// Absence means outside the hit window, not an instruction to consume a note
// or award a miss. Candidate selection and expiration own that policy.
inline std::optional<TimingJudgement> judgeOfficialMilliseconds(double deltaMs,int rank) {
    if(!std::isfinite(deltaMs))return {};
    const auto& w=officialJudgeWindow(rank);const double magnitude=std::abs(deltaMs);
    if(magnitude<=w.pgreat)return TimingJudgement::PGreat;
    if(magnitude<=w.great)return TimingJudgement::Great;
    if(magnitude<=w.good)return TimingJudgement::Good;
    if(deltaMs<0?magnitude<=w.badFast:magnitude<=w.badSlow)return TimingJudgement::Bad;
    return {};
}
}
