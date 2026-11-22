#ifdef BMS_ENABLE_SKIN_BINDINGS
bool lr2PlayEnding() {
    return gPlaying&&gLr2PlayScene&&gLr2Play.endingAt!=std::chrono::steady_clock::time_point{};
}

// An LR2 play skin shows its own ending before the result: the fail shutters for #CLOSE milliseconds,
// or a fade for #FADEOUT. True while that is still running; the play is frozen meanwhile.
sevenmania::skin::lr2::ScreenType currentLr2PlayScreen() {
    using sevenmania::skin::lr2::ScreenType;
    if(gActiveChartIndex<0||gActiveChartIndex>=static_cast<int>(gCharts.size()))return ScreenType::Play7Key;
    return lr2PlayScreen(gCharts[gActiveChartIndex].mode).value_or(ScreenType::Play7Key);
}

void reportLr2PlayGpuFailure(std::wstring_view stage) {
    const auto screen=currentLr2PlayScreen();
    reportLr2Failure(screen,lr2PlayFile(screen),stage);
}

void sampleLr2ResultGauge() {
    if(gLr2ResultFile.empty()||gActiveChartIndex<0||gActiveChartIndex>=static_cast<int>(gCharts.size()))return;
    auto& state=gLr2Play;
    const double start=gPractice.enabled?gPractice.startSeconds:0.;
    const double end=gPractice.enabled?gPractice.endSeconds:gCharts[gActiveChartIndex].durationSeconds;
    if(end<=start||!std::isfinite(gPlayhead))return;
    const auto slot=static_cast<std::size_t>(std::clamp((gPlayhead-start)/(end-start),0.,1.)*(state.gaugeHistory.size()-1));
    const auto gauge=static_cast<float>(gGrooveGauge.value());
    while(state.gaugeSamples<=slot)state.gaugeHistory[state.gaugeSamples++]=gauge;
    state.gaugeHistory[slot]=gauge;
}

// The lane cover as drawn for this frame (0-1). Display only: the setting is read, never written.
// It starts closing 500 ms after the last note is judged, or when the ending (FADEOUT, CLOSE, QUIT)
// starts if that is earlier.
float lr2LaneCoverShare(int settingPercent) {
    const auto& state=gLr2Play;
    const bool ending=state.endingAt!=std::chrono::steady_clock::time_point{};
    const double endingAge=ending?std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-state.endingAt).count():-1.;
    const double judgedAge=state.allJudgedAt<0?-1.:(gPlayhead-state.allJudgedAt)*1000;
    const int duration=ending?state.endingMilliseconds:gLr2PlayScene?gLr2PlayScene->fadeoutMilliseconds():0;
    return sevenmania::skin::lr2::laneCoverDisplay(settingPercent,
        sevenmania::skin::lr2::laneCoverClosingAge(judgedAge,endingAge),duration);
}
bool lr2HoldFinish() {
    if(!gPlaying||!gLr2PlayScene||gLr2GpuFailed||gPractice.enabled||gNoInputBenchmark||gAccuracyRestartPending)return false;
    auto& state=gLr2Play;
    const auto now=std::chrono::steady_clock::now();
    if(state.endingAt==std::chrono::steady_clock::time_point{}) {
        state.endingFailed=gGrooveGauge.depleted()||gPlayAbandoned;
        state.endingMilliseconds=state.endingFailed?gLr2PlayScene->closeMilliseconds():gLr2PlayScene->fadeoutMilliseconds();
        if(state.endingMilliseconds<=0)return false;
        state.endingAt=now;
    }
    // Not drawn yet; a second without a frame starts the clock anyway so the result still opens.
    if(state.endingClockPending) {
        if(now-state.endingAt<std::chrono::seconds(1))return true;
        state.endingClockPending=false;state.endingAt=now;
    }
    return now-state.endingAt<std::chrono::milliseconds(state.endingMilliseconds);
}
#endif

