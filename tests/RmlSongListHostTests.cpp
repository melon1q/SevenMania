#include "../src/Main.cpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

LRESULT CALLBACK shortcutProbe(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
    const int command = LOWORD(wParam);
    if (message == WM_COMMAND && (command == ID_SETTINGS || command == ID_OPEN || command == ID_RESCAN)) {
        reinterpret_cast<std::vector<int>*>(data)->push_back(command);
        return 0; // Verify routing without opening a modal picker or scanning files.
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

void verifyBrowseShortcuts(HWND window) {
    require(!GetDlgItem(window, ID_SETTINGS) && !GetDlgItem(window, ID_OPEN) && !GetDlgItem(window, ID_RESCAN),
        "browse shortcut buttons still exist");
    std::vector<int> commands;
    require(SetWindowSubclass(window, shortcutProbe, 42, reinterpret_cast<DWORD_PTR>(&commands)) != FALSE,
        "unable to observe browse shortcut commands");
    for (const auto& [key, command] : std::array<std::pair<UINT, int>, 3>{{{VK_SPACE, ID_SETTINGS}, {VK_F2, ID_OPEN}, {VK_F5, ID_RESCAN}}}) {
        const size_t previous = commands.size();
        SendMessageW(gList, WM_KEYDOWN, key, 0);
        SendMessageW(gList, WM_KEYDOWN, key, 1LL << 30);
        SendMessageW(gList, WM_KEYUP, key, 0);
        require(commands.size() == previous + 1 && commands.back() == command,
            "focused list shortcut was missing, wrong or repeated");
        SendMessageW(window, WM_KEYDOWN, key, 0);
        require(commands.size() == previous + 2 && commands.back() == command, "main-window shortcut failed");
    }
    const size_t previous = commands.size();
    gLoading = true;
    SendMessageW(gList, WM_KEYDOWN, VK_F2, 0);
    SendMessageW(gList, WM_KEYDOWN, VK_F5, 0);
    gLoading = false;
    gPlaying = true;
    SendMessageW(gList, WM_KEYDOWN, VK_F5, 0);
    gPlaying = false;
    require(commands.size() == previous, "folder shortcuts escaped the loading/gameplay guard");
    RemoveWindowSubclass(window, shortcutProbe, 42);
}

std::uint32_t printList(std::uint64_t* frameHash = nullptr) {
    RECT bounds{}; GetClientRect(gList, &bounds);
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = bounds.right; info.bmiHeader.biHeight = -bounds.bottom;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* pixels{};
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    require(bitmap && pixels, "unable to allocate native list capture");
    HGDIOBJ previous = SelectObject(dc, bitmap);
    // Pixel comparisons concern selection repainting, not elapsed marquee animation.
    if (frameHash) gBrowseTextStartedAt = std::chrono::steady_clock::now();
    SendMessageW(gList, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
    GdiFlush();
    const auto* first = static_cast<const std::uint32_t*>(pixels);
    if (frameHash) {
        *frameHash = 14695981039346656037ull;
        for (size_t i = 0; i < static_cast<size_t>(bounds.right) * bounds.bottom; ++i)
            *frameHash = (*frameHash ^ (first[i] & 0xFFFFFFu)) * 1099511628211ull;
    }
    const bool rendered = std::any_of(first, first + static_cast<size_t>(bounds.right) * bounds.bottom,
        [](auto pixel) { return (pixel & 0xFFFFFFu) != 0; });
    const auto bottomPixel = first[static_cast<size_t>(bounds.right) * bounds.bottom - 1] & 0xFFFFFFu;
    SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc);
    require(rendered && gRmlSongList.enabled(), "native list did not use its RmlUi painter");
    return bottomPixel;
}

void verifyStartupSplash(HWND window, const std::filesystem::path& output) {
    gStartupSplash = true; gStartupProgress = 50;
    setStatus(L"채보 분석 중... 50%");
    ShowWindow(gList, SW_HIDE); updateHiddenSongsVisibility();
    require(!(GetWindowLongPtrW(gHiddenSongs, GWL_STYLE) & WS_VISIBLE), "hidden-file button leaked through startup splash");
    gStartupSplashImage.reset(Gdiplus::Bitmap::FromFile((executableFolder() / L"Assets/startup-splash.png").c_str()));
    require(gStartupSplashImage && gStartupSplashImage->GetLastStatus() == Gdiplus::Ok, "packaged startup splash image did not load");
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = gWindowWidth; info.bmiHeader.biHeight = -gWindowHeight;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* pixels{};
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    require(bitmap && pixels, "unable to allocate splash capture");
    const auto previous = SelectObject(dc, bitmap);
    drawStartupSplash(dc); GdiFlush();
    const auto* data = static_cast<const uint32_t*>(pixels);
    require((data[static_cast<size_t>(gWindowHeight - 91) * gWindowWidth + gWindowWidth / 2 - 10] & 0xffffffu) == 0x65dbffu,
        "startup progress bar did not render");
    if (!output.empty()) {
        std::filesystem::create_directories(output);
        const CLSID png{0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
        Gdiplus::Bitmap capture(bitmap, nullptr);
        require(capture.Save((output / L"startup-splash.png").c_str(), &png) == Gdiplus::Ok, "unable to save splash capture");
    }
    gStartupSplashImage.reset(); // Missing image must still display the fallback.
    drawStartupSplash(dc);
    SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc);
    finishStartupSplash();
    require(!gStartupSplash && (GetWindowLongPtrW(gList, GWL_STYLE) & WS_VISIBLE), "startup did not restore the song list");
    gStartupSplash = true; gLoading = true; gLoadCancelled = false;
    ShowWindow(gList, SW_HIDE);
    ListView_DeleteAllItems(gList); insertLoadingRow();
    gLoadResult = {}; gLoadResult.error = L"Synthetic startup scan failure";
    SendMessageW(window, WM_LOAD_FINISHED, 0, 0);
    require(!gStartupSplash && !gLoading && (GetWindowLongPtrW(gList, GWL_STYLE) & WS_VISIBLE), "startup error left splash stuck");
    gLoadResult = {};
    gStartupSplash = true; gLoading = true; gLoadCancelled = true;
    ShowWindow(gList, SW_HIDE); insertLoadingRow();
    SendMessageW(window, WM_LOAD_FINISHED, 0, 0);
    require(!gStartupSplash && !gLoading && (GetWindowLongPtrW(gList, GWL_STYLE) & WS_VISIBLE), "startup cancellation left splash stuck");
    gLoadCancelled = false;
    rebuildSongRows();
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&gGdiPlusToken, &input, nullptr) != Gdiplus::Ok) return 1;
    int result = 0;
    try {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&controls);
        // No wWinMain startup: no settings file, song scan or audio device.
        gSettingsFile.clear(); gCpuPerformanceMode = false;
        gSongPreviewEnabled = false; gBrowseBackgroundMode = 0;
        WNDCLASSW type{};
        type.lpfnWndProc = windowProc; type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"BmsRmlUiListHostTest";
        require(RegisterClassW(&type) != 0, "unable to register native test window");
        HWND window = CreateWindowW(type.lpszClassName, L"RmlUi host test", WS_OVERLAPPEDWINDOW,
            0, 0, 1280, 800, nullptr, nullptr, type.hInstance, nullptr);
        require(window && gList, "unable to create actual browse controls");
        std::cout << "Browse controls created.\n" << std::flush;

        for (int song = 0; song < 60; ++song) {
            for (int variant = 0; variant < 2; ++variant) {
                bms::Chart chart;
                chart.path = std::filesystem::path(L"C:/BmsSyntheticRmlUiFixture") / std::to_wstring(song) /
                    (std::to_wstring(variant) + L".bms");
                chart.title = L"곡 제목 <&> " + std::to_wstring(song);
                chart.artist = L"Test 작곡가"; chart.subtitle = variant ? L"ANOTHER" : L"NORMAL";
                // Variants share an audio set; empty resource sets intentionally remain independent.
                chart.samples[L"01"]=chart.path.parent_path()/L"shared.wav";
                chart.genre = L"ARTCORE";
                chart.playLevel = variant ? L"12" : L"5";
                chart.playable = true; chart.mode = bms::ChartMode::SevenKey;
                gCharts.push_back(std::move(chart));
            }
        }
        buildSongGroups(); rebuildSongRows();
        verifyBrowseShortcuts(window);
        std::cout << "Synthetic library connected.\n" << std::flush;
        require(ListView_GetItemCount(gList) == 60 && selectedGroupIndex() == 0, "fixture grouping/initial selection failed");
        require(!(GetWindowLongPtrW(gHiddenSongs, GWL_STYLE) & WS_VISIBLE), "hidden-file button visible with no blocked charts");
        bms::Chart blocked; blocked.path = L"C:/BmsSyntheticRmlUiFixture/blocked.bms"; blocked.title = L"Blocked fixture";
        gCharts.push_back(blocked); buildSongGroups(); rebuildSongRows();
        require(GetWindowLongPtrW(gHiddenSongs, GWL_STYLE) & WS_VISIBLE, "blocked chart did not expose hidden-file button");
        gShowHiddenCharts = true; rebuildSongRows();
        require(GetWindowLongPtrW(gHiddenSongs, GWL_STYLE) & WS_VISIBLE, "hidden view lost its return button");
        gCharts.pop_back(); gShowHiddenCharts = false; buildSongGroups(); rebuildSongRows();
        verifyStartupSplash(window, argc == 2 ? std::filesystem::path(argv[1]) : std::filesystem::path{});
        printList();
        std::cout << "Native list painted.\n" << std::flush;
        std::array<std::uint64_t, 2> selectionFrames{};
        printList(&selectionFrames[0]);
        for (int step = 0; step < 12; ++step) {
            SendMessageW(gList, WM_KEYDOWN, step % 2 ? VK_UP : VK_DOWN, 0);
            const size_t selected = step % 2 ? 0 : 1;
            std::uint64_t frame{}, repeatedFrame{};
            printList(&frame); printList(&repeatedFrame);
            require(frame == repeatedFrame, "cached list repaint changed pixels without a selection change");
            if (step == 0) selectionFrames[1] = frame;
            require(selectionFrames[0] != selectionFrames[1] && frame == selectionFrames[selected],
                "buffered selection repaint left stale, blank or inconsistent rows");
        }
        SendMessageW(gList, WM_KEYDOWN, VK_DOWN, 0);
        require(selectedGroupIndex() == 1, "native keyboard navigation lost Rust selection");
        const int beforeVariant = selectedIndex();
        require(cycleSelectedVariant(1) && selectedIndex() != beforeVariant, "variant navigation failed with RmlUi painting");
        const auto selectedPath = gCharts[static_cast<size_t>(selectedIndex())].path;
        toggleSongSort();
        std::cout << "Keyboard, variant and sort processed.\n" << std::flush;
        require(gCharts[static_cast<size_t>(selectedIndex())].path == selectedPath, "sorting lost the selected chart");
        ListView_EnsureVisible(gList, 20, FALSE);
        printList();
        std::cout << "Scrolled list painted.\n" << std::flush;
        RECT row{}; require(ListView_GetItemRect(gList, 20, &row, LVIR_BOUNDS), "native row bounds unavailable");
        LVHITTESTINFO hit{}; hit.pt = {20, (row.top + row.bottom) / 2};
        require(ListView_HitTest(gList, &hit) == 20, "display row and native hit testing disagree");
        // ListView can enter a drag-detection message loop on button-down.
        // Queue both events so that loop can receive the matching button-up.
        PostMessageW(gList, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(hit.pt.x, hit.pt.y));
        PostMessageW(gList, WM_LBUTTONUP, 0, MAKELPARAM(hit.pt.x, hit.pt.y));
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        require(ListView_GetNextItem(gList, -1, LVNI_SELECTED) == 20, "native row click selection failed");
        std::cout << "Mouse click processed.\n" << std::flush;
        SetWindowPos(window, nullptr, 0, 0, 960, 720, SWP_NOZORDER | SWP_NOACTIVATE);
        HDC hintDc = GetDC(window);
        HGDIOBJ oldFont = SelectObject(hintDc, gSmallFont);
        SIZE hintSize{}; GetTextExtentPoint32W(hintDc, BROWSE_SHORTCUT_HINT, static_cast<int>(std::size(BROWSE_SHORTCUT_HINT) - 1), &hintSize);
        SelectObject(hintDc, oldFont); ReleaseDC(window, hintDc);
        RECT client{}; GetClientRect(window, &client);
        require(hintSize.cx <= client.right - 48, "footer shortcuts do not fit at the minimum window width");
        printList();
        captureBrowseBackdrop();
        std::cout << "Browse backdrop captured.\n" << std::flush;
        require(gResultBackdropPixels && gRmlSongList.enabled(), "browse/result capture failed with RmlUi list");
        if (argc == 2) {
            // Save a sharp UI preview; the result backdrop is deliberately blurred.
            drawBrowseScreen(gResultBackdropDc);
            POINT origin{}; MapWindowPoints(gList, window, &origin, 1);
            RECT listBounds{}; GetClientRect(gList, &listBounds);
            const int saved = SaveDC(gResultBackdropDc);
            SetViewportOrgEx(gResultBackdropDc, origin.x, origin.y, nullptr);
            IntersectClipRect(gResultBackdropDc, 0, 0, listBounds.right, listBounds.bottom);
            SendMessageW(gList, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(gResultBackdropDc), PRF_CLIENT);
            RestoreDC(gResultBackdropDc, saved); GdiFlush();
            const std::filesystem::path output(argv[1]); std::filesystem::create_directories(output);
            UINT count{}, bytes{}; Gdiplus::GetImageEncodersSize(&count, &bytes);
            std::vector<unsigned char> codecs(bytes);
            auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(codecs.data());
            Gdiplus::GetImageEncoders(count, bytes, encoders);
            for (UINT index = 0; index < count; ++index) if (std::wstring(encoders[index].MimeType) == L"image/png") {
                Gdiplus::Bitmap capture(gResultBackdropBitmap, nullptr);
                require(capture.Save((output / L"browse-layout.png").c_str(), &encoders[index].Clsid) == Gdiplus::Ok,
                    "unable to save the browse layout capture");
            }
        }
        ListView_DeleteAllItems(gList);
        require(printList() == 0x404040u, "empty native list did not reveal the parent browse background");
        DestroyWindow(window); gWindow = nullptr;
        std::cout << "RmlUi host: native keyboard, mouse, sort, variant, scroll, resize and browse capture passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; result = 1;
        if (gWindow && IsWindow(gWindow)) DestroyWindow(gWindow);
    }
    Gdiplus::GdiplusShutdown(gGdiPlusToken);
    return result;
}
