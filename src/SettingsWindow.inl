LRESULT CALLBACK settingsBodyProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_COMMAND: case WM_NOTIFY: case WM_HSCROLL: case WM_DRAWITEM: case WM_MEASUREITEM:
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN: case WM_MOUSEWHEEL:
        return SendMessageW(GetParent(window), message, wParam, lParam);
    case WM_ERASEBKGND: {
        RECT client{}; GetClientRect(window, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        return 1;
    }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void scrollSettings(HWND window, int position) {
    if (!window || !gSettingsBody) return;
    RECT client{}; GetClientRect(window, &client);
    const int viewportHeight = std::max(1, static_cast<int>(client.bottom) - 174);
    MoveWindow(gSettingsBody, 0, 96, client.right, viewportHeight, TRUE);
    const int pageHeight = gSettingsCategory == 0 ? SETTINGS_CONTENT_HEIGHT :
        gSettingsCategory == 2 ? 700 : gSettingsCategory == 3 ? 820 : 530;
    const int contentHeight = pageHeight - 96;
    gSettingsScroll = std::clamp(position, 0, std::max(0, contentHeight - viewportHeight));
    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
    info.nMin = 0; info.nMax = contentHeight - 1; info.nPage = viewportHeight; info.nPos = gSettingsScroll;
    SetScrollInfo(window, SB_VERT, &info, TRUE);
    // Absolute coordinates avoid accumulating offsets when switching pages.
    // The child viewport clips content below the fixed navigation and footer.
    HDWP batch = BeginDeferWindowPos(static_cast<int>(gSettingsControlRects.size()));
    for (const auto& [control, bounds] : gSettingsControlRects) {
        if (batch) batch = DeferWindowPos(batch, control, nullptr, bounds.left, bounds.top - gSettingsScroll,
            bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
        else MoveWindow(control, bounds.left, bounds.top - gSettingsScroll, bounds.right - bounds.left, bounds.bottom - bounds.top, TRUE);
    }
    if (batch) EndDeferWindowPos(batch);
    if (gSettingsStatus) MoveWindow(gSettingsStatus, 24, client.bottom - 66, std::max(100, static_cast<int>(client.right) - 230), 48, TRUE);
    HWND close = GetDlgItem(window, ID_CLOSE_SETTINGS);
    if (close) MoveWindow(close, client.right - 194, client.bottom - 54, 170, 40, TRUE);
    RedrawWindow(gSettingsBody, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    InvalidateRect(window, nullptr, TRUE);
}

bool showRmlSettingsPage();
void hideRmlSettingsPage();
void refreshAllNativeSettings();
void selectSettingsCategory(int category) {
    if (gCapturingBindings) cancelKeyBindingCapture();
    gSettingsCategory = std::clamp(category, 0, 6);
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(gSettingsCategory==5)gLr2SkinsScanned=false;
#endif
    hideSettingsPreviewOverlay();
    // Prepare the migrated page before replacing the visible native surface.
    if (showRmlSettingsPage()) return;
    hideRmlSettingsPage();
    ShowWindow(gSettingsBody, SW_SHOW);
    for (const auto& [control, group] : gSettingsControlCategory)
        ShowWindow(control, (group < 0 || group == gSettingsCategory) &&
            (control != gDirectCompositionChoice || gCompositionEnabled) ? SW_SHOW : SW_HIDE);
    for (size_t i = 0; i < gCategoryButtons.size(); ++i)
        if (gCategoryButtons[i]) {
            SendMessageW(gCategoryButtons[i], BM_SETCHECK, static_cast<int>(i) == gSettingsCategory ? BST_CHECKED : BST_UNCHECKED, 0);
            InvalidateRect(gCategoryButtons[i], nullptr, TRUE);
            UpdateWindow(gCategoryButtons[i]);
        }
    scrollSettings(gSettingsWindow, 0);
    refreshAllNativeSettings();
    if (gSettingsWindow) InvalidateRect(gSettingsWindow, nullptr, TRUE);
}

LRESULT CALLBACK settingsControlSubclass(HWND control, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR ownerData) {
    HWND owner = reinterpret_cast<HWND>(ownerData);
    if (message == WM_MOUSEWHEEL) return SendMessageW(owner, message, wParam, lParam);
    if (message == WM_SETFOCUS) {
        // Hidden controls from another settings page must never change the
        // scroll position of the currently visible page.
        const auto found = gSettingsControlCategory.find(control);
        if (found == gSettingsControlCategory.end() || found->second < 0 ||
            found->second != gSettingsCategory || !IsWindowVisible(control))
            return DefSubclassProc(control, message, wParam, lParam);
        RECT rect{}, client{}; GetWindowRect(control, &rect); MapWindowPoints(nullptr, gSettingsBody, reinterpret_cast<POINT*>(&rect), 2); GetClientRect(gSettingsBody, &client);
        if (rect.top < 0) scrollSettings(owner, gSettingsScroll + rect.top);
        else if (rect.bottom > client.bottom) scrollSettings(owner, gSettingsScroll + rect.bottom - client.bottom);
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(control, settingsControlSubclass, id);
    return DefSubclassProc(control, message, wParam, lParam);
}

HWND settingsControl(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style,
    int id, int x, int y, int width, int height) {
    HWND owner = parent;
    const bool pageControl = gCreatingSettingsCategory >= 0;
    if (pageControl) { parent = gSettingsBody; y -= 96; }
    HWND control = CreateWindowW(type, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(gSmallFont), TRUE);
    SetWindowSubclass(control, settingsControlSubclass, 1, reinterpret_cast<DWORD_PTR>(owner));
    gSettingsControlCategory[control] = gCreatingSettingsCategory;
    if (pageControl) { RECT bounds{}; GetWindowRect(control, &bounds); MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&bounds), 2); gSettingsControlRects[control] = bounds; }
    COMBOBOXINFO combo{sizeof(combo)};
    if (GetComboBoxInfo(control, &combo) && combo.hwndList) SetWindowSubclass(combo.hwndList, settingsControlSubclass, 1, reinterpret_cast<DWORD_PTR>(owner));
    return control;
}

void updateJudgePositionLabel() {
    if (!gJudgeValue) return;
    const std::wstring label = gJudgeLineOffset == 0 ? L"기본 위치" :
        (gJudgeLineOffset < 0 ? L"위로 " + std::to_wstring(-gJudgeLineOffset) : L"아래로 " + std::to_wstring(gJudgeLineOffset)) + L" px";
    setText(gJudgeValue, label);
}

std::wstring hudTextYOffsetLabel(int offset) {
    if (offset == 0) return L"기본 위치";
    return (offset < 0 ? L"위로 " + std::to_wstring(-offset) : L"아래로 " + std::to_wstring(offset)) + L" px";
}

#include "SettingsActions.inl"
#include "SettingsSummaryOverlay.inl"
#include "Lr2SkinSettings.inl"
#include "SettingsRmlPage.inl"

void openRmlSettingsProbe() {
    if (!gRmlSettingsHost.open(gSettingsWindow, executableFolder() / L"Assets/UI/settings-probe.rml"))
        setBindingStatus(gRmlSettingsHost.error());
}

LRESULT CALLBACK settingsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_SIZE:
        hideSettingsPreviewOverlay();
        if (gRmlSettingsPageActive && gRmlSettingsPage.window()) {
            RECT client{}; GetClientRect(window, &client);
            MoveWindow(gRmlSettingsPage.window(), 0, 0, client.right, client.bottom, TRUE);
        } else scrollSettings(window, gSettingsScroll);
        return 0;
    case WM_RML_SETTINGS_NAVIGATE:
        gNativeSettingsFallback = lParam != 0;
        selectSettingsCategory(int(wParam));
        if (!gRmlSettingsPageActive && IsWindowVisible(window) && gCategoryButtons[gSettingsCategory]) SetFocus(gCategoryButtons[gSettingsCategory]);
        return 0;
    case WM_RML_SETTINGS_ACTION:
        if (wParam >= RML_ACTION_PICK_SOUND_FOLDER && wParam <= RML_ACTION_CLEAR_BGM_FOLDER) {
            hideSettingsPreviewOverlay();
            const auto status = applySystemSoundFolderAction(window, wParam);
            syncRmlSettingsState(status);
            if (gRmlSettingsPageActive) SetFocus(gRmlSettingsPage.window());
            return 0;
        }
        if(wParam==ID_CONTROLLER_SETTINGS){hideSettingsPreviewOverlay();openControllerSettings();syncRmlSettingsState();return 0;}
        if (wParam==ID_BIND) { hideSettingsPreviewOverlay(); beginKeyBindingCapture(); }
        else if (wParam==IDCANCEL) { if(gCapturingBindings) cancelKeyBindingCapture(); }
        else if (wParam==ID_RESET_KEYS) {
            if(gCapturingBindings) cancelKeyBindingCapture();
            if(gBindingMode==1)applyLaneKeys(DEFAULT_KEYS);else {bms::KeyProfiles defaults;gKeyProfiles.keys[gBindingMode]=defaults.keys[gBindingMode];gKeyProfiles.save(gSettingsFile,gBindingMode);}setText(gSettingsKeys,keySummary());updateSelection();setBindingStatus(L"기본 키 배치로 복원했습니다.");
        } else if(wParam==1) {
            if(gCapturingBindings) cancelKeyBindingCapture();
            if(gSettingsPreviewShowing) hideSettingsPreviewOverlay();else showSettingsPreviewOverlay();
        } else if(wParam==ID_HELP) {
            if(gCapturingBindings) cancelKeyBindingCapture();
            hideSettingsPreviewOverlay();
            SendMessageW(gWindow,WM_COMMAND,ID_HELP,0);
        }
        syncRmlSettingsState();
        if(!gCapturingBindings && gRmlSettingsPageActive) SetFocus(gRmlSettingsPage.window());
        return 0;
    case WM_RML_SETTINGS_FALLBACK: {
        const auto error = gRmlSettingsPage.error();
        gRmlSettingsPageFailed = true; hideRmlSettingsPage(); gRmlSettingsPage.close();
        selectSettingsCategory(gSettingsCategory);
        if (IsWindowVisible(window) && gCategoryButtons[gSettingsCategory]) SetFocus(gCategoryButtons[gSettingsCategory]);
        setBindingStatus(wParam ? L"기존 설정창을 사용합니다." : L"새 설정 화면에 오류가 있어 기존 설정으로 돌아왔습니다. " + error);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize = {780, 400}; return 0;
    }
    case WM_MOUSEWHEEL: {
        for (HWND combo : {gFpsChoice, gSpeedChoice, gJudgementChoice, gNoteHeightChoice, gGrooveGaugeChoice, gAccuracyLimitChoice, gSummaryAccuracyLimit, gSkinJudgementChoice, gSkinComboChoice, gSoundBufferChoice, gAudioOutputModeChoice, gBgaDarknessChoice, gScreenModeChoice, gResolutionChoice, gGearChoice}) if (combo) SendMessageW(combo, CB_SHOWDROPDOWN, FALSE, 0);
        for (HWND combo : gColorChoices) if (combo) SendMessageW(combo, CB_SHOWDROPDOWN, FALSE, 0);
        scrollSettings(window, gSettingsScroll - GET_WHEEL_DELTA_WPARAM(wParam) * 100 / WHEEL_DELTA);
        return 0;
    }
    case WM_VSCROLL: {
        SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(window, SB_VERT, &info);
        int position = gSettingsScroll;
        switch (LOWORD(wParam)) {
        case SB_LINEUP: position -= 40; break;
        case SB_LINEDOWN: position += 40; break;
        case SB_PAGEUP: position -= static_cast<int>(info.nPage) * 3 / 4; break;
        case SB_PAGEDOWN: position += static_cast<int>(info.nPage) * 3 / 4; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: position = info.nTrackPos; break;
        case SB_TOP: position = 0; break;
        case SB_BOTTOM: position = gSettingsCategory == 0 ? SETTINGS_CONTENT_HEIGHT :
            gSettingsCategory == 2 ? 700 : gSettingsCategory == 3 ? 820 : 530; break;
        }
        scrollSettings(window, position); return 0;
    }
    case WM_CREATE: {
        gSettingsWindow = window;
        gRmlSettingsPageFailed = false; gNativeSettingsFallback = false;
        gSettingsScroll = 0;
        gSettingsCategory = 0;
        gSettingsControlCategory.clear();
        gSettingsControlRects.clear();
        gSettingsBody = CreateWindowExW(WS_EX_CONTROLPARENT, L"BmsPlayerSettingsBody", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 96, 740, 600, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        constexpr std::array<const wchar_t*, 7> categoryNames{L"게임플레이", L"그래픽", L"사운드", L"기타", L"요약", L"스킨", L"키 설정"};
        constexpr std::array<int, 6> categoryOrder{4, 0, 1, 2, 3, 6};
        for (size_t i = 0; i < categoryOrder.size(); ++i) {
            gCreatingSettingsCategory = -1;
            const int category = categoryOrder[i];
            const int x = 24 + static_cast<int>(i) * 118;
            gCategoryButtons[static_cast<size_t>(category)] = settingsControl(window, L"BUTTON", categoryNames[static_cast<size_t>(category)],
                WS_TABSTOP | (i == 0 ? WS_GROUP : 0) | BS_OWNERDRAW,
                ID_CATEGORY_BASE + category, x, 52, 110, 34);
        }
        gCreatingSettingsCategory = 1;
        auto heading = [&](const wchar_t* text, int x, int y, int width) {
            HWND label = settingsControl(window, L"STATIC", text, 0, 0, x, y, width, 30);
            SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);
        };
        auto percentageSetting = [&](const wchar_t* label, int id, int y, int value, HWND& slider, HWND& display) {
            settingsControl(window, L"STATIC", label, 0, id + 200, 24, y, 600, 24);
            slider = settingsControl(window, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ, id, 24, y + 28, 600, 34);
            SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(slider, TBM_SETTICFREQ, 10, 0);
            SendMessageW(slider, TBM_SETPOS, TRUE, value);
            display = settingsControl(window, L"STATIC", (std::to_wstring(value) + L"%").c_str(), 0, 0, 650, y + 32, 70, 24);
        };
        heading(L"화면", 24, 96, 710);
        settingsControl(window, L"STATIC", L"프레임 목표", 0, 0, 24, 140, 250, 24);
        gFpsChoice = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, ID_FPS, 24, 166, 250, 220);
        for (const wchar_t* label : frameRateLabels) SendMessageW(gFpsChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessageW(gFpsChoice, CB_SETCURSEL, frameRateIndex(gTargetFps), 0);
        settingsControl(window, L"STATIC", L"화면 모드", 0, 0, 400, 140, 330, 24);
        gScreenModeChoice = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, ID_SCREEN_MODE, 400, 166, 330, 180);
        for (const wchar_t* label : {L"전체화면", L"보더리스 창화면", L"창화면"}) SendMessageW(gScreenModeChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessageW(gScreenModeChoice, CB_SETCURSEL, 2 - gScreenMode, 0);
        settingsControl(window, L"STATIC", L"해상도", 0, 0, 24, 220, 330, 24);
        gResolutionOptions = availableResolutions(gWindow);
        gResolutionChoice = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, ID_RESOLUTION, 24, 246, 330, 240);
        std::wstring automaticResolution = L"자동 (창 기본 크기 / 모니터 " + std::to_wstring(gMonitorWidth) + L" × " + std::to_wstring(gMonitorHeight) + L")";
        SendMessageW(gResolutionChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(automaticResolution.c_str()));
        for (const auto& resolution : gResolutionOptions) {
            const std::wstring label = std::to_wstring(resolution.width) + L" × " + std::to_wstring(resolution.height);
            SendMessageW(gResolutionChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        int selectedResolution = 0;
        if (gResolutionConfigured) {
            const auto found = std::find_if(gResolutionOptions.begin(), gResolutionOptions.end(), [](const auto& resolution) {
                return resolution.width == gResolutionWidth && resolution.height == gResolutionHeight;
            });
            if (found != gResolutionOptions.end()) selectedResolution = 1 + static_cast<int>(found - gResolutionOptions.begin());
            else { gResolutionConfigured = false; gResolutionWidth = gResolutionHeight = 0; }
        }
        SendMessageW(gResolutionChoice, CB_SETCURSEL, selectedResolution, 0);
        gCompositionChoice = settingsControl(window, L"BUTTON", hybridRendererEnabled ? L"GPU + CPU 사용" : L"GPU 사용", WS_TABSTOP | BS_AUTOCHECKBOX, ID_COMPOSITION, 400, 220, 330, 32);
        SendMessageW(gCompositionChoice, BM_SETCHECK, gCompositionEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        gDirectCompositionChoice = settingsControl(window, L"BUTTON", L"DirectComposition", WS_TABSTOP | BS_AUTOCHECKBOX,
            ID_DIRECT_COMPOSITION, 24, 298, 330, 32);
        SendMessageW(gDirectCompositionChoice, BM_SETCHECK, gDirectCompositionEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        settingsControl(window, L"STATIC", L"GPU 사용 상한", 0, 0, 400, 270, 250, 24);
        gGpuLimitSlider = settingsControl(window, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ, ID_GPU_LIMIT, 400, 298, 270, 38);
        SendMessageW(gGpuLimitSlider, TBM_SETRANGE, TRUE, MAKELPARAM(10, 100));
        SendMessageW(gGpuLimitSlider, TBM_SETTICFREQ, 10, 0);
        SendMessageW(gGpuLimitSlider, TBM_SETPOS, TRUE, gGpuLimitPercent);
        gGpuLimitValue = settingsControl(window, L"STATIC", (std::to_wstring(gGpuLimitPercent) + L"%").c_str(), 0, 0, 680, 306, 48, 24);
        EnableWindow(gGpuLimitSlider, gCompositionEnabled);
        gSubpixelChoice = settingsControl(window, L"BUTTON", L"노트 서브픽셀 렌더링", WS_TABSTOP | BS_AUTOCHECKBOX, ID_NOTE_SUBPIXEL, 24, 360, 330, 32);
        SendMessageW(gSubpixelChoice, BM_SETCHECK, gNoteSubpixel ? BST_CHECKED : BST_UNCHECKED, 0);
        gAntialiasChoice = settingsControl(window, L"BUTTON", L"노트 안티앨리어싱", WS_TABSTOP | BS_AUTOCHECKBOX, ID_NOTE_ANTIALIAS, 400, 360, 330, 32);
        SendMessageW(gAntialiasChoice, BM_SETCHECK, gNoteAntialias ? BST_CHECKED : BST_UNCHECKED, 0);
        settingsControl(window, L"STATIC", L"저지연 기능", 0, 0, 24, 414, 300, 24);
        gLowLatencyChoice = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, ID_LOW_LATENCY, 24, 442, 330, 160);
        const auto reflexCapability = latency::probeReflex();
        const bool reflexAvailable = reflexCapability.supported && gCompositionEnabled &&
            gCompositionPresenter.lowLatency().status(latency::Mode::Reflex) != latency::Status::SessionDisabled;
        for (const wchar_t* name : {L"끔", L"NVIDIA Reflex"}) SendMessageW(gLowLatencyChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
        SendMessageW(gLowLatencyChoice, CB_SETCURSEL, gReflexRequested ? 1 : 0, 0);
        EnableWindow(gLowLatencyChoice, reflexAvailable);
        settingsControl(window,L"STATIC",L"LR2 스킨 화면 비율",0,0,400,414,330,24);
        gLr2AspectChoice=settingsControl(window,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,ID_LR2_ASPECT,400,442,330,120);
        for(const auto* label:{L"비율 유지 (레터박스)",L"화면 채우기 (스트레치)"})
            SendMessageW(gLr2AspectChoice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        SendMessageW(gLr2AspectChoice,CB_SETCURSEL,gLr2Stretch,0);
        gCreatingSettingsCategory = 0;
        heading(L"게임플레이", 24, 96, 710);
        settingsControl(window, L"STATIC", L"BGA 밝기", 0, 0, 24, 194, 250, 24);
        gBgaDarknessChoice = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, ID_BGA_DARKNESS, 24, 220, 250, 330);
        for (int brightness = 0; brightness <= 100; brightness += 10) {
            const std::wstring label = brightness == 0 ? L"0% (BGA 끔)" : std::to_wstring(brightness) + L"%";
            SendMessageW(gBgaDarknessChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        SendMessageW(gBgaDarknessChoice, CB_SETCURSEL, gBgaBrightness / 10, 0);
        settingsControl(window, L"STATIC", L"노트 배속", 0, 0, 400, 194, 330, 24);
        gSpeedChoice = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, ID_SPEED, 400, 220, 330, 300);
        for (int steps = 2; steps <= 40; ++steps) {
            const std::wstring label = number(steps / 4.0, 2) + L"x";
            SendMessageW(gSpeedChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        SendMessageW(gSpeedChoice, CB_SETCURSEL, std::clamp(static_cast<int>(std::lround(gNoteSpeed * 4)) - 2, 0, 38), 0);
        settingsControl(window, L"STATIC", L"레인 커버", 0, 0, 400, 330, 330, 24);
        gLaneCoverSlider = settingsControl(window, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ, ID_LANE_COVER, 400, 360, 270, 42);
        SetWindowSubclass(gLaneCoverSlider, settingsPreviewTrackbarSubclass, 1, 0);
        SendMessageW(gLaneCoverSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(gLaneCoverSlider, TBM_SETTICFREQ, 10, 0);
        SendMessageW(gLaneCoverSlider, TBM_SETPOS, TRUE, gLaneCoverPercent);
        const std::wstring coverLabel = gLaneCoverPercent == 0 ? L"없음" : std::to_wstring(gLaneCoverPercent) + L"%";
        gLaneCoverValue = settingsControl(window, L"STATIC", coverLabel.c_str(), 0, 0, 690, 369, 45, 24);
        heading(L"판정선 위치", 24, 438, 710);
        gJudgePosition = settingsControl(window, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ, ID_JUDGE_POSITION, 24, 480, 330, 44);
        SetWindowSubclass(gJudgePosition, settingsPreviewTrackbarSubclass, 1, 0);
        const int nominal = static_cast<int>(std::lround(gWindowHeight * 2.0 / 3.0));
        const int minimum = 40 - nominal, maximum = std::max(40, gWindowHeight - 112) - nominal;
        gJudgeLineOffset = std::clamp(gJudgeLineOffset, minimum, maximum);
        SendMessageW(gJudgePosition, TBM_SETRANGEMIN, TRUE, minimum);
        SendMessageW(gJudgePosition, TBM_SETRANGEMAX, TRUE, maximum);
        SendMessageW(gJudgePosition, TBM_SETLINESIZE, 0, 1);
        SendMessageW(gJudgePosition, TBM_SETPAGESIZE, 0, 10);
        SendMessageW(gJudgePosition, TBM_SETTICFREQ, 50, 0);
        SendMessageW(gJudgePosition, TBM_SETPOS, TRUE, gJudgeLineOffset);
        gJudgeValue = settingsControl(window, L"STATIC", L"", 0, 0, 370, 492, 150, 28);
        updateJudgePositionLabel();
        settingsControl(window, L"STATIC", L"그루브 게이지", 0, 0, 400, 522, 130, 24);
        gGrooveGaugeChoice = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, ID_GROOVE_GAUGE, 400, 548, 130, 150);
        for (const wchar_t* label : {L"NORMAL", L"EASY", L"HARD"}) SendMessageW(gGrooveGaugeChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessageW(gGrooveGaugeChoice, CB_SETCURSEL, static_cast<int>(gGrooveGaugeMode), 0);
        settingsControl(window, L"STATIC", L"NORMAL/EASY: 시작 20%, 종료 80% 이상\r\nHARD: 시작 100%, 게이지 0%에서 실패", 0, 0, 400, 580, 330, 38);
        settingsControl(window,L"STATIC",L"정확도 제한",0,0,400,630,300,24);
        gAccuracyLimitChoice=settingsControl(window,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,ID_ACCURACY_LIMIT,400,658,300,180);
        for(const auto* label:accuracyLimitLabels)SendMessageW(gAccuracyLimitChoice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        SendMessageW(gAccuracyLimitChoice,CB_SETCURSEL,static_cast<int>(gAccuracyLimit),0);
        settingsControl(window,L"STATIC",L"기준 미만 판정 시 자동 재시작 · F5: 재시작",0,0,400,698,330,38);
        settingsControl(window,L"STATIC",L"FAST / SLOW 표시",0,0,24,630,300,24);
        gTimingHintChoice=settingsControl(window,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,ID_TIMING_HINT,24,658,300,180);
        for(const auto* label:{L"PGREAT 미만",L"GREAT 미만",L"GOOD 미만",L"없음"})SendMessageW(gTimingHintChoice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        SendMessageW(gTimingHintChoice,CB_SETCURSEL,gTimingHintCondition,0);
        gCreatingSettingsCategory=6;
        heading(L"키 배치", 24, 96, 710);
        auto keyMode=settingsControl(window,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,1299,24,132,300,160);
        for(auto label:{L"5키",L"7키",L"9키",L"10키",L"14키"})SendMessageW(keyMode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendMessageW(keyMode,CB_SETCURSEL,gBindingMode,0);
        gBind = settingsControl(window, L"BUTTON", L"키 배치 변경", WS_TABSTOP | BS_PUSHBUTTON, ID_BIND, 24, 190, 160, 40);
        settingsControl(window, L"BUTTON", L"기본 키로 복원", WS_TABSTOP | BS_PUSHBUTTON, ID_RESET_KEYS, 208, 190, 160, 40);
        gHelp = settingsControl(window, L"BUTTON", L"사용법", WS_TABSTOP | BS_PUSHBUTTON, ID_HELP, 392, 190, 130, 40);
        gSettingsKeys = settingsControl(window, L"STATIC", keySummary().c_str(), 0, ID_SETTINGS_KEYS, 24, 245, 706, 48);
        gCreatingSettingsCategory=0;
        gCreatingSettingsCategory=0;
        settingsControl(window,L"BUTTON",L"컨트롤러 입력 설정...",WS_TABSTOP|BS_PUSHBUTTON,ID_CONTROLLER_SETTINGS,24,1490,320,36);
#ifdef BMS_ENABLE_SKIN_BINDINGS
        gCreatingSettingsCategory = 5;
        heading(L"외부 스킨",24,96,700);
        gSkinNativeStatus=settingsControl(window,L"STATIC",skinSummary().c_str(),0,0,24,136,700,55);
        settingsControl(window,L"STATIC",L"스킨 변경은 SevenMania 런처의 스킨 탭에서 할 수 있습니다.",0,0,24,205,700,45);

#endif
        gCreatingSettingsCategory = 2;
        heading(L"볼륨", 24, 96, 710);
        constexpr std::array<const wchar_t*, 4> volumeLabels{L"마스터 볼륨", L"노래 볼륨", L"키음 볼륨", L"효과음 볼륨"};
        for (size_t i = 0; i < volumeLabels.size(); ++i) {
            const int y = 142 + static_cast<int>(i) * 64;
            settingsControl(window, L"STATIC", volumeLabels[i], 0, 0, 24, y, 320, 24);
            gVolumeSliders[i] = settingsControl(window, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ, ID_VOLUME_BASE + static_cast<int>(i), 24, y + 26, 600, 34);
            SendMessageW(gVolumeSliders[i], TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(gVolumeSliders[i], TBM_SETTICFREQ, 10, 0);
            SendMessageW(gVolumeSliders[i], TBM_SETPOS, TRUE, gVolumes[i]);
            const std::wstring value = std::to_wstring(gVolumes[i]) + L"%";
            gVolumeValues[i] = settingsControl(window, L"STATIC", value.c_str(), 0, 0, 650, y + 30, 70, 24);
        }
        settingsControl(window, L"STATIC", L"사운드 버퍼 (샘플 프레임)", 0, 0, 24, 420, 330, 24);
        gSoundBufferChoice = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, ID_SOUND_BUFFER, 24, 446, 250, 210);
        for (int frames : SOUND_BUFFER_OPTIONS) {
            const std::wstring label = std::to_wstring(frames);
            SendMessageW(gSoundBufferChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        const auto bufferOption = std::find(SOUND_BUFFER_OPTIONS.begin(), SOUND_BUFFER_OPTIONS.end(), gSoundBufferFrames);
        SendMessageW(gSoundBufferChoice, CB_SETCURSEL, bufferOption - SOUND_BUFFER_OPTIONS.begin(), 0);
        settingsControl(window, L"STATIC", L"게임 플레이 WASAPI 출력 모드", 0, 0, 340, 420, 360, 24);
        gAudioOutputModeChoice = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST,
            ID_AUDIO_OUTPUT_MODE, 340, 446, 380, 210);
        SendMessageW(gAudioOutputModeChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"공유 모드 (다른 앱과 함께 재생)"));
        SendMessageW(gAudioOutputModeChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"독점 모드 (게임 중 다른 앱 소리 중단 가능)"));
        SendMessageW(gAudioOutputModeChoice, CB_SETCURSEL, gAudioOutputMode, 0);
        settingsControl(window, L"STATIC", L"독점 모드는 게임 중 지연 감소를 시도합니다. 사용할 수 없으면 공유 모드로 전환합니다.",
            0, 0, 340, 478, 380, 32);
        gSongPreviewChoice = settingsControl(window, L"BUTTON", L"곡 하이라이트 부분 미리보기", WS_TABSTOP | BS_AUTOCHECKBOX, ID_SONG_PREVIEW, 24, 510, 600, 32);
        SendMessageW(gSongPreviewChoice, BM_SETCHECK, gSongPreviewEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        percentageSetting(L"미리보기 음악 볼륨", ID_PREVIEW_VOLUME, 570, gPreviewVolume, gPreviewVolumeSlider, gPreviewVolumeValue);
        gCreatingSettingsCategory = 3;
        heading(L"기타", 24, 96, 710);
        heading(L"시스템 자원 사용량", 24, 520, 710);
        gResourceUsageChoice = settingsControl(window, L"BUTTON", L"시스템 자원 사용량 보기", WS_TABSTOP | BS_AUTOCHECKBOX,
            ID_RESOURCE_USAGE, 24, 556, 420, 32);
        SendMessageW(gResourceUsageChoice, BM_SETCHECK, gShowResourceUsage ? BST_CHECKED : BST_UNCHECKED, 0);
        gCpuPerformanceChoice = settingsControl(window, L"BUTTON", L"CPU 성능 우선 모드", WS_TABSTOP | BS_AUTOCHECKBOX,
            ID_CPU_PERFORMANCE, 450, 556, 280, 32);
        SendMessageW(gCpuPerformanceChoice, BM_SETCHECK, gCpuPerformanceMode ? BST_CHECKED : BST_UNCHECKED, 0);
        settingsControl(window, L"STATIC", L"CPU 사용 상한", 0, 0, 24, 598, 600, 24);
        gCpuLimitSlider = settingsControl(window, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ, ID_CPU_LIMIT, 24, 626, 600, 36);
        SendMessageW(gCpuLimitSlider, TBM_SETRANGE, TRUE, MAKELPARAM(10, 100));
        SendMessageW(gCpuLimitSlider, TBM_SETTICFREQ, 10, 0);
        SendMessageW(gCpuLimitSlider, TBM_SETPOS, TRUE, gCpuLimitPercent);
        gCpuLimitValue = settingsControl(window, L"STATIC", (std::to_wstring(gCpuLimitPercent) + L"%").c_str(), 0, 0, 650, 632, 70, 24);
        const uint64_t totalRam = gSystemResources.totalPhysicalMemoryBytes();
        const std::wstring ramLabel = totalRam == 0 ? L"메모리 사용 상한" :
            L"메모리 사용 상한 (총 RAM " + number(static_cast<double>(totalRam) / (1024.0 * 1024.0 * 1024.0), 1) + L" GB)";
        settingsControl(window, L"STATIC", ramLabel.c_str(), 0, 0, 24, 676, 600, 24);
        gMemoryLimitSlider = settingsControl(window, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ, ID_MEMORY_LIMIT, 24, 704, 560, 36);
        SendMessageW(gMemoryLimitSlider, TBM_SETRANGE, TRUE, MAKELPARAM(10, 100));
        SendMessageW(gMemoryLimitSlider, TBM_SETTICFREQ, 10, 0);
        SendMessageW(gMemoryLimitSlider, TBM_SETPOS, TRUE, gMemoryLimitPercent);
        const double memoryBudgetGb = static_cast<double>(totalRam) * gMemoryLimitPercent / (100.0 * 1024.0 * 1024.0 * 1024.0);
        const std::wstring memoryLimitLabel = std::to_wstring(gMemoryLimitPercent) + L"% (" + number(memoryBudgetGb, 1) + L" GB)";
        gMemoryLimitValue = settingsControl(window, L"STATIC", memoryLimitLabel.c_str(), 0, 0, 592, 710, 136, 24);
        gCreatingSettingsCategory = -1;
        gSettingsStatus = settingsControl(window, L"STATIC", L"", 0, ID_SETTINGS_STATUS, 24, 1210, 706, 40);
        settingsControl(window, L"BUTTON", L"닫기 (Esc)", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_CLOSE_SETTINGS, 560, 1370, 170, 40);
        gCreatingSettingsCategory = 4;
        heading(L"요약", 24, 96, 710);
        auto summaryChoice = [&](const wchar_t* label, int id, int x, int y, int width, int count, int selection, const wchar_t* const* items) {
            settingsControl(window, L"STATIC", label, 0, 0, x, y, width, 24);
            HWND combo = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, id, x, y + 28, width, 220);
            for (int i = 0; i < count; ++i) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(items[i]));
            SendMessageW(combo, CB_SETCURSEL, selection, 0);
            return combo;
        };
        constexpr std::array<const wchar_t*, 3> screenModeLabels{L"전체화면", L"보더리스 창화면", L"창화면"};
        constexpr std::array<const wchar_t*, 3> gaugeLabels{L"NORMAL", L"EASY", L"HARD"};
        constexpr std::array<const wchar_t*, 4> judgementLabels{L"BASIC", L"EASY", L"NORMAL", L"HARD"};
        gSummaryFps = summaryChoice(L"FPS", ID_SUMMARY_FPS, 24, 146, 300, static_cast<int>(frameRateLabels.size()),
            frameRateIndex(gTargetFps), frameRateLabels.data());
        std::array<std::wstring, 11> brightnessStorage{};
        std::array<const wchar_t*, 11> brightnessLabels{};
        for (int i = 0; i <= 10; ++i) {
            brightnessStorage[static_cast<size_t>(i)] = i == 0 ? L"0% (BGA 끔)" : std::to_wstring(i * 10) + L"%";
            brightnessLabels[static_cast<size_t>(i)] = brightnessStorage[static_cast<size_t>(i)].c_str();
        }
        gSummaryBga = summaryChoice(L"BGA 밝기", ID_SUMMARY_BGA, 400, 146, 330, static_cast<int>(brightnessLabels.size()), gBgaBrightness / 10, brightnessLabels.data());
        std::array<std::wstring, 39> speedStorage{};
        std::array<const wchar_t*, 39> speedLabels{};
        for (int i = 0; i < 39; ++i) {
            speedStorage[static_cast<size_t>(i)] = number((i + 2) / 4.0, 2) + L"x";
            speedLabels[static_cast<size_t>(i)] = speedStorage[static_cast<size_t>(i)].c_str();
        }
        gSummarySpeed = summaryChoice(L"노트 배속", ID_SUMMARY_SPEED, 24, 246, 300, static_cast<int>(speedLabels.size()),
            std::clamp(static_cast<int>(std::lround(gNoteSpeed * 4)) - 2, 0, 38), speedLabels.data());
        gSummaryScreenMode = summaryChoice(L"화면 모드", ID_SUMMARY_SCREEN_MODE, 24, 346, 300, static_cast<int>(screenModeLabels.size()), 2 - gScreenMode, screenModeLabels.data());
        gSummaryGauge = summaryChoice(L"게이지 모드", ID_SUMMARY_GAUGE, 400, 346, 330, static_cast<int>(gaugeLabels.size()), static_cast<int>(gGrooveGaugeMode), gaugeLabels.data());
        gSummaryAccuracyLimit=summaryChoice(L"정확도 제한",ID_SUMMARY_ACCURACY_LIMIT,24,446,300,int(accuracyLimitLabels.size()),static_cast<int>(gAccuracyLimit),accuracyLimitLabels.data());
        gCreatingSettingsCategory = -1;
        selectSettingsCategory(4);
        if (!gMemoryLimitError.empty()) setText(gSettingsStatus, gMemoryLimitError);
        return 0;
    }
    case WM_MEASUREITEM: {
        auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (item->CtlID >= ID_COLOR_BASE && item->CtlID < ID_COLOR_BASE + 8) {
            item->itemHeight = 24;
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (item->CtlID >= ID_CATEGORY_BASE && item->CtlID < ID_CATEGORY_BASE + 7) {
            const int category = static_cast<int>(item->CtlID - ID_CATEGORY_BASE);
            const bool selected = category == gSettingsCategory;
            fillRect(item->hDC, item->rcItem, selected ? RGB(34, 57, 79) : RGB(25, 32, 43));
            HBRUSH border = CreateSolidBrush(selected ? RGB(56, 189, 248) : RGB(68, 78, 92));
            FrameRect(item->hDC, &item->rcItem, border);
            DeleteObject(border);
            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, selected ? RGB(248, 250, 252) : RGB(188, 199, 214));
            HFONT oldFont = static_cast<HFONT>(SelectObject(item->hDC, gSmallFont));
            wchar_t label[32]{};
            GetWindowTextW(item->hwndItem, label, static_cast<int>(std::size(label)));
            RECT textRect = item->rcItem;
            DrawTextW(item->hDC, label, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(item->hDC, oldFont);
            if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &item->rcItem);
            return TRUE;
        }
        if (item->CtlID >= ID_COLOR_BASE && item->CtlID < ID_COLOR_BASE + 8 && item->itemID < 2) {
            const auto& color = gPalette[noteColors(item->CtlID - ID_COLOR_BASE)[item->itemID]];
            fillRect(item->hDC, item->rcItem, (item->itemState & ODS_SELECTED) ? RGB(34, 57, 79) : RGB(25, 32, 43));
            RECT swatch{ item->rcItem.left + 8, item->rcItem.top + 4, item->rcItem.left + 66, item->rcItem.bottom - 4 };
            fillRect(item->hDC, swatch, color.color);
            drawText(item->hDC, color.name, item->rcItem.left + 80, item->rcItem.top + 3, RGB(241, 245, 249), gSmallFont);
            if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &item->rcItem);
            return TRUE;
        }
        break;
    }
    case WM_HSCROLL: {
        if (LOWORD(wParam) == TB_ENDTRACK) gAudio.triggerEffect(L"click");
        const HWND slider = reinterpret_cast<HWND>(lParam);
        if (!slider) break;
        int id = 0;
        for (int candidate : {ID_CPU_LIMIT, ID_MEMORY_LIMIT, ID_GPU_LIMIT, ID_BACKGROUND_BLUR, ID_BACKGROUND_OPACITY,
            ID_PREVIEW_VOLUME, ID_LANE_COVER, ID_LANE_TRANSPARENCY, ID_JUDGE_POSITION, ID_COMBO_TEXT_Y, ID_JUDGEMENT_TEXT_Y,
            ID_VOLUME_BASE, ID_VOLUME_BASE+1, ID_VOLUME_BASE+2, ID_VOLUME_BASE+3})
            if (settingControl(candidate) == slider) { id = candidate; break; }
        if (!id) break;
        const bool applied = applyNativeSetting(id, int(SendMessageW(slider, TBM_GETPOS, 0, 0)));
        if (applied && id == ID_MEMORY_LIMIT && (LOWORD(wParam) == TB_ENDTRACK || LOWORD(wParam) == TB_THUMBPOSITION))
            setBindingStatus(L"메모리 사용 상한을 적용했습니다.");
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, RGB(218, 228, 240));
        SetBkColor(dc, RGB(0, 0, 0));
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (lParam && (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == CBN_SELCHANGE))
            gAudio.triggerEffect(L"click");
        if (id >= ID_CATEGORY_BASE && id < ID_CATEGORY_BASE + 7) {
            gNativeSettingsFallback = false;
            selectSettingsCategory(id - ID_CATEGORY_BASE);
            return 0;
        }
#ifdef BMS_ENABLE_SKIN_BINDINGS
#endif
        if(id==ID_CONTROLLER_SETTINGS){hideSettingsPreviewOverlay();openControllerSettings();return 0;}
        if (id == ID_LOW_LATENCY && HIWORD(wParam) == CBN_SELCHANGE) {
            applyNativeSetting(id, int(SendMessageW(gLowLatencyChoice, CB_GETCURSEL, 0, 0))); return 0;
        }
        if (id == ID_SONG_PREVIEW || id == ID_NOTE_SUBPIXEL || id == ID_NOTE_ANTIALIAS || id == ID_DIRECT_COMPOSITION ||
            id == ID_COMPOSITION || id == ID_RESOURCE_USAGE || id == ID_CPU_PERFORMANCE || (id >= ID_SEPARATOR_BASE && id < ID_SEPARATOR_BASE + 7)) {
            const int value = SendMessageW(settingControl(id), BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (id == ID_COMPOSITION || id == ID_DIRECT_COMPOSITION) postSetting(id, value);
            else if (applyNativeSetting(id, value) && id == ID_CPU_PERFORMANCE)
                setBindingStatus(value ? L"게임 처리 스레드 우선순위를 높였습니다." : L"게임 처리 스레드 우선순위를 복원했습니다.");
            return 0;
        }
        if (id == ID_CLOSE_SETTINGS || id == IDOK || id == IDCANCEL) { SendMessageW(window, WM_CLOSE, 0, 0); return 0; }
        if (id == ID_BIND) { beginKeyBindingCapture(); return 0; }
        if (gCapturingBindings && (id == ID_HELP || id == ID_RESET_KEYS || HIWORD(wParam) == CBN_SELCHANGE)) cancelKeyBindingCapture();
        if (id == ID_HELP) { SendMessageW(gWindow, WM_COMMAND, ID_HELP, 0); return 0; }
        if (id == ID_RESET_KEYS) {
            if(gBindingMode==1)applyLaneKeys(DEFAULT_KEYS);else {bms::KeyProfiles defaults;gKeyProfiles.keys[gBindingMode]=defaults.keys[gBindingMode];gKeyProfiles.save(gSettingsFile,gBindingMode);}
            setText(gSettingsKeys, keySummary());
            setBindingStatus(L"기본 키 배치로 복원했습니다.");
            updateSelection();
            return 0;
        }
        if (HIWORD(wParam) == CBN_SELCHANGE) {
            const int selection = int(SendMessageW(reinterpret_cast<HWND>(lParam), CB_GETCURSEL, 0, 0));
            if(id==1299){if(gCapturingBindings)cancelKeyBindingCapture();gBindingMode=std::clamp(selection,0,4);return 0;}
            if (selection < 0) return 0;
            id = bms::SettingsController::canonicalId(id);
            int value = selection;
            if (id >= ID_COLOR_BASE && id < ID_COLOR_BASE + 8 && selection < 2)
                value = noteColors(id - ID_COLOR_BASE)[selection];
            if (id == ID_FPS) {
                if (selection >= int(frameRateValues.size())) return 0;
                value = frameRateValues[selection];
            } else if (id == ID_SPEED) value = selection + 2;
            else if(id==ID_SKIN_JUDGEMENT_MODE||id==ID_SKIN_COMBO_MODE)value=selection-1;
            else if (id == ID_NOTE_HEIGHT) value = kFixedNoteHeight;
            else if (id == ID_BGA_DARKNESS) value = selection * 10;
            else if (id == ID_SCREEN_MODE) value = 2 - selection;
            else if (id == ID_SOUND_BUFFER) {
                if (selection >= int(SOUND_BUFFER_OPTIONS.size())) return 0;
                value = SOUND_BUFFER_OPTIONS[selection];
            }
            if (id == ID_SCREEN_MODE || id == ID_RESOLUTION) postSetting(id, value);
            else if (applyNativeSetting(id, value)) setBindingStatus(L"설정을 저장했습니다.");
            return 0;
        }
        break;
    }
    case WM_KEYDOWN:
        if (wParam == VK_F9 && !gCapturingBindings) { openRmlSettingsProbe(); return 0; }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && gCapturingBindings) cancelKeyBindingCapture();
        if (LOWORD(wParam) == WA_INACTIVE) hideSettingsPreviewOverlay();
        break;
    case WM_CANCELMODE:
        if(gCapturingBindings) cancelKeyBindingCapture();
        hideSettingsPreviewOverlay();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(window, &ps);
        drawText(dc, L"설정", 24, 18, RGB(125, 211, 252), gFont);
        EndPaint(window, &ps);
        return 0;
    }
    case WM_CLOSE:
        // Finish queued value/device commands before saving and releasing the UI.
        if(IsWindow(gWindow)) SendMessageW(gWindow,WM_APPLY_SETTINGS,0,0);
        gRmlSettingsPage.close(); gRmlSettingsPageActive = false;
        gRmlSettingsHost.close();
        if (gCapturingBindings) cancelKeyBindingCapture();
        hideSettingsPreviewOverlay();
        saveDisplaySettings();
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if(gCapturingBindings) cancelKeyBindingCapture();
        releaseSettingsPreviewResources();
        gRmlSettingsPage.close(); gRmlSettingsPageActive = false;
        gSettingsWindow = nullptr;
#ifdef BMS_ENABLE_SKIN_BINDINGS
        gSkinNativeStatus=nullptr;
#endif
        gCategoryButtons.fill(nullptr); gVolumeSliders.fill(nullptr); gVolumeValues.fill(nullptr); gSettingsControlCategory.clear(); gSettingsControlRects.clear(); gSettingsBody = nullptr;
        gSettingsStatus = gSettingsKeys = gFpsChoice = gSpeedChoice = gJudgementChoice = gNoteHeightChoice = gGrooveGaugeChoice = gBind = gHelp = nullptr;
        gTimingHintChoice=nullptr;gAccuracyLimitChoice=gSummaryAccuracyLimit=nullptr;gSkinJudgementChoice=gSkinComboChoice=nullptr;
        gSummaryFps = gSummaryBga = gSummarySpeed = gSummaryJudgement = gSummaryScreenMode = gSummaryGauge = nullptr;
        gSoundBufferChoice = nullptr;
        gAudioOutputModeChoice = nullptr;
        gCpuLimitSlider = gCpuLimitValue = gMemoryLimitSlider = gMemoryLimitValue = nullptr;
        gGpuLimitSlider = gGpuLimitValue = gResourceUsageChoice = nullptr;
        gComboTextYSlider = gComboTextYValue = gJudgementTextYSlider = gJudgementTextYValue = nullptr;
        gJudgePosition = gJudgeValue = nullptr;
        gLaneTransparencySlider = gLaneTransparencyValue = nullptr;
        gBgaDarknessChoice = nullptr;
        gLaneCoverSlider = gLaneCoverValue = nullptr;
        gScreenModeChoice = gResolutionChoice = gGearChoice = gCompositionChoice = gDirectCompositionChoice = gLr2AspectChoice = nullptr; gResolutionOptions.clear(); gSettingsScroll = 0;
        gSubpixelChoice = gAntialiasChoice = gLowLatencyChoice = nullptr;
        gColorChoices.fill(nullptr);
        gSeparatorButtons.fill(nullptr);
        EnableWindow(gWindow, TRUE);
        SetActiveWindow(gWindow);
        focusBrowseInput();
        InvalidateRect(gWindow, nullptr, FALSE);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void openFullSettings() {
    if (gPlaying || gPracticeWindow || (gLoading && gLoadResult.playback)) return;
    if (gSettingsWindow) { selectSettingsCategory(4); SetForegroundWindow(gSettingsWindow); return; }
    HWND settings{};
    {   DpiContextScope unaware(DPI_AWARENESS_CONTEXT_UNAWARE);
        RECT owner{}, work{};
        GetWindowRect(gWindow, &owner);
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int width = 780, height = std::min(820, static_cast<int>(work.bottom - work.top) - 30);
        const int x = std::clamp((owner.left + owner.right - width) / 2, work.left, std::max(work.left, work.right - width));
        const int y = std::clamp((owner.top + owner.bottom - height) / 2, work.top, std::max(work.top, work.bottom - height));
        settings = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, L"BmsPlayerSettingsWindow", L"SETTINGS",
            WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VSCROLL | WS_CLIPCHILDREN, x, y, width, height, gWindow, nullptr, GetModuleHandleW(nullptr), nullptr); }
    if (!settings) return;
    if (!gSettingsPreviewOverlay) {
        DpiContextScope gameDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        gSettingsPreviewOverlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            L"BmsPlayerSettingsPreviewOverlay", L"", WS_POPUP, 0, 0, 1, 1, gWindow, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (gSettingsPreviewOverlay) SetLayeredWindowAttributes(gSettingsPreviewOverlay, 0, 255, LWA_ALPHA);
    }
    EnableWindow(gWindow, FALSE);
    if(gList)ShowWindow(gList,SW_SHOWNA);
    InvalidateRect(gWindow,nullptr,FALSE);
    ShowWindow(settings, SW_SHOW);
    // All categories are connected; open the summary by default. Keep native fallback.
    selectSettingsCategory(4);
    if (!gRmlSettingsPageActive && gCategoryButtons[4]) SetFocus(gCategoryButtons[4]);
}

void openSettings() {
    if(gPlaying||gPracticeWindow||gLoading)return;
    if(gSummaryVisible)toggleSummary();
    openFullSettings();
}
