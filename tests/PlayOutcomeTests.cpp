#include "PlayOutcome.h"

#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        using bms::PauseQuitAction;
        require(bms::pauseQuitAction(true, false, false) == PauseQuitAction::FailToResult, "scored play quits as failure");
        require(bms::pauseQuitAction(true, true, false) == PauseQuitAction::ReturnToSelect, "practice quit returns to select");
        require(bms::pauseQuitAction(true, false, true) == PauseQuitAction::ReturnToSelect, "benchmark quit returns to select");
        require(bms::pauseQuitAction(false, false, false) == PauseQuitAction::ReturnToSelect, "no play returns to select");

        const auto normal = bms::playOutcome(true, false, false);
        require(normal.cleared && !normal.failed, "normal clear");
        const auto noClear = bms::playOutcome(false, false, false);
        require(!noClear.cleared && !noClear.failed, "no clear");
        const auto depleted = bms::playOutcome(false, true, false);
        require(!depleted.cleared && depleted.failed, "empty gauge fails");
        const auto quitWhileClearing = bms::playOutcome(true, false, true);
        require(!quitWhileClearing.cleared && quitWhileClearing.failed, "quit never clears");
        const auto quitLow = bms::playOutcome(false, false, true);
        require(!quitLow.cleared && quitLow.failed, "quit fails");

        // playstop belongs to the moment a play fails (empty gauge or QUIT), not to a song that runs out.
        require(bms::playStopsOnFailure(true, false) && bms::playStopsOnFailure(false, true) && bms::playStopsOnFailure(true, true),
            "empty gauge and quit play playstop");
        require(!bms::playStopsOnFailure(false, false), "a finished song (cleared or not) plays no playstop");
        // The result sound waits for the rest of playstop, never past the cap, and not at all when nothing is left.
        require(bms::resultSoundDelayMs(1.2, 0) == 1200 && bms::resultSoundDelayMs(1.2, 0.5) == 700, "wait for the rest of playstop");
        require(bms::resultSoundDelayMs(1.2, 1.5) == 0 && bms::resultSoundDelayMs(0, 0) == 0, "no wait once playstop is over or silent");
        require(bms::resultSoundDelayMs(9, 0) == 5000 && bms::resultSoundDelayMs(9, 0, 3000) == 3000, "a long playstop is capped");
        require(bms::resultSoundDelayMs(1, -2) == 1000 && bms::resultSoundDelayMs(std::nan(""), 0) == 0, "odd clocks never wait badly");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "PlayOutcomeTests passed\n";
    return 0;
}
