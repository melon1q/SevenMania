std::optional<int> handlePlayerArguments() {
    int argumentCount{};
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
#ifdef BMS_ENABLE_SKIN_BINDINGS
    const bool lr2Preview=arguments&&argumentCount>1&&std::wcscmp(arguments[1],L"--lr2-preview")==0;
    const bool lr2Play=arguments&&argumentCount>1&&std::wcscmp(arguments[1],L"--lr2-play-skin")==0;
    const bool lr2Play5=arguments&&argumentCount>1&&(std::wcscmp(arguments[1],L"--lr2-play5-skin")==0||
        std::wcscmp(arguments[1],L"--lr2-play14-skin")==0||std::wcscmp(arguments[1],L"--lr2-play10-skin")==0||
        std::wcscmp(arguments[1],L"--lr2-play9-skin")==0);
    const bool lr2Decide=arguments&&argumentCount>1&&std::wcscmp(arguments[1],L"--lr2-decide-skin")==0;
    const bool lr2Result=arguments&&argumentCount>1&&std::wcscmp(arguments[1],L"--lr2-result-skin")==0;
    const bool lr2Select=arguments&&argumentCount>1&&std::wcscmp(arguments[1],L"--lr2-select-skin")==0;
    if(lr2Preview||lr2Play||lr2Play5||lr2Decide||lr2Result||lr2Select) {
        const auto options=parseLr2LaunchOptions(argumentCount,arguments);
        LocalFree(arguments);
        arguments=nullptr;
        if(!options) {
            MessageBoxW(nullptr,L"BmsPlayer.exe --lr2-preview \"play.lr2skin\" [--resolution 1920x1080]\n"
                L"BmsPlayer.exe --lr2-play-skin \"play.lr2skin\" [--lr2-play5-skin|--lr2-play14-skin|--lr2-play10-skin|--lr2-play9-skin \"play.lr2skin\"] [--resolution 1920x1080] [--lr2-decide-skin \"decide.lr2skin\"]\n"
                L"BmsPlayer.exe --lr2-select-skin \"select.lr2skin\" [--lr2-decide-skin \"decide.lr2skin\"] [--lr2-result-skin \"result.lr2skin\"]",
                L"SevenMania LR2 Skin",MB_ICONERROR);
            return 1;
        }
        if(lr2Preview)return sevenmania::skin::lr2::runPreview(options->play,options->width,options->height);
        gLr2CommandLine=true;
        gLr2PlayFile=options->play;
        gLr2Play5File=options->play5;
        gLr2Play14File=options->play14;
        gLr2Play10File=options->play10;
        gLr2Play9File=options->play9;
        gLr2DecideFile=options->decide;
        gLr2ResultFile=options->result;
        gLr2SelectFile=options->select;
        gLr2PlayWidth=options->width;
        gLr2PlayHeight=options->height;
    }
#endif
    if (arguments && argumentCount > 1 && std::wcscmp(arguments[1], L"--catalog") == 0) {
        const int result = bms::runCatalogCommand(argumentCount, arguments);
        LocalFree(arguments);
        return result;
    }
    sevenmania::gauge_trial::TrialOptions gaugeTrialOptions;
    bool hasGaugeTrialOption=false,invalidGaugeTrialOption=false;
    for(int i=1;arguments && i<argumentCount;++i) {
        if(std::wcscmp(arguments[i],L"--gauge-compat-test")==0)gGaugeCompatibilityTest=true;
        const auto result=sevenmania::gauge_trial::parseTrialOption(arguments[i],gaugeTrialOptions);
        hasGaugeTrialOption|=result!=sevenmania::gauge_trial::TrialOptionResult::Unrecognized;
        invalidGaugeTrialOption|=result==sevenmania::gauge_trial::TrialOptionResult::Invalid;
    }
    if (arguments) LocalFree(arguments);

    if(invalidGaugeTrialOption || (hasGaugeTrialOption && !gGaugeCompatibilityTest)) {
        MessageBoxW(nullptr,L"게이지 시험 옵션이 잘못되었거나 --gauge-compat-test가 없습니다.",L"SevenMania 게이지 시험",MB_OK | MB_ICONERROR);
        return 1;
    }
    if(gGaugeCompatibilityTest) {
        gGrooveGauge.setCompatibilityEnabled(true);
        gGrooveGauge.setCompatibilityTrialOptions(gaugeTrialOptions.selected,gaugeTrialOptions.shift,gaugeTrialOptions.bottom,gaugeTrialOptions.profile);
        MessageBoxW(nullptr,L"게이지 호환 시험 모드입니다. 플레이 기록은 저장되지 않습니다. 채보 전용 게이지 설정이 있는 곡에는 기존 규칙이 적용됩니다.",L"SevenMania 게이지 시험",MB_OK | MB_ICONINFORMATION);
    }
    return std::nullopt;
}

