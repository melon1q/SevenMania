#pragma once

#include "Judgement.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

class ResultScore {
    static constexpr int maximumScore_ = 1'000'000;
    int score_ = 0;
    int64_t totalUnits_{}, processedUnits_{}, earnedWeight_{}, exHalfUnits_{};
    std::array<int64_t, 5> judgementUnits_{}; // Indexed by TimingJudgement.
public:
    // Every note has two units. Ordinary notes earn both at the head;
    // long notes split them equally between head and release.
    void reset(int totalNotes) {
        score_ = 0; processedUnits_ = earnedWeight_ = exHalfUnits_ = 0; judgementUnits_.fill(0);
        totalUnits_ = static_cast<int64_t>(std::max(0, totalNotes)) * 2;
    }
    void apply(TimingJudgement judgement, int units = 2) {
        if (units <= 0 || totalUnits_ <= 0 || processedUnits_ >= totalUnits_) return;
        const int64_t acceptedUnits = std::min<int64_t>(units, totalUnits_ - processedUnits_);
        int accuracy = 0;
        switch (judgement) {
        case TimingJudgement::PGreat: accuracy = 100; exHalfUnits_ += 2 * acceptedUnits; break;
        case TimingJudgement::Great: accuracy = 80; exHalfUnits_ += acceptedUnits; break;
        case TimingJudgement::Good: accuracy = 50; break;
        case TimingJudgement::Bad:
        case TimingJudgement::Poor: break;
        }
        judgementUnits_[static_cast<std::size_t>(judgement)] += acceptedUnits;
        processedUnits_ += acceptedUnits;
        earnedWeight_ += acceptedUnits * accuracy;
        // Round only the cumulative display value, keeping fractional awards.
        score_ = static_cast<int>(earnedWeight_ * maximumScore_ / (totalUnits_ * 100));
    }
    int value() const { return score_; }
    int exScore() const { return static_cast<int>(std::min<int64_t>(exHalfUnits_ / 2, INT32_MAX)); }
    // Notes per judgement (PGreat, Great, Good, Bad, Poor); a long note's half units count once both halves are in.
    std::array<int, 5> judgementCounts() const {
        std::array<int, 5> counts{};
        for (std::size_t grade = 0; grade < counts.size(); ++grade)
            counts[grade] = static_cast<int>(std::min<int64_t>(judgementUnits_[grade] / 2, INT32_MAX));
        return counts;
    }
    // LR2's miss count (BP): Bad and Poor, plus the notes a failed or abandoned play never reached.
    int missCount() const {
        const int64_t units = judgementUnits_[3] + judgementUnits_[4] + (totalUnits_ - processedUnits_);
        return static_cast<int>(std::min<int64_t>((units + 1) / 2, INT32_MAX));
    }
    int noteCount() const { return static_cast<int>(std::min<int64_t>(totalUnits_ / 2, INT32_MAX)); }
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
