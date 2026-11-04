#include "../src/Main.cpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr, nullptr);
    return out;
}
std::wstring readValue(const std::filesystem::path& file, const std::wstring& section, const std::wstring& key) {
    wchar_t value[2048]{};
    GetPrivateProfileStringW(section.c_str(), key.c_str(), L"<missing>", value, 2048, file.c_str());
    return value;
}
void verifyFixture(const std::filesystem::path& source, const std::filesystem::path& saved) {
    std::ifstream input(source); std::string line; std::wstring section; int count = 0;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == ';') continue;
        if (line[0] == '[') { section.assign(line.begin() + 1, line.end() - 1); continue; }
        const auto at = line.find('='); if (at == std::string::npos) continue;
        const std::wstring key(line.begin(), line.begin() + at), expected(line.begin() + at + 1, line.end());
        if (readValue(saved, section, key) != expected) throw std::runtime_error("INI round-trip mismatch: " + line);
        ++count;
    }
    require(count >= 60, "fixture inventory unexpectedly shrank");
    std::cout << "INI keys preserved=" << count << '\n';
}
HWND createSettings() {
    HWND window = CreateWindowExW(WS_EX_CONTROLPARENT, L"BmsPlayerSettingsWindow", L"Phase 0 baseline",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VSCROLL | WS_CLIPCHILDREN,
        0, 0, 780, 820, gWindow, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(window && gSettingsBody, "native settings window creation failed");
    EnableWindow(gWindow, FALSE);
    return window;
}
void registerType(const wchar_t* name, WNDPROC procedure) {
    WNDCLASSW type{}; type.lpfnWndProc = procedure; type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = name; type.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    require(RegisterClassW(&type) != 0, "window class registration failed");
}
void captureSettings(HWND window, const std::filesystem::path& path) {
    RECT client{}; GetClientRect(window, &client);
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = client.right; info.bmiHeader.biHeight = -client.bottom;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* pixels{}; HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    require(bitmap && pixels, "settings capture allocation failed");
    const auto previous = SelectObject(dc, bitmap);
    FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SendMessageW(window, WM_PRINT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
    GdiFlush();
    const CLSID png{0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
    Gdiplus::Bitmap image(bitmap, nullptr);
    const bool saved = image.Save(path.c_str(), &png) == Gdiplus::Ok;
    SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc);
    require(saved, "settings baseline capture failed");
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    const auto output = std::filesystem::absolute(argv[2]);
    std::filesystem::create_directories(output);
    const auto config = output / (L"settings-copy-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&gGdiPlusToken, &input, nullptr) != Gdiplus::Ok) return 1;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 0;
    try {
        gRmlSettingsPageEnabled = false; // Keep the recorded native baseline independent of the migrated page.
        std::filesystem::copy_file(argv[1], config); gSettingsFile = config;
        loadKeyBindings(); loadDisplaySettings(); saveKeyBindings(); saveDisplaySettings();
        verifyFixture(argv[1], config);
        require(gNoteSpeed == 2.75 && gTargetFps == 144 && gPreviewVolume == 39, "fixture values were not actually loaded");
        // Phase 1 must support applying/saving without any native control HWND.
        require(settingsController().set(ID_PREVIEW_VOLUME, -9).value == 0 &&
            readValue(config, L"Sound", L"PreviewVolume") == L"0", "headless value validation/save failed");
        settingsController().set(ID_PREVIEW_VOLUME, 39);
        require(!settingsController().set(ID_FPS, 123).applied && gTargetFps == 144, "invalid frame target changed state");
        require(settingsController().set(ID_SUMMARY_SPEED, 11).applied && gNoteSpeed == 2.75, "headless summary application failed");
        auto duplicateKeys = gLaneKeys; duplicateKeys[1] = duplicateKeys[0];
        const auto keysBefore = gLaneKeys;
        require(!applyLaneKeys(duplicateKeys) && gLaneKeys == keysBefore, "invalid value-only key bindings were committed");
        verifyFixture(argv[1], config);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES}; InitCommonControlsEx(&controls);
        registerType(L"BmsSettingsPhase0Owner", DefWindowProcW);
        registerType(L"BmsPlayerSettingsBody", settingsBodyProc);
        registerType(L"BmsPlayerSettingsWindow", settingsProc);
        gFont = CreateFontW(-24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, 0, 0, L"Malgun Gothic");
        gSmallFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, 0, 0, L"Malgun Gothic");
        gWindow = CreateWindowW(L"BmsSettingsPhase0Owner", L"Phase 0", WS_OVERLAPPEDWINDOW, 0, 0, 1280, 860,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        gList = CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT, 0, 0, 100, 100, gWindow, nullptr, GetModuleHandleW(nullptr), nullptr);
        HWND settings = createSettings();
        require(gSettingsCategory == 4, "settings no longer starts on the summary page");
        std::ofstream report(output / L"native-controls.tsv");
        report << "category\tid\tclass\tlabel\n";
        for (const auto& [control, category] : gSettingsControlCategory) {
            wchar_t name[128]{}, label[1024]{}; GetClassNameW(control, name, 128); GetWindowTextW(control, label, 1024);
            std::wstring text(label); std::replace(text.begin(), text.end(), L'\r', L' '); std::replace(text.begin(), text.end(), L'\n', L' ');
            report << category << '\t' << GetDlgCtrlID(control) << '\t' << utf8(name) << '\t' << utf8(text) << '\n';
        }
        for (int category = 0; category < 5; ++category) {
            selectSettingsCategory(category);
            require(gSettingsCategory == category, "native category selection failed");
            for (const auto& [control, group] : gSettingsControlCategory) {
                const bool expected = (group < 0 || group == category) && (control != gDirectCompositionChoice || gCompositionEnabled);
                require(((GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) != 0) == expected, "category control visibility changed");
            }
            scrollSettings(settings, 200); require(gSettingsScroll >= 0, "scroll range invalid");
            selectSettingsCategory(category); require(gSettingsScroll == 0, "category entry no longer resets scroll");
            captureSettings(settings, output / (L"category-" + std::to_wstring(category) + L".png"));
        }
        MINMAXINFO limits{}; SendMessageW(settings, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits));
        require(limits.ptMinTrackSize.x == 780 && limits.ptMinTrackSize.y == 400, "settings minimum size changed");
        SetWindowPos(settings, nullptr, 0, 0, 780, 400, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        selectSettingsCategory(0); scrollSettings(settings, 10000);
        require(gSettingsScroll > 0, "short gameplay page lost scrolling");
        captureSettings(settings, output / L"gameplay-small-scrolled.png");
        SetWindowPos(settings, nullptr, 0, 0, 780, 820, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        selectSettingsCategory(4);
        const int speedOption = 4; // 1.50x in the summary and original page.
        SendMessageW(gSummarySpeed, CB_SETCURSEL, speedOption, 0);
        SendMessageW(settings, WM_COMMAND, MAKEWPARAM(ID_SUMMARY_SPEED, CBN_SELCHANGE), reinterpret_cast<LPARAM>(gSummarySpeed));
        require(gNoteSpeed == 1.5 && SendMessageW(gSpeedChoice, CB_GETCURSEL, 0, 0) == speedOption &&
            readValue(config, L"Display", L"SpeedSteps") == L"6", "summary did not immediately apply and persist speed");
        selectSettingsCategory(3);
        SendMessageW(gResourceUsageChoice, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(settings, WM_COMMAND, MAKEWPARAM(ID_RESOURCE_USAGE, BN_CLICKED), reinterpret_cast<LPARAM>(gResourceUsageChoice));
        require(!gShowResourceUsage && readValue(config, L"System", L"ShowUsage") == L"0", "resource visibility did not immediately save");
        const auto originalKeys = gLaneKeys;
        beginKeyBindingCapture(); acceptBindingKey('Q');
        SendMessageW(settings, WM_ACTIVATE, WA_INACTIVE, 0);
        require(!gCapturingBindings && gLaneKeys == originalKeys, "partial binding capture survived focus loss or changed active keys");
        SendMessageW(settings, WM_COMMAND, IDCANCEL, 0);
        require(!gSettingsWindow && IsWindowEnabled(gWindow), "Escape/close did not release the owner");
        require(readValue(config, L"Display", L"SpeedSteps") == L"6", "Escape unexpectedly rolled back applied settings");
        settings = createSettings();
        require(SendMessageW(gSummarySpeed, CB_GETCURSEL, 0, 0) == speedOption &&
            SendMessageW(gResourceUsageChoice, BM_GETCHECK, 0, 0) == BST_UNCHECKED, "settings reopen lost applied values");
        SendMessageW(settings, WM_CLOSE, 0, 0);
        // Load legacy keys from a separate synthetic file, never a personal INI.
        const auto legacy = output / L"legacy-copy.ini";
        { std::ofstream file(legacy); file << "[Display]\nBGADarkness=25\nDirectComposition=0\n[System]\nMemoryLimitPercent=100\n"; }
        gSettingsFile = legacy; loadDisplaySettings();
        require(gBgaBrightness == 80 && gBgaDarkness == 20 && !gCompositionEnabled && !gDirectCompositionEnabled,
            "legacy brightness/GPU fallback contract changed");
        gSettingsFile.clear();
        std::cout << "Phase 0: five native categories, INI round-trip, summary sync, immediate save, close/reopen and capture cancellation passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    if (gSettingsWindow) SendMessageW(gSettingsWindow, WM_CLOSE, 0, 0);
    if (gWindow) DestroyWindow(gWindow);
    gSongPreview.shutdown();
    if (gFont) DeleteObject(gFont); if (gSmallFont) DeleteObject(gSmallFont);
    CoUninitialize(); Gdiplus::GdiplusShutdown(gGdiPlusToken); gGdiPlusToken = 0;
    return result;
}
