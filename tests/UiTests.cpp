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
bool waitUntil(const std::function<bool()>& condition) {
    const ULONGLONG start = GetTickCount64();
    while (GetTickCount64() - start < 5000) {
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
            return main && GetDlgItem(main, 1005) && GetDlgItem(main, 1011);
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
    PostThreadMessageW(thread, WM_KEYDOWN, key, 0);
    PostThreadMessageW(thread, WM_KEYUP, key, (1LL << 31) | (1LL << 30));
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
std::filesystem::path chartFolderContaining(const std::filesystem::path& root, const wchar_t* filename) {
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(root, error), end; it != end && !error; it.increment(error))
        if (it->is_regular_file(error) && it->path().filename() == filename) return it->path().parent_path();
    return {};
}
int wmain(int argc, wchar_t** argv) {
    try {
        require(argc == 3 || argc == 4, "expected player path, validation directory and optional actual song folder or --settings-only");
        const bool settingsOnly = argc == 4 && std::wstring(argv[3]) == L"--settings-only";
        const std::filesystem::path sourceExe = std::filesystem::absolute(argv[1]), validation = std::filesystem::absolute(argv[2]);
        std::filesystem::create_directories(validation);
        const auto appRoot = validation / (L"ui-app-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
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
        std::filesystem::path playbackSource, playbackCopy;
        AppProcess app; app.start(exe);
        require(waitUntil([&] { return IsWindowEnabled(GetDlgItem(app.main, 1003)) != FALSE; }), "startup song library did not finish loading");
        RECT client{}; GetClientRect(app.main, &client);
        const int panelWidth = client.right - 24 - client.right / 3 - 44;
        const int slotSize = std::max(32, std::min({100, (panelWidth - 48) / 5, static_cast<int>(client.bottom) / 8}));
        const auto clickShortcut = [&](int slot, bool increase) {
            SendMessageW(app.main, increase ? WM_LBUTTONDOWN : WM_RBUTTONDOWN, 0,
                MAKELPARAM(24 + slot * (slotSize + 12) + slotSize / 2, client.bottom - 150 + slotSize / 2));
        };
        clickShortcut(0, true);
        require(GetPrivateProfileIntW(L"Display", L"SpeedSteps", 0, config.c_str()) == 5, "left click did not increase speed shortcut");
        clickShortcut(0, false);
        require(GetPrivateProfileIntW(L"Display", L"SpeedSteps", 0, config.c_str()) == 4, "right click did not decrease speed shortcut");
        clickShortcut(1, false);
        require(GetPrivateProfileIntW(L"Gameplay", L"JudgementProfile", 99, config.c_str()) == 1, "right click did not decrease judgement difficulty");
        clickShortcut(1, true);
        clickShortcut(2, false);
        require(GetPrivateProfileIntW(L"Gameplay", L"GrooveGaugeMode", 99, config.c_str()) == 1, "right click did not select easy gauge");
        clickShortcut(2, true);
        require(GetPrivateProfileIntW(L"Gameplay", L"GrooveGaugeMode", 99, config.c_str()) == 0, "left click did not return to normal gauge");
        clickShortcut(3, true); clickShortcut(4, false);
        require(GetPrivateProfileIntW(L"Display", L"SpeedSteps", 0, config.c_str()) == 4, "empty shortcut changed settings");
        // Space must work when a child list, then a child button, receives the key.
        HWND settings = app.settingsFromSpace(GetDlgItem(app.main, 1005));
        require(GetDlgItem(app.main, 1010) == nullptr && GetDlgItem(app.main, 1009) == nullptr, "old setting buttons remain in song list");
        require(settingsItem(settings, 1010) && settingsItem(settings, 1009), "setting buttons were not moved");
        for (int id : {1260, 1261, 1262, 1263, 1264, 1265})
            require(IsWindowVisible(settingsItem(settings, id)), "summary setting is missing from the initial tab");
        require(!IsWindowVisible(settingsItem(settings, 1012)), "gameplay settings showed instead of the initial summary tab");
        choose(settings, 1260, 3);
        require(GetPrivateProfileIntW(L"Display", L"FPS", 0, config.c_str()) == 400 &&
            SendMessageW(settingsItem(settings, 1012), CB_GETCURSEL, 0, 0) == 3, "summary FPS was not synchronized with graphics settings");
        choose(settings, 1260, 2);
        choose(settings, 1261, 4);
        require(GetPrivateProfileIntW(L"Display", L"Brightness", 255, config.c_str()) == 40 &&
            SendMessageW(settingsItem(settings, 1021), CB_GETCURSEL, 0, 0) == 4, "summary BGA brightness was not synchronized with gameplay settings");
        choose(settings, 1261, 6);
        choose(settings, 1262, 8);
        require(GetPrivateProfileIntW(L"Display", L"SpeedSteps", 0, config.c_str()) == 10 &&
            SendMessageW(settingsItem(settings, 1013), CB_GETCURSEL, 0, 0) == 8, "summary note speed was not synchronized with gameplay settings");
        choose(settings, 1262, 6);
        choose(settings, 1263, 1);
        require(GetPrivateProfileIntW(L"Gameplay", L"JudgementProfile", -1, config.c_str()) == 1 &&
            SendMessageW(settingsItem(settings, 1026), CB_GETCURSEL, 0, 0) == 1, "summary judgement profile was not synchronized");
        choose(settings, 1263, 2);
        choose(settings, 1265, 1);
        require(GetPrivateProfileIntW(L"Gameplay", L"GrooveGaugeMode", -1, config.c_str()) == 1 &&
            SendMessageW(settingsItem(settings, 1029), CB_GETCURSEL, 0, 0) == 1, "summary gauge mode was not synchronized");
        choose(settings, 1265, 0);
        require(SendMessageW(settingsItem(settings, 1264), CB_GETCOUNT, 0, 0) == 3, "summary screen mode choices missing");
        screenshot(settings, validation / L"settings-summary.bmp");
        const auto relativeTop = [&](HWND control) {
            RECT bounds{}; GetWindowRect(control, &bounds);
            MapWindowPoints(nullptr, settings, reinterpret_cast<POINT*>(&bounds), 2);
            return bounds.top;
        };
        HWND pageViewport = GetParent(settingsItem(settings, 1013));
        require(pageViewport != settings && relativeTop(pageViewport) == 96, "page controls need a clipped viewport under navigation");
        for (int cycle = 0; cycle < 4; ++cycle) {
            SendMessageW(settings, WM_VSCROLL, SB_BOTTOM, 0);
            require(relativeTop(settingsItem(settings, 1060)) == 52, "settings scroll displaced category buttons");
            SendMessageW(settingsItem(settings, 1062), BM_CLICK, 0, 0);
            SendMessageW(settingsItem(settings, 1060), BM_CLICK, 0, 0);
            require(relativeTop(settingsItem(settings, 1013)) == 220, "tab changes accumulated scroll offsets");
        }
        SendMessageW(settings, WM_VSCROLL, SB_BOTTOM, 0);
        screenshot(settings, validation / L"settings-scrolled.bmp");
        SendMessageW(settings, WM_VSCROLL, SB_TOP, 0);
        require(SendMessageW(settingsItem(settings, 1029), CB_GETCOUNT, 0, 0) == 3 &&
            SendMessageW(settingsItem(settings, 1029), CB_GETCURSEL, 0, 0) == 0, "normal/easy/hard groove gauge choices missing or wrong default");
        choose(settings, 1029, 2);
        require(GetPrivateProfileIntW(L"Gameplay", L"GrooveGaugeMode", -1, config.c_str()) == 2, "hard gauge selection not saved");
        choose(settings, 1012, 1); // 144 FPS
        require(GetPrivateProfileIntW(L"Display", L"FPS", 0, config.c_str()) == 144, "144 FPS selection not saved");
        require(SendMessageW(settingsItem(settings, 1012), CB_GETCOUNT, 0, 0) == 6 &&
            SendMessageW(settingsItem(settings, 1260), CB_GETCOUNT, 0, 0) == 6, "VSync missing from graphics or summary choices");
        choose(settings, 1260, 5); // VSync from summary
        require(static_cast<int>(GetPrivateProfileIntW(L"Display", L"FPS", 0, config.c_str())) == -1 &&
            SendMessageW(settingsItem(settings, 1012), CB_GETCURSEL, 0, 0) == 5, "VSync was not saved/synchronized");
        choose(settings, 1012, 4); // Unlimited, capped at 1500
        require(GetPrivateProfileIntW(L"Display", L"FPS", 99, config.c_str()) == 0 &&
            SendMessageW(settingsItem(settings, 1260), CB_GETCURSEL, 0, 0) == 4, "unlimited did not replace VSync");
        choose(settings, 1012, 2); // 240 FPS
        choose(settings, 1013, 6); // 2.00x
        require(SendMessageW(settingsItem(settings, 1024), CB_GETCOUNT, 0, 0) == 3, "gear positions missing");
        require(SendMessageW(settingsItem(settings, 1024), CB_GETCURSEL, 0, 0) == 1, "gear must default to center");
        for (int position : {0, 2, 1}) {
            choose(settings, 1024, position);
            require(GetPrivateProfileIntW(L"Display", L"GearPosition", 9, config.c_str()) == static_cast<UINT>(position), "gear position not saved");
        }
        SendMessageW(settingsItem(settings, 1061), BM_CLICK, 0, 0);
        HWND gpuOption = settingsItem(settings, 1025);
        require(gpuOption && IsWindowVisible(gpuOption), "GPU option must be on the graphics page");
        HWND resolution = settingsItem(settings, 1048);
        require(resolution && IsWindowVisible(resolution) && SendMessageW(resolution, CB_GETCOUNT, 0, 0) >= 2,
            "graphics resolution choices are missing");
        choose(settings, 1048, 1);
        require(GetPrivateProfileIntW(L"Display", L"ResolutionWidth", 0, config.c_str()) > 0 &&
            GetPrivateProfileIntW(L"Display", L"ResolutionHeight", 0, config.c_str()) > 0,
            "selected resolution was not saved");
        choose(settings, 1048, 0);
        require(GetPrivateProfileIntW(L"Display", L"ResolutionWidth", -1, config.c_str()) == 0 &&
            GetPrivateProfileIntW(L"Display", L"ResolutionHeight", -1, config.c_str()) == 0,
            "automatic resolution was not saved");
        wchar_t gpuLabel[64]{};
        GetWindowTextW(gpuOption, gpuLabel, static_cast<int>(std::size(gpuLabel)));
        require(std::wstring(gpuLabel).find(L"DirectComposition") != std::wstring::npos, "DirectComposition option label missing");
        require(SendMessageW(settingsItem(settings, 1270), BM_GETCHECK, 0, 0) == BST_CHECKED &&
            SendMessageW(settingsItem(settings, 1271), BM_GETCHECK, 0, 0) == BST_CHECKED, "note rendering defaults changed");
        require(!IsWindowEnabled(settingsItem(settings, 1272)) && SendMessageW(settingsItem(settings, 1272), CB_GETCURSEL, 0, 0) == 0 &&
            SendMessageW(settingsItem(settings, 1272), CB_GETCOUNT, 0, 0) == 3, "SDK-free latency choices should remain unavailable and off");
        SendMessageW(settingsItem(settings, 1270), BM_CLICK, 0, 0);
        SendMessageW(settingsItem(settings, 1271), BM_CLICK, 0, 0);
        require(GetPrivateProfileIntW(L"Display", L"NoteSubpixel", 1, config.c_str()) == 0 &&
            GetPrivateProfileIntW(L"Display", L"NoteAntialias", 1, config.c_str()) == 0, "note rendering choices were not saved");
        screenshot(settings, validation / L"graphics-stage2.bmp");
        SendMessageW(gpuOption, BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(gpuOption, BM_CLICK, 0, 0);
        require(GetPrivateProfileIntW(L"Display", L"DirectComposition", 0, config.c_str()) == 1, "composition option not saved");
        HWND gpuLimit = settingsItem(settings, 1043);
        require(gpuLimit && IsWindowEnabled(gpuLimit) && SendMessageW(gpuLimit, TBM_GETPOS, 0, 0) == 100,
            "GPU usage limit should be available when GPU rendering is enabled");
        SendMessageW(gpuLimit, TBM_SETPOS, TRUE, 63);
        SendMessageW(settings, WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, 63), reinterpret_cast<LPARAM>(gpuLimit));
        require(GetPrivateProfileIntW(L"System", L"GpuLimitPercent", 0, config.c_str()) == 63,
            "GPU usage limit was not saved");
        SendMessageW(settingsItem(settings, 1063), BM_CLICK, 0, 0);
        HWND resourceUsage = settingsItem(settings, 1044);
        HWND cpuLimit = settingsItem(settings, 1041);
        HWND memoryLimit = settingsItem(settings, 1042);
        require(resourceUsage && cpuLimit && memoryLimit && IsWindowVisible(resourceUsage) &&
            SendMessageW(cpuLimit, TBM_GETPOS, 0, 0) == 100 && SendMessageW(memoryLimit, TBM_GETPOS, 0, 0) == 100,
            "system resource controls should appear in Other and default to 100%");
        SendMessageW(cpuLimit, TBM_SETPOS, TRUE, 57);
        SendMessageW(settings, WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, 57), reinterpret_cast<LPARAM>(cpuLimit));
        require(GetPrivateProfileIntW(L"System", L"CpuLimitPercent", 0, config.c_str()) == 57,
            "CPU usage limit was not saved");
        SendMessageW(memoryLimit, TBM_SETPOS, TRUE, 90);
        SendMessageW(settings, WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, 90), reinterpret_cast<LPARAM>(memoryLimit));
        require(GetPrivateProfileIntW(L"System", L"MemoryLimitPercent", 0, config.c_str()) == 90,
            "memory usage limit was not saved");
        SendMessageW(memoryLimit, TBM_SETPOS, TRUE, 100);
        SendMessageW(settings, WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, 100), reinterpret_cast<LPARAM>(memoryLimit));
        SendMessageW(resourceUsage, BM_CLICK, 0, 0);
        require(GetPrivateProfileIntW(L"System", L"ShowUsage", 0, config.c_str()) == 1,
            "system resource monitor option was not saved");
        SendMessageW(resourceUsage, BM_CLICK, 0, 0);
        require(GetPrivateProfileIntW(L"System", L"ShowUsage", 1, config.c_str()) == 0,
            "system resource monitor option could not be disabled");
        SendMessageW(resourceUsage, BM_CLICK, 0, 0);
        SendMessageW(settingsItem(settings, 1060), BM_CLICK, 0, 0);
        HWND cover = settingsItem(settings, 1022);
        require(cover && SendMessageW(cover, TBM_GETPOS, 0, 0) == 0, "default cover must be none");
        for (int percent : {100, 0, 35}) {
            SendMessageW(cover, TBM_SETPOS, TRUE, percent);
            SendMessageW(settings, WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, percent), reinterpret_cast<LPARAM>(cover));
            require(GetPrivateProfileIntW(L"Display", L"LaneCover", 255, config.c_str()) == static_cast<UINT>(percent), "cover not saved");
        }
        for (int i = 0; i < 7; ++i) {
            HWND button = settingsItem(settings, 1200 + i);
            require(button && SendMessageW(button, BM_GETCHECK, 0, 0) == (i == 0 ? BST_CHECKED : BST_UNCHECKED), "default separators");
            SendMessageW(button, BM_CLICK, 0, 0);
            const std::wstring key = L"Separator" + std::to_wstring(i);
            require(GetPrivateProfileIntW(L"Display", key.c_str(), 255, config.c_str()) == (i == 0 ? 0u : 1u), "separator toggle not saved");
        }
        require(SendMessageW(settingsItem(settings, 1021), CB_GETCOUNT, 0, 0) == 11, "BGA darkness choices");
        for (int darkness : {0, 100, 50}) {
            choose(settings, 1021, darkness / 10);
            require(GetPrivateProfileIntW(L"Display", L"Brightness", 255, config.c_str()) == static_cast<UINT>(darkness), "note brightness not saved");
        }
        require(SendMessageW(settingsItem(settings, 1013), CB_GETCOUNT, 0, 0) == 39, "speed options must cover 0.50x through 10.00x");
        choose(settings, 1013, 38);
        require(GetPrivateProfileIntW(L"Display", L"SpeedSteps", 0, config.c_str()) == 40, "10.00x speed selection not saved");
        choose(settings, 1013, 6);
        HWND noteHeight = settingsItem(settings, 1027);
        require(noteHeight && SendMessageW(noteHeight, CB_GETCOUNT, 0, 0) == 15 && SendMessageW(noteHeight, CB_GETCURSEL, 0, 0) == 4,
            "note height choices or default value missing");
        choose(settings, 1027, 8);
        require(GetPrivateProfileIntW(L"Gameplay", L"NoteHeight", 0, config.c_str()) == 20, "note height selection not saved");
        choose(settings, 1027, 4);
        HWND transparency = settingsItem(settings, 1020);
        require(transparency && SendMessageW(transparency, TBM_GETPOS, 0, 0) == 34, "default lane transparency");
        for (int percent : {0, 100, 60}) {
            SendMessageW(transparency, TBM_SETPOS, TRUE, percent);
            SendMessageW(settings, WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, percent), reinterpret_cast<LPARAM>(transparency));
            require(GetPrivateProfileIntW(L"Display", L"LaneTransparency", 255, config.c_str()) == static_cast<UINT>(percent), "transparency selection not saved");
        }
        for (int lane = 0; lane < 8; ++lane) {
            require(SendMessageW(settingsItem(settings, 1100 + lane), CB_GETCOUNT, 0, 0) == 10, "palette does not contain ten colors");
            choose(settings, 1100 + lane, (lane + 2) % 10);
        }
        SendMessageW(settingsItem(settings, 1062), BM_CLICK, 0, 0);
        require((GetWindowLongPtrW(settingsItem(settings, 1030), GWL_STYLE) & WS_VISIBLE) != 0, "sound tab did not show volume controls");
        require(SendMessageW(settingsItem(settings, 1034), CB_GETCOUNT, 0, 0) == 6, "six audio buffer choices missing");
        for (int option = 0; option < 6; ++option) {
            choose(settings, 1034, option);
            require(GetPrivateProfileIntW(L"Sound", L"BufferFrames", 0, config.c_str()) == static_cast<UINT>(32 << option), "sound buffer not saved");
        }
        screenshot(settings, validation / L"sound-settings.bmp");
        HWND previewVolume = settingsItem(settings, 1040);
        require(previewVolume && settingsItem(settings, 1039), "song preview options missing");
        SendMessageW(previewVolume, TBM_SETPOS, TRUE, 39);
        SendMessageW(settings, WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, 39), reinterpret_cast<LPARAM>(previewVolume));
        require(GetPrivateProfileIntW(L"Sound", L"PreviewVolume", 255, config.c_str()) == 39, "preview volume not saved");
        SendMessageW(settingsItem(settings, 1039), BM_CLICK, 0, 0);
        require(GetPrivateProfileIntW(L"Sound", L"SongPreview", 1, config.c_str()) == 0, "preview disable state not saved");
        SendMessageW(settingsItem(settings, 1039), BM_CLICK, 0, 0);
        require(GetPrivateProfileIntW(L"Sound", L"SongPreview", 0, config.c_str()) == 1, "preview enabled state not saved");
        SendMessageW(settingsItem(settings, 1039), BM_CLICK, 0, 0);
        SendMessageW(settingsItem(settings, 1063), BM_CLICK, 0, 0);
        require(SendMessageW(settingsItem(settings, 1035), CB_GETCOUNT, 0, 0) == 4, "four browse background choices missing");
        choose(settings, 1035, 2);
        require(GetPrivateProfileIntW(L"Browse", L"Background", 255, config.c_str()) == 2, "background mode not saved");
        for (const auto& control : {std::pair{1037, L"Blur"}, std::pair{1038, L"Opacity"}}) {
            HWND slider = settingsItem(settings, control.first);
            SendMessageW(slider, TBM_SETPOS, TRUE, 73);
            SendMessageW(settings, WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, 73), reinterpret_cast<LPARAM>(slider));
            require(GetPrivateProfileIntW(L"Browse", control.second, 255, config.c_str()) == 73, "background slider not saved");
        }
        screenshot(settings, validation / L"browse-background-settings.bmp");
        require(IsWindowVisible(settingsItem(settings, 1237)) && IsWindowVisible(settingsItem(settings, 1238)), "background slider labels are hidden");
        choose(settings, 1035, 0);
        SendMessageW(settingsItem(settings, 1062), BM_CLICK, 0, 0);
        RECT bufferRect{}, footerRect{}; GetWindowRect(settingsItem(settings, 1034), &bufferRect);
        GetWindowRect(settingsItem(settings, 1016), &footerRect);
        require(bufferRect.bottom < footerRect.top, "sound buffer and fixed status footer overlap");
        const wchar_t* volumeNames[]{L"MasterVolume", L"MusicVolume", L"KeyVolume", L"SfxVolume"};
        for (int i = 0; i < 4; ++i) {
            HWND volume = settingsItem(settings, 1030 + i);
            const int value = 25 + i * 15;
            SendMessageW(volume, TBM_SETPOS, TRUE, value);
            SendMessageW(settings, WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, value), reinterpret_cast<LPARAM>(volume));
            require(GetPrivateProfileIntW(L"Sound", volumeNames[i], 255, config.c_str()) == static_cast<UINT>(value), "sound volume was not saved");
        }
        SendMessageW(settingsItem(settings, 1060), BM_CLICK, 0, 0);
        SendMessageW(settings, WM_COMMAND, 1010, 0);
        const std::array<UINT, 8> keys{ VK_LSHIFT, 'Z', 'X', 'C', VK_SPACE, 'B', 'N', 'M' };
        for (UINT key : keys) postKey(settings, key);
        if (!waitUntil([&] { return GetPrivateProfileIntW(L"KeyBindings", L"Key7", 0, config.c_str()) == 'M'; })) {
            wchar_t status[256]{};
            SendMessageW(settingsItem(settings, 1016), WM_GETTEXT, 256, reinterpret_cast<LPARAM>(status));
            std::wcerr << L"Capture status: " << status << L'\n';
            require(false, "key capture did not save all eight keys");
        }
        require(GetPrivateProfileIntW(L"KeyBindings", L"Key4", 0, config.c_str()) == VK_SPACE, "Space could not be assigned to a playing lane");
        SendMessageW(settings, WM_COMMAND, 1010, 0);
        postKey(settings, 'A'); postKey(settings, 'A'); postKey(settings, VK_ESCAPE);
        Sleep(100);
        require(GetPrivateProfileIntW(L"KeyBindings", L"Scratch", 0, config.c_str()) == VK_LSHIFT, "cancel/duplicate key capture changed saved bindings");
        screenshot(settings, validation / L"settings.bmp");
        PostMessageW(settings, WM_CLOSE, 0, 0);
        require(waitUntil([&] { return !IsWindow(settings) && IsWindowEnabled(app.main); }), "settings close did not restore browse");
        screenshot(app.main, validation / L"song-select.bmp");
        settings = app.settingsFromSpace(GetDlgItem(app.main, 1001));
        choose(settings, 1012, 5);
        PostMessageW(settings, WM_CLOSE, 0, 0);
        require(waitUntil([&] { return !IsWindow(settings); }), "second settings close failed");
        app.stop(); app.start(exe);
        settings = app.settingsFromSpace(GetDlgItem(app.main, 1005));
        require(SendMessageW(settingsItem(settings, 1012), CB_GETCURSEL, 0, 0) == 5, "VSync did not survive restart");
        choose(settings, 1012, 2);
        require(SendMessageW(settingsItem(settings, 1013), CB_GETCURSEL, 0, 0) == 6, "speed did not survive restart");
        require(SendMessageW(settingsItem(settings, 1027), CB_GETCURSEL, 0, 0) == 4, "note height did not survive restart");
        require(SendMessageW(settingsItem(settings, 1029), CB_GETCURSEL, 0, 0) == 2, "gauge mode did not survive restart");
        require(SendMessageW(settingsItem(settings, 1024), CB_GETCURSEL, 0, 0) == 1, "gear did not survive restart");
        require(SendMessageW(settingsItem(settings, 1025), BM_GETCHECK, 0, 0) == BST_CHECKED, "composition did not survive restart");
        require(SendMessageW(settingsItem(settings, 1270), BM_GETCHECK, 0, 0) == BST_UNCHECKED &&
            SendMessageW(settingsItem(settings, 1271), BM_GETCHECK, 0, 0) == BST_UNCHECKED, "note rendering choices did not survive restart");
        SendMessageW(settingsItem(settings, 1062), BM_CLICK, 0, 0);
        require(SendMessageW(settingsItem(settings, 1034), CB_GETCURSEL, 0, 0) == 5, "sound buffer did not survive restart");
        require(SendMessageW(settingsItem(settings, 1030), TBM_GETPOS, 0, 0) == 25 && SendMessageW(settingsItem(settings, 1033), TBM_GETPOS, 0, 0) == 70, "sound volumes did not survive restart");
        SendMessageW(settingsItem(settings, 1060), BM_CLICK, 0, 0);
        require(SendMessageW(settingsItem(settings, 1020), TBM_GETPOS, 0, 0) == 60, "lane transparency did not survive restart");
        require(SendMessageW(settingsItem(settings, 1022), TBM_GETPOS, 0, 0) == 35, "cover did not survive restart");
        for (int i = 0; i < 7; ++i)
            require(SendMessageW(settingsItem(settings, 1200 + i), BM_GETCHECK, 0, 0) == (i == 0 ? BST_UNCHECKED : BST_CHECKED), "separator did not survive restart");
        require(SendMessageW(settingsItem(settings, 1021), CB_GETCURSEL, 0, 0) == 5, "BGA darkness did not survive restart");
        for (int lane = 0; lane < 8; ++lane)
            require(SendMessageW(settingsItem(settings, 1100 + lane), CB_GETCURSEL, 0, 0) == (lane + 2) % 10, "note color did not survive restart");
        PostMessageW(settings, WM_CLOSE, 0, 0);
        require(waitUntil([&] { return !IsWindow(settings); }), "last settings close failed");
        app.stop();
        // Keep browse/playback shortcut checks independent from earlier UI
        // settings persistence checks, which intentionally saved 10.00x.
        if (settingsOnly) {
            std::cout << "Settings: VSync/unlimited selection, summary synchronization and restart persistence passed.\n";
            std::filesystem::remove_all(appRoot);
            return 0;
        }
        WritePrivateProfileStringW(L"Display", L"SpeedSteps", L"28", config.c_str());
        playbackSource = validation / (L"ui-playback-fixture-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directory(playbackSource);
        writeWave(playbackSource / L"click.wav", 44100 * 10);
        writeChart(playbackSource / L"playable.bme", "UI Test [LIGHT 7]", "6", "#00011:01\n#00018:01\n#00418:01\n");
        writeChart(playbackSource / L"five.bms", "UI Test 5-key", "2", "#00011:01\n#00015:01\n");
        writeChart(playbackSource / L"double.bme", "UI Test DP", "8", "#00011:01\n#00021:01\n#00028:01\n");
        writeChart(playbackSource / L"popn.pms", "UI Test Pop'n", "10", "#00011:01\n#00019:01\n");
        WritePrivateProfileStringW(L"Library", L"LastFolder", playbackSource.c_str(), config.c_str());
        app.start(exe);
        HWND list = GetDlgItem(app.main, 1005);
        const bool scanFinished = waitUntil([&] {
            wchar_t status[1024]{};
            SendMessageW(GetDlgItem(app.main, 1007), WM_GETTEXT, 1024, reinterpret_cast<LPARAM>(status));
            return std::wstring(status).find(L"5개 채보를 불러왔습니다") != std::wstring::npos;
        });
        if (!scanFinished) {
            wchar_t status[1024]{};
            SendMessageW(GetDlgItem(app.main, 1007), WM_GETTEXT, 1024, reinterpret_cast<LPARAM>(status));
            std::wcerr << L"Scan status: " << status << L'\n';
        }
        require(scanFinished, "background scan did not finish");
        wchar_t playbackFolder[32768]{};
        GetPrivateProfileStringW(L"Library", L"LastFolder", L"", playbackFolder, 32768, config.c_str());
        const auto libraryRoot = exe.parent_path() / L"Songs";
        require(std::filesystem::path(playbackFolder) == libraryRoot, "active song path did not stay at the Songs root");
        playbackCopy = chartFolderContaining(libraryRoot, L"playable.bme");
        require(!playbackCopy.empty() && std::filesystem::exists(playbackCopy / L"click.wav"), "test chart was not imported into Songs");
        wchar_t displayedFolder[32768]{};
        SendMessageW(GetDlgItem(app.main, 1006), WM_GETTEXT, 32768, reinterpret_cast<LPARAM>(displayedFolder));
        require(std::wstring(displayedFolder).find(libraryRoot.wstring()) != std::wstring::npos, "displayed song path was not the Songs root");
        std::filesystem::remove_all(playbackSource);
        require(SendMessageW(list, LVM_GETITEMCOUNT, 0, 0) == 2, "Songs root did not include charts from multiple folders");
        HWND header = reinterpret_cast<HWND>(SendMessageW(list, LVM_GETHEADER, 0, 0));
        require(SendMessageW(header, HDM_GETITEMCOUNT, 0, 0) == 2, "list should contain level and title only");
        RECT listRect{}, mainRect{}; GetWindowRect(list, &listRect); GetWindowRect(app.main, &mainRect);
        require(listRect.left > mainRect.left + (mainRect.right - mainRect.left) / 4, "song list was not moved to the right");
        RECT clientRect{}; GetClientRect(app.main, &clientRect);
        if (std::abs((listRect.right - listRect.left) - clientRect.right / 3) > 1)
            throw std::runtime_error("song list width must be one third of client width: list=" + std::to_string(listRect.right - listRect.left) +
                " client=" + std::to_string(clientRect.right));
        SendMessageW(GetDlgItem(app.main, 1028), BM_CLICK, 0, 0);
        require(SendMessageW(list, LVM_GETITEMCOUNT, 0, 0) == 3, "hidden unsupported test charts missing");
        require(!IsWindowEnabled(GetDlgItem(app.main, 1003)), "hidden chart play button enabled");
        SendMessageW(app.main, WM_COMMAND, 1003, 0);
        require((GetWindowLongPtrW(list, GWL_STYLE) & WS_VISIBLE) != 0, "hidden chart started via direct play command");
        SendMessageW(GetDlgItem(app.main, 1028), BM_CLICK, 0, 0);
        require(SendMessageW(list, LVM_GETITEMCOUNT, 0, 0) == 2, "normal list did not return with all Songs entries");
        postKey(list, VK_HOME);
        require(waitUntil([&] { return SendMessageW(list, LVM_GETNEXTITEM, static_cast<WPARAM>(-1), LVNI_SELECTED) == 0; }), "test chart selection");
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
        // The hidden list also means loading; decoder/title fade duration varies.
        require(waitUntil([&] {
            SendMessageW(app.main, WM_KILLFOCUS, 0, 0);
            return IsWindowVisible(GetDlgItem(app.main, 1251));
        }), "losing focus did not pause gameplay");
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
            const int expectedButtonWidth = std::min(std::clamp(clientWidth * 34 / 100, 280, 560), clientWidth - 48);
            require(std::abs(buttonWidth - expectedButtonWidth) <= 2,
                "pause menu buttons do not use the reduced layout");
            require(std::abs(pauseRects[i].left - (pauseClient.right - buttonWidth) / 2) <= 2,
                "pause menu buttons are not horizontally centered");
            require(pauseRects[i].bottom - pauseRects[i].top <= 144,
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
        app.start(exe);
        settings = app.settingsFromSpace(GetDlgItem(app.main, 1005));
        require(SendMessageW(settingsItem(settings, 1013), CB_GETCURSEL, 0, 0) == 27, "pause speed setting did not survive restart");
        require(SendMessageW(settingsItem(settings, 1022), TBM_GETPOS, 0, 0) == 40, "pause lane guard setting did not survive restart");
        PostMessageW(settings, WM_CLOSE, 0, 0);
        require(waitUntil([&] { return !IsWindow(settings); }), "final settings close failed");
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
        const auto importSource = validation / (L"ui-import-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        std::filesystem::create_directory(importSource);
        for (const auto& [file, title, artist, level] : std::array<std::array<const char*, 4>, 3>{{
            {{"light.bme", "Group Song [LIGHT 7]", "composer", "6"}},
            {{"normal.bme", "Group Song [NORMAL 7]", "composer / obj: author", "8"}},
            {{"blocked.bme", "Group Song [BLOCKED 7]", "composer", "1"}}
        }}) {
            const auto path = importSource / file;
            writeChart(path, title, level, "#00011:01\n#00018:01\n");
            std::ofstream output(path, std::ios::app);
            output << "#SUBTITLE \n#ARTIST " << artist << '\n';
            if (std::string(file) == "blocked.bme") output << "#WAV01 absent.wav\n";
        }
        writeWave(importSource / L"click.wav");
        WritePrivateProfileStringW(L"Library", L"LastFolder", importSource.c_str(), config.c_str());
        app.start(exe);
        auto scanComplete = [&] {
            DWORD_PTR response{};
            require(SendMessageTimeoutW(app.main, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 200, &response) != 0, "UI blocked during song import");
            wchar_t text[256]{};
            SendMessageW(GetDlgItem(app.main, 1007), WM_GETTEXT, 256, reinterpret_cast<LPARAM>(text));
            return std::wstring(text).find(L"4개 채보를 불러왔습니다") != std::wstring::npos;
        };
        require(waitUntil(scanComplete), "imported library scan did not complete");
        wchar_t storedFolder[32768]{};
        GetPrivateProfileStringW(L"Library", L"LastFolder", L"", storedFolder, 32768, config.c_str());
        const std::filesystem::path importedFolder(storedFolder);
        const auto managedLibrary = exe.parent_path() / L"Songs";
        const auto importedCopy = chartFolderContaining(managedLibrary, L"light.bme");
        require(importedFolder == managedLibrary && !importedCopy.empty() && std::filesystem::exists(importedCopy / L"click.wav"),
            "import copy was not stored under Songs or active path did not stay at its root");
        app.stop();
        std::filesystem::remove_all(importSource);
        app.start(exe);
        require(waitUntil(scanComplete), "managed copy did not load after removing the original");
        HWND importedList = GetDlgItem(app.main, 1005);
        require(SendMessageW(importedList, LVM_GETITEMCOUNT, 0, 0) == 2, "imported charts and existing Songs entries were not both listed");
        postKey(importedList, VK_HOME);
        require(waitUntil([&] { return SendMessageW(importedList, LVM_GETNEXTITEM, static_cast<WPARAM>(-1), LVNI_SELECTED) == 0; }), "imported chart selection");
        const auto selectedVariant = [&](const wchar_t* label) {
            wchar_t info[2048]{};
            SendMessageW(GetDlgItem(app.main, 1008), WM_GETTEXT, 2048, reinterpret_cast<LPARAM>(info));
            return std::wstring(info).find(label) != std::wstring::npos;
        };
        if (!waitUntil([&] { return selectedVariant(L"[LIGHT 7]"); })) {
            wchar_t info[2048]{};
            SendMessageW(GetDlgItem(app.main, 1008), WM_GETTEXT, 2048, reinterpret_cast<LPARAM>(info));
            const int length = WideCharToMultiByte(CP_UTF8, 0, info, -1, nullptr, 0, nullptr, nullptr);
            std::string utf8(static_cast<size_t>(length), '\0');
            WideCharToMultiByte(CP_UTF8, 0, info, -1, utf8.data(), length, nullptr, nullptr);
            throw std::runtime_error("lowest exact level was not selected initially: " + utf8);
        }
        SendMessageW(importedList, WM_KEYDOWN, VK_RIGHT, 0);
        require(waitUntil([&] { return selectedVariant(L"[NORMAL 7]"); }), "right arrow did not switch chart");
        screenshot(app.main, validation / L"song-select-preview.bmp");
        SendMessageW(GetDlgItem(app.main, 1028), BM_CLICK, 0, 0);
        require(SendMessageW(importedList, LVM_GETITEMCOUNT, 0, 0) == 1 && !IsWindowEnabled(GetDlgItem(app.main, 1003)), "blocked variant was not separated into hidden list");
        SendMessageW(GetDlgItem(app.main, 1028), BM_CLICK, 0, 0);
        require(waitUntil([&] { return selectedVariant(L"[NORMAL 7]"); }), "selected difficulty was lost after hidden view");
        SendMessageW(app.main, WM_KEYDOWN, VK_LEFT, 0);
        require(waitUntil([&] { return selectedVariant(L"[LIGHT 7]"); }), "left arrow did not restore chart");
        SendMessageW(app.main, WM_COMMAND, 1003, 0);
        require(waitUntil([&] { return (GetWindowLongPtrW(importedList, GWL_STYLE) & WS_VISIBLE) == 0; }), "managed chart playback failed");
        app.stop();
        std::filesystem::remove_all(importedCopy);
        if (argc == 4) {
            const std::filesystem::path folder(argv[3]);
            WritePrivateProfileStringW(L"Library", L"LastFolder", folder.c_str(), config.c_str());
            app.start(exe);
            HWND actualList = GetDlgItem(app.main, 1005);
            auto responsiveWait = [&](const std::function<bool()>& done) {
                const auto started = GetTickCount64();
                while (!done() && GetTickCount64() - started < 60000) {
                    DWORD_PTR response{};
                    require(SendMessageTimeoutW(app.main, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 250, &response) != 0, "actual-song load blocked UI");
                    Sleep(10);
                }
                require(done(), "actual song load timed out");
            };
            responsiveWait([&] {
                wchar_t status[1024]{};
                SendMessageW(GetDlgItem(app.main, 1007), WM_GETTEXT, 1024, reinterpret_cast<LPARAM>(status));
                return std::wstring(status).find(L"개 채보를 불러왔습니다") != std::wstring::npos;
            });
            postKey(actualList, VK_HOME);
            require(waitUntil([&] { return SendMessageW(actualList, LVM_GETNEXTITEM, static_cast<WPARAM>(-1), LVNI_SELECTED) == 0; }), "actual chart selection failed");
            const auto rows = SendMessageW(actualList, LVM_GETITEMCOUNT, 0, 0);
            for (LRESULT row = 1; row < rows && !IsWindowEnabled(GetDlgItem(app.main, 1003)); ++row) {
                postKey(actualList, VK_DOWN);
                require(waitUntil([&] { return SendMessageW(actualList, LVM_GETNEXTITEM, static_cast<WPARAM>(-1), LVNI_SELECTED) == row; }), "actual song navigation failed");
            }
            require(IsWindowEnabled(GetDlgItem(app.main, 1003)), "actual library has no playable default chart");
            SendMessageW(app.main, WM_COMMAND, 1003, 0);
            responsiveWait([&] { return (GetWindowLongPtrW(actualList, GWL_STYLE) & WS_VISIBLE) == 0; });
            std::cout << "Actual song folder scan and audio preparation: UI response remained within 250ms.\n";
            app.stop();
        }
        std::cout << "Space from list/button, moved controls, 144/240 selection, ten-color palettes, key capture/cancel, restart persistence: passed\n";
        std::filesystem::remove_all(appRoot);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
