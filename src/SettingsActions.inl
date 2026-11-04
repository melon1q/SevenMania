// Included by Main.cpp inside its application namespace. Device operations
// remain here; their inputs and SettingsController contain no UI handles.
int readSettingValue(int id) {
    if (id >= ID_VOLUME_BASE && id < ID_VOLUME_BASE + 4) return gVolumes[id - ID_VOLUME_BASE];
    if (id >= ID_COLOR_BASE && id < ID_COLOR_BASE + 8) return gLaneColors[id - ID_COLOR_BASE];
    if (id >= ID_SEPARATOR_BASE && id < ID_SEPARATOR_BASE + 7) return gLaneSeparators[id - ID_SEPARATOR_BASE];
    switch (id) {
    case ID_FPS: return gTargetFps;
    case ID_SPEED: return int(std::lround(gNoteSpeed * 4));
    case ID_SKIN_JUDGEMENT_MODE:return gSkinJudgementMode;
    case ID_SKIN_COMBO_MODE:return gSkinComboMode;
    case ID_JUDGE_POSITION: return gJudgeLineOffset;
    case ID_LANE_TRANSPARENCY: return gLaneTransparency;
    case ID_BGA_DARKNESS: return gBgaBrightness;
    case ID_LANE_COVER: return gLaneCoverPercent;
    case ID_SCREEN_MODE: return gScreenMode;
    case ID_GEAR_POSITION: return gGearPosition;
    case ID_COMPOSITION: return gCompositionEnabled;
    case ID_DIRECT_COMPOSITION: return gDirectCompositionEnabled;
    case ID_JUDGEMENT_PROFILE: return int(gJudgementProfile);
    case ID_NOTE_HEIGHT: return gNoteHeight;
    case ID_GROOVE_GAUGE: return int(gGrooveGaugeMode);
    case ID_TIMING_HINT: return gTimingHintCondition;
    case ID_ACCURACY_LIMIT: return int(gAccuracyLimit);
    case ID_SOUND_BUFFER: return gSoundBufferFrames;
    case ID_BROWSE_BACKGROUND: return gBrowseBackgroundMode;
    case ID_BACKGROUND_BLUR: return gBackgroundBlur;
    case ID_BACKGROUND_OPACITY: return gBackgroundOpacity;
    case ID_SONG_PREVIEW: return gSongPreviewEnabled;
    case ID_PREVIEW_VOLUME: return gPreviewVolume;
    case ID_CPU_LIMIT: return gCpuLimitPercent;
    case ID_MEMORY_LIMIT: return gMemoryLimitPercent;
    case ID_GPU_LIMIT: return gGpuLimitPercent;
    case ID_RESOURCE_USAGE: return gShowResourceUsage;
    case ID_COMBO_TEXT_Y: return gComboTextYOffset;
    case ID_JUDGEMENT_TEXT_Y: return gJudgementTextYOffset;
    case ID_CPU_PERFORMANCE: return gCpuPerformanceMode;
    case ID_NOTE_SUBPIXEL: return gNoteSubpixel;
    case ID_NOTE_ANTIALIAS: return gNoteAntialias;
    case ID_LOW_LATENCY: return gReflexRequested;
    case ID_RESOLUTION:
        if (!gResolutionConfigured) return 0;
        for (size_t i = 0; i < gResolutionOptions.size(); ++i)
            if (gResolutionOptions[i].width == gResolutionWidth && gResolutionOptions[i].height == gResolutionHeight) return int(i + 1);
        return -1;
    default: return 0;
    }
}

