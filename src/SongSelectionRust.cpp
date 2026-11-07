#include "SongSelection.h"

#include "BmsSongSelectionAbi.h"
#include "ChartOrdering.h"
#include "PreviewTiming.h"
#include "SongGrouping.h"

#include <windows.h>

#include <array>
#include <climits>
#include <cstdint>
#include <cwctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bms::ui {
namespace {

BmsSongSelectionModel* rustModel(void* handle) noexcept {
    return static_cast<BmsSongSelectionModel*>(handle);
}

bool wideToUtf8(std::wstring_view value, std::string& output) {
    if (value.empty()) {
        output.clear();
        return true;
    }
    if (value.size() > static_cast<size_t>(INT_MAX)) return false;

    const int sourceLength = static_cast<int>(value.size());
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), sourceLength,
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) return false;
    output.resize(static_cast<size_t>(required));
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), sourceLength,
        output.data(), required, nullptr, nullptr) == required;
}

bool utf8ToWide(std::string_view value, std::wstring& output) {
    if (value.empty()) {
        output.clear();
        return true;
    }
    if (value.size() > static_cast<size_t>(INT_MAX)) return false;

    const int sourceLength = static_cast<int>(value.size());
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), sourceLength,
        nullptr, 0);
    if (required <= 0) return false;
    output.resize(static_cast<size_t>(required));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), sourceLength,
        output.data(), required) == required;
}

BmsSongSelectionUtf8Slice utf8Slice(const std::string& value) noexcept {
    return {
        reinterpret_cast<const std::uint8_t*>(value.data()),
        static_cast<std::uint32_t>(value.size())
    };
}

bool copyGroupText(BmsSongSelectionModel* model, std::uint32_t groupIndex,
    std::uint32_t field, std::wstring& output) {
    std::uint32_t required = 0;
    auto status = bms_song_selection_model_copy_group_text(model, groupIndex, field,
        nullptr, 0, &required);
    if (status != BMS_SONG_SELECTION_OK && status != BMS_SONG_SELECTION_BUFFER_TOO_SMALL) return false;

    std::vector<std::uint8_t> bytes(required);
    if (required != 0) {
        status = bms_song_selection_model_copy_group_text(model, groupIndex, field,
            bytes.data(), required, &required);
        if (status != BMS_SONG_SELECTION_OK) return false;
    }
    if (bytes.empty()) {
        output.clear();
        return true;
    }
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return utf8ToWide(text, output);
}

} // namespace

SongSelectionModel::SongSelectionModel() {
    BmsSongSelectionModel* created = nullptr;
    if (bms_song_selection_model_create(&created) == BMS_SONG_SELECTION_OK) rustModel_ = created;
}

SongSelectionModel::~SongSelectionModel() {
    if (rustModel_) bms_song_selection_model_destroy(rustModel(rustModel_));
}

int songNameGroup(const std::wstring& value) {
    for (wchar_t c : value) {
        if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z')) return 0;
        if ((c >= 0xAC00 && c <= 0xD7A3) || (c >= 0x1100 && c <= 0x11FF) ||
            (c >= 0x3130 && c <= 0x318F) || (c >= 0xA960 && c <= 0xA97F) ||
            (c >= 0xD7B0 && c <= 0xD7FF)) return 1;
        if (!std::iswspace(c) && !std::iswpunct(c) && !(c >= L'0' && c <= L'9')) return 2;
    }
    return 2;
}

int compareTitleText(const std::wstring& left, const std::wstring& right, int group) {
    if (group == 0) {
        const int result = CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()),
            right.c_str(), static_cast<int>(right.size()), TRUE);
        if (result != 0) return result - CSTR_EQUAL;
    } else {
        const wchar_t* locale = group == 1 ? L"ko-KR" : L"ja-JP";
        const int result = CompareStringEx(locale, NORM_IGNORECASE, left.c_str(), static_cast<int>(left.size()),
            right.c_str(), static_cast<int>(right.size()), nullptr, nullptr, 0);
        if (result != 0) return result - CSTR_EQUAL;
    }
    const auto a = toLower(left), b = toLower(right);
    return a < b ? -1 : a > b ? 1 : 0;
}

