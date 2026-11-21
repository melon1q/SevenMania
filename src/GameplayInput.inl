UINT laneKey(int lane) {
    auto mode=bms::ChartMode::SevenKey;
    if(gActiveChartIndex>=0 && gActiveChartIndex<static_cast<int>(gCharts.size()))mode=gCharts[gActiveChartIndex].mode;
    return bms::laneKeyFor(gKeyProfiles,mode,lane);
}
int keyLane(WPARAM key, LPARAM detail) {
    if(key==VK_SHIFT)key=MapVirtualKeyW((static_cast<UINT>(detail)>>16)&0xff,MAPVK_VSC_TO_VK_EX);
    int count=8;if(gActiveChartIndex>=0 && gActiveChartIndex<static_cast<int>(gCharts.size()))count=bms::chartLanes(gCharts[gActiveChartIndex]);
    for(int lane=0;lane<count;++lane)if(laneKey(lane)==key)return lane;
    return -1;
}

// autoNote >= 0 is an autoplay press: it judges that note as PGREAT whatever the frame's exact time.
// The player's own keys do nothing during autoplay.
void pressLane(int lane, int autoNote = -1) {
    HUD_SCOPE(InputDown);
    const bool automatic = autoNote >= 0;
    if (!gPlaying || gAccuracyRestartPending || gNoInputBenchmark || gAutoplay != automatic || lane < 0 || lane > 15 || gPressed[static_cast<size_t>(lane)]) return;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(lr2PlayEnding())return;
#endif
    BMS_PERF_SCOPE(inputMs);BMS_PERF_COUNT(keyDown);
    gPlayhead = heardPlayhead();BMS_PERF_EVENT(1,lane,gPlayhead);
    const auto& chart = gCharts[static_cast<size_t>(gActiveChartIndex)];
    updateBgaState(chart, gPlayhead);
    gPressed[static_cast<size_t>(lane)] = true;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(gLr2PlayScene) {gLr2Play.keyOn[lane]=gPlayhead;gLr2Play.keyOff[lane]=-1;}
#endif
    lr2JudgeLane(static_cast<std::size_t>(lane));
    gBgaSwitch.press(chart,lane,gPlayhead);
    if (lane == 0) gScratchDisc.update(gPlayhead, true);
    gKeyBeams.setPressed(lane, true, gPlayhead);
    if(gHolds[lane].active() && chart.notes[gHolds[lane].noteIndex()].mode==bms::LongNoteMode::HCN) return;
    const auto& notes=chart.notes;
    const double judgeT=judgePlayhead();
    const int nearest=automatic ? (static_cast<size_t>(autoNote)<gHit.size()&&!gHit[static_cast<size_t>(autoNote)]?autoNote:-1)
        : bms::nearestLaneNote(chart,gHit,lane,judgeT,playbackTimingScale(),gJudgementProfile,gPractice.enabled?gPractice.startSeconds:0);
    if (nearest >= 0) {
        gFeedback.hit(lane, gPlayhead);
        gComboPop.start();
        gHit[static_cast<size_t>(nearest)] = true;
        const auto& note = notes[static_cast<size_t>(nearest)];
        const auto grade=automatic ? TimingJudgement::PGreat
            : *bms::judgeLaneOffset(chart,judgeT,lane,(judgeT-note.timeSeconds)/playbackTimingScale(),gJudgementProfile);
#ifdef BMS_ENABLE_SKIN_BINDINGS
        if(gLr2PlayScene&&grade<=TimingJudgement::Good)gLr2Play.bomb[lane]=gPlayhead;
#endif
        if (!note.sampleId.empty()) gAudio.trigger(note.sampleId);
        if (note.longNote()) {
            gHolds[static_cast<size_t>(lane)].start(static_cast<size_t>(nearest),note.timeSeconds+note.lengthSeconds,judgeT,note.mode,grade);
        }
        showTimingJudgement(grade,note.longNote() && note.mode==bms::LongNoteMode::LN?0:2);
        // Auto-adjust learns from the raw offset (before the judge offset) so the correction cannot feed back into itself.
        if (!automatic && gAutoJudgeMode!=bms::AutoJudgeAdjuster::Off && !gPractice.enabled && !bms::isScratchLane(chart,lane) && static_cast<int>(grade)<=static_cast<int>(TimingJudgement::Good))
            gAutoJudge.feed((gPlayhead-note.timeSeconds)/playbackTimingScale()*1000.0);
        const double signedOffset=judgeT-note.timeSeconds;
        gTimingHint=automatic||gTimingHintCondition==3 ? 0 : static_cast<int>(grade)<=gTimingHintCondition ? 0 : signedOffset < -0.000001 ? -1 : signedOffset > 0.000001 ? 1 : 0;
    } else {
        const auto& indices = gLaneNoteIndices[static_cast<size_t>(lane)];
        if (!indices.empty()) {
            const auto next = std::upper_bound(indices.begin(), indices.end(), gPlayhead,
                [&](double time, size_t noteIndex) { return time < notes[noteIndex].timeSeconds; });
            const size_t noteIndex = next == indices.begin() ? indices.front() : *std::prev(next);
            const auto hidden=bms::hiddenAt(chart,lane,gPlayhead);
            if(hidden && (notes[noteIndex].timeSeconds>gPlayhead || hidden->timeSeconds>=notes[noteIndex].timeSeconds)) gAudio.trigger(hidden->sampleId);
            else if (!notes[noteIndex].sampleId.empty()) gAudio.trigger(notes[noteIndex].sampleId);
        } else if(const auto hidden=bms::hiddenAt(chart,lane,gPlayhead)) gAudio.trigger(hidden->sampleId);
    }
    InvalidateRect(gWindow, nullptr, FALSE);
}

