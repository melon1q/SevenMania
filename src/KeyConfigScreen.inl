// Key config screen: black, key modes on the left (like the skin select screen) and one row per lane with
// its keyboard key and controller button in red. Click a red text (or press Enter on a row), then press a
// keyboard key or a controller button: the lane is mapped to it and saved at once. Opened by the LR2 select
// skin's KEY CONFIG button (13). Keyboard keys are per mode (KeyProfiles); controller buttons are one set
// shared by every mode (controller::Config), so a button changed here shows in the other modes too.
// Included by SettingsWindow.inl after SkinSelectScreen.inl.

constexpr int keyConfigRowSlots = 16; // slot rows in keyconfig.rml
constexpr UINT_PTR keyConfigPollTimer = 1;

bms::ui::RmlSettingsHost gKeyConfigPage;
HWND gKeyConfigWindow{};
int gKeyConfigMode = 1;        // KeyProfiles index: 0 5K, 1 7K, 2 9K, 3 10K, 4 14K
int gKeyConfigRow = 0;         // focused row (screen order)
int gKeyConfigListening = -1;  // row waiting for a key or button, or -1
bool gKeyConfigAwaitRelease = false;
bool gKeyConfigConnected = false;
std::wstring gKeyConfigStatus;
bms::controller::Sample gKeyConfigBaseline; // controller state when listening began: only new presses count

std::string keyConfigUtf8(const std::wstring& text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(0, size)), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

// The controller slot (DP numbering: 0 1P scratch, 1-7 1P keys, 8 2P scratch, 9-15 2P keys) a profile slot
// uses; see controllerSlotLane. -1 when the mode has none there.
int keyConfigControllerSlot(int mode, int slot) {
    if (mode == 2) return slot < 7 ? slot + 1 : slot < 9 ? slot + 2 : -1; // PMS: 1P keys 1-7, then 2P keys 1-2
    if (mode == 3) return slot < 6 ? slot : slot < 12 ? slot + 2 : -1;    // 10K: 2P starts at controller slot 8
    return slot < bms::KeyProfiles::counts[mode] ? slot : -1;
}
int* keyConfigControllerField(bms::controller::Config& config, int controllerSlot) {
    if (controllerSlot == 0) return &config.scratchCw;
    if (controllerSlot >= 1 && controllerSlot <= 7) return &config.keys[static_cast<std::size_t>(controllerSlot - 1)];
    if (controllerSlot == 8) return &config.scratch2pCw;
    if (controllerSlot >= 9 && controllerSlot <= 15) return &config.keys2p[static_cast<std::size_t>(controllerSlot - 9)];
    return nullptr;
}
std::wstring keyConfigControllerText(int mode, int slot) {
    auto& config = gController.config;
    const int controllerSlot = keyConfigControllerSlot(mode, slot);
    const int* field = keyConfigControllerField(config, controllerSlot);
    if (!field) return L"-";
    std::wstring text;
    if (*field >= 0) text = L"BUTTON " + std::to_wstring(*field + 1);
    const int other = controllerSlot == 0 ? config.scratchCcw : controllerSlot == 8 ? config.scratch2pCcw : -1;
    if (other >= 0) text += (text.empty() ? L"BUTTON " : L", ") + std::to_wstring(other + 1);
    if (controllerSlot == 0 && config.axis >= 0 && config.mode > 0) {
        constexpr const wchar_t* axes[]{L"X", L"Y", L"Z", L"Rx", L"Ry", L"Rz", L"S1", L"S2"};
        text += (text.empty() ? L"" : L" + ") + std::wstring(L"AXIS ") + axes[std::clamp(config.axis, 0, 7)];
    }
    return text.empty() ? L"---" : text;
}

