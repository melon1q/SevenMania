// Difficulty tables on the select screen (data and parsing: DifficultyTable.h, download: DifficultyTableFetch.h).
// Tables appear as folders at the library root; a table opens on its levels, a level on the library's
// charts that the table lists (matched by MD5 or SHA-256). Charts the library lacks only count toward
// the "owned/total" in the level's name. Tables download only when the player presses Refresh.
// DifficultyTableFetch.h is included at the top of Main.cpp, with the other headers.
#include <mutex>
#include <thread>

constexpr UINT WM_TABLES_UPDATED = WM_APP + 98;
constexpr int tableRowBase = -1000000, levelRowBase = -2000000; // list IDs: below every real folder row

struct LoadedTable {
    bms::table::Table table;               // empty name until downloaded once
    std::vector<bms::table::LevelView> levels;
    std::wstring status;                   // last refresh result, for the settings page
};
std::vector<LoadedTable> gTables;          // one per registered URL, in the order they were added
int gTableView = -1, gTableLevel = -1;     // select screen: inside a table, and one of its levels
int gTableSelection = 0;                   // settings page: the table the buttons act on

std::filesystem::path tableCacheFolder() {
    return (gSettingsFile.empty() ? executableFolder() : gSettingsFile.parent_path()) / L"tables";
}
std::wstring tableDisplayName(const LoadedTable& loaded) {
    return loaded.table.name.empty() ? loaded.table.url : loaded.table.name;
}

void saveTableUrls() {
    if (gSettingsFile.empty()) return;
    WritePrivateProfileStringW(L"DifficultyTables", nullptr, nullptr, gSettingsFile.c_str());
    WritePrivateProfileStringW(L"DifficultyTables", L"Count", std::to_wstring(gTables.size()).c_str(), gSettingsFile.c_str());
    for (size_t i = 0; i < gTables.size(); ++i)
        WritePrivateProfileStringW(L"DifficultyTables", (L"Url" + std::to_wstring(i)).c_str(), gTables[i].table.url.c_str(), gSettingsFile.c_str());
}

// Registered URLs and their last download. Owned counts follow on the next song list build.
void loadDifficultyTables() {
    gTables.clear();
    if (gSettingsFile.empty()) return;
    const int count = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"DifficultyTables", L"Count", 0, gSettingsFile.c_str())), 0, 64);
    for (int i = 0; i < count; ++i) {
        std::vector<wchar_t> buffer(4096, L'\0');
        GetPrivateProfileStringW(L"DifficultyTables", (L"Url" + std::to_wstring(i)).c_str(), L"", buffer.data(), static_cast<DWORD>(buffer.size()), gSettingsFile.c_str());
        const auto url = bms::table::trimmed(buffer.data());
        if (url.empty()) continue;
        LoadedTable loaded;
        if (auto cached = bms::table::loadCache(tableCacheFolder(), url)) loaded.table = std::move(*cached);
        else { loaded.table.url = url; loaded.status = L"아직 내려받지 않음"; }
        gTables.push_back(std::move(loaded));
    }
}

// Owned counts per level, recomputed when the library or the tables change.
void rebuildTableLevels(bool force = false) {
    static const bms::Chart* lastData{}; static size_t lastSize = size_t(-1), lastTables = size_t(-1);
    if (!force && lastData == gCharts.data() && lastSize == gCharts.size() && lastTables == gTables.size()) return;
    lastData = gCharts.data(); lastSize = gCharts.size(); lastTables = gTables.size();
    std::set<std::wstring> md5, sha256;
    for (const auto& chart : gCharts) { if (!chart.md5.empty()) md5.insert(chart.md5); if (!chart.sha256.empty()) sha256.insert(chart.sha256); }
    for (auto& loaded : gTables) loaded.levels = bms::table::levelViews(loaded.table, md5, sha256);
    if (gTableView >= static_cast<int>(gTables.size())) gTableView = gTableLevel = -1;
    if (gTableView >= 0 && gTableLevel >= static_cast<int>(gTables[static_cast<size_t>(gTableView)].levels.size())) gTableLevel = -1;
}

