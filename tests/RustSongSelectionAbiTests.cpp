#include "BmsSongSelectionAbi.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#ifdef BMS_RUST_SONG_SELECTION_BUILD_CHECK
namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "Rust song-selection ABI test failed: %s\n", message);
        std::exit(1);
    }
}

BmsSongSelectionUtf8Slice utf8(std::string_view value) {
    return {
        reinterpret_cast<const std::uint8_t*>(value.data()),
        static_cast<std::uint32_t>(value.size())
    };
}

BmsSongChartSummaryInput chart(std::uint64_t id, std::string_view path,
    std::string_view title, std::string_view subtitle, std::string_view artist,
    std::string_view level, std::string_view directory, std::uint32_t mode,
    std::uint32_t playable) {
    return { id, utf8(path), utf8(title), utf8(subtitle), utf8(artist), utf8(level), utf8(directory),
        mode, playable, 2, 2, 0, 0.0 };
}

void requireText(BmsSongSelectionSnapshot* snapshot, std::uint32_t chartIndex,
    BmsSongSelectionTextField field, std::string_view expected, const char* message) {
    std::vector<std::uint8_t> output(expected.size() + 1, 0xCC);
    std::uint32_t required = 0;
    require(bms_song_selection_snapshot_copy_text(snapshot, chartIndex, field, output.data(),
        static_cast<std::uint32_t>(expected.size()), &required) == BMS_SONG_SELECTION_OK, message);
    require(required == expected.size(), "required text size should count UTF-8 bytes only");
    require(std::string_view(reinterpret_cast<const char*>(output.data()), required) == expected,
        "copied UTF-8 bytes should match the source text");
    require(output[required] == 0xCC, "text API should not append a NUL byte");
}

