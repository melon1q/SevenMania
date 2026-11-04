#include "PracticeMode.h"
#include "ResultScore.h"
#include <iostream>
#include <stdexcept>

void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
int main() {
    try {
        PracticeMode practice;
        practice.configure(60);
        practice.restoreRange(10, 20);
        require(practice.rate() == 1 && practice.startSeconds == 10 && practice.endSeconds == 20,
            "saved practice range did not restore at normal speed");
        require(practice.previewStart(false) == 10 && practice.previewStart(true) == 17,
            "boundary preview did not use start or final three seconds");
        practice.restoreRange(19, 20);
        require(practice.previewStart(true) == 19, "end preview escaped a short selected range");
        practice.configure(5); practice.restoreRange(10, 20);
        require(practice.valid() && practice.endSeconds == 6 && practice.startSeconds == 5,
            "saved range was not clamped to a changed chart duration");
        practice.configure(60);
        for (int i = 0; i < 50; ++i) practice.adjustRate(-1);
        require(practice.rate() == 0.5, "practice rate fell below 0.5x");
        for (int i = 0; i < 50; ++i) practice.adjustRate(1);
        require(practice.rate() == 1.5, "practice rate exceeded 1.5x");
        practice.adjustBoundary(false, 10);
        practice.adjustBoundary(true, -41);
        require(practice.valid() && practice.startSeconds == 10 && practice.endSeconds == 20, "practice boundaries are invalid");
        const std::vector<bms::Note> full{
            {1, 0, 0, L"01"}, {2, 8, 7, L"02"}, {3, 12, 0, L"03"}, {4, 18, 7, L"04"}, {5, 20, 0, L"05"}
        };
        const auto notes = practice.notesFor(full);
        require(notes.size() == 3 && notes[0].timeSeconds == 10 && notes[0].lengthSeconds == 5 && notes[0].sampleId.empty(),
            "section start lost a crossing long note or replayed an old keysound");
        require(notes[1].timeSeconds == 12 && notes[2].lengthSeconds == 2 && full[1].timeSeconds == 8,
            "section filtering changed the source chart or failed to crop its ending");
        ResultScore score; score.reset(static_cast<int>(notes.size()));
        for (const auto& note : notes) {
            score.apply(TimingJudgement::PGreat, note.longNote() ? 1 : 2);
            if (note.longNote()) score.apply(TimingJudgement::PGreat, 1);
        }
        require(score.value() == 1'000'000, "practice score includes notes outside the selected section");
        practice.adjustBoundary(false, 1000);
        require(practice.startSeconds == practice.endSeconds - 1 && practice.valid(), "start crossed end");
        practice.adjustBoundary(true, -1000);
        require(practice.valid(), "end crossed start");
        practice.configure(0);
        require(practice.valid() && practice.endSeconds == 1, "short chart produced an unusable section");
        std::cout << "Practice rate bounds, section boundaries, long-note clipping, source preservation and scoring passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
