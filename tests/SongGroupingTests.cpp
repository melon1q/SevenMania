#include "SongGrouping.h"
#include "SongLibrary.h"
#include "ChartOrdering.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <stdexcept>
#include <windows.h>

void print(const std::wstring& text) {
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), length, nullptr, nullptr);
    std::cout << utf8;
}

void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }

bms::Chart chart(const wchar_t* title, const wchar_t* artist, const wchar_t* folder = L"songs/track",
    bms::ChartMode mode = bms::ChartMode::SevenKey) {
    bms::Chart result;
    result.title = title; result.artist = artist; result.path = std::filesystem::path(folder) / L"chart.bme"; result.mode = mode;
    return result;
}

int wmain(int argc, wchar_t** argv) {
    try {
        const std::vector<bms::Chart> fixtures{
            chart(L"FREEDOM DiVE [LIGHT 7]", L"xi"),
            chart(L"FREEDOM DiVE [NORMAL 7]", L"xi / obj: other"),
            chart(L"FREEDOM DiVE [GALAXY]", L"xi / air"),
            chart(L"FREEDOM DiVE", L"xi"),
            chart(L"FREEDOM DiVE (carnival)", L"xi obj. author"),
            chart(L"FREEDOM DiVE - SINGULARITY", L"xi"),
            chart(L"FREEDoM Dibeyond!!", L"xi"),
            chart(L"FREEDOM DiVE 2", L"xi"),
            chart(L"FREEDOM DiVE [ANOTHER 14]", L"xi", L"songs/track", bms::ChartMode::FourteenKey),
            chart(L"FREEDOM DiVE [LIGHT 7]", L"xi", L"songs/another"),
            chart(L"slic.hertz", L"Sobrem", L"songs/slic"),
            chart(L"slic.hertz", L"Sobrem", L"songs/slic")
        };
        const auto bases = bms::canonicalSongTitles(fixtures);
        const auto first = bms::songGroupIdentity(fixtures[0], bases[0]);
        for (size_t i = 1; i <= 5; ++i)
            require(bms::songGroupIdentity(fixtures[i], bases[i]) == first, "chart title and author variations did not merge");
        for (size_t i = 6; i <= 7; ++i)
            require(bms::songGroupIdentity(fixtures[i], bases[i]) != first, "independent songs in the same folder were merged");
        require(bms::songGroupIdentity(fixtures[8],bases[8])==first,"a key-mode variant of the same song was split");
        require(bms::songGroupIdentity(fixtures[9], bases[9]) != first, "different song folders were merged");
        require(bms::songGroupIdentity(fixtures[10], bases[10]) == bms::songGroupIdentity(fixtures[11], bases[11]), "subtitle-based grouping regressed");
        require(fixtures[0].title == L"FREEDOM DiVE [LIGHT 7]" && fixtures[1].artist == L"xi / obj: other", "original metadata was modified");
        require(bms::songTitleWithoutChartLabel(L"[Original Song]") == L"[Original Song]", "whole bracketed title disappeared");
        require(bms::songTitleWithoutChartLabel(L"Song [NORMAL] [7KEY]") == L"Song", "stacked labels did not normalize");
        require(bms::songTitleWithoutChartLabel(L"Song[NORMAL 7]") == L"Song", "label without separating space did not normalize");
        require(bms::songTitleWithoutChartLabel(L"Song [unfinished") == L"Song [unfinished", "malformed label changed title");
        const std::vector<bms::Chart> signager{
            chart(L"Signager (Another)", L"artist"), chart(L"Signager (Hyper)", L"artist"),
            chart(L"Signager (Normal)", L"artist")};
        const auto signagerTitles = bms::canonicalSongTitles(signager);
        for (size_t i = 0; i < signager.size(); ++i)
            require(signagerTitles[i] == L"Signager" && bms::songGroupIdentity(signager[i], signagerTitles[i]) ==
                bms::songGroupIdentity(signager[0], signagerTitles[0]), "parenthesized difficulties without bare title did not merge");
        require(bms::songTitleWithoutChartLabel(L"Song (Remix)") == L"Song (Remix)", "remix title was stripped");
        require(bms::songTitleWithoutChartLabel(L"Song（SP HYPER 7）") == L"Song", "full-width round difficulty label not stripped");
        const std::vector<bms::Chart> zeroCharts{
            chart(L"-零-【ANOTHER】", L"artist", L"Songs/zero"),
            chart(L"-零-【BEGINNER】", L"artist", L"Songs/zero"),
            chart(L"-零-【HYPER】", L"artist", L"Songs/zero"),
            chart(L"-零-【NORMAL】", L"artist", L"Songs/zero"),
            chart(L"-零-［NORMAL］", L"artist", L"Songs/zero")
        };
        const auto zeroTitles = bms::canonicalSongTitles(zeroCharts);
        for (size_t i = 0; i < zeroCharts.size(); ++i)
            require(zeroTitles[i] == L"-零-" && bms::songGroupIdentity(zeroCharts[i], zeroTitles[i]) ==
                bms::songGroupIdentity(zeroCharts[0], zeroTitles[0]), "Japanese difficulty brackets split the same song");
        require(bms::songTitleWithoutChartLabel(L"【Original Song】") == L"【Original Song】",
            "whole Japanese bracketed song title changed");

        const std::vector<bms::Chart> mistCharts{
            chart(L"-霧- [ANOTHER]", L"author A", L"Songs/mist/another"),
            chart(L"-霧- [BEGINNER]", L"author B", L"Songs/mist/beginner"),
            chart(L"-霧- [HYPER]", L"author C", L"Songs/mist/hyper"),
            chart(L"-霧- [NORMAL]", L"author D", L"Songs/mist/normal"),
            chart(L"-霧- [NORMAL]", L"another song", L"Songs/other-mist/normal"),
            chart(L"-霧- [NORMAL]", L"another mode", L"Songs/mist/fourteen", bms::ChartMode::FourteenKey)
        };
        const auto mistTitles = bms::canonicalSongTitles(mistCharts);
        const auto mistIdentity = bms::songGroupIdentity(mistCharts[0], mistTitles[0]);
        for (size_t i = 1; i <= 3; ++i) {
            require(mistTitles[i] == L"-霧-", "difficulty suffix did not normalize for Japanese title");
            require(bms::songGroupIdentity(mistCharts[i], mistTitles[i]) != mistIdentity,
                "separate chart directories were merged");
        }
        require(bms::songGroupIdentity(mistCharts[4], mistTitles[4]) != mistIdentity,
            "same title in a separate imported song folder was merged");
        require(bms::songGroupIdentity(mistCharts[5], mistTitles[5]) != mistIdentity,
            "different key modes were merged");
        std::cout << "Synthetic grouping checks passed.\n";
        for (const auto* title : {L"Amplifying Effect -SP BEGINNER-", L"Amplifying Effect -SP HYPER-",
                L"Amplifying Effect -SP NORMAL-", L"Amplifying Effect-SP ANOTHER-",
                L"Amplifying Effect _DP HYPER 14KEY_", L"Amplifying Effect : NORMAL LV7",
                L"Amplifying Effect — SP HYPER —", L"Amplifying Effect ＳＰ ＮＯＲＭＡＬ",
                L"Amplifying Effect -SPHYPER7-", L"Amplifying Effect [NORMAL] -SP HYPER-",
                L"Amplifying Effect -SP HYPER- [7KEY]", L"Amplifying Effect HYPER", L"Amplifying Effect -SP STARTER-"}) {
            require(bms::songTitleWithoutChartLabel(title) == L"Amplifying Effect", "difficulty suffix variant did not normalize");
        }
        for (const auto* title : {L"Song 2", L"Song - Hyper Remix", L"Song Another World", L"Song SP",
                L"Song A", L"Song - NORMALITY-", L"Song -SP HYPER unfinished"})
            require(bms::songTitleWithoutChartLabel(title) == title, "ordinary title was mistaken for a difficulty label");
        const std::vector<bms::Chart> amplifying{
            chart(L"Amplifying Effect -SP BEGINNER-", L"artist"),
            chart(L"Amplifying Effect -SP NORMAL-", L"artist / obj: other"),
            chart(L"Amplifying Effect -SP HYPER-", L"artist"),
            chart(L"Amplifying Effect -DP HYPER-", L"artist", L"songs/track", bms::ChartMode::FourteenKey)};
        const auto amplifyingTitles = bms::canonicalSongTitles(amplifying);
        for (size_t i = 1; i < 3; ++i)
            require(bms::songGroupIdentity(amplifying[0], amplifyingTitles[0]) ==
                bms::songGroupIdentity(amplifying[i], amplifyingTitles[i]), "Amplifying Effect difficulties remain split");
        require(bms::songGroupIdentity(amplifying[0], amplifyingTitles[0]) ==
            bms::songGroupIdentity(amplifying[3], amplifyingTitles[3]), "same-folder SP and DP were split");

        // Read-only inspection of real libraries; never copies or edits songs.
        for (int argument = 1; argument < argc; ++argument) {
            const auto charts = bms::scanSongFolder(argv[argument]);
            const auto titles = bms::canonicalSongTitles(charts);
            std::map<std::wstring, std::vector<size_t>> groups;
            for (size_t i = 0; i < charts.size(); ++i) groups[bms::songGroupIdentity(charts[i], titles[i])].push_back(i);
            std::cout << charts.size() << " charts -> " << groups.size() << " groups\n";
            std::wstring lightIdentity, normalIdentity;
            for (size_t i = 0; i < charts.size(); ++i) {
                if (bms::toLower(charts[i].title) == L"freedom dive [light 7]") lightIdentity = bms::songGroupIdentity(charts[i], titles[i]);
                if (bms::toLower(charts[i].title) == L"freedom dive [normal 7]") normalIdentity = bms::songGroupIdentity(charts[i], titles[i]);
            }
            if (!lightIdentity.empty() || !normalIdentity.empty())
                require(!lightIdentity.empty() && lightIdentity == normalIdentity, "real Light 7 and Normal 7 remain separate");
            for (auto& [identity, indices] : groups) {
                std::stable_sort(indices.begin(), indices.end(), [&](size_t a, size_t b) { return bms::comparePlayLevelAscending(charts[a], charts[b]) < 0; });
                const auto& firstChart = charts[indices.front()];
                print(L"  " + titles[indices.front()] + L" | mode=" + std::to_wstring(static_cast<int>(firstChart.mode))
                    + L" | charts=" + std::to_wstring(indices.size()) + L" | initial=" + firstChart.path.filename().wstring()
                    + L" | level=" + firstChart.playLevel + L"\n");
            }
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
