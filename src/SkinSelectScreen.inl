// Skin select screen (beatoraja style): screens on the left, the chosen skin's preview with arrows to
// change it, and its CUSTOMOPTION / CUSTOMFILE / CUSTOMOFFSET rows below. Opened by the LR2 select
// skin's skin select button (14). Choices are saved and applied at once, through the same files the settings
// page uses. Previews: the skin's own picture, one beside it, or the skin drawn offscreen (Lr2Thumbnail).
// Included by SettingsWindow.inl after Lr2SkinSettings.inl.
#ifdef BMS_ENABLE_SKIN_BINDINGS
// skin/lr2/Lr2Thumbnail.h is included at the top of Main.cpp (this file sits inside its anonymous namespace).

constexpr UINT WM_SKIN_PREVIEW_READY = WM_APP + 99;
constexpr int skinSelectRowSlots = 40; // option rows in skinselect.rml

bms::ui::RmlSettingsHost gSkinSelectPage;
HWND gSkinSelectWindow{};
int gSkinSelectSlot = 0;            // lr2ScreenSlots() index of the highlighted screen
int gSkinSelectRow = -1;            // focused option row, -1 for the skin line
std::wstring gSkinSelectStatus;

// One option row: which header item it edits and how.
struct SkinSelectRow { char kind{}; std::size_t item{}; std::size_t part{}; std::string label, value; };
std::vector<SkinSelectRow> gSkinSelectRowsShown;

// ---- previews --------------------------------------------------------------------------------------
std::filesystem::path skinPreviewFolder() {
    return (gSettingsFile.empty() ? executableFolder() : gSettingsFile.parent_path()) / L"skin-previews";
}
struct SkinPreviewRender {
    struct Job { std::filesystem::path skin, output; sevenmania::skin::lr2::ScreenType type{}; sevenmania::skin::lr2::SkinChoices choices; };
    std::thread worker;
    std::mutex mutex;
    std::optional<Job> pending;
    std::atomic_bool running{false}, stopping{false};
    std::set<std::wstring> failed; // outputs that could not be drawn; not retried this session
    void start(Job job) {
        { std::lock_guard lock(mutex); pending = std::move(job); }
        if (running) return; // picked up when the current one ends
        if (worker.joinable()) worker.join();
        running = true;
        worker = std::thread([this] {
            const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            for (;;) {
                std::optional<Job> next;
                { std::lock_guard lock(mutex); next.swap(pending); }
                if (!next || stopping) break;
                std::string error;
                if (!sevenmania::skin::lr2::renderThumbnail(next->skin, next->type, next->choices, next->output, error)) {
                    std::lock_guard lock(mutex); failed.insert(next->output.wstring());
                }
                if (gSkinSelectWindow) PostMessageW(gSkinSelectWindow, WM_SKIN_PREVIEW_READY, 0, 0);
            }
            if (SUCCEEDED(com)) CoUninitialize();
            running = false;
        });
    }
    bool hasFailed(const std::filesystem::path& output) { std::lock_guard lock(mutex); return failed.contains(output.wstring()); }
    void stop() { stopping = true; if (worker.joinable()) worker.join(); stopping = false; }
    ~SkinPreviewRender() { stop(); }
} gSkinPreviewRender;
std::map<std::wstring, std::filesystem::path> gSkinPreviewFound; // skin file -> picture beside it (or empty)

std::string skinSelectUtf8(const std::wstring& text) { return lr2Utf8(text); }

