// Included after loading worker setup; shares the existing UI-owned engine state.
bool prepareSelectedChartData(bool deferred=false) {
    if(gPlaybackLoad.phase()!=bms::PlaybackLoadPhase::PreparingPlay)return false;
    if(!bms::gameplayModeAllowed(gLoadResult.chart)){
        const auto reason=bms::gameplayModeBlockReason(gLoadResult.chart);
        setStatus(reason);return false;
    }
    const int index = gLoadResult.index;
    if(index<0||index>=static_cast<int>(gCharts.size()))return false;
    gActiveChartIndex = index;
    gLoadResult.chart.partialLoad = gLoadResult.chart.partialLoad || gCharts[static_cast<size_t>(index)].partialLoad;
    appendPartialLoadReason(gLoadResult.chart, gCharts[static_cast<size_t>(index)].partialLoadReason);
    gLoadResult.chart.previewStartSeconds = gCharts[static_cast<size_t>(index)].previewStartSeconds;
    gLoadResult.chart.previewDurationSeconds = gCharts[static_cast<size_t>(index)].previewDurationSeconds;
    gCharts[static_cast<size_t>(index)] = std::move(gLoadResult.chart);
    auto& playingChart = gCharts[static_cast<size_t>(index)];
    gChartOptions=bms::forcedOptions(playingChart);gAuthoredCoverPercent=0;
    gBgaVideoStartSeconds = bms::bgaVideoStartSeconds(playingChart);
    if (gPractice.enabled) {
        gPracticeNotesChartIndex = index;
        gPracticeOriginalDuration = playingChart.durationSeconds;
        gPracticeOriginalNotes = std::move(playingChart.notes);
        playingChart.notes = gPractice.notesFor(gPracticeOriginalNotes);
    }
    rebuildPartialLoadSummary();
    gHit.assign(gCharts[static_cast<size_t>(index)].notes.size(), false);
    gLingeringLongNotes.clear();
    gResultScore.reset(static_cast<int>(bms::judgementNoteCount(gCharts[index])));
    gFeedback.reset();
    gGrooveGauge.configure(bms::judgementNoteCount(gCharts[index]),gGrooveGaugeMode,bms::chartTotal(gCharts[index],bms::judgementNoteCount(gCharts[index])));
    if(gChartOptions.gaugeAuthored)gGrooveGauge.configureAuthored(gChartOptions.gameLevel,gChartOptions.extra);
    gKeyBeams.reset();
    if(gActiveChartIndex>=0 && gActiveChartIndex<static_cast<int>(gCharts.size())) {
        const auto layout=bms::chartLayout(gCharts[gActiveChartIndex],0,68,104);
        gKeyBeams.configure(layout);gFeedback.configure(layout);
    }
    gScratchDisc.reset();
    for (auto& indices : gLaneNoteIndices) indices.clear();
    const auto& chartNotes = gCharts[static_cast<size_t>(index)].notes;
    for (size_t i = 0; i < chartNotes.size(); ++i) {
        if (chartNotes[i].lane >= 0 && chartNotes[i].lane < 16)
            gLaneNoteIndices[static_cast<size_t>(chartNotes[i].lane)].push_back(i);
    }
    gMissScanIndex = 0;
    gPressed.fill(false);gKeyboardPressed.fill(false);gControllerPressed.fill(false);gController.mapper.reset();clearAccuracyRestart();
    gHolds.fill(LongNoteHold{});
    resetMines();
    gPlayhead = gPractice.enabled ? gPractice.startSeconds : 0;
    gBgaEventIndex = 0;
    gBgaActiveImages.fill(L"");gBgaActiveImages[1]=L"00";
    gMissBgaUntil = 0;
    gBgaRaster.reset();
    if(deferred)gBgaRaster.begin(gCharts[static_cast<size_t>(index)],gWindowWidth,gWindowHeight,gPlayhead);
    else gBgaRaster.start(gCharts[static_cast<size_t>(index)], gWindowWidth, gWindowHeight, gPlayhead);
    gBgaBackplateDirty = true;
    updateBgaState(gCharts[static_cast<size_t>(index)], gPlayhead);
    gJudgementText.clear();
    gJudgementUntil = {};
    gComboPop.reset();
    gJudgementPop.reset();
    gTextRaster.clear(); gHudTextRaster.clear(); gHudMaskRaster.clear();
    ShowWindow(gList, SW_HIDE);
    ShowWindow(gFolder, SW_HIDE);
    ShowWindow(gStatus, SW_HIDE);
    ShowWindow(gInfo, SW_HIDE);
    ShowWindow(gHiddenSongs, SW_HIDE);
    ShowWindow(gResultNext, SW_HIDE);
    SetFocus(gWindow);
    // gPlaying stays false until audio output has successfully started.
    gTitleFadeOutStarted = {};
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(!deferred)prepareSkinForPlayback();
#endif
    return true;
}

bool startPreparedChart(bool notifyFailure=true) {
    if(!gPlaybackLoad.canStart()||gLoadCancelled||songEntryActive())return false;
    if(gPlaybackJob.active&&(!gPlaybackJob.consumed||!gPlaybackJob.gpuReady||!gBgaRaster.initialPrepared()))return false;
    const int index=gActiveChartIndex;
    std::wstring error;
    gSelectionAudio.stopDecision();
    gAudio.setWasapiExclusiveEnabled(gAudioOutputMode == 1);
    if (!(gPractice.enabled ? gAudio.startAt(gPractice.startSeconds,error,gPractice.rate()*gChartOptions.rate) : gAudio.startAt(0,error,gChartOptions.rate))) {
        setStatus(error);
        if(notifyFailure)MessageBoxW(gWindow, error.c_str(), L"오디오 출력을 시작하지 못했습니다", MB_ICONERROR | MB_OK);
        gPlaybackLoad.fail();gLoading=false;showBrowseView();
        return false;
    }
    gPlaybackLoad.started();gPlaying=true;gLoading=false;
    gFpsEpoch = std::chrono::steady_clock::now();
    gFpsFrameCount = 0;
    gMeasuredFps = 0;
    gFrameStatistics.reset();
    BMS_PERF_SESSION(executableFolder() / L"BmsPlayer-performance.csv");
    gPlayhead = gPractice.enabled ? gPractice.startSeconds : 0;
    gBgaEventIndex = 0;
    gBgaActiveImages.fill(L"");gBgaActiveImages[1]=L"00";
    updateBgaState(gCharts[static_cast<size_t>(index)], gPlayhead);
    gFramePacer.reset(gTargetFps);
    InvalidateRect(gWindow, nullptr, TRUE);
    return true;
}

// Preserve automatic start for phase 2; a later UI can stop after ready().
void finishSelectedChart() {
    if(gLoadCancelled){gPlaybackLoad.cancel();gLoading=false;showBrowseView();return;}
    if(!gPlaybackLoad.beginPreparation())return;
    try {
        if(!prepareSelectedChartData()) {gPlaybackLoad.fail();gLoading=false;showBrowseView();return;}
        if(gLoadCancelled){gPlaybackLoad.cancel();gLoading=false;showBrowseView();return;}
        if(gPlaybackLoad.ready())startPreparedChart();
    }catch(...){gPlaybackLoad.fail();gLoading=false;showBrowseView();setStatus(L"플레이 준비 중 오류가 발생했습니다.");}
}
