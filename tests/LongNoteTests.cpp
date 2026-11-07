#include "LongNoteHold.h"
#include <array>
#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        for (int fps : {30, 60, 240, 1500}) {
            LongNoteHold ticking;
            ticking.start(1, 13.0, 3.0);
            unsigned combo = 0;
            for (int frame = 0; frame <= fps * 10; ++frame) {
                const double time = 3.0 + static_cast<double>(frame) / fps;
                ticking.update(time, true);
                combo += ticking.takeComboTicks(time);
                require(ticking.takeComboTicks(time) == 0, "same frame awarded hold combo twice");
            }
            require(combo == 100, "hold combo depends on rendering FPS");
            require(ticking.takeComboTicks(13.05) == 0, "release grace window generated extra combo");
            ticking.release(13.05);
            require(ticking.takeComboTicks(14.0) == 0, "released hold still generated combo");
        }
        LongNoteHold catchup;
        catchup.start(2, 4.0, 3.0);
        require(catchup.takeComboTicks(3.09) == 0 && catchup.takeComboTicks(3.55) == 5 &&
            catchup.takeComboTicks(3.99) == 4 && catchup.takeComboTicks(4.0) == 1,
            "delayed update lost elapsed hold ticks or awarded before the interval");
        catchup.start(3, 6.0, 5.0);
        require(catchup.takeComboTicks(5.0) == 0 && catchup.takeComboTicks(5.1) == 1,
            "restart retained the previous hold's tick clock");
        catchup.release(5.2);
        require(catchup.takeComboTicks(5.9) == 0, "failed hold still generated combo");
        LongNoteHold held;
        held.start(123, 12.0);
        for (int frame = 0; frame < 240 * 10; ++frame) {
            const double now = 2.0 + frame / 240.0;
            require(held.update(now, true) == HoldResult::None && held.active(), "held body disappeared before its release window");
            require(held.noteIndex() == 123, "held body lost its chart note");
            for (double speed : { 135.0, 270.0, 1350.0 }) {
                const int tail = held.visibleTailY(now, 92, 645, speed);
                require(tail >= 92 && tail < 645, "held body is clipped away or outside its lane");
            }
        }
        require(held.update(12.0, true) == HoldResult::None && held.active(), "held note ended before its release timing window");
        require(held.release(12.0) == HoldResult::PGreat && !held.active(), "tail release was not PGreat");
        require(held.release(20.0) == HoldResult::None, "late key release changed a completed hold");

        held.start(5, 10.0);
        require(held.release(9.95) == HoldResult::PGreat && !held.active(), "release inside the ±116.67ms window was not PGreat");
        require(held.update(9.96, true) == HoldResult::None && !held.active(), "repress resurrected a failed hold");
        held.start(6, 10.0);
        require(held.release(9.87) == HoldResult::Poor, "release well before the tail was not Poor");
        held.start(7, 10.0);
        require(held.release(10.11) == HoldResult::PGreat, "release inside the late edge of the tail window was not PGreat");
        held.start(8, 10.0);
        require(held.release(10.13) == HoldResult::Bad, "release after the tail window was not Bad");
        held.start(10, 10.0);
        require(held.release(10.095, 0.09) == HoldResult::Bad, "HARD preset did not tighten the long-note release window");
        held.start(11, 10.0);
        require(held.release(10.15, 0.18) == HoldResult::PGreat, "BASIC preset did not widen the long-note release window");
        held.start(9, 10.0);
        require(held.update(10.0 + LONG_NOTE_RELEASE_WINDOW_SECONDS, true) == HoldResult::Bad && !held.active(), "holding well past the tail did not resolve to Bad");

        std::array<LongNoteHold, 8> lanes{};
        lanes[0].start(2, 5.0); // scratch
        lanes[7].start(3, 8.0); // simultaneous key hold
        require(lanes[0].update(4.0, false) == HoldResult::Poor, "early scratch release was not Poor");
        require(lanes[7].update(4.0, true) == HoldResult::None && lanes[7].active(), "another lane release removed this hold");
        require(lanes[7].visibleTailY(4.0, 92, 645, 1350) == 92, "long offscreen tail was not clipped to the field");
        require(lanes[7].update(7.99, true) == HoldResult::None, "start-time lookback removed a long active hold");
        require(lanes[7].update(8.0, true) == HoldResult::None && lanes[7].active(), "simultaneous hold ended before its release window");
        require(lanes[7].release(8.0) == HoldResult::PGreat, "simultaneous hold release was not judged independently");
        lanes[1].start(9, 30.0);
        lanes.fill(LongNoteHold{});
        for (auto& lane : lanes) require(!lane.active() && lane.update(0.0, true) == HoldResult::None, "restart retained stale holds");
        std::cout << "LongNoteTests: persistent body, clipping at 0.5x/1x/5x, release PGREAT/POOR/BAD boundaries, no resurrection, scratch/chords and reset passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