int runPlayer(HINSTANCE instance, int show) {
    initializeSettingsPath();
    sevenmania::SettingsGate settingsGate(gSettingsFile);
    if(!settingsGate.acquire()){MessageBoxW(nullptr,L"게임 또는 런처의 설정/캐시 작업이 실행 중입니다.",L"SevenMania",MB_ICONINFORMATION);return 0;}
    loadLr2SkinSettings();
    gLibrarySources.initialize(executableFolder()/L"Songs",gSettingsFile);
    gPlayRecordsFile = gSettingsFile.empty() ? executableFolder() / L"BmsPlayer-records.ini" :
        gSettingsFile.parent_path() / L"BmsPlayer-records.ini";
#ifdef BMS_FRAME_DIAGNOSTICS
    gPlayRecordsFile=gSettingsFile.parent_path()/L"BmsPlayer-Test-records.ini";
#endif
    loadPlayRecords();
    loadDifficultyTables();
    gSystemResources.initialize();
    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&gGdiPlusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) gGdiPlusToken = 0;
    if (gGdiPlusToken) {
        std::array<wchar_t, 32768> executablePath{};
        const DWORD pathLength = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
        if (pathLength > 0 && pathLength < executablePath.size()) {
            const auto assetPath = std::filesystem::path(executablePath.data()).parent_path() / L"Assets" / L"turntable.png";
            loadTurntableImageFile(assetPath, gTurntableImage);
            const auto blackKeyPath = std::filesystem::path(executablePath.data()).parent_path() / L"Assets" / L"blackbutton.png";
            loadRasterPngFile(blackKeyPath, gBlackKeyImage);
            const auto whiteKeyPath = std::filesystem::path(executablePath.data()).parent_path() / L"Assets" / L"whitebutton.png";
            loadRasterPngFile(whiteKeyPath, gWhiteKeyImage);
            const auto hudSlotPath = std::filesystem::path(executablePath.data()).parent_path() / L"Assets" / L"hud-icon-slot.png";
            loadRasterPngFile(hudSlotPath, gHudSlotImage);
            loadRasterPngFile(hudSlotPath.parent_path()/L"hud-hi-speed.png",gHiSpeedSlotImage);
            loadRasterPngFile(hudSlotPath.parent_path()/L"hud-fast.png",gFastHintImage);
            loadRasterPngFile(hudSlotPath.parent_path()/L"hud-slow.png",gSlowHintImage);
            loadRasterPngFile(hudSlotPath.parent_path()/L"hud-gauge-mode.png",gGaugeSlotImage);
            const auto hitSplashPath = std::filesystem::path(executablePath.data()).parent_path() / L"Assets" / L"hit-splash.png";
            if (loadRasterPngFile(hitSplashPath, gHitSplashSheet))
                gFeedback.setSpriteSheet(gHitSplashSheet.width(), gHitSplashSheet.height(), gHitSplashSheet.pixels());
        }
    }
    gTimerResolutionRequested = timeBeginPeriod(1) == TIMERR_NOERROR;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HRESULT mf = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(mf)) {
        MessageBoxW(nullptr, L"Windows Media Foundation을 초기화하지 못했습니다.", L"SevenMania", MB_ICONERROR | MB_OK);
        if (gTimerResolutionRequested) timeEndPeriod(1);
        CoUninitialize();
        return 1;
    }
    const auto effectAssets = executableFolder() / L"Assets";
