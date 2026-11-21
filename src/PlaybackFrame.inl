void updatePlaybackFrame() {
    pollControllerInput();
    BMS_TRACE_ZONE("Playback update");
    BMS_TRACE_FRAME();
    if (!gPlaying||gAccuracyRestartPending) return;
    BMS_PERF_BEGIN();HUD_FRAME_BEGIN();
    { BMS_PERF_SCOPE(positionMs);HUD_SCOPE(AudioPosition); gPlayhead = heardPlayhead(); }
    BMS_PERF_SET(rawPositionSeconds, gAudio.lastRawPositionSeconds());
#ifdef BMS_ENABLE_SKIN_BINDINGS
    // Nothing is judged while the skin's ending runs; only the picture moves.
    if(lr2PlayEnding()) {
        finishPlayback();
        // Paint now like a normal frame: a WM_PAINT left in the queue arrives during the next input
        // pump, which drops it, and the ending would never be drawn.
        if(gPlaying){InvalidateRect(gWindow,nullptr,FALSE);HUD_SCOPE(PaintDispatch);UpdateWindow(gWindow);}
        return;
    }
    sampleLr2ResultGauge();
#endif
    const int index = gActiveChartIndex;
    const double endSeconds = gPractice.enabled ? gPractice.endSeconds : gAudio.durationSeconds();
    if (index < 0 || index >= static_cast<int>(gCharts.size()) || gPlayhead >= endSeconds + 0.25 * (gPractice.enabled ? gPractice.rate() : 1.0)) {
        finishPlayback();
        return;
    }
    const auto& notes = gCharts[static_cast<size_t>(index)].notes;
    const auto& chart = gCharts[static_cast<size_t>(index)];
    const double bgaTime = gPlayhead - gBgaVideoStartSeconds;
    { BMS_PERF_SCOPE(videoMs);HUD_SCOPE(VideoUpdate); if (bgaTime >= 0 && (!hybridRendererEnabled || gBgaBrightness > 0)) gVideoBga.update(bgaTime); }
    {HUD_SCOPE(BgaStateUpdate);updateBgaState(chart, gPlayhead);}
    {HUD_SCOPE(GameLogic);
    // Realtime auto-adjust only moves the judge offset here (never in the input path), a couple of ms at a time.
    if(gAutoJudgeMode==bms::AutoJudgeAdjuster::Realtime && !gPractice.enabled){
        const int next=gAutoJudge.step(gTimingOffsets.judgeMs);
        if(next!=gTimingOffsets.judgeMs){gTimingOffsets.judgeMs=next;gAutoJudgeUsedThisPlay=true;}
    }
    const double judgeT=judgePlayhead();
    // Autoplay's presses and releases come before holds, mines and misses are judged at this time.
    if (gAutoplay) {
        gAutoPlayCursor.due(judgeT, [](const bms::AutoPlayEvent& event) {
            if (!gPlaying || gAccuracyRestartPending) return;
            if (event.press) pressLane(event.lane, static_cast<int>(event.noteIndex));
            else releaseLaneInput(event.lane, true);
        });
        if (!gPlaying || gAccuracyRestartPending) return;
    }
    for (size_t lane = 0; lane < gHolds.size(); ++lane) {
        const unsigned ticks=gHolds[lane].takeGaugeTicks(judgeT);
        for(unsigned tick=0;tick<ticks;++tick)gGrooveGauge.holdTick(gPressed[lane]);
        if(gGrooveGauge.depleted() && !gPractice.enabled && !gNoInputBenchmark){finishPlayback();return;}
        const HoldResult result=gHolds[lane].update(judgeT,gPressed[lane],currentJudgementWindows().goodMs/1000.0*playbackTimingScale());
        resolveHold(lane,result);
        if (!gPlaying||gAccuracyRestartPending) return;
        if (gPressed[lane]) showHoldCombo(gHolds[lane].takeComboTicks(judgeT));
    }
    if(gMineHit.size()!=chart.mines.size())gMineHit.assign(chart.mines.size(),false);
    for(size_t mine=0;mine<chart.mines.size();++mine) {
        const auto& n=chart.mines[mine];if(gMineHit[mine] || n.timeSeconds>judgeT)continue;
        gMineHit[mine]=true;
        if(n.timeSeconds<(gPractice.enabled?gPractice.startSeconds:0))continue;
        // Autoplay steps around mines; one inside a long note it must hold, or on a note's own time, is ignored.
        if(n.lane>=0 && n.lane<16 && gPressed[n.lane] && !gAutoplay) {
            gGrooveGauge.damage(n.damage);gAudio.trigger(L"00");setJudgement(L"MINE",RGB(255,64,64));
            if(gGrooveGauge.depleted() && !gPractice.enabled && !gNoInputBenchmark){finishPlayback();return;}
        }
    }
    if (gNoInputBenchmark) {
        HUD_SCOPE(BenchmarkKeysounds);
        for (; gBenchmarkSoundIndex < notes.size() && notes[gBenchmarkSoundIndex].timeSeconds <= gPlayhead; ++gBenchmarkSoundIndex) {
            const auto& note = notes[gBenchmarkSoundIndex];
            if (gPractice.enabled && note.timeSeconds < gPractice.startSeconds) continue;
            if (!note.sampleId.empty()) gAudio.trigger(note.sampleId);
        }
    }
    // A scratch may have a longer legacy deadline than a following key note.
    // Expire each eligible note independently; advance only over resolved heads.
    const double legacySlow=currentJudgementWindows().badMs;
    const double minimumSlow=bms::usesOfficialJudgement(chart)?std::min(legacySlow,double(bms::officialJudgeWindow(chart.rank).badSlow)):legacySlow;
    for(size_t i=gMissScanIndex;i<notes.size() && notes[i].timeSeconds<judgeT-minimumSlow/1000*playbackTimingScale();++i) {
        if(gPractice.enabled&&notes[i].timeSeconds<gPractice.startSeconds){gHit[i]=true;continue;}
        if(!gHit[i] && bms::laneNoteExpired(chart,notes[i],judgeT,playbackTimingScale(),gJudgementProfile)) {
            gHit[i]=true;
            if(notes[i].longNote())gLingeringLongNotes.push_back(i);
            lr2JudgeLane(static_cast<std::size_t>(std::max(0,notes[i].lane)));
            showTimingJudgement(TimingJudgement::Poor,2,true);
            if(!gPlaying||gAccuracyRestartPending)return;
            if(bms::applyMissedChargeTail(gGrooveGauge,notes[i]) && gGrooveGauge.depleted() && !gPractice.enabled && !gNoInputBenchmark){finishPlayback();return;}
        }
    }
    while(gMissScanIndex<notes.size() && gHit[gMissScanIndex])++gMissScanIndex;
    InvalidateRect(gWindow, nullptr, FALSE);
    }
    {HUD_SCOPE(PaintDispatch);UpdateWindow(gWindow);} // Present this frame now, independent of WM_TIMER/paint queue cadence.
    BMS_PERF_FINISH(gPlayhead);HUD_FRAME_END(gPlayhead);
}