bool triggerSystemSound(bms::SystemSound sound);
// An empty gauge or QUIT: stop the song where it is and play playstop, once, before the fail shutters
// (or the result fade) run. The playhead stays at the last heard time, so the picture holds still.
void beginFailureEnding() {
    if (gFailureEndingStarted || !gPlaying || gPractice.enabled || gNoInputBenchmark || gAccuracyRestartPending) return;
    if (!bms::playStopsOnFailure(gGrooveGauge.depleted(), gPlayAbandoned)) return;
    gFailureEndingPlayhead = gPlayhead;
    gFailureEndingStarted = true;
    gAudio.releaseSong();
    startUiEffects();
    const auto stop = bms::SystemSound::PlayStop;
    gPlayStopSeconds = triggerSystemSound(stop) ? gAudio.effectSeconds(bms::systemSoundInfo(stop).effectId) : 0;
    gPlayStopStartedAt = std::chrono::steady_clock::now();
}

void finishPlayback() {
    if (!gPlaying) return; // Finalize scores and capture the frame only once.
    beginFailureEnding();
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(!lr2PlayEnding())sampleLr2ResultGauge();
    if(lr2HoldFinish())return;
#endif
    resetResultFade();
    gResultFadeWidth = gBackBufferWidth; gResultFadeHeight = gBackBufferHeight;
    if (gBackBufferWidth > 0 && gBackBufferHeight > 0) {
        GdiFlush();
        if (!gCompositionEnabled || !gCompositionPresenter.captureLastFrame(gResultFadePixels, gBackBufferWidth, gBackBufferHeight)) {
            if (gBackBufferPixels) {
                const auto* pixels = static_cast<const uint32_t*>(gBackBufferPixels);
                gResultFadePixels.assign(pixels, pixels + static_cast<size_t>(gBackBufferWidth) * gBackBufferHeight);
            } else gResultFadePixels.clear();
        }
    }
    // Cover any synchronous repaint caused by tearing down the GPU child window.
    gResultFade.start();
    finishPresentDiagnostics();
    gPlaying = false;
    gPlaybackLoad.reset();
    gPaused = false;
    gShowingResult = true;
    // beatoraja: playstop when the play is quit, otherwise clear / fail as the result opens.
    auto resultSound = bms::SystemSound::ResultFail;
    {
        const auto outcome = bms::playOutcome(gGrooveGauge.clearConditionMet(), gGrooveGauge.depleted(), gPlayAbandoned);
        gResultGaugeCleared = outcome.cleared;
        gResultGaugeFailed = outcome.failed;
        // playstop already sounded when the play failed; the result itself is clear or fail.
        resultSound = outcome.cleared ? bms::SystemSound::ResultClear : bms::SystemSound::ResultFail;
        gPlayAbandoned = false;
    }
#ifdef BMS_ENABLE_SKIN_BINDINGS
    loadLr2Result();
#endif
    if (gAutoJudgeUsedThisPlay) {saveDisplaySettings(); gAutoJudgeUsedThisPlay = false;} // realtime mode moved the offset
    if (gAutoJudgeMode == bms::AutoJudgeAdjuster::Suggest && !gPractice.enabled && !gNoInputBenchmark && !gAutoplay) {
        if (const auto suggestion = gAutoJudge.suggestion(gTimingOffsets.judgeMs)) {
            gAutoJudgeSuggestion = *suggestion;
            gAutoJudgePending = true; // asked when the player leaves the result screen
        }
    }
    if (playSavesRecord() && gActiveChartIndex >= 0 && gActiveChartIndex < static_cast<int>(gCharts.size())) {
        savePlayRecord(gCharts[static_cast<size_t>(gActiveChartIndex)], gResultGaugeCleared,
            gFeedback.maxCombo(), gResultScore.value(), gCharts[gActiveChartIndex].judgementSpec,gCharts[gActiveChartIndex].rank);
    }
    // A fail ending already swapped the song for UI output; stopping it again would cut playstop.
    const bool failureEnding = gFailureEndingStarted;
    if (!failureEnding) gAudio.releaseSong();
    gBgaRaster.reset();
#ifdef BMS_ENABLE_SKIN_BINDINGS
    gLr2PlayScene.reset();
#endif
    gCompositionPresenter.reset();
    releaseBgaBackplate();
    if (!failureEnding) startUiEffects();
    gFailureEndingStarted = false;
    const int resultSoundDelay = failureEnding ? bms::resultSoundDelayMs(gPlayStopSeconds,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - gPlayStopStartedAt).count()) : 0;
    if (resultSoundDelay > 0 && gWindow) {
        gPendingResultSound = resultSound;
        SetTimer(gWindow, ID_RESULT_SOUND, static_cast<UINT>(resultSoundDelay), nullptr);
    } else triggerSystemSound(resultSound);
    gVideoBga.reset();
    gBgaVideos.reset();gBgaSwitch.reset();
    gPressed.fill(false);gKeyboardPressed.fill(false);gControllerPressed.fill(false);gController.mapper.reset();clearAccuracyRestart();
    gHolds.fill(LongNoteHold{});
    gAutoPlayCursor.clear();
    gLingeringLongNotes.clear();gReleasedLongNotes.clear();
    resetMines();
    ShowWindow(gResultNext, SW_HIDE);
    for (HWND button : gPauseButtons) if (button) ShowWindow(button, SW_HIDE);
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(gLr2ResultScene) {
        resetResultFade();
    } else
