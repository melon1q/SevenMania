#pragma once

#include "BmsParser.h"
#include <algorithm>
#include <cstddef>
#include <vector>

namespace bms {

// Autoplay presses every lane note exactly when it reaches the judgement line, through the same press and
// release path as the player's keys, so key beams, bombs, hold timers and keysounds behave as in a real play.
// A tap is held briefly so the key beam shows, a long note until its end. A key is let go before any mine
// on its lane and before the lane's next note, so autoplay never touches a mine it can step around.
struct AutoPlayEvent {
    double timeSeconds{};
    int lane{};
    bool press{};
    std::size_t noteIndex{}; // The note a press judges (or the note a release ends).
};

inline constexpr double autoPlayTapSeconds = 0.08;
// A key is up this long before a mine on its lane reaches the judgement line.
inline constexpr double autoPlayMineMarginSeconds = 0.002;

inline std::vector<AutoPlayEvent> buildAutoPlayEvents(const Chart& chart, double tapSeconds = autoPlayTapSeconds) {
    constexpr int lanes = 16;
    std::vector<std::vector<std::size_t>> laneNotes(lanes);
    std::vector<std::vector<double>> laneMines(lanes);
    for (std::size_t i = 0; i < chart.notes.size(); ++i) {
        const int lane = chart.notes[i].lane;
        if (lane >= 0 && lane < lanes) laneNotes[static_cast<std::size_t>(lane)].push_back(i);
    }
    for (const auto& mine : chart.mines)
        if (mine.lane >= 0 && mine.lane < lanes) laneMines[static_cast<std::size_t>(mine.lane)].push_back(mine.timeSeconds);
    std::vector<AutoPlayEvent> events;
    events.reserve(chart.notes.size() * 2);
    for (int lane = 0; lane < lanes; ++lane) {
        auto& indices = laneNotes[static_cast<std::size_t>(lane)];
        auto& mines = laneMines[static_cast<std::size_t>(lane)];
        std::stable_sort(indices.begin(), indices.end(), [&](std::size_t a, std::size_t b) {
            return chart.notes[a].timeSeconds < chart.notes[b].timeSeconds; });
        std::sort(mines.begin(), mines.end());
        for (std::size_t k = 0; k < indices.size(); ++k) {
            const auto& note = chart.notes[indices[k]];
            const double press = note.timeSeconds;
            double release = note.longNote() ? press + note.lengthSeconds : press + tapSeconds;
            if (!note.longNote()) {
                // Up again before the lane's next note and before the next mine after this press.
                if (k + 1 < indices.size()) release = std::min(release, chart.notes[indices[k + 1]].timeSeconds);
                const auto mine = std::upper_bound(mines.begin(), mines.end(), press);
                if (mine != mines.end()) release = std::min(release, *mine - autoPlayMineMarginSeconds);
                release = std::max(release, press);
            }
            events.push_back({press, lane, true, indices[k]});
            events.push_back({release, lane, false, indices[k]});
        }
    }
    // Lanes were appended one after another, each in order, so a stable sort by time keeps a lane's release
    // ahead of its next press at the same instant.
    std::stable_sort(events.begin(), events.end(), [](const AutoPlayEvent& a, const AutoPlayEvent& b) {
        return a.timeSeconds < b.timeSeconds; });
    return events;
}

// Plays the schedule forward: due() hands out each event whose time has come, once.
class AutoPlayCursor {
public:
    void reset(std::vector<AutoPlayEvent> events, double startSeconds = 0) {
        events_ = std::move(events);
        next_ = 0;
        // A play that starts later (practice) skips whole notes before the start, press and release alike.
        while (next_ < events_.size() && events_[next_].timeSeconds < startSeconds) ++next_;
        while (next_ < events_.size() && !events_[next_].press) ++next_;
    }
    void clear() { events_.clear(); next_ = 0; }
    template <class Handler>
    void due(double timeSeconds, Handler&& handler) {
        while (next_ < events_.size() && events_[next_].timeSeconds <= timeSeconds) handler(events_[next_++]);
    }
    bool finished() const noexcept { return next_ >= events_.size(); }
    std::size_t remaining() const noexcept { return events_.size() - next_; }
private:
    std::vector<AutoPlayEvent> events_;
    std::size_t next_{};
};

} // namespace bms
