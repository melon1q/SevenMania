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
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "PlayOutcomeTests passed\n";
    return 0;
}
