#pragma once
#include "IndependentGaugeShift.h"
#include <optional>
#include <string_view>

namespace sevenmania::gauge_trial {
using independent_gauge::Kind;
using independent_gauge::Profile;
using AutoShiftMode = independent_gauge::Mode;
struct TrialOptions {
    std::optional<Kind> selected;
    AutoShiftMode shift{AutoShiftMode::None};
    Kind bottom{Kind::AssistEasy};
    Profile profile{Profile::SevenKeys};
};
enum class TrialOptionResult { Unrecognized, Applied, Invalid };
inline std::optional<Kind> parseTrialKind(std::wstring_view value) noexcept {
    if(value==L"assist-easy")return Kind::AssistEasy;
    if(value==L"easy")return Kind::Easy;
    if(value==L"normal")return Kind::Normal;
    if(value==L"hard")return Kind::Hard;
    if(value==L"ex-hard")return Kind::ExHard;
    if(value==L"hazard")return Kind::Hazard;
    return std::nullopt;
}
inline TrialOptionResult parseTrialOption(std::wstring_view argument,TrialOptions& options) noexcept {
    constexpr std::wstring_view kind=L"--gauge-kind=",bottom=L"--gauge-bottom=",shift=L"--gauge-auto-shift=",profile=L"--gauge-profile=";
    if(argument.starts_with(profile)) {
        const auto value=argument.substr(profile.size());
        if(value==L"7keys")options.profile=Profile::SevenKeys;
        else if(value==L"lr2")options.profile=Profile::BeatorajaLR2;
        else if(value==L"lr2oraja")options.profile=Profile::LR2oraja;
        else return TrialOptionResult::Invalid;
        return TrialOptionResult::Applied;
    }
    if(argument.starts_with(kind)||argument.starts_with(bottom)) {
        const bool isBottom=argument.starts_with(bottom);
        const auto parsed=parseTrialKind(argument.substr(isBottom?bottom.size():kind.size()));
        if(!parsed)return TrialOptionResult::Invalid;
        if(isBottom)options.bottom=*parsed;else options.selected=*parsed;
        return TrialOptionResult::Applied;
    }
    if(argument.starts_with(shift)) {
        const auto value=argument.substr(shift.size());
        if(value==L"none")options.shift=AutoShiftMode::None;
        else if(value==L"continue")options.shift=AutoShiftMode::Continue;
        else if(value==L"survival-to-groove")options.shift=AutoShiftMode::SurvivalToGroove;
        else if(value==L"best-clear")options.shift=AutoShiftMode::BestClear;
        else if(value==L"select-to-under")options.shift=AutoShiftMode::SelectToUnder;
        else return TrialOptionResult::Invalid;
        return TrialOptionResult::Applied;
    }
    return TrialOptionResult::Unrecognized;
}
inline const wchar_t* trialKindName(Kind kind) noexcept {
    switch(kind) {
    case Kind::AssistEasy:return L"ASSIST EASY";case Kind::Easy:return L"EASY";
    case Kind::Normal:return L"NORMAL";case Kind::Hard:return L"HARD";
    case Kind::ExHard:return L"EX-HARD";case Kind::Hazard:return L"HAZARD";
    default:return L"NORMAL";
    }
}
}
