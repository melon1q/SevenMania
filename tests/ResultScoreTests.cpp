#include "ResultScore.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <tuple>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        ResultScore score;
        require(score.value() == 0 && score.rank() == L"F", "score must start at zero");
        score.reset(100);
        score.apply(TimingJudgement::PGreat); require(score.value() == 10'000, "perfect award");
        score.apply(TimingJudgement::Great); require(score.value() == 18'000, "great award");
        score.apply(TimingJudgement::Good); require(score.value() == 23'000, "good award");
        score.apply(TimingJudgement::Bad); score.apply(TimingJudgement::Poor);
        require(score.value() == 23'000, "bad and poor must neither earn nor deduct score");
        for (int count : {1, 3, 7, 997, 4097}) {
            score.reset(count);
            for (int i = 0; i < count; ++i) score.apply(TimingJudgement::PGreat);
            require(score.value() == 1'000'000 && score.rank() == L"S", "full perfect must reach exact maximum despite fractional shares");
            score.apply(TimingJudgement::PGreat); require(score.value() == 1'000'000, "extra events exceeded maximum");
        }
        score.reset(2);
        score.apply(TimingJudgement::PGreat); require(score.value() == 500'000, "ordinary note weight");
        score.apply(TimingJudgement::PGreat, 1); require(score.value() == 750'000, "long-note head half weight");
        score.apply(TimingJudgement::PGreat, 1); require(score.value() == 1'000'000, "long-note tail half weight");
        score.reset(2);
        score.apply(TimingJudgement::PGreat, 1); score.apply(TimingJudgement::Poor, 1); score.apply(TimingJudgement::PGreat);
        require(score.value() == 750'000, "failed long-note tail must lose only its share");
        score.reset(2); score.apply(TimingJudgement::Poor); score.apply(TimingJudgement::PGreat);
        require(score.value() == 500'000, "missing entire long note must leave other notes equally weighted");
        score.reset(3); score.apply(TimingJudgement::PGreat);
        require(score.value() == 333'333, "partial score should retain cumulative fractions");
        score.apply(TimingJudgement::PGreat); score.apply(TimingJudgement::PGreat);
        require(score.value() == 1'000'000, "rounding lost final perfect points");
        for (const auto& [perfects, greats, expectedRank] : {
            std::tuple{95, 0, L"S"}, {90, 0, L"AAA"}, {0, 100, L"AA"},
            {70, 0, L"A"}, {60, 0, L"B"}, {50, 0, L"C"}, {49, 0, L"F"}}) {
            score.reset(100);
            for (int i = 0; i < perfects; ++i) score.apply(TimingJudgement::PGreat);
            for (int i = 0; i < greats; ++i) score.apply(TimingJudgement::Great);
            require(score.rank() == expectedRank, "rank thresholds changed");
        }
        score.reset(0); score.apply(TimingJudgement::PGreat); require(score.value() == 0, "empty chart should not divide by zero");
        score.reset(3); score.apply(TimingJudgement::PGreat, -1); require(score.value() == 0, "negative units changed score");
        score.reset(std::numeric_limits<int>::max());
        score.apply(TimingJudgement::PGreat, std::numeric_limits<int>::max());
        require(score.value() == 500'000, "large chart arithmetic overflowed");
        score.apply(TimingJudgement::PGreat, std::numeric_limits<int>::max());
        require(score.value() == 1'000'000, "large chart lost precision");
        std::cout << "Additive score: awards, equal note weights, fractional carry, full perfect, limits and ranks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << "ResultScoreTests failed: " << error.what() << '\n'; return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