#ifdef BMS_ENABLE_SKIN_BINDINGS
    const auto skinSounds = defaultSkinPath().parent_path() / L"sounds";
#else
    const std::filesystem::path skinSounds = effectAssets;
#endif
    gSystemSoundAssets = effectAssets;
    gSystemSoundSkin = skinSounds;
    gSystemSoundFolders = bms::loadSystemSoundFolders(gSettingsFile);
    reloadSystemSounds(false);
#ifdef BMS_FRAME_DIAGNOSTICS
    gAudio.loadDiagnosticBeep();
#endif
    INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&controls);
    WNDCLASSEXW cls{ sizeof(cls) };
    cls.style = CS_DBLCLKS;
    cls.lpfnWndProc = windowProc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    cls.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, 0));
    cls.hbrBackground = CreateSolidBrush(RGB(16, 20, 28));
    cls.lpszClassName = L"BmsPlayerNativeWindow";
    RegisterClassExW(&cls);
    WNDCLASSEXW settingsClass = cls;
    settingsClass.lpfnWndProc = settingsProc;
    settingsClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    settingsClass.lpszClassName = L"BmsPlayerSettingsWindow";
    RegisterClassExW(&settingsClass);
    WNDCLASSEXW settingsBodyClass = settingsClass;
    settingsBodyClass.lpfnWndProc = settingsBodyProc;
    settingsBodyClass.lpszClassName = L"BmsPlayerSettingsBody";
    RegisterClassExW(&settingsBodyClass);
    WNDCLASSEXW previewClass = settingsClass;
    previewClass.lpfnWndProc = settingsPreviewOverlayProc;
    previewClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    previewClass.lpszClassName = L"BmsPlayerSettingsPreviewOverlay";
    RegisterClassExW(&previewClass);
    gStartupSplash = true;
    gStartupProgress = 0;
    gStartupSplashImage.reset(Gdiplus::Bitmap::FromFile((executableFolder() / L"Assets/startup-splash.png").c_str()));
    if (gStartupSplashImage && gStartupSplashImage->GetLastStatus() != Gdiplus::Ok) gStartupSplashImage.reset();
    // The main thread works in true pixels; popups opt out with DpiContextScope (see above).
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"SEVENMANIA", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 860, nullptr, nullptr, instance, nullptr);
#ifdef BMS_FRAME_DIAGNOSTICS
    if(window)SetWindowTextW(window,L"BMS Player — TEST / FPS Diagnostics");
#endif
    if (!window) {
        MFShutdown(); if (gTimerResolutionRequested) timeEndPeriod(1); CoUninitialize(); return 1;
    }
    setStatus(L"곡 라이브러리를 준비하고 있습니다...");
    sevenmania::placeConfiguredMonitor(window,gSettingsFile);
    ShowWindow(window, show);
    applyScreenMode(true);
    UpdateWindow(window);
    scanFolders({}); // Only explicitly registered sources; LastFolder is not a source.
    if (!gLoading) finishStartupSplash();
    applyPresentTuning();
    const int exitCode = runPlayerLoop();
    DeleteObject(cls.hbrBackground);
    gBgaRaster.reset();
    if (gGdiPlusToken) Gdiplus::GdiplusShutdown(gGdiPlusToken);
    MFShutdown();
    if (gTimerResolutionRequested) timeEndPeriod(1);
    CoUninitialize();
    return exitCode;
}
