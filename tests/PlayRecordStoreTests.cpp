#include "PlayRecordStore.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    try {
        const std::filesystem::path root = argc > 1 ? std::filesystem::path(argv[1]) :
            std::filesystem::temp_directory_path() / L"SevenManiaPlayRecordStoreTests";
        std::filesystem::create_directories(root);
        const auto file = root / L"records.ini";
        std::filesystem::remove(file);

        // Keys are absolute, normalized and case-insensitive; sections are stable FNV-1a hashes.
        const auto chart = root / L"Song" / L"Chart.BMS";
        const auto key = normalizedRecordPath(chart);
        require(normalizedRecordPath(root / L"SONG" / L"." / L"chart.bms") == key, "key must ignore case and dot segments");
        require(normalizedRecordPath({}).empty(), "empty path must have no key");
        require(recordSectionForPath(L"") == L"Play_14695981039346656037", "FNV offset basis must stay stable");
        require(recordSectionForPath(L"a") == L"Play_620337896427418084", "FNV-1a hash of one UTF-16 unit must stay stable");
        require(recordSectionForPath(key) == recordSectionForPath(normalizedRecordPath(chart)), "section must be deterministic");

        // Without a file nothing is written or remembered.
        PlayRecordStore store;
        require(!store.save(chart, true, 10, 500000) && store.records.empty(), "save without file must be a no-op");
        store.file = file;
        require(!store.save({}, true, 10, 500000) && store.records.empty(), "save without key must be a no-op");
        require(store.findPlayed(key) == nullptr, "unknown key has no record");

        // First play takes every provenance; later plays only update the bests they beat.
        require(store.save(chart, false, 10, 800000), "first save");
        const PlayRecord* record = store.findPlayed(key);
        require(record && record->played && !record->cleared && record->maxCombo == 10 && record->bestAccuracyScore == 800000, "first record");
        require(record->scoreJudgement.spec == L"LEGACY" && record->comboJudgement.rank == -1 && record->clearJudgement.spec == L"LEGACY", "default provenance");
        require(store.save(chart, true, 20, 700000, L"SM-JUDGE-1", 3), "second save");
        require(record->bestAccuracyScore == 800000 && record->scoreJudgement.spec == L"LEGACY", "lower score keeps score provenance");
        require(record->maxCombo == 20 && record->comboJudgement.spec == L"SM-JUDGE-1" && record->comboJudgement.rank == 3, "higher combo takes provenance");
        require(record->cleared && record->clearJudgement.spec == L"SM-JUDGE-1", "first clear takes provenance");
        require(store.save(chart, false, 5, 2'000'000, L"SM-JUDGE-1", 1), "third save");
        require(record->bestAccuracyScore == 1'000'000 && record->scoreJudgement.rank == 1, "score is clamped and takes provenance");
        require(record->cleared && record->clearJudgement.rank == 3 && record->comboJudgement.rank == 3, "fail never clears the clear flag");

        // Round trip through the INI file.
        PlayRecordStore reloaded; reloaded.file = file; reloaded.load();
        const PlayRecord* loaded = reloaded.findPlayed(key);
        require(reloaded.records.size() == 1 && loaded, "reload must find the record");
        require(loaded->cleared && loaded->maxCombo == 20 && loaded->bestAccuracyScore == 1'000'000, "reload values");
        require(loaded->scoreJudgement.spec == L"SM-JUDGE-1" && loaded->scoreJudgement.rank == 1, "reload score provenance");
        require(loaded->comboJudgement.rank == 3 && loaded->clearJudgement.rank == 3, "reload combo/clear provenance");

        // Sections whose hash does not match their path, or that are not Play_ sections, are ignored.
        WritePrivateProfileStringW(L"Play_1", L"Path", key.c_str(), file.c_str());
        WritePrivateProfileStringW(L"Play_1", L"Played", L"1", file.c_str());
        WritePrivateProfileStringW(L"Practice_x", L"Path", key.c_str(), file.c_str());
        reloaded.load();
        require(reloaded.records.size() == 1, "mismatched or foreign sections must be ignored");

        // A stored but never-played record is not reported, and load() replaces previous contents.
        const auto other = root / L"Other.bms";
        const auto otherKey = normalizedRecordPath(other);
        reloaded.records[otherKey] = PlayRecord{};
        require(reloaded.findPlayed(otherKey) == nullptr, "unplayed record must be hidden");
        reloaded.load();
        require(!reloaded.records.contains(otherKey), "load must clear old records");
        PlayRecordStore empty; empty.records[otherKey] = PlayRecord{}; empty.load();
        require(empty.records.empty(), "load without file must clear records");

        std::filesystem::remove(file);
        std::cout << "PlayRecordStore tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