// automatic marks autoplay's own release. During autoplay the player's releases (and the release of every
// lane when the window loses focus) leave autoplay's keys down.
void releaseLaneInput(int lane, bool automatic = false) {
    HUD_SCOPE(InputUp);
    if(lane<0||lane>=16||!gPressed[lane])return;
    if(gPlaying&&gAutoplay&&!automatic)return;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    const bool frozen=lr2PlayEnding(); // A release during the skin's ending is not judged.
#else
    const bool frozen=false;
#endif
    if(gPlaying&&!gPaused&&!frozen&&gActiveChartIndex>=0){
        BMS_PERF_SCOPE(inputMs);BMS_PERF_COUNT(keyUp);
        gPlayhead=heardPlayhead();BMS_PERF_EVENT(2,lane,gPlayhead);
        const double judgeT=judgePlayhead();
        lr2JudgeLane(static_cast<std::size_t>(lane));
        showHoldCombo(gHolds[lane].takeComboTicks(judgeT));
        // Autoplay lets go exactly at the tail, so a late frame still ends the hold as a clean release.
        resolveHold(lane,gHolds[lane].release(judgeT,automatic?std::numeric_limits<double>::max():
            currentJudgementWindows().goodMs/1000.0*playbackTimingScale()));
    }
    gPressed[lane]=false;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(gLr2PlayScene) {gLr2Play.keyOff[lane]=gPlayhead;gLr2Play.keyOn[lane]=-1;}
#endif
    if(lane==0)gScratchDisc.update(gPlayhead,false);
    gKeyBeams.setPressed(lane,false,gPlayhead);
    InvalidateRect(gWindow,nullptr,FALSE);
}
void pollControllerInput() {
    if(gControllerConfiguring)return;
    const bool wasEnabled=gController.config.enabled;
    const bool active=GetForegroundWindow()==gWindow&&!gSettingsWindow&&(!gLoading||gPlaybackLoad.preparing())&&!gCapturingBindings;
    gController.poll(active,[&](const bms::controller::State& state){
        if(active&&gPlaybackLoad.preparing()){
            constexpr WPARAM keys[]{VK_LEFT,VK_F1,VK_F2,VK_NEXT,VK_PRIOR,VK_UP,VK_DOWN,VK_RIGHT};
            for(int lane=0;lane<8;++lane){if(state.lanes[lane]&&!gPlayPreview.held[lane])previewKey(keys[lane]);gPlayPreview.held[lane]=state.lanes[lane];}
            if(std::any_of(state.lanes.begin(),state.lanes.end(),[](bool held){return held;}))gPlayPreview.touched();
            if(state.menuPressed)previewKey(VK_RETURN);return;
        }
        const bool playing=active&&gPlaying&&!gPaused;
        auto mode=bms::ChartMode::SevenKey;
        if(gActiveChartIndex>=0&&gActiveChartIndex<static_cast<int>(gCharts.size()))mode=gCharts[gActiveChartIndex].mode;
        for(int slot=0;slot<16;++slot){
            const int lane=bms::controllerSlotLane(mode,slot);
            if(lane<0)continue;
            const bool previous=gControllerPressed[lane],next=playing&&state.lanes[slot];
            gControllerPressed[lane]=next;
            if(next&&!previous)pressLane(lane);
            else if(previous&&!next&&!gKeyboardPressed[lane])releaseLaneInput(lane);
            if(lane==0&&next&&previous&&state.scratchPulse&&!gKeyboardPressed[0]&&!gHolds[0].active()){
                releaseLaneInput(0);pressLane(0);
            }
        }
        if(state.menuPressed&&active&&gPlaying){if(gPaused)resumePlayback();else pausePlayback();}
    });
    if(wasEnabled&&!gController.config.enabled){
        WritePrivateProfileStringW(L"Controller",L"Enabled",L"0",gSettingsFile.c_str());
        SetTimer(gWindow,ID_CONTROLLER_POLL,1000,nullptr);
    }
}
void openControllerSettings() {
    if(gCapturingBindings)cancelKeyBindingCapture();
    gControllerConfiguring=true;
    for(int lane=0;lane<16;++lane){gControllerPressed[lane]=false;if(!gKeyboardPressed[lane])releaseLaneInput(lane);}
    bms::controller::SettingsDialog dialog(gController,gSettingsWindow?gSettingsWindow:gWindow,gSettingsFile.wstring());
    dialog.show(gWindow);
    SetTimer(gWindow,ID_CONTROLLER_POLL,gController.config.enabled?4:1000,nullptr);
    gControllerConfiguring=false;
}