void syncKeyConfig() {
    if (!gKeyConfigPage.window()) return;
    for (int mode = 0; mode < 5; ++mode) gKeyConfigPage.setClass("mode-" + std::to_string(mode), "selected", mode == gKeyConfigMode);
    const int count = bms::KeyProfiles::counts[gKeyConfigMode];
    gKeyConfigRow = std::clamp(gKeyConfigRow, 0, count - 1);
    for (int row = 0; row < keyConfigRowSlots; ++row) {
        const auto id = "slot-" + std::to_string(row);
        const bool shown = row < count;
        gKeyConfigPage.setVisible(id, shown);
        if (!shown) continue;
        const int slot = bms::keyProfileCaptureSlot(gKeyConfigMode, row);
        const bool listening = row == gKeyConfigListening;
        const UINT key = gKeyProfiles.keys[static_cast<std::size_t>(gKeyConfigMode)][static_cast<std::size_t>(slot)];
        const auto pad = keyConfigControllerText(gKeyConfigMode, slot);
        gKeyConfigPage.setText(id + "-label", keyConfigUtf8(bms::keyProfileSlotName(gKeyConfigMode, slot)));
        gKeyConfigPage.setText(id + "-key", keyConfigUtf8(listening ? L"입력 대기" : keyName(key)));
        gKeyConfigPage.setText(id + "-pad", keyConfigUtf8(listening ? L"..." : pad));
        gKeyConfigPage.setClass(id + "-key", "listening", listening);
        gKeyConfigPage.setClass(id + "-pad", "listening", listening);
        gKeyConfigPage.setClass(id + "-pad", "unset", pad == L"---" || pad == L"-");
        gKeyConfigPage.setClass(id, "focused", row == gKeyConfigRow);
    }
    gKeyConfigPage.scrollIntoView("slot-" + std::to_string(gKeyConfigRow));
    std::wstring device = gKeyConfigConnected ? L"컨트롤러: 연결됨" : L"컨트롤러: 연결된 장치 없음 (키보드만 지정할 수 있습니다)";
    if (gKeyConfigConnected && !gController.config.enabled) device += L" · 버튼을 지정하면 컨트롤러 입력이 켜집니다";
    gKeyConfigPage.setText("device", keyConfigUtf8(device));
    gKeyConfigPage.setText("status", keyConfigUtf8(gKeyConfigStatus));
}

// Keys go to the popup itself while listening, so the page never turns them into navigation.
void keyConfigListen(int row) {
    if (row < 0 || row >= bms::KeyProfiles::counts[gKeyConfigMode]) return;
    gKeyConfigRow = row;
    gKeyConfigListening = row;
    gKeyConfigAwaitRelease = false;
    gKeyConfigBaseline = gController.poll(false, [](const bms::controller::State&) {}, true);
    gKeyConfigStatus.clear();
    SetFocus(gKeyConfigWindow);
    syncKeyConfig();
}
void keyConfigStopListening(const std::wstring& status) {
    gKeyConfigListening = -1;
    gKeyConfigStatus = status;
    if (gKeyConfigPage.window()) SetFocus(gKeyConfigPage.window());
    syncKeyConfig();
}

void keyConfigAssignKey(UINT key) {
    if (gKeyConfigListening < 0) return;
    if (reservedBinding(key)) {
        gKeyConfigStatus = L"Enter, Esc, F1/F2/F5, PageUp/PageDown, Tab, Alt, Windows 키는 지정할 수 없습니다.";
        syncKeyConfig();
        return;
    }
    const int count = bms::KeyProfiles::counts[gKeyConfigMode];
    const int slot = bms::keyProfileCaptureSlot(gKeyConfigMode, gKeyConfigListening);
    auto& keys = gKeyProfiles.keys[static_cast<std::size_t>(gKeyConfigMode)];
    // A key another lane of this mode had moves there in exchange, so every lane keeps a distinct key.
    for (int other = 0; other < count; ++other)
        if (other != slot && keys[static_cast<std::size_t>(other)] == key) keys[static_cast<std::size_t>(other)] = keys[static_cast<std::size_t>(slot)];
    keys[static_cast<std::size_t>(slot)] = key;
    bool saved = true;
    if (gKeyConfigMode == 1) {
        std::array<UINT, 8> seven{};
        std::copy_n(keys.begin(), 8, seven.begin());
        saved = applyLaneKeys(seven); // also keeps the older [KeyBindings] section in step
    } else gKeyProfiles.save(gSettingsFile, gKeyConfigMode);
    keyConfigStopListening(saved ? L"" : L"키 배치를 저장하지 못했습니다.");
}

