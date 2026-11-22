#include "KeyModeFilter.h"
#include "TargetScore.h"

#include <iostream>

using namespace bms;

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) { std::cerr << "FAIL: " << what << "\n"; ++failures; }
}

int main() {
    // Tab order visits every filter once, then wraps.
    KeyModeFilter f = KeyModeFilter::Key7;
    const KeyModeFilter order[]{ KeyModeFilter::Key5, KeyModeFilter::Key14, KeyModeFilter::Key10, KeyModeFilter::Pms9, KeyModeFilter::All, KeyModeFilter::Key7 };
    for (const auto expected : order) { f = nextKeyModeFilter(f); check(f == expected, "cycle order"); }

    check(keyModeFilterMatches(KeyModeFilter::Key7, ChartMode::SevenKey), "7K matches 7K");
    check(keyModeFilterMatches(KeyModeFilter::Key7, ChartMode::Unknown), "unknown counts as 7K");
    check(!keyModeFilterMatches(KeyModeFilter::Key7, ChartMode::FourteenKey), "7K hides DP");
    check(keyModeFilterMatches(KeyModeFilter::Key14, ChartMode::FourteenKey), "14K matches 14K");
    check(!keyModeFilterMatches(KeyModeFilter::Key14, ChartMode::TenKey), "14K hides 10K");
    check(keyModeFilterMatches(KeyModeFilter::Key10, ChartMode::TenKey), "10K matches 10K");
    check(keyModeFilterMatches(KeyModeFilter::Key5, ChartMode::FiveKey), "5K matches 5K");
    check(keyModeFilterMatches(KeyModeFilter::Pms9, ChartMode::PopnNine), "PMS matches PMS");
    check(!keyModeFilterMatches(KeyModeFilter::Pms9, ChartMode::SevenKey), "PMS hides 7K");
    for (const auto m : { ChartMode::FiveKey, ChartMode::SevenKey, ChartMode::TenKey, ChartMode::FourteenKey, ChartMode::PopnNine, ChartMode::Other })
        check(keyModeFilterMatches(KeyModeFilter::All, m), "ALL matches everything");

    for (int i = 0; i <= static_cast<int>(KeyModeFilter::All); ++i)
        check(static_cast<int>(keyModeFilterFromInt(i)) == i, "int round trip");
    check(keyModeFilterFromInt(99) == KeyModeFilter::Key7 && keyModeFilterFromInt(-1) == KeyModeFilter::Key7, "bad ini value falls back to 7K");
    check(std::wstring(keyModeFilterLabel(KeyModeFilter::All)) == L"ALL", "label");

    // Difficulty filter (LR2 button 10): ALL, then BEGINNER..INSANE, both directions wrap.
    check(nextDifficultyFilter(0, true) == 1 && nextDifficultyFilter(5, true) == 0 && nextDifficultyFilter(0, false) == 5,
        "difficulty filter cycles");
    check(difficultyFilterMatches(0, 3) && difficultyFilterMatches(0, 0), "ALL shows every chart");
    check(difficultyFilterMatches(3, 3) && !difficultyFilterMatches(3, 4) && !difficultyFilterMatches(3, 0),
        "HYPER shows only HYPER; a chart without #DIFFICULTY shows only in ALL");
    check(difficultyFilterFromInt(9) == 0 && difficultyFilterFromInt(-1) == 0 && difficultyFilterFromInt(4) == 4, "bad ini value falls back to ALL");
    check(std::wstring(difficultyFilterLabel(5)) == L"INSANE" && std::wstring(difficultyFilterLabel(0)) == L"ALL", "difficulty labels");

    // Target score (LR2 buttons 76/77): best score, rank borders rounded up, or a percent of the maximum.
    check(targetExScore(TargetKind::MyBest, 1234, 1000, 50) == 1234 && targetExScore(TargetKind::MyBest, std::nullopt, 1000, 50) == 0,
        "MY BEST is the best EX score, 0 without one");
    check(targetExScore(TargetKind::MyBest, 5000, 1000, 50) == 2000, "a best beyond this chart's maximum is capped");
    check(targetExScore(TargetKind::RankAAA, std::nullopt, 1000, 50) == 1778 && targetExScore(TargetKind::RankAA, std::nullopt, 1000, 50) == 1556 &&
        targetExScore(TargetKind::RankA, std::nullopt, 1000, 50) == 1334, "rank borders (8/9, 7/9, 6/9 of 2000, rounded up)");
    check(targetExScore(TargetKind::RankAAA, std::nullopt, 900, 50) == 1600, "an exact border is not rounded");
    check(targetExScore(TargetKind::Default, std::nullopt, 1000, 75) == 1500 && targetExScore(TargetKind::Default, std::nullopt, 1000, 150) == 2000 &&
        targetExScore(TargetKind::Default, std::nullopt, 1000, -5) == 0, "default target is a clamped percent");
    check(targetExScore(TargetKind::RankA, std::nullopt, 0, 50) == 0, "a chart without notes has no target");
    check(nextTargetKind(TargetKind::Default, true) == TargetKind::MyBest && nextTargetKind(TargetKind::MyBest, false) == TargetKind::Default &&
        targetKindFromInt(7) == TargetKind::MyBest && targetKindFromInt(2) == TargetKind::RankAA, "target kinds cycle and read back");

    if (failures == 0) std::cout << "KeyModeFilterTests passed\n";
    return failures == 0 ? 0 : 1;
}
