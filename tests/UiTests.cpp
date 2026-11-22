#include <windows.h>
#include <commctrl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>
#include <stdexcept>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include "../src/ResolutionScale.h"

void require(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
void writeWave(const std::filesystem::path& path, uint32_t frames = 2205) {
    constexpr uint32_t rate = 44100;
    constexpr uint16_t channels = 2, bits = 16;
    const uint32_t dataSize = frames * channels * sizeof(int16_t);
    std::ofstream out(path, std::ios::binary);
    auto write = [&out](const auto& value) { out.write(reinterpret_cast<const char*>(&value), sizeof(value)); };
    out.write("RIFF", 4); uint32_t riff = 36 + dataSize; write(riff); out.write("WAVEfmt ", 8);
    uint32_t fmtSize = 16; write(fmtSize); uint16_t format = 1; write(format); write(channels); write(rate);
    uint32_t byteRate = rate * channels * bits / 8; write(byteRate); uint16_t align = channels * bits / 8; write(align); write(bits);
    out.write("data", 4); write(dataSize);
    for (uint32_t i = 0; i < frames; ++i) {
        const int16_t value = static_cast<int16_t>(std::sin(6.283185307179586 * 523.25 * i / rate) * 4500.0);
        write(value); write(value);
    }
}
void writeChart(const std::filesystem::path& path, const std::string& title, const std::string& level, const std::string& notes) {
    std::ofstream out(path);
    out << "#PLAYER 1\n#TITLE " << title << "\n#ARTIST composer\n#PLAYLEVEL " << level
        << "\n#GENRE TEST GENRE\n#BPM 120\n#WAV01 click.wav\n" << notes;
}
bool waitUntil(const std::function<bool()>& condition, ULONGLONG timeoutMs = 5000) {
    const ULONGLONG start = GetTickCount64();
    while (GetTickCount64() - start < timeoutMs) {
        if (condition()) return true;
        Sleep(10);
    }
    return false;
}
struct FindWindowData { DWORD pid; const wchar_t* name; HWND window{}; };
BOOL CALLBACK findWindow(HWND window, LPARAM parameter) {
    auto* data = reinterpret_cast<FindWindowData*>(parameter);
    DWORD pid{}; GetWindowThreadProcessId(window, &pid);
    wchar_t name[128]{}; GetClassNameW(window, name, 128);
    if (pid == data->pid && wcscmp(name, data->name) == 0) { data->window = window; return FALSE; }
    return TRUE;
}
HWND processWindow(DWORD pid, const wchar_t* name) {
    FindWindowData data{ pid, name };
    EnumWindows(findWindow, reinterpret_cast<LPARAM>(&data));
    return data.window;
}
HWND settingsItem(HWND root, int id) {
    if (HWND direct = GetDlgItem(root, id)) return direct;
    struct Search { int id; HWND found{}; } search{id};
    EnumChildWindows(root, [](HWND child, LPARAM data) -> BOOL {
        auto* item = reinterpret_cast<Search*>(data);
        if (GetDlgCtrlID(child) == item->id) { item->found = child; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}
struct AppProcess {
    PROCESS_INFORMATION process{};
    HWND main{};
    ~AppProcess() { stop(); }
    void stop() {
        if (!process.hProcess) return;
        if (main && IsWindow(main)) PostMessageW(main, WM_CLOSE, 0, 0);
        if (WaitForSingleObject(process.hProcess, 5000) != WAIT_OBJECT_0) {
            TerminateProcess(process.hProcess, 2);
            WaitForSingleObject(process.hProcess, 5000); // Wait until the copied EXE is unlocked before cleanup.
        }
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        process = {}; main = nullptr;
    }
    void start(const std::filesystem::path& exe) {
        STARTUPINFOW startup{ sizeof(startup) };
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_SHOWNOACTIVATE;
        std::wstring command = L"\"" + exe.wstring() + L"\"";
        require(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, exe.parent_path().c_str(), &startup, &process), "app start failed");
        require(waitUntil([&] {
            main = processWindow(process.dwProcessId, L"BmsPlayerNativeWindow");
            return main && GetDlgItem(main, 1005); // Play/Settings are skin buttons now, not child windows
        }), "main window controls not ready");
        ShowWindow(main, SW_SHOWNOACTIVATE);
        UpdateWindow(main);
    }
    HWND settingsFromSpace(HWND keyTarget) {
        PostMessageW(keyTarget, WM_KEYDOWN, VK_SPACE, 0);
        PostMessageW(keyTarget, WM_KEYUP, VK_SPACE, 0);
        HWND settings{};
        require(waitUntil([&] { settings = processWindow(process.dwProcessId, L"BmsPlayerSettingsWindow"); return settings && settingsItem(settings, 1107) && !IsWindowEnabled(main); }), "Space did not open a ready settings window");
        require(!IsWindowEnabled(main), "settings did not disable browse commands");
        return settings;
    }
};
void choose(HWND settings, int id, int index) {
    HWND control = settingsItem(settings, id);
    require(control != nullptr, "settings control missing");
    SendMessageW(control, CB_SETCURSEL, index, 0);
    SendMessageW(settings, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), reinterpret_cast<LPARAM>(control));
}
void postKey(HWND target, UINT key) {
    DWORD pid{};
    const DWORD thread = GetWindowThreadProcessId(target, &pid);
    require(thread != 0, "key target thread missing");
    // Thread messages (no window) are dropped by DispatchMessage, so list keys such as Up/Down never reached the
    // song list; post to the window itself. The app loop still sees these before dispatching them.
    PostMessageW(target, WM_KEYDOWN, key, 1);
    PostMessageW(target, WM_KEYUP, key, (1LL << 31) | (1LL << 30) | 1);
}
void screenshot(HWND window, const std::filesystem::path& file) {
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    Sleep(100); // Allow the compositor to publish the repainted native child controls.
    RECT bounds{}; GetWindowRect(window, &bounds);
    const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
    void* pixels{};
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, bitmap);
    wchar_t className[80]{}; GetClassNameW(window, className, 80);
    const bool browseWindow = wcscmp(className, L"BmsPlayerNativeWindow") == 0;
    if (browseWindow) { ShowWindow(window, SW_SHOWNOACTIVATE); UpdateWindow(window); }
    const BOOL rendered = PrintWindow(window, dc, 2);
    if (browseWindow) ShowWindow(window, SW_HIDE);
    if (rendered) {
        const DWORD bytes = static_cast<DWORD>(width * height * 4);
        BITMAPFILEHEADER header{};
        header.bfType = 0x4d42;
        header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
        header.bfSize = header.bfOffBits + bytes;
        std::ofstream stream(file, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(&header), sizeof(header));
        stream.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(info.bmiHeader));
        stream.write(static_cast<const char*>(pixels), bytes);
    }
    SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(nullptr, screen);
    require(rendered, "settings screenshot failed");
}
// Songs are only indexed from explicitly registered sources (SongLibrarySources.h stores UTF-16 hex).
void registerSongSource(const std::filesystem::path& settings, const std::filesystem::path& root) {
    constexpr wchar_t hex[] = L"0123456789abcdef";
    std::wstring value = L"utf16:";
    for (wchar_t c : std::filesystem::absolute(root).wstring())
        for (int shift : {12, 8, 4, 0}) value += hex[(uint16_t(c) >> shift) & 15];
    require(WritePrivateProfileStringW(L"SongLibrarySources", L"Root1", value.c_str(), settings.c_str()) != FALSE, "song source registration failed");
}
std::filesystem::path chartFolderContaining(const std::filesystem::path& root, const wchar_t* filename) {
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(root, error), end; it != end && !error; it.increment(error))
        if (it->is_regular_file(error) && it->path().filename() == filename) return it->path().parent_path();
    return {};
}
int wmain(int argc, wchar_t** argv) {
    // The player's game window works in true pixels; measure it in the same units.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try {
        require(argc == 3 || argc == 4, "expected player path, validation directory and optional actual song folder or --settings-only");
        const bool settingsOnly = argc == 4 && std::wstring(argv[3]) == L"--settings-only";
        const std::filesystem::path sourceExe = std::filesystem::absolute(argv[1]), validation = std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(validation);
        const auto appRoot = validation / (L"ui-app-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        // The app remembers the last browse location (ROOT / ALL SONGS / folder); start every run at the root.
        { std::error_code ignored; std::filesystem::remove_all(validation / L".sevenmania-index" / L"libraries" / L"Navigation", ignored); }
        std::filesystem::create_directories(appRoot);
        const auto exe = appRoot / L"BmsPlayer.exe";
        std::filesystem::copy_file(sourceExe, exe, std::filesystem::copy_options::overwrite_existing);
        // Experimental builds also need their runtime DLLs in the isolated copy.
        for (const auto& entry : std::filesystem::directory_iterator(sourceExe.parent_path()))
            if (entry.is_regular_file() && entry.path().extension() == L".dll")
                std::filesystem::copy_file(entry.path(), appRoot / entry.path().filename(), std::filesystem::copy_options::overwrite_existing);
        const auto sourceAssets = sourceExe.parent_path() / L"Assets";
        if (std::filesystem::exists(sourceAssets))
            std::filesystem::copy(sourceAssets, appRoot / L"Assets", std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
        const auto existingSong = appRoot / L"Songs" / L"existing-library-song";
        std::filesystem::create_directories(existingSong);
        writeWave(existingSong / L"click.wav");
        writeChart(existingSong / L"resident.bme", "Resident Library Song [LIGHT 7]", "20", "#00011:01\n#00018:01\n");
        const auto config = validation / (L"ui-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L".ini");
        SetEnvironmentVariableW(L"BMS_PLAYER_CONFIG_PATH", config.c_str());
        registerSongSource(config, exe.parent_path() / L"Songs");
        std::filesystem::path playbackSource, playbackCopy;
        AppProcess app; app.start(exe);
        require(waitUntil([&] { return SendMessageW(GetDlgItem(app.main, 1005), LVM_GETITEMCOUNT, 0, 0) > 0; }), "startup song library did not finish loading");
        // Selection shortcuts are skin slots (1920x1080 reference): 0 = speed, 2 = gauge.
        // The judgement-profile slot was removed; the profile is pinned to Normal.
        RECT client{}; GetClientRect(app.main, &client);
        // Slots sit in the left half (start anchor) and are vertically centred (ResolutionScale.h).
        const auto shortcutView = bms::ui::viewport(client.right, client.bottom);
        const double shortcutScale = shortcutView.scale;
        const double shortcutX = 0, shortcutY = shortcutView.extraY / 2;
        const auto clickShortcut = [&](int slot, bool increase) {
            const double referenceX = slot == 0 ? 74 : 187;
            SendMessageW(app.main, increase ? WM_LBUTTONDOWN : WM_RBUTTONDOWN, 0,
                MAKELPARAM(int(shortcutX + referenceX * shortcutScale), int(shortcutY + 856 * shortcutScale)));
        };
        // The skin loads asynchronously after startup: click until the first shortcut takes effect (it stops at the first change).
        const int initialSpeed = GetPrivateProfileIntW(L"Display", L"SpeedSteps", 0, config.c_str());
        waitUntil([&] { clickShortcut(0, true); return GetPrivateProfileIntW(L"Display", L"SpeedSteps", 0, config.c_str()) != initialSpeed; });
        require(GetPrivateProfileIntW(L"Display", L"SpeedSteps", 0, config.c_str()) == 5, "left click did not increase speed shortcut");
        clickShortcut(0, false);
        require(GetPrivateProfileIntW(L"Display", L"SpeedSteps", 0, config.c_str()) == 4, "right click did not decrease speed shortcut");
        clickShortcut(2, false);
        require(GetPrivateProfileIntW(L"Gameplay", L"GrooveGaugeMode", 99, config.c_str()) == 1, "right click did not select easy gauge");
        clickShortcut(2, true);
        require(GetPrivateProfileIntW(L"Gameplay", L"GrooveGaugeMode", 99, config.c_str()) == 0, "left click did not return to normal gauge");
        // The native settings window (child controls 1009-1272) is only the RmlUi fallback now;
        // the RmlUi settings page is covered by BmsSettingsPhase*Tests / BmsRmlSettingsHostTests.
        app.stop();
        WritePrivateProfileStringW(L"Display", L"SpeedSteps", L"28", config.c_str());
        WritePrivateProfileStringW(L"Display", L"LaneCover", L"35", config.c_str());   // the removed native settings steps used to set this
        // Songs next to the executable is the managed library; LastFolder is no longer imported on startup.
        playbackSource = exe.parent_path() / L"Songs" / L"ui-playback-fixture";
        std::filesystem::create_directories(playbackSource);
        writeWave(playbackSource / L"click.wav", 44100 * 10);
        writeChart(playbackSource / L"playable.bme", "UI Test [LIGHT 7]", "6", "#00011:01\n#00018:01\n#00418:01\n");
        // All key modes are playable now, so the hidden (unplayable) entry is an invalid LNTYPE.
        writeChart(playbackSource / L"blocked.bme", "UI Test Blocked", "2", "#LNTYPE 9\n#00011:01\n");
        app.start(exe);
        HWND list = GetDlgItem(app.main, 1005);
        // The status label is no longer observable; a finished scan shows the resident song and the fixture folder.
        bool scanFinished = false;
        for (int attempt = 0; attempt < 6 && !scanFinished; ++attempt) // up to ~30 s for a cold scan
            scanFinished = waitUntil([&] { return SendMessageW(list, LVM_GETITEMCOUNT, 0, 0) == 2; });
        if (!scanFinished) std::wcerr << L"Scan list rows: " << SendMessageW(list, LVM_GETITEMCOUNT, 0, 0) << L'\n';
        require(scanFinished, "background scan did not finish");
        const auto libraryRoot = exe.parent_path() / L"Songs";
        playbackCopy = chartFolderContaining(libraryRoot, L"playable.bme");
        require(!playbackCopy.empty() && std::filesystem::exists(playbackCopy / L"click.wav"), "test chart was not imported into Songs");
        require(SendMessageW(list, LVM_GETITEMCOUNT, 0, 0) == 2, "Songs root did not include charts from multiple folders");
        HWND header = reinterpret_cast<HWND>(SendMessageW(list, LVM_GETHEADER, 0, 0));
        require(SendMessageW(header, HDM_GETITEMCOUNT, 0, 0) == 2, "list should contain level and title only");
        RECT listRect{}, mainRect{}; GetWindowRect(list, &listRect); GetWindowRect(app.main, &mainRect);
        require(listRect.left > mainRect.left + (mainRect.right - mainRect.left) / 4, "song list was not moved to the right");
        RECT clientRect{}; GetClientRect(app.main, &clientRect);
        if (std::abs((listRect.right - listRect.left) - clientRect.right / 3) > 1)
            throw std::runtime_error("song list width must be one third of client width: list=" + std::to_string(listRect.right - listRect.left) +
                " client=" + std::to_string(clientRect.right));
        // The library root only lists folders: enter ALL SONGS (UP + every chart) before using the hidden view.
        postKey(list, VK_HOME);
        postKey(list, VK_RETURN);
        require(waitUntil([&] { return SendMessageW(list, LVM_GETITEMCOUNT, 0, 0) == 3; }), "ALL SONGS did not list the UP row and both charts");
        SendMessageW(GetDlgItem(app.main, 1028), BM_CLICK, 0, 0);
        require(waitUntil([&] { return SendMessageW(list, LVM_GETITEMCOUNT, 0, 0) == 2; }), "hidden unsupported test charts missing");
        SendMessageW(app.main, WM_COMMAND, 1003, 0);
        require((GetWindowLongPtrW(list, GWL_STYLE) & WS_VISIBLE) != 0, "hidden chart started via direct play command");
        SendMessageW(GetDlgItem(app.main, 1028), BM_CLICK, 0, 0);
        // Leaving the hidden view returns to the library root (folders only); enter ALL SONGS again.
        if (SendMessageW(list, LVM_GETITEMCOUNT, 0, 0) == 2) { postKey(list, VK_HOME); postKey(list, VK_RETURN); }
        require(waitUntil([&] { return SendMessageW(list, LVM_GETITEMCOUNT, 0, 0) == 3; }), "normal list did not return with all Songs entries");
        postKey(list, VK_HOME);
        waitUntil([&] { return SendMessageW(list, LVM_GETNEXTITEM, static_cast<WPARAM>(-1), LVNI_SELECTED) == 0; });
        postKey(list, VK_DOWN);   // row 0 is the UP row
        if (!waitUntil([&] { return SendMessageW(list, LVM_GETNEXTITEM, static_cast<WPARAM>(-1), LVNI_SELECTED) == 1; }))
            throw std::runtime_error("test chart selection: selected=" + std::to_string(SendMessageW(list, LVM_GETNEXTITEM, static_cast<WPARAM>(-1), LVNI_SELECTED)) + " rows=" + std::to_string(SendMessageW(list, LVM_GETITEMCOUNT, 0, 0)));
        SendMessageW(app.main, WM_COMMAND, MAKEWPARAM(1013, CBN_SELCHANGE), reinterpret_cast<LPARAM>(GetDlgItem(app.main, 1013)));
        SendMessageW(app.main, WM_COMMAND, 1003, 0);
        require(waitUntil([&] {
            DWORD_PTR response{};
            require(SendMessageTimeoutW(app.main, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 200, &response) != 0, "UI blocked during audio preparation");
            return (GetWindowLongPtrW(list, GWL_STYLE) & WS_VISIBLE) == 0;
        }), "test chart playback failed");
        const DWORD appThread = GetWindowThreadProcessId(app.main, nullptr);
        auto focusedWindow = [&] {
            GUITHREADINFO info{ sizeof(info) };
            return GetGUIThreadInfo(appThread, &info) ? info.hwndFocus : nullptr;
        };
        // The hidden list also means loading. Focus lost while the play loads or counts down (about
        // three seconds after the load, or the LR2 skin's lead-in) opens the song on the pause menu,
        // so this waits for loading, the countdown and the start rather than the usual five seconds.
        const ULONGLONG focusLostAt = GetTickCount64();
        if (!waitUntil([&] {
            SendMessageW(app.main, WM_KILLFOCUS, 0, 0);
            return IsWindowVisible(GetDlgItem(app.main, 1251));
        }, 20000)) throw std::runtime_error("losing focus did not pause gameplay");
        std::cout << "Focus loss paused the play after " << (GetTickCount64() - focusLostAt) << " ms\n";
        Sleep(100);
        require(IsWindowVisible(GetDlgItem(app.main, 1251)) &&
            (GetWindowLongPtrW(list, GWL_STYLE) & WS_VISIBLE) == 0, "pause menu disappeared after focus moved away");
        SendMessageW(app.main, WM_SETFOCUS, 0, 0);
        require(waitUntil([&] { return focusedWindow() == GetDlgItem(app.main, 1251); }), "pause menu focus was not restored with the window");
        postKey(app.main, VK_ESCAPE);
        require(waitUntil([&] { return !IsWindowVisible(GetDlgItem(app.main, 1251)); }), "Escape did not resume after returning from another window");
        PostMessageW(app.main, WM_KEYDOWN, VK_ESCAPE, 0);
        require(waitUntil([&] {
            return IsWindowVisible(GetDlgItem(app.main, 1251)) &&
                IsWindowVisible(GetDlgItem(app.main, 1252)) &&
                IsWindowVisible(GetDlgItem(app.main, 1253));
        }), "Escape did not open the pause menu");
        RECT pauseClient{}; GetClientRect(app.main, &pauseClient);
        std::array<RECT, 3> pauseRects{};
        for (size_t i = 0; i < pauseRects.size(); ++i) {
            GetWindowRect(GetDlgItem(app.main, 1251 + static_cast<int>(i)), &pauseRects[i]);
            MapWindowPoints(nullptr, app.main, reinterpret_cast<POINT*>(&pauseRects[i]), 2);
            const int buttonWidth = pauseRects[i].right - pauseRects[i].left;
            const int clientWidth = static_cast<int>(pauseClient.right);
            // FHD pixel limits scale with the window (ResolutionScale.h).
            const double pauseScale = bms::ui::viewport(pauseClient.right, pauseClient.bottom).scale;
            const int expectedButtonWidth = std::min(std::clamp(clientWidth * 34 / 100, bms::ui::scaled(280, pauseScale), bms::ui::scaled(560, pauseScale)), clientWidth - bms::ui::scaled(48, pauseScale));
            require(std::abs(buttonWidth - expectedButtonWidth) <= 2,
                "pause menu buttons do not use the reduced layout");
            require(std::abs(pauseRects[i].left - (pauseClient.right - buttonWidth) / 2) <= 2,
                "pause menu buttons are not horizontally centered");
            require(pauseRects[i].bottom - pauseRects[i].top <= bms::ui::scaled(144, pauseScale),
                "pause menu buttons are taller than intended");
            if (i > 0) require(pauseRects[i].top > pauseRects[i - 1].bottom,
                "pause menu buttons do not form a vertical stack");
            wchar_t label[32]{};
            GetWindowTextW(GetDlgItem(app.main, 1251 + static_cast<int>(i)), label, static_cast<int>(std::size(label)));
            require(std::wstring(label) == std::array<std::wstring, 3>{L"RESUME", L"RESTART", L"QUIT"}[i],
                "pause menu button label is missing");
        }
        require(waitUntil([&] { return focusedWindow() == GetDlgItem(app.main, 1251); }), "pause menu did not focus Resume initially");
        const std::array<std::pair<UINT, int>, 5> navigation{{
            {VK_RIGHT, 1252}, {VK_DOWN, 1253}, {VK_UP, 1252}, {VK_LEFT, 1251}, {VK_RIGHT, 1252}
        }};
        for (const auto& [key, expectedId] : navigation) {
            postKey(app.main, key);
            require(waitUntil([&] { return focusedWindow() == GetDlgItem(app.main, expectedId); }), "pause menu arrow navigation did not move focus");
        }
        screenshot(app.main, validation / L"pause-menu.bmp");
        postKey(app.main, VK_F2);
        require(waitUntil([&] { return GetPrivateProfileIntW(L"Display", L"SpeedSteps", 0, config.c_str()) == 29; }), "pause shortcut did not raise note speed");
        postKey(app.main, VK_PRIOR);
        require(waitUntil([&] { return GetPrivateProfileIntW(L"Display", L"LaneCover", 0, config.c_str()) == 40; }), "pause shortcut did not raise lane guard");
        postKey(app.main, VK_RETURN);
        require(waitUntil([&] {
            return !IsWindowVisible(GetDlgItem(app.main, 1251)) &&
                (GetWindowLongPtrW(list, GWL_STYLE) & WS_VISIBLE) == 0;
        }), "pause restart did not resume gameplay from the beginning");
        app.stop();
        std::filesystem::remove_all(playbackCopy);
        // Settings changed from the pause menu must have been persisted (checked in the config file).
        require(GetPrivateProfileIntW(L"Display", L"SpeedSteps", 0, config.c_str()) == 29, "pause speed setting was not persisted");
        require(GetPrivateProfileIntW(L"Display", L"LaneCover", 0, config.c_str()) == 40, "pause lane guard setting was not persisted");
        // Escape goes up one browse level first; make sure this run starts at the library root.
        { std::error_code ignored; std::filesystem::remove_all(validation / L".sevenmania-index" / L"libraries" / L"Navigation", ignored); }
        app.start(exe);
        // Wait for the startup splash/library load to finish: Escape cancels a load instead of asking to quit.
        require(waitUntil([&] { return (GetWindowLongPtrW(GetDlgItem(app.main, 1005), GWL_STYLE) & WS_VISIBLE) != 0 &&
            IsWindowEnabled(GetDlgItem(app.main, 1005)); }), "song list did not become ready");
        Sleep(1500);
        postKey(app.main, VK_ESCAPE);
        HWND confirm{};
        require(waitUntil([&] { confirm = FindWindowW(L"#32770", L"종료 확인"); return confirm != nullptr; }), "Escape did not show exit confirmation");
        SendMessageW(GetDlgItem(confirm, IDNO), BM_CLICK, 0, 0);
        require(waitUntil([&] { return !FindWindowW(L"#32770", L"종료 확인"); }), "No did not dismiss confirmation");
        require(WaitForSingleObject(app.process.hProcess, 0) == WAIT_TIMEOUT, "No closed the application");
        postKey(app.main, VK_ESCAPE);
        require(waitUntil([&] { confirm = FindWindowW(L"#32770", L"종료 확인"); return confirm != nullptr; }), "second Escape did not show confirmation");
        SendMessageW(GetDlgItem(confirm, IDYES), BM_CLICK, 0, 0);
        require(WaitForSingleObject(app.process.hProcess, 5000) == WAIT_OBJECT_0, "Yes did not close the application");
        app.stop();
        // Removed: the Library/LastFolder import, the status-label (1007/1008) checks and the optional real-library run.
        // They exercised features that no longer exist (songs come from registered sources, imports run in the launcher).
        std::cout << "Space from list/button, moved controls, 144/240 selection, ten-color palettes, key capture/cancel, restart persistence: passed\n";
        std::filesystem::remove_all(appRoot);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