#endif
    {
        gResultFade.start();
        SetTimer(gWindow, ID_RESULT_FADE, 4, nullptr);
    }
    SetFocus(gWindow);
    InvalidateRect(gWindow, nullptr, TRUE);
    if (gNoInputBenchmark && gBenchmarkRepeat > 0 && !gBenchmarkChart.empty() && !gPractice.enabled) {
        ++gBenchmarkPlays;
        SetTimer(gWindow, ID_BENCHMARK_NEXT, 3000, nullptr); // let the result fade and CSV writes finish
    }
}

// Next benchmark play of the same chart, or close the game after the last one.
void continueBenchmarkSeries() {
    if (!gNoInputBenchmark || gPlaying || gLoading) return;
    if (gBenchmarkPlays >= gBenchmarkRepeat) {
        gBenchmarkChart.clear();
        if (gBenchmarkExit && gWindow) DestroyWindow(gWindow);
        return;
    }
    if (gShowingResult) showBrowseView();
    const int index = selectedIndex();
    if (index < 0 || index >= static_cast<int>(gCharts.size()) || gCharts[static_cast<size_t>(index)].path != gBenchmarkChart) {
        setStatus(L"벤치마크 반복: 선택된 곡이 바뀌어 반복을 중단했습니다.");
        gBenchmarkChart.clear(); return;
    }
    gBenchmarkAutoStarting = true;
    beginSelectedChart();
    gBenchmarkAutoStarting = false;
}

void pausePlayback(bool focusMenu = true) {
    HUD_SCOPE(PauseTransition);
    // The fail ending has already stopped the song; pausing would cut playstop and stall the ending.
    if (!gPlaying || gPaused || gFailureEndingStarted || !gAudio.pause()) return;
    gPlayhead = heardPlayhead();
    gPaused = true;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    setSevenManiaSkinAnimationPaused(true);
#endif
    gCompositionPresenter.lowLatency().endFrame();
    gCompositionPresenter.lowLatency().request(latency::Mode::Off);
    gPauseSelection = 0;
    layoutControls();
    for (HWND button : gPauseButtons) if (button) ShowWindow(button, SW_SHOW);
    if (focusMenu) SetFocus(gPauseButtons[0] ? gPauseButtons[0] : gWindow);
    InvalidateRect(gWindow, nullptr, TRUE);
}