bool applySettingValue(int id, int value, std::wstring& error) {
    if (id >= ID_VOLUME_BASE && id < ID_VOLUME_BASE + 4) {
        gVolumes[id - ID_VOLUME_BASE] = value;
        gAudio.setVolumes(gVolumes[0], gVolumes[1], gVolumes[2], gVolumes[3]); return true;
    }
    if (id >= ID_COLOR_BASE && id < ID_COLOR_BASE + 8) { gLaneColors[id - ID_COLOR_BASE] = value; return true; }
    if (id >= ID_SEPARATOR_BASE && id < ID_SEPARATOR_BASE + 7) { gLaneSeparators[id - ID_SEPARATOR_BASE] = value != 0; return true; }
    switch (id) {
    case ID_FPS: gTargetFps = value; gFramePacer.reset(value); break;
    case ID_SPEED: gNoteSpeed = value / 4.0; break;
    case ID_SKIN_JUDGEMENT_MODE:case ID_SKIN_COMBO_MODE:
#ifdef BMS_ENABLE_SKIN_BINDINGS
        if(auto failure=gSkinApplication.setHudModes(id==ID_SKIN_JUDGEMENT_MODE?value:gSkinJudgementMode,id==ID_SKIN_COMBO_MODE?value:gSkinComboMode)){error=skinWide(failure->message);return false;}
#endif
        (id==ID_SKIN_JUDGEMENT_MODE?gSkinJudgementMode:gSkinComboMode)=value;break;
    case ID_JUDGEMENT_PROFILE: gJudgementProfile = static_cast<JudgementProfile>(value); break;
    case ID_GROOVE_GAUGE: gGrooveGaugeMode = static_cast<GrooveGaugeMode>(value); break;
    case ID_TIMING_HINT: gTimingHintCondition=value;gTimingHint=0;break;
    case ID_ACCURACY_LIMIT: gAccuracyLimit=static_cast<AccuracyLimit>(value);clearAccuracyRestart();break;
    case ID_NOTE_HEIGHT: gNoteHeight = value; break;
    case ID_GEAR_POSITION:
#ifdef BMS_ENABLE_SKIN_BINDINGS
        if((gGearPosition==0&&value==2)||(gGearPosition==2&&value==0)){const int choice=MessageBoxW(gSettingsWindow,L"주변 UI 사물의 위치를 좌우 반전해 배치할까요?",L"기어 배치 전환",MB_YESNOCANCEL|MB_ICONQUESTION);if(choice==IDCANCEL)return false;gSkinMirrorUi=choice==IDYES;}
        else gSkinMirrorUi=false;
#endif
        gGearPosition = value; gBgaBackplateDirty = true; break;
    case ID_BGA_DARKNESS: gBgaBrightness = value; gBgaDarkness = 100 - value; gBgaBackplateDirty = true; break;
    case ID_LANE_TRANSPARENCY: gLaneTransparency = value; gBgaBackplateDirty = true; break;
    case ID_LANE_COVER: gLaneCoverPercent = value; break;
    case ID_JUDGE_POSITION: {
        const int nominal = int(std::lround(gWindowHeight * 2.0 / 3.0));
        gJudgeLineOffset = std::clamp(value, 40 - nominal, std::max(40, gWindowHeight - 112) - nominal); break;
    }
    case ID_COMBO_TEXT_Y: gComboTextYOffset = value; break;
    case ID_JUDGEMENT_TEXT_Y: gJudgementTextYOffset = value; break;
    case ID_BACKGROUND_BLUR: gBackgroundBlur = value; gSongPreview.tick(gVolumes[0], gPreviewVolume, gSoundBufferFrames); break;
    case ID_BACKGROUND_OPACITY: gBackgroundOpacity = value; gSongPreview.tick(gVolumes[0], gPreviewVolume, gSoundBufferFrames); break;
    case ID_PREVIEW_VOLUME: gPreviewVolume = value; gSongPreview.tick(gVolumes[0], gPreviewVolume, gSoundBufferFrames); break;
    case ID_BROWSE_BACKGROUND: gBrowseBackgroundMode = value; updateBrowseMedia(); break;
    case ID_SONG_PREVIEW: gSongPreviewEnabled = value != 0; updateBrowseMedia(); break;
    case ID_CPU_LIMIT: gCpuLimitPercent = value; break;
    case ID_GPU_LIMIT: gGpuLimitPercent = value; gGpuFrameAccumulator = 100; break;
    case ID_MEMORY_LIMIT:
        if (!gSystemResources.setMemoryLimitPercent(value, error)) { gMemoryLimitError = error; return false; }
        gMemoryLimitPercent = value; gMemoryLimitError.clear(); break;
    case ID_RESOURCE_USAGE: gShowResourceUsage = value != 0; break;
    case ID_CPU_PERFORMANCE: {
        const bool previous = gCpuPerformanceMode; gCpuPerformanceMode = value != 0;
        if (!applyCpuPerformanceMode()) { gCpuPerformanceMode = previous; error = L"CPU 성능 우선 모드를 적용하지 못했습니다."; return false; }
        break;
    }
    case ID_SOUND_BUFFER: gSoundBufferFrames = value; gAudio.setBufferFrames(value); startUiEffects(); break;
    case ID_NOTE_SUBPIXEL: gNoteSubpixel = value != 0; break;
    case ID_NOTE_ANTIALIAS: gNoteAntialias = value != 0; break;
    case ID_LOW_LATENCY:
        gReflexRequested = value != 0;
        if (!gReflexRequested) gCompositionPresenter.lowLatency().request(latency::Mode::Off);
        break;
    case ID_DIRECT_COMPOSITION:
        gDirectCompositionEnabled = value != 0; gCompositionPresenter.setDirectComposition(gDirectCompositionEnabled); gGpuUnavailable = false; break;
    case ID_COMPOSITION:
        gCompositionEnabled = value != 0; gGpuFrameAccumulator = 100; gGpuUnavailable = false; gCompositionPresenter.reset(); break;
    case ID_RESOLUTION:
        if (value > int(gResolutionOptions.size())) { error = L"현재 사용할 수 없는 해상도입니다."; return false; }
        gResolutionConfigured = value != 0;
        gResolutionWidth = value ? gResolutionOptions[value - 1].width : 0;
        gResolutionHeight = value ? gResolutionOptions[value - 1].height : 0;
        [[fallthrough]];
    case ID_SCREEN_MODE: {
        if (id == ID_SCREEN_MODE) gScreenMode = value;
        applyScreenMode(true);
        const int nominal = int(std::lround(gWindowHeight * 2.0 / 3.0));
        gJudgeLineOffset = std::clamp(gJudgeLineOffset, 40 - nominal, std::max(40, gWindowHeight - 112) - nominal);
        break;
    }
    default: error = L"지원하지 않는 설정입니다."; return false;
    }
    return true;
}

