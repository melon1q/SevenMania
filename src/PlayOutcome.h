#pragma once
#include <algorithm>
#include <cmath>

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

// LR2 / beatoraja: an empty gauge or QUIT ends the play on the spot. The song stops, the fail shutters
// close with playstop, and the result screen then plays fail. A song that simply runs out (cleared or
// not) plays no playstop; its result plays clear or fail.
constexpr bool playStopsOnFailure(bool gaugeDepleted, bool abandoned) noexcept { return gaugeDepleted || abandoned; }

// Only one system sound plays at a time and a new one cuts the old, so the result sound waits for what
// is left of playstop. Milliseconds to wait (0: play it now), never more than capMs.
inline int resultSoundDelayMs(double playStopSeconds, double elapsedSeconds, int capMs = 5000) noexcept {
    if (!std::isfinite(playStopSeconds) || !std::isfinite(elapsedSeconds) || playStopSeconds <= 0) return 0;
    const double remaining = playStopSeconds - std::max(0.0, elapsedSeconds);
    if (remaining <= 0) return 0;
    return static_cast<int>(std::min<double>(capMs, std::ceil(remaining * 1000)));
}

} // namespace bms