void resumePlayback() {
    HUD_SCOPE(ResumeTransition);
    gFrameStatistics.suspend();
    if (!gPaused) return;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    prepareSkinForPlayback();
#endif
    if (!gAudio.resume()) {
        showBrowseView();
        return;
    }
    gPaused = false;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    setSevenManiaSkinAnimationPaused(false);
#endif
    for (HWND button : gPauseButtons) if (button) ShowWindow(button, SW_HIDE);
    SetFocus(gWindow);
    gFramePacer.reset(gTargetFps);
    InvalidateRect(gWindow, nullptr, TRUE);
}

// QUIT in the pause menu: a scored play ends like an empty gauge (LR2 skins run their #CLOSE shutters,
// the default skin its result fade) and is recorded as FAILED. Practice and benchmarks just leave.
void quitPausedPlayback() {
    if (bms::pauseQuitAction(gPlaying, gPractice.enabled, gNoInputBenchmark, gAutoplay) == bms::PauseQuitAction::ReturnToSelect) {
        showBrowseView();
        return;
    }
#ifdef BMS_ENABLE_SKIN_BINDINGS
    // An LR2 skin plays its #CLOSE fail animation straight from the pause menu. Like a gauge fail the
    // play stops where it is: the song stays paused and nothing is resumed or re-prepared first, which
    // used to hold the picture for a moment before the result.
    if(gPaused&&gLr2PlayScene&&!gLr2GpuFailed&&!gAccuracyRestartPending&&gLr2PlayScene->closeMilliseconds()>0) {
        gPaused=false;
        setSevenManiaSkinAnimationPaused(false);
        for(HWND button:gPauseButtons)if(button)ShowWindow(button,SW_HIDE);
        SetFocus(gWindow);
        gPlayAbandoned=true;
        finishPlayback(); // starts the ending and holds
        if(lr2PlayEnding())gLr2Play.endingClockPending=true;
        gFramePacer.reset(gTargetFps);
        InvalidateRect(gWindow,nullptr,FALSE);
        return;
    }
#endif
    resumePlayback();
    if (!gPlaying || gPaused) return; // resume failed and already left for song select
    gPlayAbandoned = true;
    finishPlayback();
}

void restartPlayback() {
    gFrameStatistics.reset();
    // A failed play goes on to its result; the song it would restart is already released.
    if (!gPlaying || gLoading || gFailureEndingStarted || gActiveChartIndex < 0 || gActiveChartIndex >= static_cast<int>(gCharts.size())) return;
    if(gAudio.isRunning()&&!gAudio.isPaused()&&!gAudio.pause())gAudio.stop();
    const auto& chart = gCharts[static_cast<size_t>(gActiveChartIndex)];
    gPaused = false;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    setSevenManiaSkinAnimationPaused(false);
#endif
    gPlayhead = gPractice.enabled ? gPractice.startSeconds : 0;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(gLr2PlayScene||!gLr2ResultFile.empty())gLr2Play.resetTimers(gPlayhead);
    if(gLr2PlayScene&&gLr2GpuFailed&&gCompositionEnabled) {
        gLr2GpuFailed=false;
        gGpuUnavailable=false;
    }
#endif
    gHit.assign(chart.notes.size(), false);
    gLingeringLongNotes.clear();gReleasedLongNotes.clear();
    gResultScore.reset(static_cast<int>(bms::judgementNoteCount(chart)));
    gFeedback.reset();
    gPlayAbandoned=false;
    resetFailureEnding();
    bms::configurePlaybackGauge(gGrooveGauge,chart,gGrooveGaugeMode,gPractice.enabled);
    if(gChartOptions.gaugeAuthored)gGrooveGauge.configureAuthored(gChartOptions.gameLevel,gChartOptions.extra);
    gKeyBeams.reset();
    if(gActiveChartIndex>=0 && gActiveChartIndex<static_cast<int>(gCharts.size())) {
        const auto layout=bms::chartLayout(gCharts[gActiveChartIndex],0,68,104);
        gKeyBeams.configure(layout);gFeedback.configure(layout);
    }
    gScratchDisc.reset();
    gPressed.fill(false);gKeyboardPressed.fill(false);gControllerPressed.fill(false);gController.mapper.reset();clearAccuracyRestart();
    gHolds.fill(LongNoteHold{});
    resetMines();
    resetAutoPlay();
    gMissScanIndex = 0;
    gAutoJudge.reset();
    gBenchmarkSoundIndex = 0;
    gBgaEventIndex = 0;
    gBgaActiveImages.fill(L"");gBgaActiveImages[1]=L"00";
    gMissBgaUntil = 0;
    gJudgementText.clear();
    gJudgementUntil = {};
    gComboPop.reset();
    gJudgementPop.reset();
    gTextRaster.clear(); gHudTextRaster.clear(); gHudMaskRaster.clear();
    const auto bgaCanvas=playBgaCanvas();
    gBgaRaster.start(chart,bgaCanvas.right,bgaCanvas.bottom,gPlayhead);
    if (!chart.fallbackVideo.empty()) {
        gVideoBga.request(std::max(0.0, gPlayhead - gBgaVideoStartSeconds), gWindowWidth, gWindowHeight);
        gVideoBga.waitUntilPrepared();
    }
    releaseBgaBackplate();
    updateBgaState(chart, gPlayhead);
    std::wstring error;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    prepareSkinForPlayback();
#endif
    if (!(gPractice.enabled ? gAudio.startAt(gPractice.startSeconds,error,gPractice.rate()*gChartOptions.rate) : gAudio.startAt(0,error,gChartOptions.rate))) {
        showBrowseView();
        MessageBoxW(gWindow, error.c_str(), L"곡을 다시 시작하지 못했습니다", MB_ICONERROR | MB_OK);
        return;
    }
    gPrewarmTextPending = true; // first gameplay frame; never between pause and audio restart
    for (HWND button : gPauseButtons) if (button) ShowWindow(button, SW_HIDE);
    SetFocus(gWindow);
    gFramePacer.reset(gTargetFps);
    InvalidateRect(gWindow, nullptr, TRUE);
}