bms::SettingsController& settingsController() {
    static bms::SettingsController controller(readSettingValue, applySettingValue, saveDisplaySettings);
    return controller;
}

// Presentation adapter. Only this section reads/writes native controls.
HWND settingControl(int id) {
    if (id >= ID_VOLUME_BASE && id < ID_VOLUME_BASE + 4) return gVolumeSliders[id - ID_VOLUME_BASE];
    if (id >= ID_COLOR_BASE && id < ID_COLOR_BASE + 8) return gColorChoices[id - ID_COLOR_BASE];
    if (id >= ID_SEPARATOR_BASE && id < ID_SEPARATOR_BASE + 7) return gSeparatorButtons[id - ID_SEPARATOR_BASE];
    switch (id) {
    case ID_FPS: return gFpsChoice; case ID_SPEED: return gSpeedChoice;
    case ID_JUDGE_POSITION: return gJudgePosition; case ID_LANE_TRANSPARENCY: return gLaneTransparencySlider;
    case ID_BGA_DARKNESS: return gBgaDarknessChoice; case ID_LANE_COVER: return gLaneCoverSlider;
    case ID_SCREEN_MODE: return gScreenModeChoice; case ID_GEAR_POSITION: return gGearChoice;
    case ID_COMPOSITION: return gCompositionChoice; case ID_DIRECT_COMPOSITION: return gDirectCompositionChoice;
    case ID_JUDGEMENT_PROFILE: return gJudgementChoice; case ID_NOTE_HEIGHT: return gNoteHeightChoice;
    case ID_GROOVE_GAUGE: return gGrooveGaugeChoice; case ID_SOUND_BUFFER: return gSoundBufferChoice;
    case ID_TIMING_HINT: return gTimingHintChoice;
    case ID_ACCURACY_LIMIT: return gAccuracyLimitChoice;
    case ID_SKIN_JUDGEMENT_MODE:return gSkinJudgementChoice;
    case ID_SKIN_COMBO_MODE:return gSkinComboChoice;
    case ID_BROWSE_BACKGROUND: return gBrowseBackgroundChoice; case ID_BACKGROUND_BLUR: return gBackgroundBlurSlider;
    case ID_BACKGROUND_OPACITY: return gBackgroundOpacitySlider; case ID_SONG_PREVIEW: return gSongPreviewChoice;
    case ID_PREVIEW_VOLUME: return gPreviewVolumeSlider; case ID_CPU_LIMIT: return gCpuLimitSlider;
    case ID_MEMORY_LIMIT: return gMemoryLimitSlider; case ID_GPU_LIMIT: return gGpuLimitSlider;
    case ID_RESOURCE_USAGE: return gResourceUsageChoice; case ID_COMBO_TEXT_Y: return gComboTextYSlider;
    case ID_JUDGEMENT_TEXT_Y: return gJudgementTextYSlider; case ID_CPU_PERFORMANCE: return gCpuPerformanceChoice;
    case ID_NOTE_SUBPIXEL: return gSubpixelChoice; case ID_NOTE_ANTIALIAS: return gAntialiasChoice;
    case ID_LOW_LATENCY: return gLowLatencyChoice; case ID_RESOLUTION: return gResolutionChoice;
    default: return nullptr;
    }
}
void refreshSettingControls(int id) {
    if (gRmlSettingsPageActive) return;
    if (!gSettingsWindow || !IsWindow(gSettingsWindow)) return;
    const int value = readSettingValue(id); HWND control = settingControl(id);
    if (id == ID_COMPOSITION) {
        if (gGpuLimitSlider) EnableWindow(gGpuLimitSlider, gCompositionEnabled);
        if (gLowLatencyChoice) EnableWindow(gLowLatencyChoice, gCompositionEnabled && latency::probeReflex().supported &&
            gCompositionPresenter.lowLatency().status(latency::Mode::Reflex) != latency::Status::SessionDisabled);
        if (gDirectCompositionChoice) ShowWindow(gDirectCompositionChoice, gCompositionEnabled && gSettingsCategory == 1 ? SW_SHOW : SW_HIDE);
    }
    if (id == ID_RESOLUTION || id == ID_SCREEN_MODE) {
        const int nominal = int(std::lround(gWindowHeight * 2.0 / 3.0));
        if (gJudgePosition) {
            SendMessageW(gJudgePosition, TBM_SETRANGEMIN, TRUE, 40 - nominal);
            SendMessageW(gJudgePosition, TBM_SETRANGEMAX, TRUE, std::max(40, gWindowHeight - 112) - nominal);
            SendMessageW(gJudgePosition, TBM_SETPOS, TRUE, gJudgeLineOffset);
        }
        if (gScreenModeChoice) SendMessageW(gScreenModeChoice, CB_SETCURSEL, 2 - gScreenMode, 0);
        updateJudgePositionLabel();
    }
    HWND display = nullptr;
    if (id >= ID_VOLUME_BASE && id < ID_VOLUME_BASE + 4) display = gVolumeValues[id - ID_VOLUME_BASE];
    switch (id) {
    case ID_CPU_LIMIT: display = gCpuLimitValue; break; case ID_GPU_LIMIT: display = gGpuLimitValue; break;
    case ID_BACKGROUND_BLUR: display = gBackgroundBlurValue; break; case ID_BACKGROUND_OPACITY: display = gBackgroundOpacityValue; break;
    case ID_PREVIEW_VOLUME: display = gPreviewVolumeValue; break;
    case ID_LANE_TRANSPARENCY: display = gLaneTransparencyValue; break;
    case ID_MEMORY_LIMIT:
        setText(gMemoryLimitValue, std::to_wstring(value) + L"% (" + number(double(gSystemResources.totalPhysicalMemoryBytes()) * value / (100.0 * 1024 * 1024 * 1024), 1) + L" GB)"); break;
    case ID_LANE_COVER: setText(gLaneCoverValue, value ? std::to_wstring(value) + L"%" : L"없음"); break;
    case ID_JUDGE_POSITION: updateJudgePositionLabel(); break;
    case ID_COMBO_TEXT_Y: setText(gComboTextYValue, hudTextYOffsetLabel(value)); break;
    case ID_JUDGEMENT_TEXT_Y: setText(gJudgementTextYValue, hudTextYOffsetLabel(value)); break;
    }
    if (display) setText(display, std::to_wstring(value) + L"%");
    if (!control) return;
    wchar_t type[64]{}; GetClassNameW(control, type, 64);
    if (_wcsicmp(type, TRACKBAR_CLASSW) == 0) SendMessageW(control, TBM_SETPOS, TRUE, value);
    else if (_wcsicmp(type, L"Button") == 0) SendMessageW(control, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    else {
        int option = value;
        if (id == ID_FPS) option = frameRateIndex(value);
        if (id == ID_SPEED) option = value - 2;
        if(id==ID_SKIN_JUDGEMENT_MODE||id==ID_SKIN_COMBO_MODE)option=value+1;
        if (id == ID_BGA_DARKNESS) option = value / 10;
        if (id == ID_NOTE_HEIGHT) option = (value - 4) / 2;
        if (id == ID_SCREEN_MODE) option = 2 - value;
        if (id == ID_SOUND_BUFFER) option = int(std::find(SOUND_BUFFER_OPTIONS.begin(), SOUND_BUFFER_OPTIONS.end(), value) - SOUND_BUFFER_OPTIONS.begin());
        SendMessageW(control, CB_SETCURSEL, option, 0);
        HWND summary = id == ID_FPS ? gSummaryFps : id == ID_SPEED ? gSummarySpeed : id == ID_BGA_DARKNESS ? gSummaryBga :
            id == ID_JUDGEMENT_PROFILE ? gSummaryJudgement : id == ID_SCREEN_MODE ? gSummaryScreenMode : id == ID_GROOVE_GAUGE ? gSummaryGauge : id == ID_ACCURACY_LIMIT ? gSummaryAccuracyLimit : nullptr;
        if (summary) SendMessageW(summary, CB_SETCURSEL, option, 0);
    }
}
bool applyNativeSetting(int id, int value) {
    const auto result = settingsController().set(id, value);
    refreshSettingControls(bms::SettingsController::canonicalId(id));
    if (!result.applied) setBindingStatus(result.error);
    if (gWindow) InvalidateRect(gWindow, nullptr, FALSE);
    return result.applied;
}
bool postSetting(int id, int value) {
    if (!IsWindow(gWindow)) return false;
    if (!settingsController().enqueue(id, value)) return false;
    if (PostMessageW(gWindow, WM_APPLY_SETTINGS, 0, 0)) return true;
    settingsController().clearPending(); return false;
}
