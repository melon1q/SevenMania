#include "AutoPlay.h"
#include "LongNoteHold.h"
#include "PlayOutcome.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bms::Note note(int lane, double time, double length = 0, bms::LongNoteMode mode = bms::LongNoteMode::LN) {
    bms::Note n;
    n.lane = lane; n.timeSeconds = time; n.lengthSeconds = length; n.mode = mode; n.sampleId = L"01";
    return n;
}

// A small stand-in for the host's frame loop: autoplay events first, then holds and mines, the same
// order as updatePlaybackFrame. Presses judge the scheduled note as PGREAT; releases end holds.
struct Simulation {
    struct Outcome {
        int presses{}, releases{}, mineHits{}, keysounds{};
        std::vector<HoldResult> holds;
        std::vector<int> pressedNotes;
        bool everyNoteOnce{};
    };
    static Outcome run(const bms::Chart& chart, double frameSeconds, double start = 0) {
        Outcome out;
        bms::AutoPlayCursor cursor;
        cursor.reset(bms::buildAutoPlayEvents(chart), start);
        std::array<bool, 16> pressed{};
        std::array<LongNoteHold, 16> holds{};
        std::vector<int> hit(chart.notes.size());
        std::vector<bool> mineDone(chart.mines.size());
        double end = 1;
        for (const auto& n : chart.notes) end = std::max(end, n.timeSeconds + n.lengthSeconds + 1);
        // Frames land off the beat on purpose (a third of a frame late).
        for (double t = start + frameSeconds / 3; t < end; t += frameSeconds) {
            cursor.due(t, [&](const bms::AutoPlayEvent& e) {
                const auto lane = static_cast<size_t>(e.lane);
                if (e.press) {
                    if (pressed[lane]) return;
                    pressed[lane] = true; ++out.presses;
                    if (!hit[e.noteIndex]) {
                        ++hit[e.noteIndex]; ++out.keysounds; out.pressedNotes.push_back(static_cast<int>(e.noteIndex));
                        const auto& n = chart.notes[e.noteIndex];
                        if (n.longNote()) holds[lane].start(e.noteIndex, n.timeSeconds + n.lengthSeconds, t, n.mode);
                    }
                } else {
                    if (!pressed[lane]) return;
                    pressed[lane] = false; ++out.releases;
                    if (const auto r = holds[lane].release(t, std::numeric_limits<double>::max()); r != HoldResult::None) out.holds.push_back(r);
                }
            });
            for (size_t lane = 0; lane < holds.size(); ++lane)
                if (const auto r = holds[lane].update(t, pressed[lane], 0.1); r != HoldResult::None) out.holds.push_back(r);
            for (size_t m = 0; m < chart.mines.size(); ++m) {
                const auto& mine = chart.mines[m];
                if (mineDone[m] || mine.timeSeconds > t) continue;
                mineDone[m] = true;
                if (pressed[static_cast<size_t>(mine.lane)]) ++out.mineHits;
            }
        }
        out.everyNoteOnce = true;
        for (size_t i = 0; i < hit.size(); ++i)
            if (chart.notes[i].timeSeconds >= start && hit[i] != 1) out.everyNoteOnce = false;
        require(cursor.finished(), "the schedule runs to its end");
        return out;
    }
};

} // namespace

