#pragma once

#include "Judgement.h"
#include <algorithm>
#include <cstdint>
#include <string_view>

class ResultScore {
    static constexpr int maximumScore_ = 1'000'000;
    int score_ = 0;
    int64_t totalUnits_{}, processedUnits_{}, earnedWeight_{};
public:
    // Every note has two units. Ordinary notes earn both at the head;
    // long notes split them equally between head and release.
    void reset(int totalNotes) {
        score_ = 0; processedUnits_ = earnedWeight_ = 0;
        totalUnits_ = static_cast<int64_t>(std::max(0, totalNotes)) * 2;
    }
    void apply(TimingJudgement judgement, int units = 2) {
        if (units <= 0 || processedUnits_ >= totalUnits_) return;
        const int64_t acceptedUnits = std::min<int64_t>(units, totalUnits_ - processedUnits_);
        int accuracy = 0;
        switch (judgement) {
        case TimingJudgement::PGreat: accuracy = 100; break;
        case TimingJudgement::Great: accuracy = 80; break;
        case TimingJudgement::Good: accuracy = 50; break;
        case TimingJudgement::Bad:
        case TimingJudgement::Poor: break;
        }
        processedUnits_ += acceptedUnits;
        earnedWeight_ += acceptedUnits * accuracy;
        // Round only the cumulative display value, keeping fractional awards.
        score_ = static_cast<int>(earnedWeight_ * maximumScore_ / (totalUnits_ * 100));
    }
    int value() const { return score_; }
    static std::wstring_view rankForScore(int score) {
        if (score >= 950'000) return L"S";
        if (score >= 900'000) return L"AAA";
        if (score >= 800'000) return L"AA";
        if (score >= 700'000) return L"A";
        if (score >= 600'000) return L"B";
        if (score >= 500'000) return L"C";
        return L"F";
    }
    std::wstring_view rank() const { return rankForScore(score_); }
};