// The picture to show for a skin with these choices; starts drawing one when there is none yet.
void showSkinPreview(const std::filesystem::path& skin, sevenmania::skin::lr2::ScreenType type) {
    if (skin.empty()) {
        gSkinSelectPage.setImage("preview", "");
        gSkinSelectPage.setText("preview-note", "SevenMania 스킨");
        return;
    }
    auto found = gSkinPreviewFound.find(skin.wstring());
    if (found == gSkinPreviewFound.end())
        found = gSkinPreviewFound.emplace(skin.wstring(), sevenmania::skin::lr2::findThumbnail(skin)).first;
    std::filesystem::path picture = found->second;
    if (picture.empty()) {
        const auto choices = lr2SkinChoices(skin);
        const auto rendered = skinPreviewFolder() / sevenmania::skin::lr2::thumbnailCacheName(skin, choices);
        std::error_code ec;
        if (std::filesystem::is_regular_file(rendered, ec)) picture = rendered;
        else if (gSkinPreviewRender.hasFailed(rendered)) {
            gSkinSelectPage.setImage("preview", "");
            gSkinSelectPage.setText("preview-note", "미리보기를 만들지 못했습니다");
            return;
        } else {
            gSkinPreviewRender.start({skin, rendered, type, choices});
            gSkinSelectPage.setImage("preview", "");
            gSkinSelectPage.setText("preview-note", "미리보기 만드는 중...");
            return;
        }
    }
    gSkinSelectPage.setImage("preview", skinSelectUtf8(picture.wstring()) + "#preview");
    gSkinSelectPage.setText("preview-note", "");
}

// ---- the chosen skin and its rows -------------------------------------------------------------------
// Candidates for a screen: the SevenMania skin (empty path), then every found skin of that type.
std::vector<std::filesystem::path> skinSelectCandidates(sevenmania::skin::lr2::ScreenType type) {
    std::vector<std::filesystem::path> out{{}};
    for (const auto& skin : gLr2Skins) if (skin.header.type == static_cast<int>(type)) out.push_back(skin.file);
    return out;
}
const sevenmania::skin::lr2::SkinEntry* skinSelectEntry(const std::filesystem::path& file) {
    for (const auto& skin : gLr2Skins) if (sameLr2SkinFile(skin.file, file)) return &skin;
    return nullptr;
}

std::vector<SkinSelectRow> skinSelectRows(const std::filesystem::path& file) {
    std::vector<SkinSelectRow> rows;
    if (file.empty()) return rows;
    const auto document = lr2SkinHeader(file);
    if (!document) return rows;
    const auto& header = document->header;
    for (std::size_t i = 0; i < header.options.size(); ++i) {
        const auto& option = header.options[i];
        if (option.choices.empty()) continue;
        const auto selected = std::min<std::size_t>(static_cast<std::size_t>(std::max(0, option.selected)), option.choices.size() - 1);
        rows.push_back({'o', i, 0, option.name, option.choices[selected]});
    }
    for (std::size_t i = 0; i < header.files.size(); ++i) {
        const auto& entry = header.files[i];
        const auto stems = sevenmania::skin::lr2::listWildcardChoices(document->root, file, entry.pattern);
        std::string value = stems.empty() ? std::string("(파일 없음)") : stems.front();
        for (const auto& stem : stems) if (stem == entry.selected) { value = stem; break; }
        if (entry.selected.empty()) for (const auto& stem : stems) if (stem == entry.defaultName) { value = stem; break; }
        rows.push_back({'f', i, 0, entry.name, value});
    }
    for (std::size_t i = 0; i < header.offsets.size(); ++i)
        for (std::size_t part = 0; part < lr2OffsetParts.size(); ++part)
            if (header.offsets[i].enabled[part])
                rows.push_back({'k', i, part, header.offsets[i].name + " " + lr2OffsetParts[part].label, std::to_string(header.offsets[i].values[part])});
    return rows;
}

