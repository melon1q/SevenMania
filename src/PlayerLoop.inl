int runPlayerLoop() {
    MSG message{};
    bool running = true;
    while (running) {
        auto& latencyControl = gCompositionPresenter.lowLatency();
        bool latencyFrame = false;
        if (gPlaying && !gPaused && gCompositionEnabled && !gGpuUnavailable &&
            (latencyControl.active() == latency::Mode::Reflex || gFramePacer.due())) {
            BMS_PERF_SCOPE(latencyWaitMs);
            latencyFrame = latencyControl.beginFrame();
        }
        gSelectionAudio.tickDecision();
        gLatencyInputPump = latencyFrame;
        {HUD_SCOPE_IF(InputPump,latencyFrame);
        // Bound message processing so continuous input cannot starve a due frame.
        for (int processed = 0; processed < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++processed) {
            if (message.message == WM_QUIT) { running = false; break; }
            if(!gSettingsWindow&&!gPracticeWindow&&handleOverlayEscape(message.message,message.wParam))continue;
            if (handleSettingsCaptureMessage(message.message,message.wParam,message.lParam)) continue;
            if (gRmlSettingsHost.owns(message.hwnd) || gRmlSettingsPage.owns(message.hwnd)) {
                TranslateMessage(&message); DispatchMessageW(&message); continue;
            }
            if (gSettingsWindow && !gCapturingBindings && message.message == WM_KEYDOWN && message.wParam == VK_F9) {
                SendMessageW(gSettingsWindow, WM_KEYDOWN, VK_F9, message.lParam); continue;
            }
            if (gPracticeWindow && (message.message == WM_KEYDOWN || message.message == WM_KEYUP)) {
                if (message.message == WM_KEYDOWN) SendMessageW(gPracticeWindow, WM_KEYDOWN, message.wParam, message.lParam);
                continue;
            }
            if(gLoading&&gPlaybackLoad.preparing()&&message.message==WM_KEYDOWN&&previewKey(message.wParam))continue;
            if (gLoading && message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
                gLoadCancelled = true; setStatus(L"불러오기를 취소하고 있습니다..."); continue;
            }
            if(gPlaying&&!gLoading&&!gSettingsWindow&&message.message==WM_KEYDOWN&&message.wParam==VK_F5){
                if((message.lParam&(1LL<<30))==0)restartPlayback();
                continue;
            }
            if(gPlaying && !gPaused && message.message==WM_KEYDOWN && gChartOptions.manualHiSpeed &&
               (message.wParam==VK_F1 || message.wParam==VK_F2)) {
                gChartOptions.hiSpeed=std::clamp((gChartOptions.hiSpeed>0?gChartOptions.hiSpeed:gNoteSpeed)+(message.wParam==VK_F2?.25:-.25),.25,100.0);
                continue;
            }
            if(gPlaying && !gPaused && message.message==WM_KEYDOWN && gChartOptions.hidden==L"m-sudden" &&
               (message.wParam==VK_F3 || message.wParam==VK_F4)) {
                gAuthoredCoverPercent=std::clamp(gAuthoredCoverPercent+(message.wParam==VK_F3?5:-5),0,100);
                continue;
            }
            if (gPaused && message.message == WM_KEYDOWN) {
                handlePausedKey(message.wParam, (message.lParam & (1LL << 30)) != 0);
                continue;
            }
            if (gPaused && message.message == WM_KEYUP) {
                SendMessageW(gWindow, message.message, message.wParam, message.lParam);
                continue;
            }
            // Browse shortcuts also work while the list or hidden-files control owns focus.
            if (gShowingResult && !gSettingsWindow && message.message == WM_KEYDOWN &&
                (message.wParam == VK_RETURN || message.wParam == VK_ESCAPE)) {
                if ((message.lParam & (1LL << 30)) == 0) requestResultExit();
                continue;
            }
            if (!gPlaying && !gShowingResult && !gSettingsWindow && !gPracticeWindow && !gLoading &&
                message.message == WM_KEYDOWN && message.wParam == VK_F1) {
                if ((message.lParam & (1LL << 30)) == 0) showSelectedChartErrors();
                continue;
            }
            if (!gPlaying && !gShowingResult && !gSettingsWindow && !gPracticeWindow && !gLoading &&
                message.message == WM_KEYDOWN && message.wParam == VK_RETURN) {
                if ((message.lParam & (1LL << 30)) == 0) {
                    gAudio.triggerEffect(L"click");
                    startSelectedChartFromEnter(); // Shift = practice, Ctrl = autoplay.
                }
                continue;
            }
            if (handleBrowseShortcut(message.message, message.wParam, message.lParam)) continue;
            if (!gPlaying && !gShowingResult && !gSettingsWindow && message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
                if (!gLoading && (message.lParam & (1LL << 30)) == 0) {
                    if (MessageBoxW(gWindow, L"SevenMania를 종료하시겠습니까?", L"종료 확인", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) == IDYES)
                        DestroyWindow(gWindow);
                }
                continue;
            }
            if (!gPlaying && !gShowingResult && !gSettingsWindow && message.message == WM_KEYUP && message.wParam == VK_ESCAPE) continue;
            if (gSettingsWindow && !gCapturingBindings && IsDialogMessageW(gSettingsWindow, &message)) continue;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        }
        gLatencyInputPump = false;
        if (!running) { latencyControl.endFrame(); break; }
        if (!gPlaying || gPaused) latencyControl.endFrame();
        if (gOutputWakeReady) {
            gOutputWakeReady = false;
            // A due simulation/Reflex frame will paint the freshest state and
            // consume the reserved permit itself. Never invoke SDK Sleep twice.
            if (!latencyFrame && (gPaused || !gFramePacer.due())) retryPendingGpuOutput();
        }
        if (!gPlaying && !gShowingResult && !gLoading) {
            tickBrowseMedia();
            // Animated select frames follow the select FPS setting (60 or 120), not the play frame target.
            if ((gBrowseBackgroundMode == 1 || gBrowseTextOverflow || gRmlSongList.needsAnimation()
#ifdef BMS_ENABLE_SKIN_BINDINGS
                || gLr2SelectActive
#endif
                ) && gSelectFrameClock.due(SelectFrameClock::Clock::now())) {
                gSelectFrameClock.advance(SelectFrameClock::Clock::now());
                InvalidateRect(gWindow, nullptr, FALSE); UpdateWindow(gWindow);
            }
        } else {gSelectionAudio.stopMusic();if(!gBrowseMediaKey.empty())stopBrowseMedia();}
        if (!(gLoading && gLoadResult.playback) && !gShowingResult) gTransitionClockActive = false; // re-read the monitor next time
        if (gPlaying && gPaused) {
            const DWORD waitResult = waitForPlaybackWork(nullptr, gShowResourceUsage ? 500 : INFINITE);
            if (gShowResourceUsage && waitResult == WAIT_TIMEOUT) {
                gSystemResources.sample();
                InvalidateRect(gWindow, nullptr, FALSE);
                UpdateWindow(gWindow);
            }
            continue;
        }
        if (gPlaying) {
            if (latencyFrame || gFramePacer.due()) {
                if (gShowResourceUsage || gCpuLimitPercent < 100) gSystemResources.sample();
                const int cpuWait = gSystemResources.cpuThrottleWaitMilliseconds(gCpuLimitPercent, gTargetFps);
                if (cpuWait > 0)
                    MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(cpuWait), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                updatePlaybackFrame();
                latencyControl.endFrame();
                updateVblankAlignment();
                gFramePacer.advance();
                continue;
            }
            if (gFramePacer.pollBeforeDeadline()) {
                if (const HANDLE output = pendingGpuOutputHandle(); output && WaitForSingleObject(output, 0) == WAIT_OBJECT_0) {
                    gOutputWakeReady = gCompositionPresenter.acceptPresentationWake(output);
                    continue;
                }
                YieldProcessor();
                continue;
            }
            HANDLE timer = gFramePacer.arm();
            waitForPlaybackWork(timer, timer ? INFINITE : 1);
        } else if (gLoading && gLoadResult.playback) {
            pollPreparingPlayback();
            tickTransitionFrame();
            MsgWaitForMultipleObjectsEx(0,nullptr,transitionWaitMilliseconds(),QS_ALLINPUT,MWMO_INPUTAVAILABLE);
#ifdef BMS_ENABLE_SKIN_BINDINGS
        } else if (gShowingResult && gLr2ResultScene) {
            // The LR2 result's timer keeps handling its exit; its frames follow the monitor here.
            tickTransitionFrame();
            MsgWaitForMultipleObjectsEx(0,nullptr,transitionWaitMilliseconds(),QS_ALLINPUT,MWMO_INPUTAVAILABLE);
#endif
        } else {
            const bool hasPeriodicBrowseWork = gBrowseBackgroundMode == 1 || gSongPreviewEnabled ||
                gBrowseTextOverflow || gRmlSongList.needsAnimation()
#ifdef BMS_ENABLE_SKIN_BINDINGS
                || gLr2SelectActive
#endif
                ;
            const DWORD waitMs = hasPeriodicBrowseWork ? std::min<DWORD>(33, gSelectFrameClock.waitMilliseconds(SelectFrameClock::Clock::now())) :
                gShowResourceUsage ? 500 : INFINITE;
            const DWORD waitResult = MsgWaitForMultipleObjectsEx(0, nullptr, waitMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (gShowResourceUsage && !hasPeriodicBrowseWork && waitResult == WAIT_TIMEOUT) {
                gSystemResources.sample();
                InvalidateRect(gWindow, nullptr, FALSE);
                UpdateWindow(gWindow);
            }
        }
    }
    return static_cast<int>(message.wParam);
}
