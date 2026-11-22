// Difficulty tables on the settings library page: add a URL, remove one, refresh all.
// Included by SettingsWindow.inl before SettingsRmlPage.inl. Nothing downloads until Refresh.

std::wstring tableSettingsWide(const std::string& utf8) { return bms::table::decodeText(utf8); }

void syncTableSettings(const std::wstring& status) {
    std::vector<std::pair<std::string,std::string>> options;
    for (size_t i = 0; i < gTables.size(); ++i) {
        const auto& loaded = gTables[i];
        std::wstring label = tableDisplayName(loaded);
        if (!loaded.table.name.empty()) label += L"  · " + std::to_wstring(loaded.table.entries.size()) + L"개";
        if (!loaded.status.empty()) label += L"  · " + loaded.status;
        options.emplace_back(std::to_string(i), bms::table::utf8(label));
    }
    if (options.empty()) options.emplace_back("none", "(등록된 난이도표 없음)");
    gTableSelection = std::clamp(gTableSelection, 0, std::max(0, static_cast<int>(gTables.size()) - 1));
    gRmlSettingsPage.setOptions("table-list", options);
    gRmlSettingsPage.setValue("table-list", gTables.empty() ? "none" : std::to_string(gTableSelection));
    gRmlSettingsPage.setEnabled("table-remove", !gTables.empty() && !gTableRefresh.running);
    gRmlSettingsPage.setEnabled("table-refresh", !gTables.empty() && !gTableRefresh.running);
    const std::wstring state = !status.empty() ? status : gTableRefresh.running ? L"난이도표를 내려받는 중입니다..." :
        gTables.empty() ? L"난이도표 주소(bmstable 페이지 또는 header.json)를 넣고 추가를 누르세요." :
        L"새로고침을 눌러야 인터넷에서 최신 표를 받습니다. 곡 선택 화면의 맨 위 폴더에 표가 나타납니다.";
    gRmlSettingsPage.setText("table-status", bms::table::utf8(state));
}

// True when the element belongs to this section.
bool handleTableSetting(const std::string& id, const std::string& value) {
    if (!id.starts_with("table-")) return false;
    if (id == "table-url") return true; // typing; read when Add is pressed
    if (id == "table-list") { try { gTableSelection = std::stoi(value); } catch (...) {} syncTableSettings(); return true; }
    if (gTableRefresh.running) { syncTableSettings(L"내려받는 중에는 바꿀 수 없습니다."); return true; }
    if (id == "table-add") {
        const auto url = bms::table::trimmed(tableSettingsWide(gRmlSettingsPage.value("table-url")));
        const auto lower = bms::table::lowerAscii(url);
        if (!lower.starts_with(L"http://") && !lower.starts_with(L"https://")) { syncTableSettings(L"http:// 또는 https://로 시작하는 주소를 넣으세요."); return true; }
        if (std::any_of(gTables.begin(), gTables.end(), [&](const LoadedTable& t) { return t.table.url == url; })) { syncTableSettings(L"이미 등록된 주소입니다."); return true; }
        if (gTables.size() >= 64) { syncTableSettings(L"난이도표는 64개까지 등록할 수 있습니다."); return true; }
        LoadedTable loaded; loaded.table.url = url; loaded.status = L"아직 내려받지 않음";
        gTables.push_back(std::move(loaded));
        gTableSelection = static_cast<int>(gTables.size()) - 1;
        saveTableUrls();
        gRmlSettingsPage.setValue("table-url", "");
        syncTableSettings(L"추가했습니다. 새로고침을 누르면 내려받습니다.");
        return true;
    }
    if (id == "table-remove" && gTableSelection >= 0 && gTableSelection < static_cast<int>(gTables.size())) {
        bms::table::removeCache(tableCacheFolder(), gTables[static_cast<size_t>(gTableSelection)].table.url);
        gTables.erase(gTables.begin() + gTableSelection);
        gTableView = gTableLevel = -1; // the song list may have been inside it
        saveTableUrls();
        rebuildTableLevels(true);
        if (!gPlaying && !gLoading && !gShowingResult && gList) rebuildSongRows(gSongSelection.selectedGroupIndex());
        syncTableSettings(L"제거했습니다.");
        return true;
    }
    if (id == "table-refresh") { syncTableSettings(refreshDifficultyTables() ? L"난이도표를 내려받는 중입니다..." : L"새로고침할 표가 없습니다."); return true; }
    return true;
}
