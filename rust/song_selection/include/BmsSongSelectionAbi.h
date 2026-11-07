#ifndef BMS_SONG_SELECTION_ABI_H
#define BMS_SONG_SELECTION_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BMS_SONG_SELECTION_ABI_VERSION 2u

typedef uint32_t BmsSongSelectionStatus;
#define BMS_SONG_SELECTION_OK 0u
#define BMS_SONG_SELECTION_INVALID_ARGUMENT 1u
#define BMS_SONG_SELECTION_INVALID_UTF8 2u
#define BMS_SONG_SELECTION_DUPLICATE_CHART_ID 3u
#define BMS_SONG_SELECTION_INDEX_OUT_OF_RANGE 4u
#define BMS_SONG_SELECTION_BUFFER_TOO_SMALL 5u
#define BMS_SONG_SELECTION_LENGTH_OVERFLOW 6u
#define BMS_SONG_SELECTION_PANIC 255u

typedef uint32_t BmsSongSelectionMode;
#define BMS_SONG_MODE_UNKNOWN 0u
#define BMS_SONG_MODE_FIVE_KEY 1u
#define BMS_SONG_MODE_SEVEN_KEY 2u
#define BMS_SONG_MODE_TEN_KEY 3u
#define BMS_SONG_MODE_FOURTEEN_KEY 4u
#define BMS_SONG_MODE_POPN_NINE 5u
#define BMS_SONG_MODE_OTHER 6u

typedef uint32_t BmsSongSelectionTextField;
#define BMS_SONG_TEXT_PATH 1u
#define BMS_SONG_TEXT_TITLE 2u
#define BMS_SONG_TEXT_SUBTITLE 3u
#define BMS_SONG_TEXT_ARTIST 4u
#define BMS_SONG_TEXT_PLAY_LEVEL 5u
#define BMS_SONG_TEXT_DIRECTORY 6u

#define BMS_SONG_GROUP_TEXT_TITLE 1u
#define BMS_SONG_GROUP_TEXT_ARTIST 2u
#define BMS_SONG_GROUP_TEXT_DIRECTORY 3u

typedef struct BmsSongSelectionSnapshot BmsSongSelectionSnapshot;
typedef struct BmsSongSelectionModel BmsSongSelectionModel;

typedef struct BmsSongSelectionUtf8Slice {
    const uint8_t* data;
    uint32_t length_bytes;
} BmsSongSelectionUtf8Slice;

typedef struct BmsSongChartSummaryInput {
    uint64_t chart_id;
    BmsSongSelectionUtf8Slice path;
    BmsSongSelectionUtf8Slice title;
    BmsSongSelectionUtf8Slice subtitle;
    BmsSongSelectionUtf8Slice artist;
    BmsSongSelectionUtf8Slice play_level;
    BmsSongSelectionUtf8Slice directory;
    BmsSongSelectionMode mode;
    uint32_t playable;
    uint32_t title_group;
    uint32_t subtitle_group;
    uint32_t has_numeric_play_level;
    double numeric_play_level;
} BmsSongChartSummaryInput;

typedef struct BmsSongChartSummaryInfo {
    uint64_t chart_id;
    BmsSongSelectionMode mode;
    uint32_t playable;
} BmsSongChartSummaryInfo;

typedef struct BmsSongSelectionModelInfo {
    uint64_t generation;
    uint32_t sort_descending;
    uint32_t group_count;
    int32_t selected_group_index;
    int32_t selected_chart_index;
} BmsSongSelectionModelInfo;

typedef struct BmsSongSelectionGroupInfo {
    uint32_t chart_count;
    uint32_t selected_variant;
    BmsSongSelectionMode mode;
} BmsSongSelectionGroupInfo;

uint32_t bms_song_selection_abi_version(void);

/*
 * Creates an immutable Rust-owned snapshot. For count > 0, charts must point
 * to count readable, properly aligned entries. Every non-empty UTF-8 slice
 * must point to length_bytes readable bytes that remain unchanged during this
 * call. out_snapshot must point to separate, aligned writable storage that
 * does not overlap the input array or any input string.
 */