void SongSelectionModel::clear() {
    if (rustModel_) bms_song_selection_model_clear(rustModel(rustModel_));
    groups_.clear();
    groupIndexByDirectory_.clear();
    previewChartIndexByDirectory_.clear();
    clearSelection();
}

void SongSelectionModel::rebuild(std::vector<Chart>& charts) {
    clear();
    if (charts.size() > static_cast<size_t>(INT_MAX) || charts.size() > UINT32_MAX - 1u) return;

    if (!rustModel_) {
        BmsSongSelectionModel* created = nullptr;
        if (bms_song_selection_model_create(&created) != BMS_SONG_SELECTION_OK || !created) return;
        rustModel_ = created;
    }

    const auto baseTitles = canonicalSongTitles(charts);
    using ChartStrings = std::array<std::string, 6>;
    std::vector<ChartStrings> chartStrings;
    chartStrings.reserve(charts.size());
    for (size_t index = 0; index < charts.size(); ++index) {
        const auto& chart = charts[index];
        ChartStrings values;
        if (!wideToUtf8(chart.path.wstring(), values[0]) ||
            !wideToUtf8(individualCharts_ ? chart.title : baseTitles[index], values[1]) ||
            !wideToUtf8(chart.subtitle, values[2]) ||
            !wideToUtf8(chart.artist, values[3]) ||
            !wideToUtf8(chart.playLevel, values[4]) ||
            !wideToUtf8(individualCharts_ ? toLower(chart.path.lexically_normal().generic_wstring()) : songGroupIdentity(chart,baseTitles[index],collectVariants_), values[5])) {
            clear();
            return;
        }
        chartStrings.push_back(std::move(values));
    }

    std::vector<BmsSongChartSummaryInput> input;
    input.reserve(charts.size());
    for (size_t index = 0; index < charts.size(); ++index) {
        const auto& chart = charts[index];
        const auto level = numericPlayLevel(chart);
        const auto& strings = chartStrings[index];
        input.push_back({
            static_cast<std::uint64_t>(index) + 1,
            utf8Slice(strings[0]), utf8Slice(strings[1]), utf8Slice(strings[2]),
            utf8Slice(strings[3]), utf8Slice(strings[4]), utf8Slice(strings[5]),
            static_cast<std::uint32_t>(chart.mode), chart.playable ? 1u : 0u,
            static_cast<std::uint32_t>(songNameGroup(baseTitles[index])),
            static_cast<std::uint32_t>(songNameGroup(chart.subtitle)),
            level ? 1u : 0u, level.value_or(0.0)
        });
    }

    if (++catalogGeneration_ == 0) catalogGeneration_ = 1;
    if (bms_song_selection_model_rebuild(rustModel(rustModel_), catalogGeneration_, input.data(),
        static_cast<std::uint32_t>(input.size())) != BMS_SONG_SELECTION_OK) {
        clear();
        return;
    }
    refreshRustProjection(&charts);
}

void SongSelectionModel::sort() {
    if (!rustModel_ || bms_song_selection_model_sort(rustModel(rustModel_)) != BMS_SONG_SELECTION_OK ||
        !refreshRustProjection()) clear();
}

void SongSelectionModel::toggleSort() {
    if (!rustModel_ || bms_song_selection_model_toggle_sort(rustModel(rustModel_)) != BMS_SONG_SELECTION_OK ||
        !refreshRustProjection()) clear();
}

void SongSelectionModel::clearSelection() noexcept {
    selectedDirectory_.clear();
    if (rustModel_) bms_song_selection_model_clear_selection(rustModel(rustModel_));
}

int SongSelectionModel::selectedGroupIndex() const noexcept {
    if (!rustModel_) return -1;
    BmsSongSelectionModelInfo info{};
    if (bms_song_selection_model_get_info(rustModel(rustModel_), &info) != BMS_SONG_SELECTION_OK) return -1;
    return info.selected_group_index;
}

int SongSelectionModel::selectedChartIndex() const noexcept {
    if (!rustModel_) return -1;
    BmsSongSelectionModelInfo info{};
    if (bms_song_selection_model_get_info(rustModel(rustModel_), &info) != BMS_SONG_SELECTION_OK) return -1;
    return info.selected_chart_index;
}

