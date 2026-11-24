LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(lr2SelectInputLocked(message))return 0;
    if(message==WM_SETCURSOR&&lr2SelectSetCursor(lParam))return TRUE;
    if(lr2SelectInput(window,message,wParam,lParam))return 0;
#endif
    switch (message) {
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN:
        if (gStartupSplash) return 0;
        if (adjustBrowseShortcut(static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)), message == WM_LBUTTONDOWN)) return 0;
        break;
    case WM_CREATE: {
        gWindow = window;
        DragAcceptFiles(window,TRUE);
        BufferedPaintInit();
        const int currentThreadPriority = GetThreadPriority(GetCurrentThread());
        if (currentThreadPriority != THREAD_PRIORITY_ERROR_RETURN)
            gDefaultThreadPriority = currentThreadPriority;
        loadKeyBindings();
        loadDisplaySettings();
        gController.initialize(window);gController.load(gSettingsFile.wstring());
        if(gController.disableIfUnavailable())WritePrivateProfileStringW(L"Controller",L"Enabled",L"0",gSettingsFile.c_str());
        SetTimer(window,ID_CONTROLLER_POLL,gController.config.enabled?4:1000,nullptr);
        if (gCpuPerformanceMode && !applyCpuPerformanceMode()) gCpuPerformanceMode = false;
        gControlBrush = CreateSolidBrush(RGB(16, 20, 28));
        gFont = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        gSmallFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        refreshUiScale();
        gFolder = CreateWindowW(L"STATIC", L"곡 폴더를 선택하세요.", WS_CHILD | SS_ENDELLIPSIS | SS_NOPREFIX, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FOLDER)), nullptr, nullptr);
        gList = CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOCOLUMNHEADER | LVS_SHAREIMAGELISTS, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LIST)), nullptr, nullptr);
        SetWindowSubclass(gList, songListSubclass, 1, 0);
        ListView_SetExtendedListViewStyle(gList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        ListView_SetBkColor(gList, RGB(64, 64, 64));
        ListView_SetTextBkColor(gList, RGB(64, 64, 64));
        gSongRowImages = ImageList_Create(1, ui(songListFallbackRowHeight), ILC_COLOR32, 1, 1);
        if (gSongRowImages) ListView_SetImageList(gList, gSongRowImages, LVSIL_SMALL);
        const std::array<std::pair<const wchar_t*, int>, 2> columns{ {{L"레벨", 72}, {L"곡명 / 채보", 380}} };
        for (int i = 0; i < static_cast<int>(columns.size()); ++i) {
            LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            column.pszText = const_cast<LPWSTR>(columns[static_cast<size_t>(i)].first);
            column.cx = columns[static_cast<size_t>(i)].second; column.iSubItem = i;
            ListView_InsertColumn(gList, i, &column);
        }
        gInfo = CreateWindowW(L"STATIC", L"곡을 선택하면 상세 정보가 표시됩니다.", WS_CHILD | SS_NOPREFIX, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_INFO)), nullptr, nullptr);
        gStatus = CreateWindowW(L"STATIC", L"곡 폴더를 선택하거나 기존 곡 라이브러리를 불러오세요.", WS_CHILD, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS)), nullptr, nullptr);
        gHiddenSongs = CreateWindowW(L"BUTTON", L"숨겨진 파일 보기 (0)", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_HIDDEN_SONGS)), nullptr, nullptr);
        gResultNext = CreateWindowW(L"BUTTON", L"NEXT", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_RESULT_NEXT)), nullptr, nullptr);
        SendMessageW(gResultNext, WM_SETFONT, reinterpret_cast<WPARAM>(gResultNextFont), TRUE);
        ShowWindow(gResultNext, SW_HIDE);
        constexpr std::array<const wchar_t*, 4> pauseLabels{L"RESUME", L"RESTART", L"QUIT", L"Practice"};
        constexpr std::array<int, 4> pauseIds{ID_PAUSE_RESUME, ID_PAUSE_RESTART, ID_PAUSE_QUIT, ID_PAUSE_PRACTICE};
        for (size_t i = 0; i < gPauseButtons.size(); ++i) {
            gPauseButtons[i] = CreateWindowExW(WS_EX_TRANSPARENT, L"BUTTON", pauseLabels[i], WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0,
                window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(pauseIds[i])), nullptr, nullptr);
            SendMessageW(gPauseButtons[i], WM_SETFONT, reinterpret_cast<WPARAM>(gResultNextFont), TRUE);
            ShowWindow(gPauseButtons[i], SW_HIDE);
        }
        EnableWindow(gHiddenSongs, FALSE);
        for (HWND control : { gFolder, gInfo, gStatus, gHiddenSongs }) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(gSmallFont), TRUE);
        SendMessageW(gList, WM_SETFONT, reinterpret_cast<WPARAM>(gListFont), TRUE);
        if (gStartupSplash) ShowWindow(gList, SW_HIDE);
        updateHiddenSongsVisibility();
        layoutControls();
#ifdef BMS_ENABLE_SKIN_BINDINGS
        restoreSkinSelection();
