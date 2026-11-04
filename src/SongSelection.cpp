#include "SongSelection.h"

#include "ChartOrdering.h"
#include "PreviewTiming.h"
#include "SongGrouping.h"

#include <algorithm>
#include <cwctype>
#include <windows.h>

namespace bms::ui {

SongSelectionModel::SongSelectionModel() = default;
SongSelectionModel::~SongSelectionModel() = default;

void SongSelectionModel::clearSelection() noexcept {
    selectedDirectory_.clear();
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
    groups_.clear();
    groupIndexByDirectory_.clear();
    clearSelection();
}

void SongSelectionModel::rebuild(std::vector<Chart>& charts) {
    clear();
    const auto baseTitles = canonicalSongTitles(charts);
    std::unordered_map<std::wstring, size_t> byIdentity;
    byIdentity.reserve(charts.size());
    groups_.reserve(charts.size());

    for (size_t chartIndex = 0; chartIndex < charts.size(); ++chartIndex) {
        const auto& chart = charts[chartIndex];
        auto identity = individualCharts_ ? toLower(chart.path.lexically_normal().generic_wstring()) : songGroupIdentity(chart, baseTitles[chartIndex],collectVariants_);
        auto [entry, inserted] = byIdentity.try_emplace(std::move(identity), groups_.size());
        if (inserted) {
            SongGroup group;
            group.title = individualCharts_ ? chart.title : baseTitles[chartIndex];
            group.artist = chart.artist;
            group.directory = entry->first;
            group.mode = chart.mode;
            groups_.push_back(std::move(group));
        }
        groups_[entry->second].chartIndices.push_back(chartIndex);
    }

    for (auto& group : groups_) {
        std::stable_sort(group.chartIndices.begin(), group.chartIndices.end(), [&](size_t leftIndex, size_t rightIndex) {
            const auto& left = charts[leftIndex];
            const auto& right = charts[rightIndex];
            if (left.playable != right.playable) return left.playable;
            const int levelOrder = comparePlayLevelAscending(left, right);
            if (levelOrder != 0) return levelOrder < 0;
            const int subtitleGroup = songNameGroup(left.subtitle);
            const int subtitleOrder = compareTitleText(left.subtitle, right.subtitle, subtitleGroup);
            if (subtitleOrder != 0) return subtitleOrder < 0;
            return compareTitleText(left.path.wstring(), right.path.wstring(), 2) < 0;
        });
        group.previewChartIndex = sharePreviewTiming(charts, group.chartIndices);
    }

    sort();
}

void SongSelectionModel::sort() {
    std::stable_sort(groups_.begin(), groups_.end(), [&](const SongGroup& left, const SongGroup& right) {
        const int leftGroup = songNameGroup(left.title), rightGroup = songNameGroup(right.title);
        int order = leftGroup - rightGroup;
        if (order == 0) order = compareTitleText(left.title, right.title, leftGroup);
        if (order == 0) order = compareTitleText(left.artist, right.artist, 2);
        if (order == 0) order = static_cast<int>(left.mode) - static_cast<int>(right.mode);
        if (order == 0) order = compareTitleText(left.directory, right.directory, 2);
        return sortDescending_ ? order > 0 : order < 0;
    });
    rebuildGroupIndex();
}

void SongSelectionModel::toggleSort() {
    sortDescending_ = !sortDescending_;
    sort();
}

bool SongSelectionModel::selectGroup(int groupIndex) {
    if (groupIndex < 0 || groupIndex >= static_cast<int>(groups_.size())) return false;
    selectedDirectory_ = groups_[static_cast<size_t>(groupIndex)].directory;
    return true;
}

int SongSelectionModel::selectedGroupIndex() const noexcept {
    if (selectedDirectory_.empty()) return -1;
    const auto found = groupIndexByDirectory_.find(selectedDirectory_);
    return found == groupIndexByDirectory_.end() ? -1 : static_cast<int>(found->second);
}

int SongSelectionModel::selectedChartIndex() const noexcept {
    const int groupIndex = selectedGroupIndex();
    if (groupIndex < 0) return -1;
    const auto& group = groups_[static_cast<size_t>(groupIndex)];
    if (group.chartIndices.empty()) return -1;
    const size_t variant = std::min(group.selectedVariant, group.chartIndices.size() - 1);
    return static_cast<int>(group.chartIndices[variant]);
}

bool SongSelectionModel::cycleSelectedVariant(int delta) {
    const int groupIndex = selectedGroupIndex();
    if (groupIndex < 0) return false;
    auto& group = groups_[static_cast<size_t>(groupIndex)];
    if (group.chartIndices.size() < 2) return false;
    const auto count = static_cast<long long>(group.chartIndices.size());
    const auto current = std::min(static_cast<long long>(group.selectedVariant), count - 1);
    const auto next = ((current + static_cast<long long>(delta)) % count + count) % count;
    if (next == current) return false;
    group.selectedVariant = static_cast<size_t>(next);
    return true;
}

void SongSelectionModel::rebuildGroupIndex() {
    groupIndexByDirectory_.clear();
    groupIndexByDirectory_.reserve(groups_.size());
    for (size_t index = 0; index < groups_.size(); ++index)
        groupIndexByDirectory_.try_emplace(groups_[index].directory, index);
}

} // namespace bms::ui