int main() {
    try {
        // Taps on scratch, keys and the 2P side: one press at each note's own time, then a short release.
        {
            bms::Chart chart;
            chart.notes = {note(0, 1.0), note(1, 1.0), note(7, 1.25), note(8, 1.5), note(15, 1.5), note(3, 2.0)};
            const auto events = bms::buildAutoPlayEvents(chart);
            require(events.size() == 12, "a press and a release for every note");
            for (size_t i = 1; i < events.size(); ++i) require(events[i - 1].timeSeconds <= events[i].timeSeconds, "events in time order");
            for (size_t i = 0; i < chart.notes.size(); ++i) {
                int presses = 0;
                for (const auto& e : events)
                    if (e.press && e.noteIndex == i) {
                        ++presses;
                        require(e.timeSeconds == chart.notes[i].timeSeconds && e.lane == chart.notes[i].lane, "pressed on the note itself");
                    } else if (!e.press && e.noteIndex == i)
                        require(std::abs(e.timeSeconds - chart.notes[i].timeSeconds - bms::autoPlayTapSeconds) < 1e-9, "tap held briefly");
                require(presses == 1, "every note pressed once");
            }
            for (const double frame : {1.0 / 60, 1.0 / 240, 1.0 / 1000}) {
                const auto out = Simulation::run(chart, frame);
                require(out.everyNoteOnce && out.keysounds == 6 && out.presses == 6 && out.releases == 6, "simulated taps all judged once");
            }
        }
        // Fast notes on one lane: the key is up again before the next note, so none is swallowed.
        {
            bms::Chart chart;
            chart.notes = {note(2, 1.0), note(2, 1.03), note(2, 1.06), note(2, 1.06 + 1e-4)};
            const auto events = bms::buildAutoPlayEvents(chart);
            for (size_t i = 0; i + 1 < events.size(); ++i)
                if (events[i].press) require(!events[i + 1].press && events[i + 1].lane == 2, "release before the next press");
            require(events[1].timeSeconds == 1.03, "released exactly when the next note comes");
            const auto out = Simulation::run(chart, 1.0 / 1000);
            require(out.everyNoteOnce && out.presses == 4, "a 30 ms trill and a near-chord are all hit");
        }
        // Long notes of every mode end as clean releases, held through the body, even on 60 FPS frames.
        {
            bms::Chart chart;
            chart.notes = {note(1, 1.0, 0.5, bms::LongNoteMode::LN), note(2, 1.0, 0.75, bms::LongNoteMode::CN),
                note(3, 1.2, 1.0, bms::LongNoteMode::HCN), note(0, 2.0, 0.4, bms::LongNoteMode::CN), note(1, 1.6)};
            const auto events = bms::buildAutoPlayEvents(chart);
            for (const auto& e : events)
                if (!e.press && chart.notes[e.noteIndex].longNote()) {
                    const auto& n = chart.notes[e.noteIndex];
                    require(e.timeSeconds == n.timeSeconds + n.lengthSeconds, "a long note is let go on its tail");
                }
            for (const double frame : {1.0 / 60, 1.0 / 144, 1.0 / 1000}) {
                const auto out = Simulation::run(chart, frame);
                require(out.everyNoteOnce && out.holds.size() == 4, "every long note resolves once");
                for (const auto r : out.holds) require(r == HoldResult::PGreat, "long notes end as PGREAT releases");
            }
        }
        // Mines: the key is up before a mine on its lane, between notes and after a tap; other lanes don't matter.
        {
            bms::Chart chart;
            chart.notes = {note(4, 1.0), note(4, 1.2), note(5, 1.0), note(6, 2.0, 0.5, bms::LongNoteMode::CN)};
            chart.mines = {{4, 1.03, 10}, {4, 1.1, 10}, {5, 1.05, 10}, {6, 1.9, 10}, {6, 2.6, 10}, {1, 1.0, 10}};
            const auto events = bms::buildAutoPlayEvents(chart);
            for (const auto& e : events)
                if (!e.press && e.noteIndex == 0) require(e.timeSeconds < 1.03 && e.timeSeconds > 1.0, "tap let go before a close mine");
            for (const double frame : {1.0 / 60, 1.0 / 240, 1.0 / 1000}) {
                const auto out = Simulation::run(chart, frame);
                require(out.mineHits == 0 && out.everyNoteOnce, "autoplay steps around every mine it can");
                for (const auto r : out.holds) require(r == HoldResult::PGreat, "the long note between mines is clean");
            }
            // A mine inside a long note cannot be avoided; the host ignores mines during autoplay for that case.
            bms::Chart inside;
            inside.notes = {note(2, 1.0, 1.0, bms::LongNoteMode::CN)};
            inside.mines = {{2, 1.5, 10}};
            require(Simulation::run(inside, 1.0 / 1000).mineHits == 1, "a mine in a long note body is under the held key");
        }
        // The cursor hands out each event once; a later start skips whole notes before it.
        {
            bms::Chart chart;
            chart.notes = {note(1, 1.0), note(1, 2.0), note(2, 3.0, 0.5)};
            bms::AutoPlayCursor cursor;
            cursor.reset(bms::buildAutoPlayEvents(chart), 1.5);
            int seen = 0;
            cursor.due(1.99, [&](const auto&) { ++seen; });
            require(seen == 0, "nothing before the next note");
            cursor.due(2.0, [&](const bms::AutoPlayEvent& e) { ++seen; require(e.press && e.noteIndex == 1, "first event is the note after the start"); });
            cursor.due(2.0, [&](const auto&) { ++seen; });
            require(seen == 1, "an event is handed out once");
            cursor.due(10, [&](const auto&) { ++seen; });
            require(seen == 4 && cursor.finished(), "the rest of the schedule");
            cursor.reset(bms::buildAutoPlayEvents(chart), 1.05);
            require(cursor.remaining() == 4, "a release left over from a skipped note is dropped");
            const auto practice = Simulation::run(chart, 1.0 / 120, 1.5);
            require(practice.everyNoteOnce && practice.presses == 2, "notes after a later start are all played");
        }
        // Records: autoplay joins practice, the benchmark and diagnostics as plays that save nothing.
        {
            require(bms::playSavesRecord({}), "a normal play is recorded");
            require(!bms::playSavesRecord({.autoplay = true}), "autoplay saves no record");
            require(!bms::playSavesRecord({.practice = true}) && !bms::playSavesRecord({.noInputBenchmark = true}) &&
                !bms::playSavesRecord({.diagnostic = true}), "practice, benchmark and diagnostics save no record");
            require(bms::pauseQuitAction(true, false, false, true) == bms::PauseQuitAction::ReturnToSelect,
                "quitting autoplay leaves for song select without a failure");
            require(bms::pauseQuitAction(true, false, false, false) == bms::PauseQuitAction::FailToResult, "a scored play still fails");
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "AutoPlayTests passed\n";
    return 0;
}
