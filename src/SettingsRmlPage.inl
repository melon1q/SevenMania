std::string settingsUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(length, '\0'); WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), length, nullptr, nullptr);
    return out;
}
void syncLibrarySettings() {
    if(!gRmlSettingsPage.window())return;
    std::vector<std::pair<std::string,std::string>> options;
    for(size_t i=0;i<gLibrarySources.roots.size();++i){std::error_code ec;const auto& root=gLibrarySources.roots[i];
        options.emplace_back(std::to_string(i),settingsUtf8(root.filename().wstring()+L" · "+root.wstring()+(std::filesystem::is_directory(root,ec)?L"":L" [연결 끊김]")));}
    if(options.empty())options.emplace_back("none","(없음)");
    if(gLibrarySourceSelection>=gLibrarySources.roots.size())gLibrarySourceSelection=0;
    gRmlSettingsPage.setOptions("library-sources",options);
    gRmlSettingsPage.setValue("library-sources",gLibrarySources.roots.empty()?"none":std::to_string(gLibrarySourceSelection));
    gRmlSettingsPage.setOptions("library-navigation",{{"0","스마트 탐색"},{"1","원본 폴더 탐색"}});
    gRmlSettingsPage.setValue("library-navigation",gLibrarySources.originalFolders?"1":"0");
    gRmlSettingsPage.setOptions("library-grouping",{{"0","그룹 보기 (음원 기준)"},{"1","개별 채보 보기"},{"2","같은 곡 차분 모아 보기 (SP / DP)"}});
    gRmlSettingsPage.setValue("library-grouping",gLibrarySources.individualCharts?"1":gLibrarySources.collectVariants?"2":"0");
    const bool allowed=!gPlaying&&!gShowingResult&&!gLoading;
    for(auto id:{"library-add","library-refresh","library-rescan","library-navigation","library-grouping"})gRmlSettingsPage.setEnabled(id,allowed);
    gRmlSettingsPage.setEnabled("library-remove",allowed&&gLibrarySourceSelection<gLibrarySources.roots.size());
    gRmlSettingsPage.setEnabled("library-rescan",allowed&&!gLibrarySources.roots.empty());
    gRmlSettingsPage.setText("library-status",settingsUtf8(gLibraryIndexStatus));
}
#include "SettingsRmlBindings.inl"
constexpr std::array<const char*,6> rmlPageNames{"gameplay","graphics","sound","other","summary","skin"};
std::vector<std::pair<std::string,std::string>> rmlSettingsOptions(int command) {
    const int id = bms::SettingsController::canonicalId(command);
    std::vector<std::pair<std::string,std::string>> options;
    auto add = [&](int value, const std::wstring& label) { options.emplace_back(std::to_string(value), settingsUtf8(label)); };
    if (id == ID_FPS) for (size_t i=0;i<frameRateValues.size();++i) add(frameRateValues[i],frameRateLabels[i]);
    else if(id==ID_SKIN_JUDGEMENT_MODE||id==ID_SKIN_COMBO_MODE){add(-1,L"스킨 설정");add(0,L"폰트");add(1,L"이미지");}
    else if (id == ID_SPEED) for(int i=2;i<=40;++i) add(i,number(i/4.0,2)+L"x");
    else if (id == ID_BGA_DARKNESS) for(int i=0;i<=100;i+=10) add(i,std::to_wstring(i)+L"%");
    else if (id == ID_NOTE_HEIGHT) for(int i=4;i<=32;i+=2) add(i,std::to_wstring(i)+L" px");
    else if (id == ID_JUDGEMENT_PROFILE) { int i=0; for(auto s:{L"BASIC",L"EASY",L"NORMAL",L"HARD"}) add(i++,s); }
    else if (id == ID_GROOVE_GAUGE) { int i=0; for(auto s:{L"NORMAL",L"EASY",L"HARD"}) add(i++,s); }
    else if(id==ID_TIMING_HINT){add(0,L"PGREAT 미만");add(1,L"GREAT 미만");add(2,L"GOOD 미만");}
    else if (id == ID_ACCURACY_LIMIT) { for(int i=0;i<int(accuracyLimitLabels.size());++i)add(i,accuracyLimitLabels[i]); }
    else if (id == ID_BROWSE_BACKGROUND) { int i=0; for(auto s:{L"단색",L"BGA",L"타이틀 이미지",L"사용자 이미지"}) add(i++,s); }
    else if (id == ID_SCREEN_MODE) { add(0,L"창화면");add(1,L"보더리스 창화면");add(2,L"전체화면"); }
    else if (id == ID_RESOLUTION) {
        add(0,L"자동 (창 / 모니터 기본 크기)");
        for(size_t i=0;i<gResolutionOptions.size();++i) add(int(i+1),std::to_wstring(gResolutionOptions[i].width)+L" × "+std::to_wstring(gResolutionOptions[i].height));
    }
    else if (id == ID_GEAR_POSITION) { add(0,L"왼쪽");add(1,L"가운데");add(2,L"오른쪽"); }
    else if (id == ID_SOUND_BUFFER) for(int frames:SOUND_BUFFER_OPTIONS) add(frames,std::to_wstring(frames)+L" frames");
    else if (id == ID_LOW_LATENCY) { add(0,L"끔");add(1,L"NVIDIA Reflex"); }
    else if (id>=ID_COLOR_BASE && id<ID_COLOR_BASE+8) for(int i=0;i<int(gPalette.size());++i) add(i,gPalette[i].name);
    return options;
}
void refreshAllNativeSettings() {
    if (gRmlSettingsPageActive) return;
    for (const auto& [id, value] : settingsController().snapshot()) { (void)value; refreshSettingControls(id); }
}
void syncRmlSettingsState(const std::wstring& status = L"") {
    if (!gRmlSettingsPage.window()) return;
    syncLibrarySettings();
    const int nominal=int(std::lround(gWindowHeight*2.0/3.0));
    gRmlSettingsPage.setRange("judge-position",40-nominal,std::max(40,gWindowHeight-112)-nominal);
    const auto snapshot = settingsController().snapshot();
    for(const auto& binding:rmlSettingBindings()) {
        const int value=snapshot.at(bms::SettingsController::canonicalId(binding.command));
        if(binding.kind==RmlSettingBinding::Check) gRmlSettingsPage.setChecked(binding.element,value!=0);
        else gRmlSettingsPage.setValue(binding.element,std::to_string(value));
        if(binding.kind==RmlSettingBinding::Range) {
            const bool pixels=binding.command==ID_JUDGE_POSITION || binding.command==ID_COMBO_TEXT_Y || binding.command==ID_JUDGEMENT_TEXT_Y;
            gRmlSettingsPage.setText(std::string(binding.element)+"-value",std::to_string(value)+(pixels?" px":"%"));
        }
    }
    gRmlSettingsPage.setEnabled("gpu-limit",snapshot.at(ID_COMPOSITION)!=0);
    gRmlSettingsPage.setEnabled("direct-composition",gCompositionEnabled);
    const bool reflexAvailable=latency::probeReflex().supported && gCompositionEnabled &&
        gCompositionPresenter.lowLatency().status(latency::Mode::Reflex)!=latency::Status::SessionDisabled;
    gRmlSettingsPage.setEnabled("low-latency",reflexAvailable || gReflexRequested);
    gRmlSettingsPage.setText("key-summary",settingsUtf8(keySummary()));
    gRmlSettingsPage.setEnabled("bind-keys",!gCapturingBindings);
    gRmlSettingsPage.setEnabled("cancel-keys",gCapturingBindings);
    gRmlSettingsPage.setText("game-preview",gSettingsPreviewShowing?"플레이 화면 미리보기 끄기":"플레이 화면 미리보기 켜기");
    gRmlSettingsPage.setText("background-name",gCustomBackgroundName.empty()?"선택된 이미지 없음":settingsUtf8(gCustomBackgroundName));
    gRmlSettingsPage.setText("resource-state", gShowResourceUsage ? "켜짐" : "꺼짐");
#ifdef BMS_ENABLE_SKIN_BINDINGS
    gRmlSettingsPage.setText("skin-state",settingsUtf8(skinSummary()));
    gRmlSettingsPage.setText("skin-path",settingsUtf8(gSkinSource.wstring()));
    gRmlSettingsPage.setEnabled("skin-clear",true);
#else
    gRmlSettingsPage.setText("skin-state","이 빌드에서는 외부 스킨을 지원하지 않습니다.");
    gRmlSettingsPage.setEnabled("skin-load",false);gRmlSettingsPage.setEnabled("skin-clear",false);gRmlSettingsPage.setEnabled("skin-judgement-mode",false);gRmlSettingsPage.setEnabled("skin-combo-mode",false);
#endif
    gRmlSettingsPage.setText("settings-status", status.empty() ? "변경사항이 자동으로 저장됩니다." : settingsUtf8(status));
}
void runSettingsImagePicker(const std::function<void(HWND)>& picker) {
    picker(gSettingsWindow);
    syncRmlSettingsState();
    if(gRmlSettingsPageActive) { SetFocus(gRmlSettingsPage.window()); gRmlSettingsPage.focus("browse-image"); }
}
void hideRmlSettingsPage() {
    if (gRmlSettingsPage.window()) ShowWindow(gRmlSettingsPage.window(), SW_HIDE);
    gRmlSettingsPageActive = false;
}
bool showRmlSettingsPage() {
    if (!gRmlSettingsPageEnabled || gRmlSettingsPageFailed || gNativeSettingsFallback || !gSettingsWindow) return false;
    try {
        if (!gRmlSettingsPage.window()) {
            const auto root = gRmlSettingsAssetRoot.empty() ? executableFolder() / L"Assets/UI" : gRmlSettingsAssetRoot;
            std::error_code ec;
            if (!std::filesystem::is_regular_file(root / L"settings.rcss", ec)) throw std::runtime_error("Missing settings.rcss");
            const auto changed = [](const std::string& id, const std::string& value) {
                if(id.starts_with("library-")) {
                    if(id=="library-sources"){try{gLibrarySourceSelection=std::stoull(value);}catch(...){}PostMessageW(gWindow,WM_LIBRARY_ACTION,7,0);return;}
                    if(gPlaying||gShowingResult||gLoading)return;
                    if(id=="library-navigation"&&(value=="0"||value=="1")){gLibrarySources.originalFolders=value=="1";PostMessageW(gWindow,WM_LIBRARY_ACTION,5,0);return;}
                    if(id=="library-grouping"&&(value=="0"||value=="1"||value=="2")){gLibrarySources.individualCharts=value=="1";gLibrarySources.collectVariants=value=="2";PostMessageW(gWindow,WM_LIBRARY_ACTION,6,0);return;}
                    for(const auto& [name,action]:{std::pair{"library-add",1},{"library-remove",2},{"library-rescan",3},{"library-refresh",4}})
                        if(id==name){PostMessageW(gWindow,WM_LIBRARY_ACTION,action,0);return;}
                }
                for(const auto& binding:rmlSettingBindings()) if(id==binding.element) {
                    if(gCapturingBindings) cancelKeyBindingCapture();
                    try {
                        size_t used{}; const double parsed=std::stod(value,&used);
                        if(used!=value.size() || !std::isfinite(parsed) || parsed < INT_MIN || parsed > INT_MAX || parsed!=std::round(parsed) ||
                            !postSetting(binding.command,static_cast<int>(parsed))) syncRmlSettingsState(L"지원하지 않는 설정 값입니다.");
                    } catch(const std::exception&) { syncRmlSettingsState(L"지원하지 않는 설정 값입니다."); }
                    return;
                }
                if (id == "settings-close") { PostMessageW(gSettingsWindow, WM_CLOSE, 0, 0); return; }
                if (id == "controller-settings") { PostMessageW(gSettingsWindow, WM_RML_SETTINGS_ACTION, ID_CONTROLLER_SETTINGS, 0); return; }
#ifdef BMS_ENABLE_SKIN_BINDINGS
                if(id=="skin-load"||id=="skin-clear") {
                    PostMessageW(gSettingsWindow,WM_RML_SETTINGS_ACTION,id=="skin-load"?ID_SKIN_LOAD:ID_SKIN_CLEAR,0);return;
                }
#endif
                for(const auto& [name,action]:{std::pair{"bind-keys",ID_BIND},{"cancel-keys",IDCANCEL},{"reset-keys",ID_RESET_KEYS},{"game-preview",1},{"settings-help",ID_HELP}})
                    if(id==name) { PostMessageW(gSettingsWindow,WM_RML_SETTINGS_ACTION,action,0);return; }
                if (id == "browse-image") { PostMessageW(gSettingsWindow,WM_RML_SETTINGS_PICK_IMAGE,0,0); return; }
                if (id == "native-fallback") { PostMessageW(gSettingsWindow, WM_RML_SETTINGS_FALLBACK, 1, 0); return; }
                for(size_t i=0;i<rmlPageNames.size();++i)
                    if(id==std::string("native-")+rmlPageNames[i]) { PostMessageW(gSettingsWindow,WM_RML_SETTINGS_NAVIGATE,i,1); return; }
                for (const auto& [name, category] : {std::pair{"nav-summary",4}, {"nav-gameplay",0}, {"nav-graphics",1}, {"nav-sound",2}, {"nav-other",3}, {"nav-skin",5}})
                    if (id == name) { PostMessageW(gSettingsWindow, WM_RML_SETTINGS_NAVIGATE, category, 0); return; }
            };
            const auto failed = [] { if (gSettingsWindow) PostMessageW(gSettingsWindow, WM_RML_SETTINGS_FALLBACK, 0, 0); };
            if (!gRmlSettingsPage.openEmbedded(gSettingsWindow, root / L"settings.rml", changed, failed, false))
                throw std::runtime_error(settingsUtf8(gRmlSettingsPage.error()));
            if (!gRmlSettingsPage.setChecked("resource-usage", gShowResourceUsage) ||
                !gRmlSettingsPage.setText("settings-status", "변경사항이 자동으로 저장됩니다."))
                throw std::runtime_error("Settings document is missing required controls");
            for(const auto& binding:rmlSettingBindings()) {
                if(binding.kind==RmlSettingBinding::Select) gRmlSettingsPage.setOptions(binding.element,rmlSettingsOptions(binding.command));
                if(!gRmlSettingsPage.setValue(binding.element,"0")) throw std::runtime_error("Missing settings control");
            }
        }
        syncRmlSettingsState();
        for(size_t i=0;i<rmlPageNames.size();++i) {
            gRmlSettingsPage.setVisible(std::string("page-")+rmlPageNames[i],int(i)==gSettingsCategory);
            gRmlSettingsPage.setClass(std::string("nav-")+rmlPageNames[i],"active",int(i)==gSettingsCategory);
        }
        gRmlSettingsPage.scrollTop("settings-content");
        gRmlSettingsPageActive = true;
        for (const auto& [control, category] : gSettingsControlCategory) { (void)category; ShowWindow(control, SW_HIDE); }
        ShowWindow(gSettingsBody, SW_HIDE);
        ShowScrollBar(gSettingsWindow, SB_VERT, FALSE);
        RECT bounds{}; GetClientRect(gSettingsWindow, &bounds);
        SetWindowPos(gRmlSettingsPage.window(), HWND_TOP, 0, 0, bounds.right, bounds.bottom, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        UpdateWindow(gRmlSettingsPage.window());
        if (IsWindowVisible(gRmlSettingsPage.window())) { SetFocus(gRmlSettingsPage.window()); gRmlSettingsPage.focus(std::string("nav-")+rmlPageNames[gSettingsCategory]); }
        return true;
    } catch (const std::exception& e) {
        gRmlSettingsPageFailed = true; gRmlSettingsPage.close(); gRmlSettingsPageActive = false;
        const std::string detail = e.what();
        setBindingStatus(L"새 설정 화면을 열 수 없어 기존 설정을 사용합니다. " + std::wstring(detail.begin(), detail.end()));
        return false;
    }
}