void keyConfigAssignController(int button, int axis) {
    if (gKeyConfigListening < 0) return;
    auto& config = gController.config;
    const int controllerSlot = keyConfigControllerSlot(gKeyConfigMode, bms::keyProfileCaptureSlot(gKeyConfigMode, gKeyConfigListening));
    int* field = keyConfigControllerField(config, controllerSlot);
    if (!field) { keyConfigStopListening(L"이 레인에는 컨트롤러 버튼을 지정할 수 없습니다."); return; }
    if (axis >= 0) {
        config.axis = axis;
        if (config.mode == 0) config.mode = 1; // a turntable axis
    } else {
        // A button another lane had moves there in exchange.
        std::array<int*, 19> fields{&config.scratchCw, &config.scratchCcw, &config.scratch2pCw, &config.scratch2pCcw, &config.menu};
        for (std::size_t i = 0; i < 7; ++i) { fields[5 + i] = &config.keys[i]; fields[12 + i] = &config.keys2p[i]; }
        for (int* other : fields) if (other != field && *other == button) *other = *field;
        *field = button;
    }
    config.enabled = true;
    const bool saved = gController.save(gSettingsFile.wstring());
    keyConfigStopListening(saved ? L"" : L"컨트롤러 설정을 저장하지 못했습니다.");
}

// Polled while the screen is open: the device's connection, and a new button press (or a turned
// turntable for the 1P scratch) while listening.
void keyConfigPoll() {
    const auto sample = gController.poll(false, [](const bms::controller::State&) {}, true);
    if (sample.connected != gKeyConfigConnected) { gKeyConfigConnected = sample.connected; syncKeyConfig(); }
    if (gKeyConfigListening < 0 || !sample.connected) return;
    if (!gKeyConfigBaseline.connected) { gKeyConfigBaseline = sample; return; }
    for (int button = 0; button < 128; ++button)
        if (sample.buttons[static_cast<std::size_t>(button)] && !gKeyConfigBaseline.buttons[static_cast<std::size_t>(button)]) {
            keyConfigAssignController(button, -1);
            return;
        }
    for (int button = 0; button < 128; ++button) // released buttons may be pressed again
        if (!sample.buttons[static_cast<std::size_t>(button)]) gKeyConfigBaseline.buttons[static_cast<std::size_t>(button)] = false;
    const int controllerSlot = keyConfigControllerSlot(gKeyConfigMode, bms::keyProfileCaptureSlot(gKeyConfigMode, gKeyConfigListening));
    if (controllerSlot == 0)
        for (int axis = 0; axis < 8; ++axis)
            if (std::abs(sample.axes[static_cast<std::size_t>(axis)] - gKeyConfigBaseline.axes[static_cast<std::size_t>(axis)]) > 12000) {
                keyConfigAssignController(-1, axis);
                return;
            }
}

void keyConfigSetMode(int mode) {
    gKeyConfigMode = std::clamp(mode, 0, 4);
    gKeyConfigRow = 0;
    gKeyConfigListening = -1;
    gKeyConfigStatus.clear();
    syncKeyConfig();
}

void keyConfigChanged(const std::string& id, const std::string&) {
    if (id.starts_with("mode-")) {
        try { keyConfigSetMode(std::stoi(id.substr(5))); } catch (...) {}
    } else if (id.starts_with("slot-")) {
        int row = -1;
        try { row = std::stoi(id.substr(5)); } catch (...) { return; }
        if (id.ends_with("-key") || id.ends_with("-pad")) keyConfigListen(row);
        else { gKeyConfigRow = row; gKeyConfigListening = -1; syncKeyConfig(); }
    }
}

// Keys the page forwards (not listening): ↑↓ move, Enter maps, PageUp/PageDown change the mode.
void keyConfigNavigate(WPARAM key) {
    constexpr int order[]{1, 4, 0, 3, 2}; // modes as listed on the left
    int at = 0;
    for (int i = 0; i < 5; ++i) if (order[i] == gKeyConfigMode) at = i;
    switch (key) {
    case VK_UP: gKeyConfigRow = std::max(0, gKeyConfigRow - 1); syncKeyConfig(); break;
    case VK_DOWN: gKeyConfigRow = std::min(bms::KeyProfiles::counts[gKeyConfigMode] - 1, gKeyConfigRow + 1); syncKeyConfig(); break;
    case VK_RETURN: keyConfigListen(gKeyConfigRow); break;
    case VK_PRIOR: case VK_NEXT: keyConfigSetMode(order[(at + 5 + (key == VK_NEXT ? 1 : -1)) % 5]); break;
    }
}