BmsSongSelectionStatus bms_song_selection_snapshot_create(
    uint64_t generation,
    const BmsSongChartSummaryInput* charts,
    uint32_t chart_count,
    BmsSongSelectionSnapshot** out_snapshot);

/* Null is accepted. A non-null handle must be live and destroyed exactly once. */
void bms_song_selection_snapshot_destroy(BmsSongSelectionSnapshot* snapshot);

BmsSongSelectionStatus bms_song_selection_snapshot_get_metadata(
    const BmsSongSelectionSnapshot* snapshot,
    uint64_t* out_generation,
    uint32_t* out_chart_count);

/* out_info must point to writable storage separate from the opaque handle. */
BmsSongSelectionStatus bms_song_selection_snapshot_get_chart(
    const BmsSongSelectionSnapshot* snapshot,
    uint32_t chart_index,
    BmsSongChartSummaryInfo* out_info);

/*
 * Copies exact UTF-8 bytes without a trailing NUL. No partial copy is made.
 * output and out_required_bytes must be separate and must not overlap the
 * opaque snapshot's storage.
 */
BmsSongSelectionStatus bms_song_selection_snapshot_copy_text(
    const BmsSongSelectionSnapshot* snapshot,
    uint32_t chart_index,
    BmsSongSelectionTextField field,
    uint8_t* output,
    uint32_t output_capacity_bytes,
    uint32_t* out_required_bytes);

BmsSongSelectionStatus bms_song_selection_model_create(BmsSongSelectionModel** out_model);
void bms_song_selection_model_destroy(BmsSongSelectionModel* model);
BmsSongSelectionStatus bms_song_selection_model_rebuild(
    BmsSongSelectionModel* model,
    uint64_t generation,
    const BmsSongChartSummaryInput* charts,
    uint32_t chart_count);
BmsSongSelectionStatus bms_song_selection_model_clear(BmsSongSelectionModel* model);
BmsSongSelectionStatus bms_song_selection_model_sort(BmsSongSelectionModel* model);
BmsSongSelectionStatus bms_song_selection_model_toggle_sort(BmsSongSelectionModel* model);
BmsSongSelectionStatus bms_song_selection_model_get_info(
    const BmsSongSelectionModel* model,
    BmsSongSelectionModelInfo* out_info);
BmsSongSelectionStatus bms_song_selection_model_get_group(
    const BmsSongSelectionModel* model,
    uint32_t group_index,
    BmsSongSelectionGroupInfo* out_info);
BmsSongSelectionStatus bms_song_selection_model_get_group_chart_index(
    const BmsSongSelectionModel* model,
    uint32_t group_index,
    uint32_t variant_index,
    uint32_t* out_chart_index);
BmsSongSelectionStatus bms_song_selection_model_select_group(
    BmsSongSelectionModel* model,
    int32_t group_index,
    uint32_t* out_selected);
BmsSongSelectionStatus bms_song_selection_model_clear_selection(BmsSongSelectionModel* model);
BmsSongSelectionStatus bms_song_selection_model_cycle_selected_variant(
    BmsSongSelectionModel* model,
    int32_t delta,
    uint32_t* out_changed);
BmsSongSelectionStatus bms_song_selection_model_copy_group_text(
    const BmsSongSelectionModel* model,
    uint32_t group_index,
    uint32_t field,
    uint8_t* output,
    uint32_t output_capacity_bytes,
    uint32_t* out_required_bytes);

#ifdef __cplusplus
} // extern "C"

static_assert(sizeof(BmsSongSelectionUtf8Slice) == 16);
static_assert(sizeof(BmsSongChartSummaryInput) == 136);
static_assert(sizeof(BmsSongChartSummaryInfo) == 16);
static_assert(sizeof(BmsSongSelectionModelInfo) == 24);
static_assert(sizeof(BmsSongSelectionGroupInfo) == 12);
static_assert(alignof(BmsSongSelectionUtf8Slice) == 8);
static_assert(alignof(BmsSongChartSummaryInput) == 8);
static_assert(alignof(BmsSongChartSummaryInfo) == 8);
static_assert(alignof(BmsSongSelectionModelInfo) == 8);
#endif

#endif // BMS_SONG_SELECTION_ABI_H