void testSnapshotRoundTrip() {
    std::string title = "\xEA\xB3\xA1" " A"; // Korean: 곡 A
    const auto expectedTitle = title;
    const std::string artist = "\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88"; // Japanese: テスト
    const auto first = chart(101, "C:/Songs/Alpha/a.bms", title, "", artist, "12",
        "C:/Songs/Alpha", BMS_SONG_MODE_SEVEN_KEY, 1);
    const auto second = chart(102, "C:/Songs/Alpha/b.bms", "Song A", "Another",
        "Artist", "7", "C:/Songs/Alpha", BMS_SONG_MODE_FIVE_KEY, 0);
    const std::array entries{ first, second };

    BmsSongSelectionSnapshot* snapshot = nullptr;
    require(bms_song_selection_snapshot_create(42, entries.data(),
        static_cast<std::uint32_t>(entries.size()), &snapshot) == BMS_SONG_SELECTION_OK,
        "valid snapshot should be created");
    require(snapshot != nullptr, "successful creation should return a handle");
    title[0] = 'X'; // The Rust snapshot must own a copy, not this caller buffer.

    std::uint64_t generation = 0;
    std::uint32_t count = 0;
    require(bms_song_selection_snapshot_get_metadata(snapshot, &generation, &count) == BMS_SONG_SELECTION_OK,
        "snapshot metadata should be readable");
    require(generation == 42 && count == 2, "generation and count should be preserved");

    BmsSongChartSummaryInfo info{};
    require(bms_song_selection_snapshot_get_chart(snapshot, 1, &info) == BMS_SONG_SELECTION_OK,
        "chart info should be readable");
    require(info.chart_id == 102 && info.mode == BMS_SONG_MODE_FIVE_KEY && info.playable == 0,
        "fixed-width chart fields should be preserved");

    requireText(snapshot, 0, BMS_SONG_TEXT_PATH, "C:/Songs/Alpha/a.bms", "path text should be available");
    requireText(snapshot, 0, BMS_SONG_TEXT_TITLE, expectedTitle, "UTF-8 title should copy to caller memory");
    requireText(snapshot, 0, BMS_SONG_TEXT_ARTIST, artist, "UTF-8 artist should copy to caller memory");
    requireText(snapshot, 0, BMS_SONG_TEXT_PLAY_LEVEL, "12", "play level should be available");
    requireText(snapshot, 0, BMS_SONG_TEXT_DIRECTORY, "C:/Songs/Alpha", "directory should be available");

    std::array<std::uint8_t, 2> shortBuffer{ 0xA5, 0x5A };
    std::uint32_t required = 0;
    require(bms_song_selection_snapshot_copy_text(snapshot, 0, BMS_SONG_TEXT_TITLE,
        shortBuffer.data(), static_cast<std::uint32_t>(shortBuffer.size()), &required) ==
        BMS_SONG_SELECTION_BUFFER_TOO_SMALL, "short output buffer should be reported");
    require(required == expectedTitle.size(), "short-buffer result should report the required size");
    require(shortBuffer[0] == 0xA5 && shortBuffer[1] == 0x5A,
        "short-buffer result should not partially write output");

    required = 99;
    require(bms_song_selection_snapshot_copy_text(snapshot, 0, BMS_SONG_TEXT_SUBTITLE,
        nullptr, 0, &required) == BMS_SONG_SELECTION_OK && required == 0,
        "empty text should support a null zero-capacity output");
    require(bms_song_selection_snapshot_copy_text(snapshot, 0, BMS_SONG_TEXT_TITLE,
        nullptr, 1, &required) == BMS_SONG_SELECTION_INVALID_ARGUMENT,
        "positive output capacity should require a non-null buffer");
    std::array<std::uint8_t, 32> copiedTitle{};
    require(bms_song_selection_snapshot_copy_text(snapshot, 0, BMS_SONG_TEXT_TITLE,
        copiedTitle.data(), static_cast<std::uint32_t>(copiedTitle.size()), nullptr) ==
        BMS_SONG_SELECTION_INVALID_ARGUMENT, "required-length output pointer should be mandatory");
    require(bms_song_selection_snapshot_copy_text(snapshot, 2, BMS_SONG_TEXT_TITLE,
        copiedTitle.data(), static_cast<std::uint32_t>(copiedTitle.size()), &required) ==
        BMS_SONG_SELECTION_INDEX_OUT_OF_RANGE, "invalid chart index should be rejected");
    require(bms_song_selection_snapshot_copy_text(snapshot, 0, 99,
        copiedTitle.data(), static_cast<std::uint32_t>(copiedTitle.size()), &required) ==
        BMS_SONG_SELECTION_INVALID_ARGUMENT, "unknown text field should be rejected");
    require(bms_song_selection_snapshot_get_chart(snapshot, 0, nullptr) ==
        BMS_SONG_SELECTION_INVALID_ARGUMENT, "null output pointer should be rejected");

    bms_song_selection_snapshot_destroy(snapshot);
}