LRESULT CALLBACK keyConfigWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_SIZE:
        if (gKeyConfigPage.window()) MoveWindow(gKeyConfigPage.window(), 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        return 0;
    case WM_KEYDOWN:
        if (gKeyConfigListening < 0) { if (wParam == VK_ESCAPE) DestroyWindow(window); else keyConfigNavigate(wParam); return 0; }
        if (wParam == VK_ESCAPE) { keyConfigStopListening(L"취소했습니다."); return 0; }
        if ((lParam & (1LL << 30)) == 0) keyConfigAssignKey(normalizedKey(wParam, lParam));
        return 0;
    case WM_SYSKEYDOWN:
        if (gKeyConfigListening >= 0) { gKeyConfigStatus = L"Alt 키와 Alt 조합은 지정할 수 없습니다."; syncKeyConfig(); return 0; }
        break;
    case WM_TIMER:
        if (wParam == keyConfigPollTimer) keyConfigPoll();
        return 0;
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY:
        KillTimer(window, keyConfigPollTimer);
        gKeyConfigPage.close();
        gKeyConfigWindow = nullptr;
        gKeyConfigListening = -1;
        gController.reconnect(gWindow); // back to the game window, which owns the device in play
        SetTimer(gWindow, ID_CONTROLLER_POLL, gController.config.enabled ? 4 : 1000, nullptr);
        gControllerConfiguring = false;
        if (IsWindow(gWindow)) { SetForegroundWindow(gWindow); SetFocus(gList ? gList : gWindow); }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool keyConfigCapturesKey(UINT message) {
    return gKeyConfigListening >= 0 && (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN ||
        message == WM_SYSKEYUP || message == WM_CHAR || message == WM_SYSCHAR || message == WM_DEADCHAR);
}
// The popup or the RmlUi page inside it: the main loop hands their messages straight to them.
bool keyConfigOwns(HWND window) {
    return gKeyConfigWindow && window && (window == gKeyConfigWindow || IsChild(gKeyConfigWindow, window));
}

// A popup over the game window, the size of its client area. Without RmlUi the settings window opens instead.
void openKeyConfigScreen() {
    if (gPlaying || gLoading || gShowingResult) return;
    if (gKeyConfigWindow) { SetForegroundWindow(gKeyConfigWindow); return; }
    static bool registered = false;
    if (!registered) {
        WNDCLASSW type{};
        type.lpfnWndProc = keyConfigWindowProc; type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"BmsPlayerKeyConfigWindow"; type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        registered = RegisterClassW(&type) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    RECT client{};
    GetClientRect(gWindow, &client);
    POINT origin{0, 0};
    ClientToScreen(gWindow, &origin);
    gKeyConfigWindow = CreateWindowExW(0, L"BmsPlayerKeyConfigWindow", L"KEY CONFIG", WS_POPUP | WS_CLIPCHILDREN,
        origin.x, origin.y, client.right, client.bottom, gWindow, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!gKeyConfigWindow) return;
    const auto document = (gRmlSettingsAssetRoot.empty() ? executableFolder() / L"Assets/UI" : gRmlSettingsAssetRoot) / L"keyconfig.rml";
    if (!gKeyConfigPage.openEmbedded(gKeyConfigWindow, document, keyConfigChanged, {}, true)) {
        DestroyWindow(gKeyConfigWindow);
        openSettings();
        return;
    }
    gKeyConfigPage.forwardKeys({VK_UP, VK_DOWN, VK_RETURN, VK_PRIOR, VK_NEXT});
    // The controller is read here (the game's own polling pauses), owned by this window while it is in front.
    gControllerConfiguring = true;
    gController.initialize(gKeyConfigWindow);
    gController.reconnect(gKeyConfigWindow);
    gKeyConfigConnected = false;
    SetTimer(gKeyConfigWindow, keyConfigPollTimer, 16, nullptr);
    gKeyConfigMode = std::clamp(gBindingMode, 0, 4); gKeyConfigRow = 0; gKeyConfigListening = -1; gKeyConfigStatus.clear();
    ShowWindow(gKeyConfigWindow, SW_SHOW);
    SetFocus(gKeyConfigPage.window());
    syncKeyConfig();
}
