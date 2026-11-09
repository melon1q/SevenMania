#include "KeyModeFilter.h"

#include <iostream>

using namespace bms;

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) { std::cerr << "FAIL: " << what << "\n"; ++failures; }
}

int main() {
    // Tab order visits every filter once, then wraps.
    KeyModeFilter f = KeyModeFilter::Key7;
    const KeyModeFilter order[]{ KeyModeFilter::Key5, KeyModeFilter::Key14, KeyModeFilter::Key10, KeyModeFilter::Pms9, KeyModeFilter::All, KeyModeFilter::Key7 };
    for (const auto expected : order) { f = nextKeyModeFilter(f); check(f == expected, "cycle order"); }

    check(keyModeFilterMatches(KeyModeFilter::Key7, ChartMode::SevenKey), "7K matches 7K");
    check(keyModeFilterMatches(KeyModeFilter::Key7, ChartMode::Unknown), "unknown counts as 7K");
    check(!keyModeFilterMatches(KeyModeFilter::Key7, ChartMode::FourteenKey), "7K hides DP");
    check(keyModeFilterMatches(KeyModeFilter::Key14, ChartMode::FourteenKey), "14K matches 14K");
    check(!keyModeFilterMatches(KeyModeFilter::Key14, ChartMode::TenKey), "14K hides 10K");
    check(keyModeFilterMatches(KeyModeFilter::Key10, ChartMode::TenKey), "10K matches 10K");
    check(keyModeFilterMatches(KeyModeFilter::Key5, ChartMode::FiveKey), "5K matches 5K");
    check(keyModeFilterMatches(KeyModeFilter::Pms9, ChartMode::PopnNine), "PMS matches PMS");
    check(!keyModeFilterMatches(KeyModeFilter::Pms9, ChartMode::SevenKey), "PMS hides 7K");
    for (const auto m : { ChartMode::FiveKey, ChartMode::SevenKey, ChartMode::TenKey, ChartMode::FourteenKey, ChartMode::PopnNine, ChartMode::Other })
        check(keyModeFilterMatches(KeyModeFilter::All, m), "ALL matches everything");

    for (int i = 0; i <= static_cast<int>(KeyModeFilter::All); ++i)
        check(static_cast<int>(keyModeFilterFromInt(i)) == i, "int round trip");
    check(keyModeFilterFromInt(99) == KeyModeFilter::Key7 && keyModeFilterFromInt(-1) == KeyModeFilter::Key7, "bad ini value falls back to 7K");
    check(std::wstring(keyModeFilterLabel(KeyModeFilter::All)) == L"ALL", "label");

    if (failures == 0) std::cout << "KeyModeFilterTests passed\n";
    return failures == 0 ? 0 : 1;
}