// The level the song list shows, or nullptr outside a table level.
const bms::table::LevelView* currentTableLevel() {
    if (gTableView < 0 || gTableLevel < 0 || gTableView >= static_cast<int>(gTables.size())) return nullptr;
    const auto& levels = gTables[static_cast<size_t>(gTableView)].levels;
    return gTableLevel < static_cast<int>(levels.size()) ? &levels[static_cast<size_t>(gTableLevel)] : nullptr;
}
bool chartInTableLevel(const bms::Chart& chart) {
    const auto* level = currentTableLevel();
    return !level || bms::table::levelHas(*level, chart.md5, chart.sha256);
}
std::wstring tableLevelLabel(const LoadedTable& loaded, const bms::table::LevelView& level) {
    return loaded.table.symbol + level.level + L"  (" + std::to_wstring(level.owned) + L"/" + std::to_wstring(level.total) + L")";
}
std::wstring tableLocationText() {
    if (gTableView < 0 || gTableView >= static_cast<int>(gTables.size())) return {};
    const auto& loaded = gTables[static_cast<size_t>(gTableView)];
    std::wstring text = L"난이도표: " + tableDisplayName(loaded);
    if (const auto* level = currentTableLevel()) text += L" / " + loaded.table.symbol + level->level;
    return text;
}

// ---- background refresh ------------------------------------------------------------------------
struct TableRefresh {
    std::thread worker;
    std::atomic_bool cancel{false}, running{false};
    std::mutex mutex;
    std::vector<bms::table::FetchResult> results;
    void stop() { cancel = true; if (worker.joinable()) worker.join(); cancel = false; }
    ~TableRefresh() { stop(); }
} gTableRefresh;

bool refreshDifficultyTables() {
    if (gTableRefresh.running || gTables.empty()) return false;
    gTableRefresh.stop();
    std::vector<std::wstring> urls;
    for (auto& loaded : gTables) { urls.push_back(loaded.table.url); loaded.status = L"내려받는 중..."; }
    gTableRefresh.running = true;
    gTableRefresh.worker = std::thread([urls, window = gWindow] {
        for (const auto& url : urls) {
            if (gTableRefresh.cancel) break;
            auto result = bms::table::fetchTable(url, &gTableRefresh.cancel);
            { std::lock_guard lock(gTableRefresh.mutex); gTableRefresh.results.push_back(std::move(result)); }
            if (window) PostMessageW(window, WM_TABLES_UPDATED, 0, 0);
        }
        gTableRefresh.running = false;
        if (window) PostMessageW(window, WM_TABLES_UPDATED, 1, 0);
    });
    return true;
}

void syncTableSettings(const std::wstring& status = L"");
void rebuildSongRows(int preferred);

// UI thread: takes finished downloads, saves them and refreshes the song list and settings page.
void applyTableResults() {
    std::vector<bms::table::FetchResult> results;
    { std::lock_guard lock(gTableRefresh.mutex); results.swap(gTableRefresh.results); }
    for (auto& result : results) {
        const auto found = std::find_if(gTables.begin(), gTables.end(), [&](const LoadedTable& t) { return t.table.url == result.table.url; });
        if (found == gTables.end()) continue; // removed while downloading
        if (result.ok) {
            const bool saved = bms::table::saveCache(tableCacheFolder(), result.table, result.header, result.data);
            found->table = std::move(result.table);
            found->status = L"차트 " + std::to_wstring(found->table.entries.size()) + L"개" + (saved ? L"" : L" (저장 실패)");
        } else found->status = L"실패: " + result.error;
    }
    rebuildTableLevels(true);
    if (!gPlaying && !gLoading && !gShowingResult && gList) rebuildSongRows(gSongSelection.selectedGroupIndex());
    syncTableSettings(gTableRefresh.running ? L"" : L"새로고침을 마쳤습니다.");
}
