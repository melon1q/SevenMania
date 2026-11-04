#include "Judgement.h"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        const auto normal = judgementWindows(JudgementProfile::Normal);
        for (double sign : {-1.0, 1.0}) {
            require(judgeTimingOffset(sign * normal.pGreatMs / 1000.0) == TimingJudgement::PGreat, "PGREAT boundary");
            require(judgeTimingOffset(sign * (normal.pGreatMs + 0.001) / 1000.0) == TimingJudgement::Great, "GREAT lower boundary");
            require(judgeTimingOffset(sign * normal.greatMs / 1000.0) == TimingJudgement::Great, "GREAT boundary");
            require(judgeTimingOffset(sign * (normal.greatMs + 0.001) / 1000.0) == TimingJudgement::Good, "GOOD lower boundary");
            require(judgeTimingOffset(sign * normal.goodMs / 1000.0) == TimingJudgement::Good, "GOOD boundary");
            require(judgeTimingOffset(sign * (normal.goodMs + 0.001) / 1000.0) == TimingJudgement::Bad, "BAD lower boundary");
            require(judgeTimingOffset(sign * normal.badMs / 1000.0) == TimingJudgement::Bad, "BAD boundary");
            require(judgeTimingOffset(sign * (normal.badMs + 0.001) / 1000.0) == TimingJudgement::Poor, "POOR beyond BAD window");
        }
        require(JUDGEMENT_WINDOW_SECONDS == 0.25, "late miss window is not 250ms");
        const std::array<std::pair<JudgementProfile, JudgementWindows>, 4> profiles{{
            {JudgementProfile::Basic, {25.0, 55.0, 180.0, 300.0}},
            {JudgementProfile::Easy, {21.0, 45.0, 150.0, 275.0}},
            {JudgementProfile::Normal, {16.67, 33.33, 116.67, 250.0}},
            {JudgementProfile::Hard, {12.50, 25.0, 90.0, 200.0}}
        }};
        for (const auto& [profile, expected] : profiles) {
            const auto actual = judgementWindows(profile);
            require(actual.pGreatMs == expected.pGreatMs && actual.greatMs == expected.greatMs &&
                actual.goodMs == expected.goodMs && actual.badMs == expected.badMs, "profile windows differ from the reference table");
            require(judgeTimingOffset(expected.pGreatMs / 1000.0, profile) == TimingJudgement::PGreat, "profile PGREAT edge");
            require(judgeTimingOffset(expected.greatMs / 1000.0, profile) == TimingJudgement::Great, "profile GREAT edge");
            require(judgeTimingOffset(expected.goodMs / 1000.0, profile) == TimingJudgement::Good, "profile GOOD edge");
            require(judgeTimingOffset(expected.badMs / 1000.0, profile) == TimingJudgement::Bad, "profile BAD edge");
            require(judgeTimingOffset((expected.badMs + 0.01) / 1000.0, profile) == TimingJudgement::Poor, "profile POOR threshold");
        }
        std::cout << "JudgementTests: symmetric PGREAT/GREAT/GOOD/BAD/POOR boundaries passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "JudgementTests failed: " << error.what() << '\n';
        return 1;
    }
}
