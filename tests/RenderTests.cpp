// Exercise the exact renderer used by the app, including real BGA event changes.
// This target is opt-in and never links into the distributed player.
#include "../src/Main.cpp"
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::wcerr << L"Usage: BmsRenderTests chart.bme trace.csv\n"; return 2; }
    auto parsed = bms::BmsParser().parse(argv[1]);
    if (!parsed.chart.playable) { std::wcerr << parsed.chart.blockReason << L'\n'; return 1; }
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&gGdiPlusToken, &input, nullptr) != Gdiplus::Ok) return 1;
    gWindow = CreateWindowW(L"STATIC", L"Render fixture", WS_OVERLAPPEDWINDOW,
        0, 0, 1936, 1119, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    RECT client{}; GetClientRect(gWindow, &client);
    gWindowWidth = client.right; gWindowHeight = client.bottom;
    HDC reference = GetDC(gWindow);
    ensureBackBuffer(reference, client.right, client.bottom);
    gSystemResources.initialize();
    if (argc > 7) { gSettingsFile = argv[7]; loadDisplaySettings(); }
    const auto makeFont = [](int size) { return CreateFontW(-size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI"); };
    gFont = makeFont(20); gSmallFont = makeFont(15); gHudBoldFont = makeFont(16);
    gComboFont = makeFont(64); gJudgementFont = makeFont(80);
    loadTurntableImageFile(executableFolder() / L"Assets/turntable.png", gTurntableImage);
    loadRasterPngFile(executableFolder() / L"Assets/blackbutton.png", gBlackKeyImage);
    loadRasterPngFile(executableFolder() / L"Assets/whitebutton.png", gWhiteKeyImage);
    loadRasterPngFile(executableFolder() / L"Assets/hud-icon-slot.png", gHudSlotImage);
    loadRasterPngFile(executableFolder() / L"Assets/hit-splash.png", gHitSplashSheet);
    if (gHitSplashSheet.loaded()) gFeedback.setSpriteSheet(gHitSplashSheet.width(), gHitSplashSheet.height(), gHitSplashSheet.pixels());
    gCharts.push_back(std::move(parsed.chart));
    gActiveChartIndex = 0;
    gHit.assign(gCharts[0].notes.size(), 0);
    gNoteSpeed = argc > 3 ? std::stod(argv[3]) : 5;
    const double firstSecond = argc > 4 ? std::stod(argv[4]) : 3;
    const std::wstring missMode = argc > 5 ? argv[5] : L"on";
    const std::wstring hudMode = argc > 6 ? argv[6] : L"steady";
    const std::wstring outputMode = argc > 8 ? argv[8] : L"offscreen";
    const int frameCount = argc > 9 ? std::max(1, std::stoi(argv[9])) : 240;
    const double timeStep = argc > 10 ? std::stod(argv[10]) : 1.0 / 15.0;
    if (hudMode != L"off") {
        for (int i = 0; i < 100; ++i) gFeedback.hit(3, -100.0);
        setJudgement(L"GREAT!!", RGB(255, 214, 20)); gJudgementPop.reset();
    }
    if (outputMode != L"offscreen") { ShowWindow(gWindow, SW_SHOWNOACTIVATE); UpdateWindow(gWindow); }
    gFpsEpoch = std::chrono::steady_clock::now();
    BMS_PERF_SESSION(std::filesystem::path(argv[2]));
    const bool gpu = outputMode == L"gpu";
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < frameCount; ++i) {
        gPlayhead = firstSecond + i * timeStep;
        gMissBgaUntil = missMode == L"off" ? 0 : missMode == L"toggle" && i % 2 == 0 ? 0 : gPlayhead + 2.5;
        if (hudMode != L"off") gJudgementUntil = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        if (hudMode == L"animated") { gComboPop.start(); gJudgementPop.start(); }
        if (hudMode == L"effects") {
            gFeedback.hit(i % 8, gPlayhead);
            gComboPop.start(); gJudgementPop.start();
            for (int lane = 0; lane < 8; ++lane) {
                gPressed[static_cast<size_t>(lane)] = (i + lane) % 3 != 0;
                gKeyBeams.setPressed(lane, gPressed[static_cast<size_t>(lane)], gPlayhead - 0.04);
            }
        }
        BMS_PERF_BEGIN();
        { BMS_PERF_SCOPE(resourceMs); if (gShowResourceUsage || gCpuLimitPercent < 100) gSystemResources.sample(); }
        updateBgaState(gCharts[0], gPlayhead);
        gGpuFrame = gpu;
        if (gpu && !gCompositionPresenter.prepare(gWindow, gWindowWidth, gWindowHeight)) return 1;
        drawGameScreen(gBackBufferDc);
        drawResourceUsageOverlay(gBackBufferDc);
        GdiFlush();
        if (outputMode != L"offscreen") {
            BMS_PERF_SCOPE(presentMs);
            if (gpu) {
                if (!gCompositionPresenter.present(gWindow, gBackBufferPixels, gWindowWidth, gWindowHeight)) return 1;
            } else if (outputMode == L"compose") gCompositionPresenter.present(gWindow, gBackBufferPixels, gWindowWidth, gWindowHeight);
            else BitBlt(reference, 0, 0, gWindowWidth, gWindowHeight, gBackBufferDc, 0, 0, SRCCOPY);
        }
        BMS_PERF_FINISH(gPlayhead);
        gGpuFrame = false;
    }
    const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << gWindowWidth << 'x' << gWindowHeight << ": " << frameCount << " real BGA/notes renders: "
        << milliseconds << " ms, average " << milliseconds / frameCount << " ms\n";
    perf::trace.save();
    gCompositionPresenter.reset();
    ReleaseDC(gWindow, reference);
    SelectObject(gBackBufferDc, gBackBufferOriginalBitmap);
    DeleteObject(gBackBufferBitmap); DeleteDC(gBackBufferDc);
    if (gBgaBackplateDc) {
        SelectObject(gBgaBackplateDc, gBgaBackplateOriginalBitmap);
        DeleteObject(gBgaBackplateBitmap); DeleteDC(gBgaBackplateDc);
    }
    DestroyWindow(gWindow);
    for (const auto& [color, brush] : gBrushCache) { (void)color; DeleteObject(brush); }
    gBgaRaster.reset();
    for (HFONT font : {gFont, gSmallFont, gHudBoldFont, gComboFont, gJudgementFont}) DeleteObject(font);
    Gdiplus::GdiplusShutdown(gGdiPlusToken); gGdiPlusToken = 0;
    return 0;
}
