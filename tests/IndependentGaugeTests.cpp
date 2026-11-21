#include "../src/IndependentGauge.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace sevenmania::independent_gauge;

static bool near(float a, float b) { return std::fabs(a - b) < 0.001f; }

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #condition); \
        std::abort(); \
    } \
} while (false)

int main() {
    Bank bank;
    CHECK(!bank.configure(Profile::SevenKeys, 0, 100));
    CHECK(!bank.configure(Profile::SevenKeys, std::numeric_limits<double>::infinity(), 100));
    CHECK(!bank.configure(Profile::SevenKeys, 200, 0));
    CHECK(near(bank.value(Kind::Normal), 0));
    CHECK(bank.configure(Profile::SevenKeys, 200, 100));
    CHECK(near(bank.value(Kind::Normal), 20));
    CHECK(near(bank.value(Kind::Hard), 100));
    CHECK(!bank.qualified(Kind::Normal));
    CHECK(bank.qualified(Kind::Hard));
    bank.apply(Event::Great);
    CHECK(near(bank.value(Kind::AssistEasy), 22));
    CHECK(near(bank.value(Kind::Easy), 22));
    CHECK(near(bank.value(Kind::Normal), 22));
    bank.setValue(Kind::Hard, 50);
    bank.setValue(Kind::ExHard, 50);
    bank.apply(Event::PGreat);
    CHECK(near(bank.value(Kind::Hard), 50.15f));
    CHECK(near(bank.value(Kind::ExHard), 50.15f));

    // Strictly less than each threshold; the unmodified value chooses the multiplier.
    for (int n = 1; n <= 5; ++n) {
        CHECK(bank.configure(Profile::SevenKeys, 200, 100));
        bank.setValue(Kind::Hard, n * 10.0f - 0.01f);
        bank.setValue(Kind::Class, n * 5.0f - 0.01f);
        bank.apply(Event::Bad);
        CHECK(near(bank.value(Kind::Hard), n * 10.0f - 0.01f - 5.0f * (0.3f + n * 0.1f)));
        CHECK(near(bank.value(Kind::Class), n * 5.0f - 0.01f - 1.5f * (0.3f + n * 0.1f)));
    }
    CHECK(bank.configure(Profile::SevenKeys, 200, 100));
    bank.setValue(Kind::Hard, 10);
    bank.apply(Event::Bad, .5f);
    CHECK(near(bank.value(Kind::Hard), 8.75f));
    bank.setValue(Kind::Hard, 0);
    bank.setValue(Kind::Hard, 50);
    bank.apply(Event::PGreat);
    CHECK(near(bank.value(Kind::Hard), 0));
    CHECK(!bank.qualified(Kind::Hard));

    CHECK(bank.configure(Profile::BeatorajaLR2, 2000, 1000));
    bank.apply(Event::Good);
    CHECK(near(bank.value(Kind::AssistEasy), 21.2f));
    CHECK(near(bank.value(Kind::Easy), 21.2f));
    CHECK(near(bank.value(Kind::Normal), 21));
    bank.setValue(Kind::Hard, 29);
    bank.setValue(Kind::Class, 29);
    bank.setValue(Kind::ExClass, 29);
    bank.setValue(Kind::ExHard, 29);
    bank.apply(Event::Bad);
    CHECK(near(bank.value(Kind::Hard), 25.4f));
    CHECK(near(bank.value(Kind::Class), 27.8f));
    CHECK(near(bank.value(Kind::ExClass), 25.4f));
    CHECK(near(bank.value(Kind::ExHard), 17));

    // Test both sides of every TOTAL boundary with no note-count correction.
    const double boundaries[] = {240, 230, 210, 200, 180, 160, 150, 130, 120};
    const double factors[] = {1, 1.11, 1.25, 1.5, 1.666, 2, 2.5, 3.333, 5, 10};
    for (int i = 0; i < 9; ++i) {
        CHECK(bank.configure(Profile::BeatorajaLR2, boundaries[i], 1000));
        bank.apply(Event::Bad);
        CHECK(near(bank.value(Kind::Hard), static_cast<float>(100 - 6 * factors[i])));
        CHECK(bank.configure(Profile::BeatorajaLR2, boundaries[i] - 0.001, 1000));
        bank.apply(Event::Bad);
        CHECK(near(bank.value(Kind::Hard), static_cast<float>(100 - 6 * factors[i + 1])));
    }
    const std::size_t noteCounts[] = {1001, 1000, 750, 500, 250, 100, 10, 2, 1};
    const double noteFactors[] = {1, 1, 1, 1, 1, 1, 3.312, 9.584, 10.096};
    for (int i = 0; i < 9; ++i) {
        CHECK(bank.configure(Profile::BeatorajaLR2, 240, noteCounts[i]));
        bank.apply(Event::Bad);
        CHECK(near(bank.value(Kind::Hard), static_cast<float>(100 - 6 * noteFactors[i])));
        CHECK(near(bank.value(Kind::ExHard), static_cast<float>(std::max(0.0, 100 - 12 * noteFactors[i]))));
    }
    // The two corrections are combined by maximum and then HARD guts is applied.
    CHECK(bank.configure(Profile::BeatorajaLR2, 200, 500));
    bank.setValue(Kind::Hard, 20);
    bank.apply(Event::Bad, .5f);
    CHECK(near(bank.value(Kind::Hard), 17.3f));

    CHECK(bank.configure(Profile::SevenKeys, 200, 100));
    bank.setValue(Kind::Hard, 20);
    bank.mineDamage(5);
    CHECK(near(bank.value(Kind::Hard), 15));
    bank.mineDamage(1000);
    CHECK(near(bank.value(Kind::Normal), 2));
    CHECK(near(bank.value(Kind::Hard), 0));
    bank.hcnTick(true);
    CHECK(near(bank.value(Kind::Hard), 0));
    CHECK(near(bank.value(Kind::Normal), 3));
    CHECK(bank.configure(Profile::SevenKeys, 200, 100));
    bank.setValue(Kind::Hard, 20);
    bank.hcnTick(false);
    CHECK(near(bank.value(Kind::Hard), 18.5f));
    CHECK(near(bank.value(Kind::Normal), 18.5f));

    CHECK(bank.configure(Profile::SevenKeys, 160, 100));
    bank.setValue(Kind::AssistEasy, 0);
    bank.setValue(Kind::Easy, 0);
    bank.setValue(Kind::Normal, 0);
    CHECK(near(bank.value(Kind::AssistEasy), 2));
    CHECK(near(bank.value(Kind::Easy), 2));
    CHECK(near(bank.value(Kind::Normal), 2));
    bank.setValue(Kind::Hard, 50);
    bank.apply(Event::PGreat);
    CHECK(near(bank.value(Kind::Hard), 50));
    bank.setValue(Kind::AssistEasy, 60);
    bank.setValue(Kind::Easy, 79.9f);
    CHECK(bank.qualified(Kind::AssistEasy));
    CHECK(!bank.qualified(Kind::Easy));
    CHECK(!bank.configure(Profile::SevenKeys, -1, 100));
    CHECK(near(bank.value(Kind::AssistEasy), 0));
    CHECK(!bank.qualified(Kind::AssistEasy));

    // LR2oraja shares LR2's judgment values when both damage factors are one.
    CHECK(bank.configure(Profile::LR2oraja, 240, 1000));
    bank.apply(Event::Bad);
    const float badValues[] = {16.8f, 16.8f, 16, 94, 88, 0, 98, 94, 88};
    for (unsigned i = 0; i < 9; ++i)
        CHECK(near(bank.value(static_cast<Kind>(i)), badValues[i]));

    // Each TOTAL step is tested immediately below, at, and above its boundary.
    const double totalSteps[] = {96, 112, 128, 144, 160, 176, 192, 208, 224, 240};
    const double beforeFactors[] = {10, 10, 5, 10.0 / 3, 2.5, 2, 10.0 / 6, 10.0 / 7, 1.25, 10.0 / 9};
    const double atFactors[] = {10, 5, 10.0 / 3, 2.5, 2, 10.0 / 6, 10.0 / 7, 1.25, 10.0 / 9, 1};
    for (int i = 0; i < 10; ++i) {
        for (int side = -1; side <= 1; ++side) {
            CHECK(bank.configure(Profile::LR2oraja, totalSteps[i] + side * 0.001, 1000));
            bank.apply(Event::Bad, .1f);
            const double factor = side < 0 ? beforeFactors[i] : atFactors[i];
            CHECK(near(bank.value(Kind::Hard), static_cast<float>(100 - .6 * factor)));
            CHECK(near(bank.value(Kind::ExHard), static_cast<float>(100 - 1.2 * factor)));
        }
    }

    const std::size_t lr2orajaNotes[] = {19, 20, 21, 29, 30, 31, 59, 60, 61, 124, 125, 126,
                                        249, 250, 251, 499, 500, 501, 999, 1000, 1001};
    const double lr2orajaFactors[] = {10, 10, 9.8, 8.2, 7, 6.933333333, 5.066666667, 5,
                                      4.984615385, 4.015384615, 4, 3.992, 3.008, 3, 2.996,
                                      2.004, 2, 1.998, 1.002, 1, 1};
    for (std::size_t i = 0; i < sizeof(lr2orajaNotes) / sizeof(lr2orajaNotes[0]); ++i) {
        CHECK(bank.configure(Profile::LR2oraja, 240, lr2orajaNotes[i]));
        bank.apply(Event::Bad, .1f);
        CHECK(near(bank.value(Kind::Hard), static_cast<float>(100 - .6 * lr2orajaFactors[i])));
        CHECK(near(bank.value(Kind::ExHard), static_cast<float>(100 - 1.2 * lr2orajaFactors[i])));
    }
    // Select the larger of the factors before applying rate and GUTS.
    CHECK(bank.configure(Profile::LR2oraja, 112, 500));
    bank.setValue(Kind::Hard, 31);
    bank.apply(Event::Bad, .5f);
    CHECK(near(bank.value(Kind::Hard), 22));

    const Kind gutsKinds[] = {Kind::Hard, Kind::Class, Kind::ExClass};
    for (Kind kind : gutsKinds) {
        for (int side = -1; side <= 1; ++side) {
            CHECK(bank.configure(Profile::LR2oraja, 240, 1000));
            const float start = 32.0f + side * .001f;
            bank.setValue(kind, start);
            bank.apply(Event::EmptyPoor);
            CHECK(near(bank.value(kind), start - (side < 0 ? 1.2f : 2.0f)));
        }
    }
    for (unsigned i = 0; i < 9; ++i) {
        const auto kind = static_cast<Kind>(i);
        CHECK(bank.configure(Profile::LR2oraja, 240, 1000));
        bank.setValue(kind, 2.001f);
        CHECK(near(bank.value(kind), 2.001f));
        bank.setValue(kind, 2.0f);
        CHECK(near(bank.value(kind), 2.0f));
        bank.setValue(kind, 1.999f);
        CHECK(near(bank.value(kind), i < 3 ? 2.0f : 0.0f));
    }
    CHECK(bank.configure(Profile::LR2oraja, 240, 1000));
    bank.setValue(Kind::Hard, 3);
    bank.mineDamage(1);
    CHECK(near(bank.value(Kind::Hard), 2));
    bank.mineDamage(.001f);
    CHECK(near(bank.value(Kind::Hard), 0));
    CHECK(bank.configure(Profile::LR2oraja, 240, 1000));
    bank.setValue(Kind::ExClass, 2.5f);
    bank.hcnTick(false);
    CHECK(near(bank.value(Kind::ExClass), 0));
}