void adjustPausedSetting(WPARAM key) {
    if (key == VK_F1 || key == VK_F2) {
        gNoteSpeed = std::clamp(gNoteSpeed + (key == VK_F2 ? 0.25 : -0.25), 0.5, 10.0);
        saveDisplaySettings();
    } else if (key == VK_PRIOR || key == VK_NEXT) {
        gLaneCoverPercent = std::clamp(gLaneCoverPercent + (key == VK_PRIOR ? 5 : -5), 0, 100);
        saveDisplaySettings();
    }
    InvalidateRect(gWindow, nullptr, FALSE);
}

void handlePausedKey(WPARAM key, bool repeated) {
    if (key == VK_ESCAPE) {
        if (!repeated) resumePlayback();
    } else if (key == VK_RETURN) {
        if (!repeated) {
            if (gPauseSelection < gPauseButtons.size() && gPauseButtons[gPauseSelection])
                SendMessageW(gPauseButtons[gPauseSelection], BM_CLICK, 0, 0);
            else resumePlayback();
        }
    } else if (key == VK_LEFT || key == VK_UP || key == VK_RIGHT || key == VK_DOWN) {
        if (!repeated && !gPauseButtons.empty()) {
            const int direction = (key == VK_LEFT || key == VK_UP) ? -1 : 1;
            const int count = static_cast<int>(gPauseButtons.size());
            gPauseSelection = static_cast<size_t>((static_cast<int>(gPauseSelection) + direction + count) % count);
            if (gPauseButtons[gPauseSelection]) SetFocus(gPauseButtons[gPauseSelection]);
            for (HWND button : gPauseButtons) if (button) InvalidateRect(button, nullptr, FALSE);
            InvalidateRect(gWindow, nullptr, FALSE);
            UpdateWindow(gWindow);
        }
    } else if (key == 'R' || key == VK_F5) {
        if (!repeated) restartPlayback();
    } else if (key == 'Q') {
        if (!repeated) quitPausedPlayback();
    } else if (key == VK_F1 || key == VK_F2 || key == VK_PRIOR || key == VK_NEXT) {
        adjustPausedSetting(key);
    }
}