void syncSkinSelect() {
    if (!gSkinSelectPage.window()) return;
    const auto slots = lr2ScreenSlots();
    gSkinSelectSlot = std::clamp(gSkinSelectSlot, 0, static_cast<int>(slots.size()) - 1);
    for (std::size_t i = 0; i < slots.size(); ++i) gSkinSelectPage.setClass("screen-" + std::to_string(i), "selected", static_cast<int>(i) == gSkinSelectSlot);
    const auto& slot = slots[static_cast<std::size_t>(gSkinSelectSlot)];
    const auto* entry = skinSelectEntry(*slot.file);
    if (slot.file->empty()) {
        gSkinSelectPage.setText("skin-name", "SevenMania 스킨 (LR2 스킨 사용 안 함)");
        gSkinSelectPage.setText("skin-author", "");
    } else {
        gSkinSelectPage.setText("skin-name", entry ? entry->header.name : skinSelectUtf8(slot.file->filename().wstring()));
        gSkinSelectPage.setText("skin-author", entry ? entry->header.author : skinSelectUtf8(slot.file->wstring()));
    }
    showSkinPreview(*slot.file, slot.type);
    {
        // The skin line: its name and where it sits among the screen's skins (the SevenMania skin first).
        const auto candidates = skinSelectCandidates(slot.type);
        std::size_t at = 0;
        for (std::size_t i = 0; i < candidates.size(); ++i) if (!slot.file->empty() && sameLr2SkinFile(candidates[i], *slot.file)) at = i;
        const std::string name = slot.file->empty() ? std::string("SevenMania 스킨") :
            entry ? entry->header.name : skinSelectUtf8(slot.file->stem().wstring());
        gSkinSelectPage.setText("skin-value", name + "  (" + std::to_string(at + 1) + "/" + std::to_string(candidates.size()) + ")");
        gSkinSelectPage.setClass("skin-row", "focused", gSkinSelectRow < 0);
    }
    gSkinSelectRowsShown = skinSelectRows(*slot.file);
    gSkinSelectRow = std::min(gSkinSelectRow, static_cast<int>(gSkinSelectRowsShown.size()) - 1);
    for (int i = 0; i < skinSelectRowSlots; ++i) {
        const auto id = "option-" + std::to_string(i);
        const bool shown = i < static_cast<int>(gSkinSelectRowsShown.size());
        gSkinSelectPage.setVisible(id, shown);
        if (!shown) continue;
        gSkinSelectPage.setText(id + "-label", gSkinSelectRowsShown[static_cast<std::size_t>(i)].label);
        gSkinSelectPage.setText(id + "-value", gSkinSelectRowsShown[static_cast<std::size_t>(i)].value);
        gSkinSelectPage.setClass(id, "focused", i == gSkinSelectRow);
    }
    // Keep the focused row on screen when the arrows move past the visible part of the list.
    if (gSkinSelectRow >= 0) gSkinSelectPage.scrollIntoView("option-" + std::to_string(gSkinSelectRow));
    else gSkinSelectPage.scrollTop("options");
    const bool locked = gLr2CommandLine || gPlaying || gLoading || gShowingResult;
    gSkinSelectPage.setText("status", skinSelectUtf8(!gSkinSelectStatus.empty() ? gSkinSelectStatus :
        gLr2CommandLine ? L"명령줄로 지정한 LR2 스킨을 쓰는 중이라 바꿀 수 없습니다." : locked ? L"곡 선택 화면에서 바꿀 수 있습니다." :
        gLr2Skins.empty() ? L"Theme 폴더에서 LR2 스킨을 찾지 못했습니다." : L""));
}

bool skinSelectLocked() { return gLr2CommandLine || gPlaying || gLoading || gShowingResult; }

// The screen's skin, one step along its candidates.
void stepSkinSelectSkin(int direction) {
    if (skinSelectLocked()) { syncSkinSelect(); return; }
    const auto slots = lr2ScreenSlots();
    const auto& slot = slots[static_cast<std::size_t>(gSkinSelectSlot)];
    const auto candidates = skinSelectCandidates(slot.type);
    std::size_t at = 0;
    for (std::size_t i = 0; i < candidates.size(); ++i) if (!slot.file->empty() && sameLr2SkinFile(candidates[i], *slot.file)) at = i;
    *slot.file = candidates[(at + candidates.size() + static_cast<std::size_t>(direction > 0 ? 1 : candidates.size() - 1)) % candidates.size()];
    saveLr2SkinSettings();
    applyLr2SkinSettings();
    gSkinSelectRow = -1;
    syncSkinSelect();
    syncLr2SkinSettings(); // the settings page, when open, shows the same choice
}