#endif
        return 0;
    }
    case WM_SIZE:
        if(!songEntryActive()&&(gWindowWidth!=LOWORD(lParam)||gWindowHeight!=HIWORD(lParam)))gSongIntro.reset();
        hideSettingsPreviewOverlay();
        gWindowWidth = LOWORD(lParam); gWindowHeight = HIWORD(lParam); layoutControls();
        if(gRmlSettingsPageActive) syncRmlSettingsState();
        return 0;
    case WM_DPICHANGED: {
        // Windowed: take the suggested rectangle. Borderless/fullscreen re-fit the monitor in true pixels.
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        if (gScreenMode == 0 && suggested) SetWindowPos(window, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        else applyScreenMode(true);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        const UINT dpi = std::max(96u, GetDpiForWindow(window));
        limits->ptMinTrackSize.x = MulDiv(960, static_cast<int>(dpi), 96);
        limits->ptMinTrackSize.y = MulDiv(680, static_cast<int>(dpi), 96);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, RGB(218, 228, 240));
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    }
    case WM_APPLY_SETTINGS: {
        const auto results = settingsController().drain();
        for (const auto& [id, value] : settingsController().snapshot()) { (void)value; refreshSettingControls(id); }
        for (const auto& result : results) if (!result.applied) setBindingStatus(result.error);
        std::wstring status = L"설정을 저장했습니다.";
        for (const auto& result : results) if (!result.applied) status = result.error;
        syncRmlSettingsState(status);
        if(gSettingsPreviewShowing) { InvalidateRect(gSettingsPreviewOverlay,nullptr,FALSE); UpdateWindow(gSettingsPreviewOverlay); }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_LOAD_PROGRESS:
        if (gLoading && !gLoadResult.playback) {
            if (gStartupSplash) gStartupProgress = wParam == 1 ? static_cast<int>(lParam) / 4 : 25 + static_cast<int>(lParam) * 3 / 4;
            setStatus((wParam == 1 ? L"곡 폴더 복사 중... " : L"채보 분석 중... ") + std::to_wstring(lParam) + L"%");
        }
        return 0;
    case WM_LIBRARY_PROGRESS:
        if(!gPlaying&&!gShowingResult){gLibraryIndexStatus=L"라이브러리 확인 중... "+std::to_wstring(wParam)+L"%";setStatus(gLibraryIndexStatus);if(gRmlSettingsPage.window())gRmlSettingsPage.setText("library-status",settingsUtf8(gLibraryIndexStatus));}
        return 0;
    case WM_LIBRARY_ACTION: {
        if(gPlaying||gShowingResult||gLoading){setStatus(L"곡 선택 화면에서 라이브러리를 관리하세요.");return 0;}
        if(wParam==1){std::vector<std::filesystem::path> paths;if(bms::ui::chooseSongSources(gSettingsWindow?gSettingsWindow:window,gCurrentFolder.empty()?executableFolder():gCurrentFolder,paths))registerLibraryPaths(paths);}
        else if(wParam==2&&gLibrarySourceSelection<gLibrarySources.roots.size()){
            gSongIndexWorker.cancel();gLibrarySources.remove(gLibrarySourceSelection);gLibrarySourceSelection=0;gLibrarySources.save();startLibraryValidation();
        } else if(wParam==3&&gLibrarySourceSelection<gLibrarySources.roots.size())startLibraryValidation(gLibrarySources.roots[gLibrarySourceSelection]);
        else if(wParam==4){gSongPreview.clearCache();gBrowseAudioKey.clear();startLibraryValidation();}
        else if(wParam==5||wParam==6)applyLibraryView();
        syncLibrarySettings();return 0;
    }
    case WM_DROPFILES: {
        const auto drop=reinterpret_cast<HDROP>(wParam);std::vector<std::filesystem::path> paths;
        const UINT count=DragQueryFileW(drop,0xffffffff,nullptr,0);
        for(UINT i=0;i<count;++i){const UINT n=DragQueryFileW(drop,i,nullptr,0);std::wstring path(n+1,L'\0');DragQueryFileW(drop,i,path.data(),n+1);path.resize(n);paths.emplace_back(path);}
        DragFinish(drop);
        if(!gPlaying&&!gShowingResult&&!gLoading&&!gSettingsWindow)registerLibraryPaths(paths);
        else setStatus(L"곡 선택 화면으로 돌아온 뒤 폴더를 드롭하세요.");
        return 0;
    }
    case WM_TABLES_UPDATED:
        applyTableResults();
        return 0;
    case WM_LIBRARY_INDEX: {
        if(gSettingsWindow&&gSongIndexWorker.hasPending()){
            gLibraryIndexStatus=L"라이브러리 확인 완료 · 설정을 닫으면 목록에 반영됩니다.";syncLibrarySettings();return 0;
        }
        if(gPlaying||gShowingResult||gLoading||gSettingsWindow||gPracticeWindow)return 0;
        auto update=gSongIndexWorker.take();if(!update)return 0;
        if(!update->complete){gLibraryIndexStatus=update->warnings.front();setStatus(gLibraryIndexStatus);syncLibrarySettings();return 0;}
        const int oldIndex=selectedIndex();const auto selected=oldIndex>=0&&oldIndex<int(gCharts.size())?gCharts[size_t(oldIndex)].path:std::filesystem::path{};
        gSongFolders.remember(std::max(0,ListView_GetNextItem(gList,-1,LVNI_SELECTED)));
        gCurrentFolder=gLibrarySources.roots.empty()?std::filesystem::path{}:gLibrarySources.roots.front();
        gSongFolders.invalidate();gCharts=std::move(update->charts);gSongFolders.knownFolders=std::move(update->folders);
        rebuildPartialLoadSummary();rebuildChartRecordKeys();buildSongGroups();int preferred=restoreLibraryChart(selected);
        rebuildSongRows(preferred);gLibraryIndexStatus=gLibrarySources.roots.empty()?L"곡 폴더: (없음) · F2 또는 기타 설정에서 폴더를 등록하세요.":update->warnings.empty()?L"곡 라이브러리 확인 완료":update->warnings.front();setStatus(gLibraryIndexStatus);syncLibrarySettings();return 0;
    }
    case WM_PLAY_CHART_READY: acceptPlaybackChart(static_cast<std::uintptr_t>(wParam));return 0;
    case WM_AUTO_JUDGE_SUGGEST: {
        const int current = gTimingOffsets.judgeMs, suggested = gAutoJudgeSuggestion;
        const std::wstring text = L"이번 플레이의 FAST/SLOW 경향을 분석했습니다.\n\n판정 타이밍 보정을 " + std::to_wstring(current) +
            L" ms에서 " + std::to_wstring(suggested) + L" ms로 바꾸면 편향이 줄어듭니다.\n적용할까요? (노트 표시 위치는 바뀌지 않습니다)";
        if (MessageBoxW(gWindow, text.c_str(), L"자동 판정 타이밍 보정", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            gTimingOffsets.judgeMs = bms::clampOffsetMs(suggested, bms::TimingOffsets::judgeLimitMs);
            saveDisplaySettings();
        }
        gAutoJudge.reset();
        return 0;
    }
    case WM_LOAD_FINISHED:
        if(wParam){completePlaybackWorker(static_cast<std::uintptr_t>(wParam));return 0;}
        if (gLoadWorker.joinable()) gLoadWorker.join();
        if(gLoadResult.playback){if(gLoadCancelled)gPlaybackLoad.cancel();else if(!gLoadResult.error.empty())gPlaybackLoad.fail();else gPlaybackLoad.resourcesLoaded();}
        if (gLoadResult.playback && !gLoadCancelled && gLoadResult.error.empty()) {
            const bool hasTitle = gLoadResult.index >= 0 && gLoadResult.index < static_cast<int>(gCharts.size()) &&
                !gCharts[static_cast<size_t>(gLoadResult.index)].titleImage.empty();
            if (!hasTitle) {
                finishSelectedChart();
                return 0;
            }
            gTitleFadeOutStarted = std::max(std::chrono::steady_clock::now(), gPlaybackLoadStarted + titleFadeDuration);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        gLoading = false;
        EnableWindow(gHiddenSongs, gShowHiddenCharts || std::any_of(gCharts.begin(), gCharts.end(), [](const auto& chart) { return !chart.playable; }));
        if (gLoadCancelled) {
            if (!gLoadResult.playback) ListView_DeleteItem(gList, 0);
            else {
                gAudio.releaseSong(); gLoadResult.chart = {};
                gVideoBga.reset();
    gBgaVideos.reset();gBgaSwitch.reset();
                startUiEffects();
                for (HWND control : {gList, gHiddenSongs})
                    if (control) ShowWindow(control, SW_SHOW);
            }
            setStatus(L"불러오기를 취소했습니다."); updateSelection(); finishStartupSplash(); return 0;
        }
        if (!gLoadResult.error.empty()) {
            if (!gLoadResult.playback) ListView_DeleteItem(gList, 0);
            else {
                gAudio.releaseSong(); gLoadResult.chart = {};
                gVideoBga.reset();
    gBgaVideos.reset();gBgaSwitch.reset();
                startUiEffects();
                for (HWND control : {gList, gHiddenSongs})
                    if (control) ShowWindow(control, SW_SHOW);
            }
            setStatus(gLoadResult.error); updateSelection(); finishStartupSplash(); return 0;
        }
        if (gLoadResult.playback) finishSelectedChart();
        else {
            gCurrentFolder = gLoadResult.folder;
            setText(gFolder, L"곡 폴더: " + gCurrentFolder.wstring());
            if (!gSettingsFile.empty()) WritePrivateProfileStringW(L"Library", L"LastFolder", gCurrentFolder.c_str(), gSettingsFile.c_str());
            gSongFolders.invalidate();gCharts = std::move(gLoadResult.charts);
            rebuildPartialLoadSummary();
            rebuildChartRecordKeys();
            buildSongGroups();
            std::vector<bms::Chart> previews;
            for (const auto& group : gSongSelection.groups()) {
                const auto& chart = gCharts[group.previewChartIndex];
                if (!chart.playable && chart.previewAudio.empty()) continue;
                previews.push_back(chart);
                previews.back().bitmaps.clear(); previews.back().bga.clear(); previews.back().bgaClips.clear(); previews.back().tempos.clear();
            }
            gSongPreview.prewarm(std::move(previews));
            gSongFolders.configure(gLibrarySources.navigationRoots(),gLibrarySources.originalFolders);const auto restored=gSongFolders.restoreSaved(gCharts);
            rebuildSongRows(restoreLibraryChart(restored));
            startLibraryValidation();
            std::wstring status = std::to_wstring(ListView_GetItemCount(gList)) + L"곡 · " + std::to_wstring(gCharts.size()) + L"개 채보를 불러왔습니다. 분석 " + number(gLoadResult.scanSeconds, 2) + L"초";
            if (gLoadResult.copiedFiles) status += L" · " + std::to_wstring(gLoadResult.copiedFiles) + L"개 파일을 Songs에 복사했습니다.";
            if (gLoadResult.unchangedFiles) status += L" · 변경 없는 " + std::to_wstring(gLoadResult.unchangedFiles) + L"개 파일은 유지했습니다.";
            if(!gLoadResult.importWarnings.empty())status+=L" · "+std::to_wstring(gLoadResult.importWarnings.size())+L"개 항목 추가 실패: "+gLoadResult.importWarnings.front();
            if(gLoadResult.cachedIndex)status+=L" · 캐시 확인 중";
            setStatus(status);
            startUiEffects();
        }
        finishStartupSplash();
        return 0;
    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (item && item->CtlID >= ID_PAUSE_RESUME && item->CtlID <= ID_PAUSE_PRACTICE) {
            // The parent pause overlay draws both borders and labels on the
            // same surface as the dimmed game, including in GPU mode.
            return TRUE;
        }
        if (item && item->CtlID == ID_RESULT_NEXT) {
            fillRect(item->hDC, item->rcItem, (item->itemState & ODS_SELECTED) ? RGB(32, 75, 102) : RGB(21, 54, 77));
            HBRUSH border = CreateSolidBrush(RGB(90, 205, 245));
            if (border) { FrameRect(item->hDC, &item->rcItem, border); DeleteObject(border); }
            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, RGB(245, 250, 255));
            HGDIOBJ oldFont = SelectObject(item->hDC, gResultNextFont ? gResultNextFont : gFont);
            RECT text = item->rcItem;
            DrawTextW(item->hDC, L"NEXT", -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (oldFont) SelectObject(item->hDC, oldFont);
            if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &item->rcItem);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_OPEN: { gAudio.triggerEffect(L"click"); PostMessageW(window,WM_LIBRARY_ACTION,1,0); return 0; }
        case ID_RESCAN: gAudio.triggerEffect(L"click"); if(!gLoading&&!gPlaying){gSongPreview.clearCache();gBrowseAudioKey.clear();startLibraryValidation();} return 0;
        case ID_PLAY: gAudio.triggerEffect(L"click"); beginSelectedChart(); return 0;
        case ID_PRACTICE_PLAY: gAudio.triggerEffect(L"click"); openPracticeSetup(); return 0;
        case ID_RESULT_NEXT: requestResultExit(true); return 0;
        case ID_PAUSE_RESUME: resumePlayback(); return 0;
        case ID_PAUSE_RESTART: restartPlayback(); return 0;
        case ID_PAUSE_QUIT: quitPausedPlayback(); return 0;
        case ID_PAUSE_PRACTICE:
            if (gPlaying && gPaused) { showBrowseView(); openPracticeSetup(); }
            return 0;
        case ID_SETTINGS: gAudio.triggerEffect(L"click"); openSettings(); return 0;
        case ID_HIDDEN_SONGS:
            gAudio.triggerEffect(L"click");
            if (gLoading || gPlaying || gShowingResult) return 0;
            gBrowseSelections[gShowHiddenCharts ? 1 : 0] = selectedGroupIndex();
            gShowHiddenCharts = !gShowHiddenCharts;
            rebuildSongRows(gBrowseSelections[gShowHiddenCharts ? 1 : 0]);
            return 0;
        case ID_HELP:
            MessageBoxW(gSettingsWindow ? gSettingsWindow : window, (L"현재 키 배치: " + keySummary() + L"\r\n설정창에서 키 배치, 판정선 위치, 기본 배속, 레인 투명도, 레인 커버, 레인 구분선, BGA 밝기, 기어 위치, DirectComposition, 레인별 노트 색상, 60 / 144 / 240 / 400 FPS 또는 무제한을 변경합니다. 판정선 위치는 미리보기를 보며 조절할 수 있습니다.\r\n\r\n곡 목록의 Space: 설정창\r\nEnter: 선택 곡 시작\r\n재생 중 F5: 즉시 재시작 / Esc: 일시정지 창\r\n일시정지 중 방향키: 메뉴 선택, Enter: 선택, Esc: 재개, R: 재시작, Q: 곡 선택으로\r\n일시정지 중 F1/F2: 노트 배속 낮추기/올리기 (0.50x~10.00x)\r\n일시정지 중 PageDown/PageUp: 레인 가드 낮추기/올리기 (5% 단위)\r\n재생 중 PageDown/PageUp: BGA 밝기 낮추기/올리기 (10단위)\r\n\r\n노트 입력은 ±150ms에서 PERFECT / GREAT / GOOD로 표시하고, 놓친 노트는 MISS로 표시합니다. 실제 화면의 표시 빈도는 Windows 디스플레이 설정의 모니터 주사율에도 영향을 받습니다.").c_str(), L"사용법", MB_ICONINFORMATION | MB_OK);
            return 0;
        }
        break;
    case WM_NOTIFY: {
        auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header->hwndFrom == gList && header->code == LVN_ITEMCHANGED) {
            const auto* change = reinterpret_cast<const NMLISTVIEW*>(lParam);
            if (change->iItem >= 0 && (change->uNewState & LVIS_SELECTED))
                selectSongGroupFromListRow(change->iItem);
            updateSelection();
            InvalidateRect(gList, nullptr, FALSE);
            return 0;
        }
        if (header->hwndFrom == gList && header->code == NM_CLICK) { gAudio.triggerEffect(L"click"); return 0; }
        if (header->hwndFrom == gList && header->code == NM_DBLCLK) { beginSelectedChart(); return 0; }
        if (header->hwndFrom == gList && header->code == NM_RDBLCLK) {
            const int row = reinterpret_cast<NMLISTVIEW*>(lParam)->iItem;
            if (!gLoading && !gPlaying && !gShowingResult && row >= 0 && row < ListView_GetItemCount(gList)) {
                ListView_SetItemState(gList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_SetItemState(gList, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                gAudio.triggerEffect(L"click");
                openPracticeSetup();
            }
            return 0;
        }
        if (header->hwndFrom == gList && header->code == LVN_KEYDOWN) {
            const auto key = reinterpret_cast<NMLVKEYDOWN*>(lParam)->wVKey;
            if((key==VK_BACK||key==VK_ESCAPE)&&navigateSongFolder(true))return 0;
            if (key == VK_UP || key == VK_DOWN) gAudio.triggerEffect(L"songlistscroll");
            if (key == VK_RETURN) { gAudio.triggerEffect(L"click"); startSelectedChartFromEnter(); return 0; }
            if (key == VK_LEFT && cycleSelectedVariant(-1)) return 0;
            if (key == VK_RIGHT && cycleSelectedVariant(1)) return 0;
        }
        if (header->hwndFrom == gList && header->code == NM_CUSTOMDRAW) return handleCustomDraw(lParam);
        break;
    }
    case WM_KEYDOWN:
        if(gSongInfoVisible){if(wParam==VK_ESCAPE){gSongInfoVisible=false;ShowWindow(gList,SW_SHOWNA);InvalidateRect(window,nullptr,FALSE);}return 0;}
        if((wParam==VK_BACK||wParam==VK_ESCAPE)&&navigateSongFolder(true))return 0;
    if (handleBrowseShortcut(message, wParam, lParam)) return 0;
        if (gLoading) {
#ifdef BMS_ENABLE_SKIN_BINDINGS
            if (lr2PlayInputLocked()) return 0;
#endif
            if (wParam == VK_ESCAPE) { gLoadCancelled = true; setStatus(L"불러오기를 취소하고 있습니다..."); }
            else if (wParam == VK_RETURN && (lParam & (1LL << 30)) == 0 && skipSongEntry()) InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (gCapturingBindings) {
            if (wParam == VK_ESCAPE) { cancelKeyBindingCapture(); return 0; }
            if (gCaptureAwaitRelease || (lParam & (1LL << 30)) != 0) return 0;
            acceptBindingKey(normalizedKey(wParam, lParam));
            return 0;
        }
        if (gPlaying) {
            if(wParam==VK_F5&&!gSettingsWindow){if((lParam&(1LL<<30))==0)restartPlayback();return 0;}
            if (gPaused) { handlePausedKey(wParam, (lParam & (1LL << 30)) != 0); return 0; }
            if (wParam == VK_ESCAPE) { if ((lParam & (1LL << 30)) == 0) pausePlayback(); return 0; }
            if((GetKeyState(VK_CONTROL)&0x8000)&&(wParam==VK_PRIOR||wParam==VK_NEXT||wParam==VK_UP||wParam==VK_DOWN)){
                if(wParam==VK_PRIOR||wParam==VK_NEXT)gLaneCoverPercent=std::clamp(gLaneCoverPercent+(wParam==VK_PRIOR?1:-1),0,100);
                else gJudgeLineOffset=std::clamp(gJudgeLineOffset+(wParam==VK_UP?-1:1),-4096,4096);
                gBgaBackplateDirty=true;saveDisplaySettings();InvalidateRect(window,nullptr,FALSE);return 0;
            }
            if (wParam == VK_RETURN) return 0;
            if (wParam == VK_F1 || wParam == VK_F2) {
                const double delta = (wParam == VK_F2) ? 0.25 : -0.25;
                gNoteSpeed = std::clamp(gNoteSpeed + delta, 0.5, 10.0);
                saveDisplaySettings();
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_PRIOR || wParam == VK_NEXT) {
                gBgaBrightness = std::clamp(gBgaBrightness + (wParam == VK_PRIOR ? 10 : -10), 0, 100);
                gBgaDarkness = 100 - gBgaBrightness;
                gBgaBackplateDirty = true;
                saveDisplaySettings();
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            int lane = keyLane(wParam, lParam);
            // Bit 30 is the state of the side-less VK_SHIFT, so pressing Right Shift while Left Shift is
            // held looks like an auto-repeat. Only for Shift, ignore it when this lane is already down; any other
            // repeat (e.g. a key held across a restart, which clears gKeyboardPressed) is always ignored.
            if (lane >= 0) { if((lParam&(1LL<<30))!=0&&(wParam!=VK_SHIFT||gKeyboardPressed[lane]))return 0;gKeyboardPressed[lane]=true;pressLane(lane); return 0; }
        } else if (gShowingResult) {
            if (gResultFade.active()) return 0;
            // A held key's repeats are ignored: on an LR2 result the first press only shows the score update.
            if ((wParam == VK_RETURN || wParam == VK_ESCAPE) && (lParam & (1LL << 30)) == 0) { requestResultExit(); return 0; }
            return 0;
        } else {
            if (wParam == VK_TAB) {
                if ((lParam & (1LL << 30)) == 0) cycleKeyModeFilter();
                return 0;
            }
            if (wParam == 'S') {
                if ((lParam & (1LL << 30)) == 0) toggleSongSort();
                return 0;
            }
            if (wParam == VK_LEFT && cycleSelectedVariant(-1)) return 0;
            if (wParam == VK_RIGHT && cycleSelectedVariant(1)) return 0;
            if (wParam == VK_RETURN) { gAudio.triggerEffect(L"click"); startSelectedChartFromEnter(); return 0; }
        }
        break;
    case WM_SYSKEYDOWN:
        if (gCapturingBindings) {
            setStatus(L"Alt 키와 Alt 조합은 Windows 메뉴 키이므로 레인에 지정할 수 없습니다.");
            return 0;
        }
        break;
    case WM_KEYUP:
    if (handleBrowseShortcut(message, wParam, lParam)) return 0;
        if (gCapturingBindings) {
            gCaptureAwaitRelease = false;
            return 0;
        }
        if (gPlaying) {
            int lane = keyLane(wParam, lParam);
            if (lane >= 0) {
                gKeyboardPressed[lane]=false;
                if(!gControllerPressed[lane])releaseLaneInput(lane);
                return 0;
            }
        }
        break;
    case WM_SETFOCUS:
        gPauseWhenPlayStarts = false; // back before the song started: start it normally
        if (gPaused && gPauseSelection < gPauseButtons.size() && gPauseButtons[gPauseSelection])
            SetFocus(gPauseButtons[gPauseSelection]);
        else if (!gPlaying && !gShowingResult && (!gLoading || !gLoadResult.playback) && !gSettingsWindow)
            focusBrowseInput();
        break;
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE && !gPlaying && !gShowingResult &&
            (!gLoading || !gLoadResult.playback) && !gSettingsWindow)
            focusBrowseInput();
        break;
    case WM_KILLFOCUS:
        if (gCapturingBindings && !gSettingsWindow) cancelKeyBindingCapture();
        // A no-input benchmark keeps running in the background (nothing to pause for).
        if (gPlaying && !gPaused && !gNoInputBenchmark) pausePlayback(false);
        else if (!gPlaying && gPlaybackLoad.preparing() && !gNoInputBenchmark) gPauseWhenPlayStarts = true;
        gKeyboardPressed.fill(false);gControllerPressed.fill(false);gController.mapper.reset();
        for(int lane=0;lane<16;++lane)releaseLaneInput(lane);
        if (gShowingResult && GetFocus() != gResultNext) {
#ifdef BMS_ENABLE_SKIN_BINDINGS
            if(!gLr2ResultScene)
#endif
                showBrowseView();
        }
        break;
    case WM_ACCURACY_RESTART:
        if(gAccuracyRestartPending&&wParam==static_cast<WPARAM>(gPlaybackGeneration)){
            gAccuracyRestartPending=false;
            if(gPlaying&&!gPaused&&!gLoading&&gAccuracyLimit!=AccuracyLimit::None)restartPlayback();
        }
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_TIMER:
        if(wParam==ID_CONTROLLER_POLL){pollControllerInput();return 0;}
        if(wParam==ID_RESULT_SOUND) {
            KillTimer(window,ID_RESULT_SOUND);
            if(gShowingResult&&gPendingResultSound!=bms::SystemSound::Count)triggerSystemSound(gPendingResultSound);
            gPendingResultSound=bms::SystemSound::Count;
            return 0;
        }
#ifdef BMS_ENABLE_SKIN_BINDINGS
        if(wParam==ID_SKIN_RETRY){retrySkinSelection();return 0;}
        if(wParam==ID_SKIN_ANIMATION){if(!gPlaying)InvalidateRect(window,nullptr,FALSE);return 0;}
        if(wParam==ID_LR2_SELECT_FADE) {
            KillTimer(window,ID_LR2_SELECT_FADE);
            // The song starts only if it is still the one chosen; otherwise the select screen comes back.
            if(gLr2SelectFadeAt!=std::chrono::steady_clock::time_point{}&&selectedIndex()==gLr2SelectFadeIndex&&
               !gPlaying&&!gLoading&&!gShowingResult&&!gSettingsWindow) {
                gLr2SelectFadeFinishing=true;
                beginSelectedChart(gLr2SelectFadePractice,gLr2SelectFadeAutoplay);
                gLr2SelectFadeFinishing=false;
            }
            gLr2SelectFadeAt={};
            gLr2SelectFadeIndex=-1;
            InvalidateRect(window,nullptr,FALSE);
            return 0;
        }
        if(wParam==ID_LR2_RESULT_ANIMATION) {
            if(gShowingResult&&gLr2ResultScene) {
                using namespace std::chrono;
                const auto now=steady_clock::now();
                if(gLr2ResultExitStart!=steady_clock::time_point{}&&
                   now-gLr2ResultExitStart>=milliseconds(gLr2ResultScene->fadeoutMilliseconds())) {
                    showBrowseView();
                } else if(gLr2ResultStart!=steady_clock::time_point{}&&gLr2ResultScene->sceneMilliseconds()>0&&
                          now-gLr2ResultStart>=milliseconds(gLr2ResultScene->sceneMilliseconds())) {
                    beginLr2ResultExit(now);
                }
                if(gShowingResult)InvalidateRect(window,nullptr,FALSE);
            }
            return 0;
        }
#endif
        if (wParam == ID_RESULT_FADE) {
            if (gResultFade.finished(ResultFade::Clock::now())) {
                resetResultFade();
                syncNativeResultNext();
            }
            InvalidateRect(window, nullptr, FALSE);
            UpdateWindow(window);
            return 0;
        }
        if (wParam == ID_SONG_LIST_ANIMATION) { tickSongListAnimations(); return 0; }
        if (wParam == ID_BENCHMARK_NEXT) { KillTimer(window, ID_BENCHMARK_NEXT); continueBenchmarkSeries(); return 0; }
        if (wParam == ID_PAUSE_PRESENT_RETRY) {
            KillTimer(window, ID_PAUSE_PRESENT_RETRY);
            if (gPlaying && gPaused && gCompositionEnabled && !gGpuUnavailable) {
                InvalidateRect(window, nullptr, FALSE);
                UpdateWindow(window);
            }
            return 0;
        }
        break;
    case WM_PAINT: {
        HUD_SCOPE(WindowPaint);
        notePaintEntry();
#ifdef BMS_HUD_FRAME_PROBE
        if(gLatencyInputPump)HUD_COUNT(InputPaint);if(gPaused)HUD_COUNT(PausedPaint);if(GetForegroundWindow()!=gWindow)HUD_COUNT(UnfocusedPaint);
#endif
        if (gPlaying && !gPaused && gLatencyInputPump) {
            HUD_COUNT(DeferredInputPaint);
            PAINTSTRUCT deferred{}; BeginPaint(window, &deferred); EndPaint(window, &deferred);
            return 0; // The simulation update below re-invalidates after input sampling.
        }
        if (!gPlaying && !gShowingResult && gRmlSongList.enabled() && gList)
            InvalidateRect(gList, nullptr, FALSE);
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(window, &ps);
        RECT client{};
        GetClientRect(window, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        ensureBackBuffer(dc, width, height);
#ifdef BMS_ENABLE_SKIN_BINDINGS
        if(presentLr2Select(width,height)) {
            EndPaint(window,&ps);
            return 0;
        }
        if(gLr2DecideScene) {
            if(gLoading&&!gLoadCancelled&&songEntryActive()) {
                if(width>0&&height>0&&gBackBufferDc&&gBackBufferPixels) {
                    if(presentLr2Decide(width,height)) {
                        EndPaint(window,&ps);
                        return 0;
                    }
                    reportLr2Failure(sevenmania::skin::lr2::ScreenType::Decide,gLr2DecideFile,L"GPU 출력 실패");
                    gLr2DecideScene.reset();
                    gCompositionPresenter.reset();
                } else {
                    fillRect(dc,client,RGB(0,0,0));
                    EndPaint(window,&ps);
                    return 0;
                }
            } else {
                gLr2DecideScene.reset();
                gCompositionPresenter.reset();
            }
        }
        // The LR2 play screen is on while the chart loads; its gear slides in on timers 0 and 40.
        if(gLoading&&!gLoadCancelled&&gLoadResult.playback&&width>0&&height>0&&gBackBufferPixels&&
           presentLr2PlayLeadIn(width,height)) {
            EndPaint(window,&ps);
            return 0;
        }
        if(gShowingResult&&gLr2ResultScene&&
           (gResultFade.active()||width<=0||height<=0||!gBackBufferPixels)) {
            // Presenter teardown can synchronously repaint before the LR2 result is ready.
            fillRect(dc,client,RGB(0,0,0));
            EndPaint(window,&ps);
            return 0;
        }
        if(gShowingResult&&gLr2ResultScene&&width>0&&height>0&&gBackBufferPixels) {
            const auto now=std::chrono::steady_clock::now();
            if(gLr2ResultStart==std::chrono::steady_clock::time_point{})gLr2ResultStart=now;
            const double exitAge=gLr2ResultExitStart==std::chrono::steady_clock::time_point{}?-1.:
                std::chrono::duration<double,std::milli>(now-gLr2ResultExitStart).count();
            const double resultElapsed=std::chrono::duration<double,std::milli>(now-gLr2ResultStart).count();
            sevenmania::skin::lr2::ScreenTimers resultTimers;
            resultTimers.fadeout=exitAge;
            resultTimers.updateScore=sevenmania::skin::lr2::resultUpdateAge(gLr2ResultScene->resultInput(),resultElapsed,gLr2ResultKeyAt);
            if(presentLr2Screen(*gLr2ResultScene,resultElapsed,width,height,nullptr,resultTimers)) {
                EndPaint(window,&ps);
                return 0;
            }
            reportLr2Failure(sevenmania::skin::lr2::ScreenType::Result,gLr2ResultFile,L"GPU 출력 실패");
            gLr2ResultScene.reset();
            KillTimer(window,ID_LR2_RESULT_ANIMATION);
            gCompositionPresenter.reset();
            if(exitAge>=0) {
                EndPaint(window,&ps);
                showBrowseView();
                return 0;
            }
            syncNativeResultNext();
        }
        const bool lr2PlayPending=gPlaying&&gLr2PlayScene&&!gLr2GpuFailed;
        if(lr2PlayPending&&(!gBackBufferDc||!gBackBufferBitmap)) {
            fillRect(dc,client,RGB(0,0,0));
            EndPaint(window,&ps);
            return 0;
        }
        if((gSkinRetryPending||!gSkinApplication.active())&&!lr2PlayPending&&
           !(gLoading&&gLoadResult.playback&&waitingForLr2Play())) {
            gCompositionPresenter.reset();fillRect(dc,client,RGB(0,0,0));skinVisibility(false);
            EndPaint(window,&ps);
            return 0;
        }
#endif
        if (gBackBufferDc && gBackBufferBitmap) {
            gGpuFrame = gPlaying && gCompositionEnabled && !gGpuUnavailable;
            gCompositionPresenter.setDirectComposition(gDirectCompositionEnabled);
            gCompositionPresenter.setVsync(gTargetFps == vsyncFps);
            gCompositionPresenter.setAntialias(gNoteAntialias);
            if(gGpuFrame) {
#ifdef BMS_ENABLE_SKIN_BINDINGS
                const bool lr2=gLr2PlayScene&&!gLr2GpuFailed;
                gCompositionPresenter.setModernDrawing(lr2);
                bool prepared=gCompositionPresenter.prepare(window,width,height);
                if(lr2&&width>0&&height>0&&(!prepared||!gLr2PlayScene->prepare(
                    static_cast<ID2D1DeviceContext*>(gCompositionPresenter.renderTarget())))) {
                    gLr2PlayScene->releaseDevice();
                    gLr2GpuFailed=true;
                    reportLr2PlayGpuFailure(L"GPU 준비 실패");
                    gCompositionPresenter.setModernDrawing(false);
                    prepared=gCompositionPresenter.prepare(window,width,height);
                } else if(lr2&&prepared) {
                    gLr2RuntimeErrors[static_cast<std::size_t>(currentLr2PlayScreen())].clear();
                }
#else
                const bool prepared=gCompositionPresenter.prepare(window,width,height);
#endif
                if(!prepared) { gGpuFrame=false; gGpuUnavailable=true; notePresentFallback(); }
            }
            if (gGpuFrame) {
                const int limit = gTargetFps > 0 ? gTargetFps : unlimitedFpsLimit;
                gCompositionPresenter.lowLatency().request(gReflexRequested && !gPaused ? latency::Mode::Reflex : latency::Mode::Off,
                    gTargetFps == uncappedFps ? 0u : static_cast<uint32_t>((1'000'000 + limit - 1) / limit));
            }
            if (gGpuFrame && !shouldRenderGpuFrame()) {
                gGpuFrame = false;
                EndPaint(window, &ps);
                return 0;
            }
            if (hybridRendererEnabled && gGpuFrame && gTargetFps == vsyncFps && !gCompositionPresenter.canBeginHybridFrame()) {
                // Aligned VSync found the queue still busy: paint on the ready signal (the
                // pre-alignment behaviour) for ~0.25 s rather than waiting for the next slot.
                if (gVsyncReadyWake && gFramePacer.vblankAligned()) gFramePacer.suspendVblank(60);
                // Keep audio/input/judgement updates running, but avoid painting
                // CPU gear/HUD frames the compositor cannot accept yet.
                BMS_PERF_SET(presentDeferred, true);
                if (gPaused) SetTimer(window, ID_PAUSE_PRESENT_RETRY, 24, nullptr);
                gGpuFrame = false;
                EndPaint(window, &ps);
                return 0;
            }
            {HUD_SCOPE(ResourceStats);recordResourceFrame(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());}
            if (!HUD_LEGACY_REFLEX()&&gPlaying && !gPaused && gGpuFrame) gCompositionPresenter.lowLatency().renderStart();
#ifdef BMS_ENABLE_SKIN_BINDINGS
            prepareAppliedSkinGpu();
#endif
            if (HUD_LEGACY_REFLEX()&&gPlaying&&!gPaused&&gGpuFrame) gCompositionPresenter.lowLatency().renderStart();
            if (gPlaying && gPrewarmTextPending) prewarmPlayText();
            std::optional<sevenmania::skin::lr2::FrameScale> upscale;
#ifdef BMS_ENABLE_SKIN_BINDINGS
            if(gGpuFrame&&gLr2PlayScene&&!gLr2GpuFailed) {
                upscale=gLr2PlayScene->upscale(gLr2UpscaleFilter,width,height);
                if(upscale&&(!gLr2PlayScene->playLanes(upscale->width,upscale->height)||
                             !gCompositionPresenter.setFrameUpscale(*upscale,true)))upscale.reset();
            }
#endif
            if (gPlaying) drawGameScreen(gBackBufferDc,upscale?&*upscale:nullptr);
            else if (gShowingResult) drawResultScreen(gBackBufferDc);
            else if (gLoading && gLoadResult.playback) drawPreparingPlayback(gBackBufferDc);
            else if (gStartupSplash) drawStartupSplash(gBackBufferDc);
            else drawBrowseScreen(gBackBufferDc);
            if (!gStartupSplash&&!songEntryActive()&&!(gLoading&&gLoadResult.playback&&waitingForLr2Play()))
                drawResourceUsageOverlay(gBackBufferDc);
            if (gShowingResult) drawResultFade(gBackBufferDc);
            {HUD_SCOPE(FinalGdiFlush);GdiFlush();} // Complete GDI writes before the D3D texture upload reads the DIB.
            bool pauseFrameDeferred = false;
            { BMS_PERF_SCOPE(presentMs);
                const uint64_t submittedBefore = gCompositionPresenter.submittedFrames();
                gDiagPresentStart = std::chrono::steady_clock::now();
                bool presented = gGpuFrame && gCompositionPresenter.present(window, gBackBufferPixels, width, height);
#ifdef BMS_ENABLE_SKIN_BINDINGS
                if(!presented&&upscale&&std::string_view(gCompositionPresenter.lastFailureStage())=="lr2-upscale-draw") {
                    notePresentFallback();
                    if(gCompositionPresenter.prepare(window,width,height)&&gLr2PlayScene->prepare(
                        static_cast<ID2D1DeviceContext*>(gCompositionPresenter.renderTarget()))) {
                        prepareAppliedSkinGpu();
                        drawGameScreen(gBackBufferDc);
                        drawResourceUsageOverlay(gBackBufferDc);
                        GdiFlush();
                        presented=gCompositionPresenter.present(window,gBackBufferPixels,width,height);
                    }
                }
#endif
                if (presented) notePresentWork();
                if (presented && gCompositionPresenter.submittedFrames() != submittedBefore) notePresentSubmitted(true);
                BMS_PERF_SET(presentDeferred, presented && gCompositionPresenter.presentationDeferred());
                pauseFrameDeferred = presented && gPaused && gCompositionPresenter.presentationDeferred();
                if (!presented) {
                    if (gGpuFrame) {
#ifdef BMS_ENABLE_SKIN_BINDINGS
                        if(gLr2PlayScene&&!gLr2GpuFailed) {
                            gLr2PlayScene->releaseDevice();
                            gLr2GpuFailed=true;
                            reportLr2PlayGpuFailure(L"GPU 출력 실패");
                        }
#endif
                        gGpuFrame = false; gGpuUnavailable = true; notePresentFallback();
                        drawGameScreen(gBackBufferDc); GdiFlush();
                    }
                    gCompositionPresenter.reset();
                    BitBlt(dc, 0, 0, width, height, gBackBufferDc, 0, 0, SRCCOPY);
                    notePresentSubmitted(false);
                    if (gPlaying && !gPaused && gTargetFps == vsyncFps) {
                        GdiFlush();
                        DwmFlush();
                    }
                }
            }
            if (pauseFrameDeferred) SetTimer(window, ID_PAUSE_PRESENT_RETRY, 24, nullptr);
            else KillTimer(window, ID_PAUSE_PRESENT_RETRY);
            gGpuFrame = false;
        } else {
            if (gPlaying) drawGameScreen(dc);
            else if (gShowingResult) { drawResultScreen(dc); drawResultFade(dc); }
            else if (gLoading && gLoadResult.playback) drawPreparingPlayback(dc);
            else if (gStartupSplash) drawStartupSplash(dc);
            else drawBrowseScreen(dc);
        }
        EndPaint(window, &ps); return 0;
    }
    case WM_CLOSE:
        if (!gPlaying && MessageBoxW(window, L"SevenMania를 종료하시겠습니까?", L"종료 확인", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES) return 0;
        if (gLoading) gLoadCancelled = true;
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        saveLibraryNavigation();gSongIndexWorker.cancel();gTableRefresh.stop();
        KillTimer(window,ID_CONTROLLER_POLL);gController.reconnect(nullptr);
#ifdef BMS_ENABLE_SKIN_BINDINGS
        unloadSkinSelection();
#endif
        settingsController().clearPending();
        gRmlSettingsPage.close();
        gRmlSettingsHost.close();
        gRmlSongList.reset();
        gStartupSplashImage.reset();
        BufferedPaintUnInit();
        resetResultFade();
        gLoadCancelled = true;gPlaybackLoad.cancel();
        gPauseButtons.fill(nullptr);
        if (gLoadWorker.joinable()) gLoadWorker.join();
        if(gPlaybackJob.active){samplePlaybackLoadMemory();reportPlaybackLoad("shutdown");gPlaybackJob.releaseChart();gPlaybackJob.active=false;}
#ifdef BMS_ENABLE_SKIN_BINDINGS
        gLr2PlayScene.reset();
        gLr2DecideScene.reset();
        gLr2ResultScene.reset();
        gLr2SelectScene.reset();
        KillTimer(window,ID_LR2_RESULT_ANIMATION);
#endif
        gCompositionPresenter.reset();
        gAudio.stop();
        gSongPreview.shutdown();
        gSelectionAudio.shutdown();
        gBrowseBackdrop.reset();
        gVideoBga.reset();
    gBgaVideos.reset();gBgaSwitch.reset();
        restoreFullscreenDisplay();
        gTextRaster.clear(); gHudTextRaster.clear(); gHudMaskRaster.clear();
        gResourceTextRaster.clear();
        if (gFont) DeleteObject(gFont);
        if (gSmallFont) DeleteObject(gSmallFont);
        if (gPracticeHudFont) DeleteObject(gPracticeHudFont);
        if (gListFont) DeleteObject(gListFont);
        if (gListSelectedFont) DeleteObject(gListSelectedFont);
        if (gListLevelFont) DeleteObject(gListLevelFont);
        if (gListSelectedLevelFont) DeleteObject(gListSelectedLevelFont);
        if (gBrowseTitleFont) DeleteObject(gBrowseTitleFont);
        if (gBrowseSongTitleFont) DeleteObject(gBrowseSongTitleFont);
        if (gBrowseSongInfoFont) DeleteObject(gBrowseSongInfoFont);
        if (gBrowseLevelFont) DeleteObject(gBrowseLevelFont);
        if (gBrowseRankFont) DeleteObject(gBrowseRankFont);
        if (gBrowseMetadataFont) DeleteObject(gBrowseMetadataFont);
        if (gSongRowImages) ImageList_Destroy(gSongRowImages);
        if (gHudBoldFont) DeleteObject(gHudBoldFont);
        if (gGaugeModeFont) DeleteObject(gGaugeModeFont);
        if (gComboFont) DeleteObject(gComboFont);
        if (gJudgementFont) DeleteObject(gJudgementFont);
        gEmbeddedHudFont.reset();
        if (gResultRankFont) DeleteObject(gResultRankFont);
        if (gResultScoreFont) DeleteObject(gResultScoreFont);
        if (gResultTitleFont) DeleteObject(gResultTitleFont);
        if (gResultDetailsFont) DeleteObject(gResultDetailsFont);
        if (gResultNextFont) DeleteObject(gResultNextFont);
        if (gPlayFont) DeleteObject(gPlayFont);
        if (gPlaySmallFont) DeleteObject(gPlaySmallFont);
        releaseSettingsPreviewResources();
        if (gControlBrush) DeleteObject(gControlBrush);
        if (gBackBufferDc && gBackBufferOriginalBitmap) SelectObject(gBackBufferDc, gBackBufferOriginalBitmap);
        if (gBackBufferBitmap) DeleteObject(gBackBufferBitmap);
        if (gBackBufferDc) DeleteDC(gBackBufferDc);
        if (gResultBackdropDc && gResultBackdropOriginalBitmap) SelectObject(gResultBackdropDc, gResultBackdropOriginalBitmap);
        if (gResultBackdropBitmap) DeleteObject(gResultBackdropBitmap);
        if (gResultBackdropDc) DeleteDC(gResultBackdropDc);
        if (gBgaBackplateDc && gBgaBackplateOriginalBitmap) SelectObject(gBgaBackplateDc, gBgaBackplateOriginalBitmap);
        if (gBgaBackplateBitmap) DeleteObject(gBgaBackplateBitmap);
        if (gBgaBackplateDc) DeleteDC(gBgaBackplateDc);
        for (const auto& [color, brush] : gBrushCache) { (void)color; if (brush) DeleteObject(brush); }
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
