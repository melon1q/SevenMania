#include "../src/IndependentGaugeShift.h"

#include <cstdio>
#include <cstdlib>

using namespace sevenmania::independent_gauge;

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #condition); \
        std::abort(); \
    } \
} while (false)

int main() {
    Bank bank;
    Selector selector;
    CHECK(selector.failed());
    CHECK(bank.configure(Profile::SevenKeys, 200, 100));
    CHECK(!selector.configure(static_cast<Kind>(9), Mode::None, Kind::Normal));
    CHECK(selector.failed());
    CHECK(!selector.configure(Kind::Hard, static_cast<Mode>(5), Kind::Normal));
    CHECK(!selector.configure(Kind::Hard, Mode::None, Kind::Class));
    selector.update(bank);
    CHECK(selector.failed());
    CHECK(!selector.changed());

    CHECK(selector.configure(Kind::Hard, Mode::None, Kind::Normal));
    bank.setValue(Kind::Hard, 0);
    selector.update(bank);
    CHECK(selector.current() == Kind::Hard && selector.failed() && !selector.changed());
    selector.update(bank);
    CHECK(selector.failed() && !selector.changed());
    CHECK(bank.configure(Profile::SevenKeys, 200, 100));
    selector.update(bank);
    CHECK(selector.failed());
    bank.setValue(Kind::Hard, 0);
    CHECK(selector.configure(Kind::Hard, Mode::Continue, Kind::Normal));
    selector.update(bank);
    CHECK(selector.current() == Kind::Hard && !selector.failed());

    CHECK(bank.configure(Profile::SevenKeys, 200, 100));
    CHECK(selector.configure(Kind::ExHard, Mode::SurvivalToGroove, Kind::Normal));
    bank.setValue(Kind::ExHard, 0);
    selector.update(bank);
    CHECK(selector.current() == Kind::Normal && selector.changed() && !selector.failed());
    selector.update(bank);
    CHECK(selector.changed());
    CHECK(selector.configure(Kind::ExClass, Mode::SurvivalToGroove, Kind::AssistEasy));
    bank.setValue(Kind::ExClass, 0);
    selector.update(bank);
    CHECK(selector.current() == Kind::ExClass && !selector.changed() && !selector.failed());

    // All gauges accumulated the same events before any candidate is selected.
    CHECK(bank.configure(Profile::SevenKeys, 200, 100));
    bank.setValue(Kind::Hard, 20);
    bank.setValue(Kind::ExHard, 15);
    bank.setValue(Kind::Hazard, 1);
    bank.apply(Event::Bad);
    CHECK(bank.value(Kind::Hazard) == 0);
    CHECK(selector.configure(Kind::Hard, Mode::BestClear, Kind::Normal));
    selector.update(bank);
    CHECK(selector.current() == Kind::ExHard && selector.changed() && !selector.failed());
    bank.setValue(Kind::ExHard, 0);
    selector.update(bank);
    CHECK(selector.current() == Kind::Hard && !selector.changed());
    selector.update(bank);
    CHECK(!selector.changed());

    CHECK(selector.configure(Kind::Hard, Mode::SelectToUnder, Kind::AssistEasy));
    selector.update(bank);
    CHECK(selector.current() == Kind::Hard && !selector.changed());
    bank.setValue(Kind::Hard, 0);
    selector.update(bank);
    CHECK(selector.current() == Kind::AssistEasy && selector.changed() && !selector.failed());

    // A bottom above the current gauge does not move the start above current.
    CHECK(bank.configure(Profile::SevenKeys, 200, 100));
    CHECK(selector.configure(Kind::Normal, Mode::SelectToUnder, Kind::Hazard));
    selector.update(bank);
    CHECK(selector.current() == Kind::Normal && !selector.changed());
    // Bottom sets the lower search bound when it is below the current selection.
    CHECK(selector.configure(Kind::Hard, Mode::SelectToUnder, Kind::Easy));
    bank.setValue(Kind::Hard, 0);
    selector.update(bank);
    CHECK(selector.current() == Kind::Easy && selector.changed());

    CHECK(bank.configure(Profile::SevenKeys, 200, 100));
    CHECK(selector.configure(Kind::Class, Mode::BestClear, Kind::AssistEasy));
    selector.update(bank);
    CHECK(selector.current() == Kind::ExHardClass && selector.changed());
    bank.setValue(Kind::ExHardClass, 0);
    selector.update(bank);
    CHECK(selector.current() == Kind::ExClass && selector.changed());
    CHECK(selector.configure(Kind::ExClass, Mode::SelectToUnder, Kind::Hazard));
    selector.update(bank);
    CHECK(selector.current() == Kind::ExClass && !selector.changed());
    bank.setValue(Kind::ExClass, 0);
    bank.setValue(Kind::Class, 0);
    selector.update(bank);
    CHECK(selector.current() == Kind::Class && selector.changed() && !selector.failed());
    selector.update(bank);
    CHECK(selector.changed() && !selector.failed());

    // All survival gauges at zero do not create a failure in either search mode.
    CHECK(bank.configure(Profile::SevenKeys, 200, 100));
    bank.setValue(Kind::Hard, 0);
    bank.setValue(Kind::ExHard, 0);
    bank.setValue(Kind::Hazard, 0);
    CHECK(selector.configure(Kind::Hard, Mode::BestClear, Kind::Hard));
    selector.update(bank);
    CHECK(selector.current() == Kind::Hard && !selector.changed() && !selector.failed());
    selector.update(bank);
    CHECK(!selector.changed() && !selector.failed());
    CHECK(selector.configure(Kind::Hard, Mode::SelectToUnder, Kind::Hard));
    selector.update(bank);
    CHECK(selector.current() == Kind::Hard && !selector.changed() && !selector.failed());

    // A fresh configuration resets transition and failure state.
    CHECK(selector.configure(Kind::Easy, Mode::None, Kind::AssistEasy));
    CHECK(selector.current() == Kind::Easy && !selector.changed() && !selector.failed());
    selector.update(bank);
    CHECK(!selector.failed());
}
