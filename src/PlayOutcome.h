#pragma once

namespace bms {

// Plays that never become records: practice, the no-input benchmark, gauge diagnostics and autoplay.
// Nothing of them is saved (best score, clear lamp, play count, EX score) and no record counts as updated.
struct PlayRecordExclusions {
    bool practice{}, noInputBenchmark{}, diagnostic{}, autoplay{};
};
constexpr bool playSavesRecord(const PlayRecordExclusions& play) noexcept {
    return !play.practice && !play.noInputBenchmark && !play.diagnostic && !play.autoplay;
}

// What the pause menu's QUIT does. A scored play ends as a failure (the same path as an empty gauge),
// while practice, the no-input benchmark and autoplay never fail and so keep going straight back to song select.
enum class PauseQuitAction { ReturnToSelect, FailToResult };

constexpr PauseQuitAction pauseQuitAction(bool playing, bool practice, bool noInputBenchmark, bool autoplay = false) noexcept {
    return playing && !practice && !noInputBenchmark && !autoplay ? PauseQuitAction::FailToResult : PauseQuitAction::ReturnToSelect;
}

struct PlayOutcome {
    bool cleared{};
    bool failed{};
};

// A play the player gave up on counts as failed whatever the gauge says, and never as cleared.
constexpr PlayOutcome playOutcome(bool gaugeClearConditionMet, bool gaugeDepleted, bool abandoned) noexcept {
    const bool failed = gaugeDepleted || abandoned;
    return {gaugeClearConditionMet && !failed, failed};
}

} // namespace bms