// One option row, one step: the next choice, the next file, or the offset by one.
void stepSkinSelectRow(int row, int direction) {
    if (skinSelectLocked() || row < 0 || row >= static_cast<int>(gSkinSelectRowsShown.size())) { syncSkinSelect(); return; }
    const auto slots = lr2ScreenSlots();
    const auto& file = *slots[static_cast<std::size_t>(gSkinSelectSlot)].file;
    const auto document = file.empty() ? std::nullopt : lr2SkinHeader(file);
    if (!document) return;
    const auto& shown = gSkinSelectRowsShown[static_cast<std::size_t>(row)];
    auto choices = lr2SkinChoices(file);
    const auto& header = document->header;
    const auto cycle = [direction](std::size_t current, std::size_t count) {
        return (current + count + static_cast<std::size_t>(direction > 0 ? 1 : count - 1)) % count;
    };
    if (shown.kind == 'o' && shown.item < header.options.size()) {
        const auto& option = header.options[shown.item];
        const auto current = static_cast<std::size_t>(std::max(0, option.selected));
        choices.options[option.name] = option.choices[cycle(std::min(current, option.choices.size() - 1), option.choices.size())];
    } else if (shown.kind == 'f' && shown.item < header.files.size()) {
        const auto stems = sevenmania::skin::lr2::listWildcardChoices(document->root, file, header.files[shown.item].pattern);
        if (stems.empty()) return;
        std::size_t current = 0;
        for (std::size_t i = 0; i < stems.size(); ++i) if (stems[i] == shown.value) current = i;
        choices.files[header.files[shown.item].name] = stems[cycle(current, stems.size())];
    } else if (shown.kind == 'k' && shown.item < header.offsets.size() && shown.part < lr2OffsetParts.size()) {
        const auto& offset = header.offsets[shown.item];
        auto& values = choices.offsets.try_emplace(offset.name, offset.values).first->second;
        const int range = lr2OffsetParts[shown.part].range;
        values[shown.part] = std::clamp(values[shown.part] + (direction > 0 ? 1 : -1), -range, range);
    } else return;
    if (!sevenmania::skin::lr2::saveSkinChoices(lr2ChoiceStore(), file, choices)) gSkinSelectStatus = L"스킨 옵션을 저장하지 못했습니다.";
    else { gSkinSelectStatus.clear(); applyLr2SkinSettings(); }
    gSkinSelectRow = row;
    syncSkinSelect();
    syncLr2SkinSettings();
}

void closeSkinSelectScreen() {
    if (gSkinSelectWindow) DestroyWindow(gSkinSelectWindow);
}

void skinSelectChanged(const std::string& id, const std::string&) {
    gSkinSelectStatus.clear();
    if (id.starts_with("screen-")) {
        try { gSkinSelectSlot = std::stoi(id.substr(7)); } catch (...) {}
        gSkinSelectRow = -1;
        syncSkinSelect();
    } else if (id == "skin-prev" || id == "skin-next" || id == "skin-dec" || id == "skin-inc")
        stepSkinSelectSkin(id == "skin-next" || id == "skin-inc" ? 1 : -1);
    else if (id == "skin-row") { gSkinSelectRow = -1; syncSkinSelect(); }
    else if (id.starts_with("option-")) {
        int row = -1;
        try { row = std::stoi(id.substr(7)); } catch (...) { return; }
        if (id.ends_with("-dec")) stepSkinSelectRow(row, -1);
        else if (id.ends_with("-inc")) stepSkinSelectRow(row, 1);
        else { gSkinSelectRow = row; syncSkinSelect(); }
    }
}