bool SongSelectionModel::selectGroup(int groupIndex) {
    if (!rustModel_) return false;
    std::uint32_t selected = 0;
    return bms_song_selection_model_select_group(rustModel(rustModel_), groupIndex, &selected) ==
        BMS_SONG_SELECTION_OK && selected != 0;
}

bool SongSelectionModel::cycleSelectedVariant(int delta) {
    if (!rustModel_) return false;
    std::uint32_t changed = 0;
    if (bms_song_selection_model_cycle_selected_variant(rustModel(rustModel_), delta, &changed) !=
        BMS_SONG_SELECTION_OK || changed == 0) return false;

    BmsSongSelectionModelInfo modelInfo{};
    if (bms_song_selection_model_get_info(rustModel(rustModel_), &modelInfo) != BMS_SONG_SELECTION_OK ||
        modelInfo.selected_group_index < 0 ||
        static_cast<size_t>(modelInfo.selected_group_index) >= groups_.size()) return true;
    BmsSongSelectionGroupInfo groupInfo{};
    if (bms_song_selection_model_get_group(rustModel(rustModel_),
        static_cast<std::uint32_t>(modelInfo.selected_group_index), &groupInfo) == BMS_SONG_SELECTION_OK) {
        groups_[static_cast<size_t>(modelInfo.selected_group_index)].selectedVariant = groupInfo.selected_variant;
    }
    return true;
}

bool SongSelectionModel::refreshRustProjection(std::vector<Chart>* charts) {
    if (!rustModel_) return false;
    BmsSongSelectionModelInfo modelInfo{};
    if (bms_song_selection_model_get_info(rustModel(rustModel_), &modelInfo) != BMS_SONG_SELECTION_OK) return false;

    std::vector<SongGroup> nextGroups;
    std::unordered_map<std::wstring, size_t> nextPreviews;
    nextGroups.reserve(modelInfo.group_count);
    nextPreviews.reserve(modelInfo.group_count);
    for (std::uint32_t groupIndex = 0; groupIndex < modelInfo.group_count; ++groupIndex) {
        BmsSongSelectionGroupInfo info{};
        if (bms_song_selection_model_get_group(rustModel(rustModel_), groupIndex, &info) != BMS_SONG_SELECTION_OK)
            return false;

        SongGroup group;
        if (!copyGroupText(rustModel(rustModel_), groupIndex, BMS_SONG_GROUP_TEXT_TITLE, group.title) ||
            !copyGroupText(rustModel(rustModel_), groupIndex, BMS_SONG_GROUP_TEXT_ARTIST, group.artist) ||
            !copyGroupText(rustModel(rustModel_), groupIndex, BMS_SONG_GROUP_TEXT_DIRECTORY, group.directory))
            return false;
        group.mode = static_cast<ChartMode>(info.mode);
        group.selectedVariant = info.selected_variant;
        group.chartIndices.reserve(info.chart_count);
        for (std::uint32_t variant = 0; variant < info.chart_count; ++variant) {
            std::uint32_t chartIndex = 0;
            if (bms_song_selection_model_get_group_chart_index(rustModel(rustModel_), groupIndex,
                variant, &chartIndex) != BMS_SONG_SELECTION_OK) return false;
            if (charts && chartIndex >= charts->size()) return false;
            group.chartIndices.push_back(chartIndex);
        }

        if (charts) {
            group.previewChartIndex = bms::sharePreviewTiming(*charts, group.chartIndices);
        } else if (const auto found = previewChartIndexByDirectory_.find(group.directory);
            found != previewChartIndexByDirectory_.end()) {
            group.previewChartIndex = found->second;
        } else if (!group.chartIndices.empty()) {
            group.previewChartIndex = group.chartIndices.front();
        }
        nextPreviews[group.directory] = group.previewChartIndex;
        nextGroups.push_back(std::move(group));
    }

    groups_ = std::move(nextGroups);
    previewChartIndexByDirectory_ = std::move(nextPreviews);
    sortDescending_ = modelInfo.sort_descending != 0;
    rebuildGroupIndex();
    return true;
}

void SongSelectionModel::rebuildGroupIndex() {
    groupIndexByDirectory_.clear();
    groupIndexByDirectory_.reserve(groups_.size());
    for (size_t index = 0; index < groups_.size(); ++index)
        groupIndexByDirectory_.try_emplace(groups_[index].directory, index);
}

} // namespace bms::ui
