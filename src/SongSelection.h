#pragma once

#include "BmsParser.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace bms::ui {

struct SongGroup {
    std::wstring title;
    std::wstring artist;
    std::wstring directory;
    ChartMode mode{ ChartMode::Unknown };
    std::vector<size_t> chartIndices;
    size_t selectedVariant{};
    size_t previewChartIndex{};
};

// Owns the menu's grouped chart order and selected group. The native list view is
// only a presentation of this state, so sorting does not invalidate selection.
class SongSelectionModel {
public:
    SongSelectionModel();
    ~SongSelectionModel();
    SongSelectionModel(const SongSelectionModel&) = delete;
    SongSelectionModel& operator=(const SongSelectionModel&) = delete;

    const std::vector<SongGroup>& groups() const noexcept { return groups_; }

    void clear();
    void rebuild(std::vector<Chart>& charts);
    void setCollectVariants(bool enabled) noexcept { collectVariants_=enabled; }
    void setIndividualCharts(bool enabled) noexcept { individualCharts_=enabled; }
    void sort();
    void toggleSort();

    bool sortDescending() const noexcept { return sortDescending_; }
    bool selectGroup(int groupIndex);
    void clearSelection() noexcept;
    int selectedGroupIndex() const noexcept;
    int selectedChartIndex() const noexcept;
    bool cycleSelectedVariant(int delta);

private:
    bool individualCharts_{};
    bool collectVariants_{};
    void rebuildGroupIndex();
    bool refreshRustProjection(std::vector<Chart>* charts = nullptr);

    void* rustModel_{};
    std::vector<SongGroup> groups_;
    std::unordered_map<std::wstring, size_t> groupIndexByDirectory_;
    std::unordered_map<std::wstring, size_t> previewChartIndexByDirectory_;
    std::wstring selectedDirectory_;
    bool sortDescending_{};
    std::uint64_t catalogGeneration_{};
};

int songNameGroup(const std::wstring& value);
int compareTitleText(const std::wstring& left, const std::wstring& right, int group);

} // namespace bms::ui