// Keys the embedded page forwards: arrows move and change, PageUp/PageDown change the screen.
void skinSelectKey(WPARAM key) {
    const int rows = static_cast<int>(gSkinSelectRowsShown.size());
    const auto slots = lr2ScreenSlots();
    switch (key) {
    case VK_UP: gSkinSelectRow = std::max(-1, gSkinSelectRow - 1); syncSkinSelect(); break;
    case VK_DOWN: gSkinSelectRow = std::min(rows - 1, gSkinSelectRow + 1); syncSkinSelect(); break;
    case VK_LEFT: case VK_RIGHT:
        if (gSkinSelectRow < 0) stepSkinSelectSkin(key == VK_RIGHT ? 1 : -1);
        else stepSkinSelectRow(gSkinSelectRow, key == VK_RIGHT ? 1 : -1);
        break;
    case VK_PRIOR: case VK_NEXT:
        gSkinSelectSlot = (gSkinSelectSlot + static_cast<int>(slots.size()) + (key == VK_NEXT ? 1 : -1)) % static_cast<int>(slots.size());
        gSkinSelectRow = -1; syncSkinSelect(); break;
    }
}

LRESULT CALLBACK skinSelectWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_SIZE:
        if (gSkinSelectPage.window()) MoveWindow(gSkinSelectPage.window(), 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        return 0;
    case WM_KEYDOWN: skinSelectKey(wParam); return 0;
    case WM_SKIN_PREVIEW_READY: syncSkinSelect(); return 0;
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY:
        gSkinSelectPage.close();
        gSkinSelectWindow = nullptr;
        if (IsWindow(gWindow)) { SetForegroundWindow(gWindow); SetFocus(gList ? gList : gWindow); }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

// The popup or the RmlUi page inside it: the main loop hands their messages straight to them.
bool skinSelectOwns(HWND window) {
    return gSkinSelectWindow && window && (window == gSkinSelectWindow || IsChild(gSkinSelectWindow, window));
}

// A popup over the game window, the size of its client area.
void openSkinSelectScreen() {
    if (gPlaying || gLoading || gShowingResult) return;
    if (gSkinSelectWindow) { SetForegroundWindow(gSkinSelectWindow); return; }
    ensureLr2SkinsScanned();
    static bool registered = false;
    if (!registered) {
        WNDCLASSW type{};
        type.lpfnWndProc = skinSelectWindowProc; type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"BmsPlayerSkinSelectWindow"; type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        registered = RegisterClassW(&type) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    RECT client{};
    GetClientRect(gWindow, &client);
    POINT origin{0, 0};
    ClientToScreen(gWindow, &origin);
    gSkinSelectWindow = CreateWindowExW(0, L"BmsPlayerSkinSelectWindow", L"SKIN SELECT", WS_POPUP | WS_CLIPCHILDREN,
        origin.x, origin.y, client.right, client.bottom, gWindow, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!gSkinSelectWindow) return;
    const auto document = (gRmlSettingsAssetRoot.empty() ? executableFolder() / L"Assets/UI" : gRmlSettingsAssetRoot) / L"skinselect.rml";
    if (!gSkinSelectPage.openEmbedded(gSkinSelectWindow, document, skinSelectChanged, {}, true)) {
        const auto error = gSkinSelectPage.error();
        DestroyWindow(gSkinSelectWindow);
        MessageBoxW(gWindow, (L"스킨 선택 화면을 열지 못했습니다.\n" + error).c_str(), L"SevenMania", MB_ICONWARNING);
        return;
    }
    gSkinSelectPage.forwardKeys({VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_PRIOR, VK_NEXT});
    // Open on the screen the player is looking at: the select skin.
    gSkinSelectSlot = 0; gSkinSelectRow = -1; gSkinSelectStatus.clear();
    ShowWindow(gSkinSelectWindow, SW_SHOW);
    SetFocus(gSkinSelectPage.window());
    syncSkinSelect();
}
#else
void openSkinSelectScreen() {}
bool skinSelectOwns(HWND) { return false; }
#endif
