#include "SongSelection.h"
#include <set>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bms::Chart chart(const wchar_t* title, const wchar_t* directory, const wchar_t* filename,
    bool playable, const wchar_t* level, const wchar_t* subtitle,
    bms::ChartMode mode = bms::ChartMode::SevenKey) {
    bms::Chart result;
    result.path = std::filesystem::path(L"C:/Songs") / directory / filename;
    result.title = title;
    result.artist = L"Test Artist";
    result.samples[L"01"]=result.path.parent_path()/L"song.wav";
    result.playable = playable;
    result.playLevel = level;
    result.subtitle = subtitle;
    result.mode = mode;
    return result;
}

} // namespace

int main() {
    try {
        bms::ui::SongSelectionModel empty;
        require(empty.groups().empty(), "new model should have no groups");
        require(empty.selectedGroupIndex() == -1 && empty.selectedChartIndex() == -1,
            "new model should have no selection");
        require(!empty.selectGroup(0), "empty model should reject selection");

        std::vector<bms::Chart> charts;
        charts.push_back(chart(L"Zeta", L"Zeta", L"zeta.bms", true, L"2", L""));
        charts.push_back(chart(L"Alpha [ANOTHER 14]", L"Alpha", L"another.bms", true, L"12", L"Another"));
        charts.push_back(chart(L"Alpha [NORMAL]", L"Alpha", L"normal.bms", true, L"5", L"Normal"));
        charts.push_back(chart(L"Alpha [BEGINNER]", L"Alpha", L"beginner.bms", false, L"1", L"Beginner",
            bms::ChartMode::FourteenKey));
        charts.push_back(chart(L"Beta [NORMAL]", L"Beta", L"normal.bms", true, L"7", L"Normal"));
        charts.push_back(chart(L"Beta [ANOTHER 14]", L"Beta", L"another.bms", true, L"14", L"Another",
            bms::ChartMode::FourteenKey));
        charts.push_back(chart(L"Shared", L"SharedA", L"chart.bms", true, L"3", L""));
        charts.push_back(chart(L"Shared", L"SharedB", L"chart.bms", true, L"3", L""));

        charts[2].previewStartDefined = true;
        charts[2].durationSeconds = 120.0;
        charts[2].previewStartSeconds = 15.0;
        charts[2].previewDurationSeconds = 12.0;

        bms::ui::SongSelectionModel model;
        model.rebuild(charts);
        const auto& groups = model.groups();
        require(groups.size() == 5, "same-folder charts should group; same-title different folders should not");
        require(groups[0].title == L"Alpha" && groups[1].title == L"Beta" && groups[4].title == L"Zeta",
            "ascending group title order changed");
        require(groups[0].chartIndices == std::vector<size_t>{2, 1, 3},
            "chart order should prioritize playable charts, then numeric level");
        require(groups[1].chartIndices == std::vector<size_t>{4, 5},
            "same-folder charts should remain grouped across key modes");
        require(groups[1].mode == bms::ChartMode::SevenKey,
            "group mode should retain the first chart's mode");
        require(groups[2].title == L"Shared" && groups[3].title == L"Shared" &&
            groups[2].directory != groups[3].directory,
            "same title in separate directories should remain separate groups");

        require(groups[0].previewChartIndex == 2, "first valid explicit preview should be representative");
        for (const size_t index : groups[0].chartIndices) {
            require(charts[index].previewStartSeconds == 15.0 && charts[index].previewDurationSeconds == 12.0,
                "representative preview timing should be shared by group charts");
        }

        require(!model.sortDescending(), "new model should sort ascending");
        require(model.selectGroup(0), "valid group selection failed");
        require(model.selectedGroupIndex() == 0 && model.selectedChartIndex() == 2,
            "selection should resolve to the first sorted variant");
        require(model.cycleSelectedVariant(1) && model.selectedChartIndex() == 1,
            "variant selection should advance");
        require(model.cycleSelectedVariant(1) && model.selectedChartIndex() == 3,
            "variant selection should include the unplayable entry in model order");
        require(model.cycleSelectedVariant(1) && model.selectedChartIndex() == 2,
            "variant selection should wrap forward");
        require(model.cycleSelectedVariant(-1) && model.selectedChartIndex() == 3,
            "variant selection should wrap backward");

        require(model.selectGroup(1) && model.selectedChartIndex() == 4,
            "selecting a group should select its first ordered variant");
        require(model.cycleSelectedVariant(1) && model.selectedChartIndex() == 5,
            "two-chart group should allow variant changes");
        require(!model.selectGroup(-1) && !model.selectGroup(5),
            "out-of-range group selection should fail");
        require(model.selectedGroupIndex() == 1 && model.selectedChartIndex() == 5,
            "failed selection should leave the previous selection unchanged");
        require(model.selectGroup(4) && model.selectedChartIndex() == 0,
            "single-chart group should be selectable");
        require(!model.cycleSelectedVariant(1) && model.selectedChartIndex() == 0,
            "single-chart group should reject variant changes");

        require(model.selectGroup(1) && model.selectedChartIndex() == 5,
            "selecting another group should preserve Beta's previously selected variant");
        model.toggleSort();
        require(model.sortDescending() && groups[0].title == L"Zeta",
            "sort toggle should reverse group order");
        require(model.selectedGroupIndex() == 3 && model.selectedChartIndex() == 5,
            "sorting should preserve selected directory and variant");
        model.toggleSort();
        require(!model.sortDescending() && model.selectedGroupIndex() == 1 && model.selectedChartIndex() == 5,
            "second sort toggle should restore ascending order without losing selection");

        model.clearSelection();
        require(model.selectedGroupIndex() == -1 && model.selectedChartIndex() == -1 && !model.groups().empty(),
            "clearSelection should clear only selection state");
        model.toggleSort();
        model.clear();
        require(model.groups().empty() && model.selectedGroupIndex() == -1 && model.selectedChartIndex() == -1,
            "clear should remove groups and selection");
        require(model.sortDescending(), "clear should preserve sort direction");
        model.rebuild(charts);
        require(model.groups().front().title == L"Zeta" && model.sortDescending(),
            "rebuild should preserve sort direction");

        require(bms::ui::songNameGroup(L"123 Alpha") == 0, "Latin title classification changed");
        require(bms::ui::songNameGroup(L"123 가나다") == 1, "Hangul title classification changed");
        require(bms::ui::songNameGroup(L"日本語") == 2, "other-script title classification changed");
        require(bms::ui::compareTitleText(L"bms", L"BMS", 0) == 0,
            "Latin title comparison should ignore case");
        require(bms::ui::compareTitleText(L"가", L"나", 1) < 0,
            "Korean title comparison should use Korean collation");
        require(bms::ui::compareTitleText(L"あ", L"い", 2) < 0,
            "other-script title comparison should preserve current Japanese collation");

        auto independent=chart(L"Other song",L"Alpha",L"other.bms",true,L"4",L"");
        charts.push_back(independent);auto sameTitle=chart(L"Alpha",L"Alpha",L"unrelated.bms",true,L"4",L"");sameTitle.samples[L"01"]=L"C:/Songs/Alpha/unrelated.wav";charts.push_back(sameTitle);
        model.rebuild(charts);require(model.groups().size()==7,"independent title/audio were incorrectly merged");
        size_t total=0;for(const auto& g:model.groups())total+=g.chartIndices.size();require(total==charts.size(),"grouping dropped a chart");
        model.setIndividualCharts(true);model.rebuild(charts);
        require(model.groups().size()==charts.size(),"individual view merged charts");
        std::set<size_t> individualIndices;
        for(const auto& g:model.groups()){require(g.chartIndices.size()==1,"individual view retained variants");individualIndices.insert(g.chartIndices.front());}
        require(individualIndices.size()==charts.size(),"individual view lost or duplicated a chart");
        model.setIndividualCharts(false);model.rebuild(charts);
        require(model.groups().size()==7,"group view did not restore after individual view");
        std::vector<bms::Chart> variants;
        int serial=0;
        for(auto title:{L"Oekaki [DP Another]",L"Oekaki [DP Hyper]",L"Oekaki [DP Normal]",L"Oekaki [SP Another]",L"Oekaki [SP Hyper]",L"Oekaki [SP Normal]"}){
            const bool dp=serial<3;auto c=chart(title,L"Oekaki",(std::to_wstring(serial++)+L".bms").c_str(),true,L"9",L"",dp?bms::ChartMode::FourteenKey:bms::ChartMode::SevenKey);
            c.catalogOnly=true;c.catalogGroupKey=dp?L"audio:1258:dp":L"audio:1259:sp";variants.push_back(c);
        }
        variants.push_back(chart(L"Other song",L"Oekaki",L"other.bms",true,L"3",L""));
        variants.push_back(chart(L"Oekaki [SP Hyper]",L"Separate",L"separate.bms",true,L"3",L""));
        model.setIndividualCharts(false);model.setCollectVariants(false);model.rebuild(variants);require(model.groups().size()==4,"strict resource grouping changed");
        model.setCollectVariants(true);model.rebuild(variants);require(model.groups().size()==3,"collect mode merged different titles/directories or split SP/DP");
        std::set<size_t> collected;int found=-1;
        for(size_t i=0;i<model.groups().size();++i){const auto& g=model.groups()[i];if(g.chartIndices.size()==6)found=int(i);for(auto index:g.chartIndices)require(collected.insert(index).second,"collect mode duplicated a chart");}
        require(found>=0&&collected.size()==variants.size(),"collect mode lost a variant");model.selectGroup(found);const int firstVariant=model.selectedChartIndex();
        for(int i=0;i<6;++i)require(model.cycleSelectedVariant(1),"collected variant selection failed");require(model.selectedChartIndex()==firstVariant,"collected variants did not wrap");
        model.setIndividualCharts(true);model.rebuild(variants);require(model.groups().size()==variants.size(),"individual mode no longer overrides collection");
        std::cout << "Song selection behavior contract passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
