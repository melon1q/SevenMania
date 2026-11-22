#pragma once
// 1P lane option applied to a freshly loaded chart before play: OFF, MIRROR, RANDOM and S-RANDOM.
// The order and names follow LR2's random option (select button 42, string 63; best-record
// conditions 126-131), which also has H-RANDOM and ALL-SCR; SevenMania does not offer those two.
// Only the 1P keys move: the scratch stays, and a double-play chart's 2P side is left as written
// (LR2 gives the 2P side its own option, button 43).
#include "BmsParser.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace bms {
enum class LaneOption : int { Off, Mirror, Random, SRandom, Count };
inline constexpr int laneOptionCount = static_cast<int>(LaneOption::Count);

inline LaneOption laneOptionFromInt(int value) noexcept {
    return value >= 0 && value < laneOptionCount ? static_cast<LaneOption>(value) : LaneOption::Off;
}
inline const wchar_t* laneOptionName(LaneOption option) noexcept {
    switch (option) {
    case LaneOption::Mirror: return L"MIRROR";
    case LaneOption::Random: return L"RANDOM";
    case LaneOption::SRandom: return L"S-RANDOM";
    default: return L"OFF";
    }
}
// Back from a stored name; unknown or empty text is OFF (records from before the option existed).
inline LaneOption laneOptionFromName(const std::wstring& name) noexcept {
    for (int i = 0; i < laneOptionCount; ++i)
        if (name == laneOptionName(static_cast<LaneOption>(i))) return static_cast<LaneOption>(i);
    return LaneOption::Off;
}

// The 1P key lanes of a chart, left to right (Note::lane numbering).
inline std::vector<int> laneOptionKeys(const Chart& chart) {
    std::vector<int> lanes;
    const int first = chart.mode == ChartMode::PopnNine ? 0 : 1;
    const int last = chart.mode == ChartMode::PopnNine ? 8 :
        chart.mode == ChartMode::FiveKey || chart.mode == ChartMode::TenKey ? 5 : 7;
    for (int lane = first; lane <= last; ++lane) lanes.push_back(lane);
    return lanes;
}

// Moves the chart's 1P key notes (with their hidden key sounds and mines) and returns, for MIRROR and
// RANDOM, the lane each original key lane went to (index = position in laneOptionKeys); empty for OFF
// and S-RANDOM. S-RANDOM picks a lane per note, never one a long note is still holding or one another
// note already uses at the same moment; a note with no such lane keeps its own.
inline std::vector<int> applyLaneOption(Chart& chart, LaneOption option, uint64_t seed) {
    const auto keys = laneOptionKeys(chart);
    if (option == LaneOption::Off || keys.size() < 2) return {};
    const auto isKey = [&](int lane) { return std::find(keys.begin(), keys.end(), lane) != keys.end(); };
    std::mt19937_64 random(seed);
    if (option == LaneOption::Mirror || option == LaneOption::Random) {
        std::vector<int> target = keys;
        if (option == LaneOption::Mirror) std::reverse(target.begin(), target.end());
        else std::shuffle(target.begin(), target.end(), random);
        const auto map = [&](int& lane) {
            const auto at = std::find(keys.begin(), keys.end(), lane);
            if (at != keys.end()) lane = target[static_cast<size_t>(at - keys.begin())];
        };
        for (auto& note : chart.notes) map(note.lane);
        for (auto& hidden : chart.invisible) map(hidden.lane);
        for (auto& mine : chart.mines) map(mine.lane);
        return target;
    }
    // S-RANDOM. Notes are in time order; a lane is busy until its long note's end, and taken for the
    // instant a note lands on it.
    constexpr double sameMoment = 1e-6;
    std::vector<double> busyUntil(16, -1e300), takenAt(16, -1e300);
    const auto pick = [&](double time, bool avoidTaken) -> int {
        std::vector<int> free;
        for (const int lane : keys)
            if (busyUntil[static_cast<size_t>(lane)] < time - sameMoment &&
                (!avoidTaken || std::abs(takenAt[static_cast<size_t>(lane)] - time) > sameMoment)) free.push_back(lane);
        if (free.empty()) return -1;
        return free[std::uniform_int_distribution<size_t>(0, free.size() - 1)(random)];
    };
    for (auto& note : chart.notes) {
        if (!isKey(note.lane)) continue;
        const int lane = pick(note.timeSeconds, true);
        if (lane >= 0) note.lane = lane;
        takenAt[static_cast<size_t>(note.lane)] = note.timeSeconds;
        if (note.longNote()) busyUntil[static_cast<size_t>(note.lane)] = note.timeSeconds + note.lengthSeconds;
    }
    // Hidden key sounds and mines take a lane no long note holds then; they may share a note's moment.
    std::vector<std::pair<double, double>> holds[16];
    for (const auto& note : chart.notes)
        if (note.longNote() && note.lane >= 0 && note.lane < 16) holds[note.lane].push_back({note.timeSeconds, note.timeSeconds + note.lengthSeconds});
    const auto held = [&](int lane, double time) {
        for (const auto& [start, end] : holds[lane]) if (time >= start - sameMoment && time <= end + sameMoment) return true;
        return false;
    };
    const auto pickFree = [&](double time, int fallback) {
        std::vector<int> free;
        for (const int lane : keys) if (!held(lane, time)) free.push_back(lane);
        return free.empty() ? fallback : free[std::uniform_int_distribution<size_t>(0, free.size() - 1)(random)];
    };
    for (auto& hidden : chart.invisible) if (isKey(hidden.lane)) hidden.lane = pickFree(hidden.timeSeconds, hidden.lane);
    for (auto& mine : chart.mines) if (isKey(mine.lane)) mine.lane = pickFree(mine.timeSeconds, mine.lane);
    return {};
}
} // namespace bms