void testEmptySnapshotAndInvalidInputs() {
    BmsSongSelectionSnapshot* empty = nullptr;
    require(bms_song_selection_snapshot_create(7, nullptr, 0, &empty) == BMS_SONG_SELECTION_OK,
        "empty catalog should create a valid snapshot");
    std::uint64_t generation = 0;
    std::uint32_t count = 99;
    require(bms_song_selection_snapshot_get_metadata(empty, &generation, &count) == BMS_SONG_SELECTION_OK &&
        generation == 7 && count == 0, "empty snapshot should preserve generation and report zero charts");
    BmsSongChartSummaryInfo info{};
    require(bms_song_selection_snapshot_get_chart(empty, 0, &info) ==
        BMS_SONG_SELECTION_INDEX_OUT_OF_RANGE, "empty snapshot should reject every chart index");
    bms_song_selection_snapshot_destroy(empty);
    bms_song_selection_snapshot_destroy(nullptr);

    const auto valid = chart(201, "chart.bms", "Title", "", "Artist", "",
        "folder", BMS_SONG_MODE_UNKNOWN, 1);
    BmsSongSelectionSnapshot* seed = nullptr;
    require(bms_song_selection_snapshot_create(8, &valid, 1, &seed) == BMS_SONG_SELECTION_OK,
        "seed snapshot should be created for failed-output reset check");
    BmsSongSelectionSnapshot* failed = seed;
    require(bms_song_selection_snapshot_create(0, &valid, 1, &failed) ==
        BMS_SONG_SELECTION_INVALID_ARGUMENT && failed == nullptr,
        "generation zero should be rejected without returning a handle");
    bms_song_selection_snapshot_destroy(seed);
    require(bms_song_selection_snapshot_create(1, nullptr, 1, &failed) ==
        BMS_SONG_SELECTION_INVALID_ARGUMENT && failed == nullptr,
        "null non-empty input array should be rejected");
    require(bms_song_selection_snapshot_create(1, &valid, 1, nullptr) ==
        BMS_SONG_SELECTION_INVALID_ARGUMENT, "null output handle pointer should be rejected");

    const auto zeroId = chart(0, "chart.bms", "Title", "", "Artist", "",
        "folder", BMS_SONG_MODE_UNKNOWN, 1);
    require(bms_song_selection_snapshot_create(1, &zeroId, 1, &failed) ==
        BMS_SONG_SELECTION_INVALID_ARGUMENT, "zero chart id should be rejected");

    const auto invalidPlayable = chart(204, "chart.bms", "Title", "", "Artist", "",
        "folder", BMS_SONG_MODE_UNKNOWN, 2);
    require(bms_song_selection_snapshot_create(1, &invalidPlayable, 1, &failed) ==
        BMS_SONG_SELECTION_INVALID_ARGUMENT, "playable must be encoded as zero or one");

    auto nullTextData = valid;
    nullTextData.title = { nullptr, 1 };
    require(bms_song_selection_snapshot_create(1, &nullTextData, 1, &failed) ==
        BMS_SONG_SELECTION_INVALID_ARGUMENT, "non-empty text requires a non-null pointer");

    const std::array duplicate{ valid, valid };
    require(bms_song_selection_snapshot_create(1, duplicate.data(), 2, &failed) ==
        BMS_SONG_SELECTION_DUPLICATE_CHART_ID && failed == nullptr,
        "duplicate chart ids should be rejected");

    const std::string invalidBytes("\xC3\x28", 2);
    const auto invalidUtf8 = chart(202, "chart.bms", invalidBytes, "", "Artist", "",
        "folder", BMS_SONG_MODE_UNKNOWN, 1);
    require(bms_song_selection_snapshot_create(1, &invalidUtf8, 1, &failed) ==
        BMS_SONG_SELECTION_INVALID_UTF8 && failed == nullptr,
        "invalid UTF-8 should be rejected without returning a handle");

    const auto invalidMode = chart(203, "chart.bms", "Title", "", "Artist", "",
        "folder", 99, 1);
    require(bms_song_selection_snapshot_create(1, &invalidMode, 1, &failed) ==
        BMS_SONG_SELECTION_INVALID_ARGUMENT, "unknown mode value should be rejected");

    std::uint64_t outGeneration = 1;
    std::uint32_t outCount = 1;
    require(bms_song_selection_snapshot_get_metadata(nullptr, &outGeneration, &outCount) ==
        BMS_SONG_SELECTION_INVALID_ARGUMENT, "null snapshot should be rejected");
    require(bms_song_selection_snapshot_get_metadata(nullptr, nullptr, &outCount) ==
        BMS_SONG_SELECTION_INVALID_ARGUMENT, "null metadata output should be rejected");
}

} // namespace
#endif

int main() {
#ifdef BMS_RUST_SONG_SELECTION_BUILD_CHECK
    require(bms_song_selection_abi_version() == BMS_SONG_SELECTION_ABI_VERSION,
        "ABI version should match the header");
    testSnapshotRoundTrip();
    testEmptySnapshotAndInvalidInputs();
#endif
    return 0;
}
