#include "../src/Main.cpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

// Drives the LR2 section of the settings skin page: folder scan, per-screen selection,
// CUSTOMOPTION/CUSTOMFILE choices, persistence across a restart and the guarded states.
namespace {
void require(bool v, const char* message) { if (!v) throw std::runtime_error(message); }
void registerType(const wchar_t* name, WNDPROC proc) {
    WNDCLASSW type{}; type.lpfnWndProc = proc; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = name;
    type.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    require(RegisterClassW(&type), "window registration failed");
}
void pump() {
    MSG message{}; unsigned count = 0;
    while (count++ < 300 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    require(count < 300, "settings synchronization created a message feedback loop");
}
void paintPage(const std::filesystem::path& output) {
    RECT client{}; GetClientRect(gRmlSettingsPage.window(), &client);
    HDC dc = CreateCompatibleDC(nullptr); BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=client.right; info.bmiHeader.biHeight=-client.bottom;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    void* pixels{}; HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    require(bitmap && pixels,"settings capture allocation failed"); const auto old=SelectObject(dc,bitmap);
    SendMessageW(gRmlSettingsPage.window(),WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),0); GdiFlush();
    const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    Gdiplus::Bitmap image(bitmap,nullptr); require(image.Save(output.c_str(),&png)==Gdiplus::Ok,"settings capture save failed");
    SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc);
}
void writeText(const std::filesystem::path& path,const char* text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path,std::ios::binary)<<text;
}
std::string skinIndex(const std::filesystem::path& file) {
    for(std::size_t i=0;i<gLr2Skins.size();++i)if(gLr2Skins[i].file==file)return std::to_string(i);
    throw std::runtime_error("fixture skin was not found by the scan");
}
bool samePath(const std::filesystem::path& a,const std::filesystem::path& b) {return sameLr2SkinFile(a,b);}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=3&&argc!=4)return 2;
    const auto output=std::filesystem::absolute(argv[2]);
    std::filesystem::remove_all(output);std::filesystem::create_directories(output);
    const auto config=output/L"lr2-settings.ini";
    Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&gGdiPlusToken,&input,nullptr);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int result=0;
    try {
        gCharts.resize(1);
        auto& chartForSummary = gCharts.front();
        chartForSummary.initialBpm = 120;
        chartForSummary.notes = {{0, 1.0}};
        rebuildChartRecordKeys();
        require(lr2ChartSummary(0).judgementNotes == 1 && !lr2ChartSummary(0).hasLongNotes,
                "single-note select summary");
        chartForSummary.notes.front().lengthSeconds = 1.0;
        chartForSummary.notes.front().mode = bms::LongNoteMode::CN;
        chartForSummary.tempos = {{0, 95}, {1, 180}};
        rebuildChartRecordKeys(); // Library replacement can reuse the same index and note count.
        const auto& summary = lr2ChartSummary(0);
        require(summary.judgementNotes == 2 && summary.hasLongNotes && summary.minBpm == 95 && summary.maxBpm == 180,
                "refreshed select summary");
        PlayRecord oldRecord{};
        oldRecord.bestExScore = 3;
        require(lr2RecordNotes(&oldRecord, 0) == 2, "legacy EX score uses chart count");
        oldRecord.noteCount = 7;
        require(lr2RecordNotes(&oldRecord, 0) == 7, "saved record count takes precedence");
        gCharts.clear();
        rebuildChartRecordKeys();
        std::filesystem::copy_file(argv[1],config);gSettingsFile=config;
        const auto theme=output/L"Theme/T";
        writeText(theme/L"play.lr2skin","#INFORMATION,0,Test Play,Author\n#CUSTOMOPTION,Side,900,Left,Right\n"
            "#CUSTOMFILE,Frame,LR2files\\Theme\\T\\frame\\*.png,a\n");
        writeText(theme/L"select.lr2skin","#INFORMATION,5,Test Select,Author\n");
        writeText(theme/L"decide.lr2skin","#INFORMATION,6,Test Decide,Author\n");
        writeText(theme/L"result.lr2skin","#INFORMATION,7,Test Result,Author\n");
        writeText(theme/L"play9.lr2skin","#INFORMATION,4,Test Nine,Author\n");
        writeText(theme/L"frame/a.png","x");writeText(theme/L"frame/b.png","x");
        const auto play=theme/L"play.lr2skin",select=theme/L"select.lr2skin",decide=theme/L"decide.lr2skin",resultSkin=theme/L"result.lr2skin";

        if(argc==4)gRmlSettingsAssetRoot=std::filesystem::absolute(argv[3]);
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
        registerType(L"Lr2SettingsOwner",windowProc);registerType(L"BmsPlayerSettingsWindow",settingsProc);
        registerType(L"BmsPlayerSettingsBody",settingsBodyProc);registerType(L"BmsPlayerSettingsPreviewOverlay",settingsPreviewOverlayProc);
        gWindow=CreateWindowW(L"Lr2SettingsOwner",L"Lr2Settings",WS_OVERLAPPEDWINDOW,0,0,1280,860,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(gWindow&&bms::ui::RmlSettingsHost::available(),"default RML host unavailable");
        bms::Chart chart;chart.path=L"synthetic.bms";chart.title=L"Lr2Settings";chart.playable=true;chart.initialBpm=120;
        gCharts={chart};gActiveChartIndex=0;buildSongGroups();rebuildSongRows();

        const auto oldRoot=output/L"external";
        writeText(oldRoot/L"outside.lr2skin","#INFORMATION,5,Outside,Author\n");
        writeLr2IniPath(L"Root",oldRoot);
        loadLr2SkinSettings(output);
        require(sameLr2SkinFile(gLr2SkinRoot,output/L"Theme")&&gLr2PlayFile.empty(),"default search folder near the executable");
        openSettings();require(gSettingsWindow&&gRmlSettingsPageActive,"settings did not open");
        ShowWindow(gSettingsWindow,SW_HIDE);selectSettingsCategory(5);
        const auto value=[](const char* id){return gRmlSettingsPage.value(id);};
        selectSettingsCategory(1);
        const auto aspectBinding=std::find_if(rmlSettingBindings().begin(),rmlSettingBindings().end(),
            [](const auto& binding){return binding.command==ID_LR2_ASPECT;});
        require(aspectBinding!=rmlSettingBindings().end(),"display page binds the LR2 aspect setting");
        require(value(aspectBinding->element)=="0","display page defaults to the original LR2 aspect ratio");
        require(!handleLr2SkinSetting(aspectBinding->element,"1"),"the skin page must let the display setting reach its binding");
        require(postSetting(aspectBinding->command,1),"stretch setting queued");
        pump();
        require(gLr2Stretch&&value(aspectBinding->element)=="1"&&
            GetPrivateProfileIntW(L"Display",L"Lr2Stretch",0,gSettingsFile.c_str())==1,"stretch is shown and saved");
        require(settingsController().set(ID_LR2_ASPECT,0).applied&&!gLr2Stretch,"letterbox applies again");
        selectSettingsCategory(5);
        require(value("lr2-play7")=="none"&&value("lr2-custom-screen")=="none"&&value("lr2-custom-value")=="none","empty LR2 section state");
        paintPage(output/L"lr2-skin-page-empty.png");

        require(gLr2Skins.size()==5,"the skin page automatically scans Theme instead of the old folder");
        writeText(theme/L"play5.lr2skin","#INFORMATION,1,Test Five,Author\n");
        selectSettingsCategory(1);
        selectSettingsCategory(5);
        require(gLr2Skins.size()==6,"reopening the skin tab discovers newly installed skins");
        const auto playIndex=skinIndex(play);
        require(handleLr2SkinSetting("lr2-play7",skinIndex(select))&&gLr2PlayFile.empty()&&value("lr2-play7")=="none",
            "a skin of another screen type must be refused");
        require(handleLr2SkinSetting("lr2-play7","4096")&&gLr2PlayFile.empty(),"out-of-range skin index accepted");
        require(handleLr2SkinSetting("lr2-play7",playIndex)&&samePath(gLr2PlayFile,play),"play skin selection");
        require(value("lr2-play7")==playIndex,"play skin selection is shown");
        require(samePath(readLr2IniPath(L"Play7"),play)&&samePath(readLr2IniPath(L"Root"),oldRoot),"only the screen assignment is saved");
        // The fixture skins have headers only, so each is reported as unusable instead of failing silently.
        require(gLr2SlotError[2].find(L"DST_NOTE")!=std::wstring::npos&&gLr2SlotError[0].empty(),"unloadable play skin must be reported");
        handleLr2SkinSetting("lr2-play7","none");
        require(gLr2SlotError[2].empty(),"an unassigned screen must not keep an error");
        handleLr2SkinSetting("lr2-play7",playIndex);
        gLr2SelectAttempted=true;
        require(handleLr2SkinSetting("lr2-select",skinIndex(select))&&samePath(gLr2SelectFile,select)&&!gLr2SelectAttempted,
            "changing the select skin must schedule a reload");
        handleLr2SkinSetting("lr2-decide",skinIndex(decide));handleLr2SkinSetting("lr2-result",skinIndex(resultSkin));
        require(samePath(gLr2DecideFile,decide)&&samePath(gLr2ResultFile,resultSkin),"decide and result selection");

        // Options of the play skin: the select skin has none, so the play slot is the one with items.
        require(handleLr2SkinSetting("lr2-custom-screen","2")&&value("lr2-custom-screen")=="2"&&value("lr2-custom-item")=="o0"&&
            value("lr2-custom-value")=="0","custom option defaults to the first choice");
        const auto store=output/L"lr2-skins.txt";
        require(handleLr2SkinSetting("lr2-custom-value","1")&&value("lr2-custom-value")=="1"&&
            sevenmania::skin::lr2::loadSkinChoices(store,play).options.at("Side")=="Right","custom option choice was not saved");
        require(handleLr2SkinSetting("lr2-custom-value","9")&&value("lr2-custom-value")=="1","out-of-range option choice accepted");
        require(handleLr2SkinSetting("lr2-custom-item","f0")&&value("lr2-custom-value")=="0","custom file shows the skin default");
        require(handleLr2SkinSetting("lr2-custom-value","1")&&value("lr2-custom-value")=="1"&&
            sevenmania::skin::lr2::loadSkinChoices(store,play).files.at("Frame")=="b"&&
            sevenmania::skin::lr2::loadSkinChoices(store,play).options.at("Side")=="Right","custom file choice must keep the option choice");
        require(handleLr2SkinSetting("lr2-custom-screen","0")&&value("lr2-custom-item")=="none"&&value("lr2-custom-value")=="none",
            "a skin without options lists nothing");
        handleLr2SkinSetting("lr2-custom-screen","2");
        paintPage(output/L"lr2-skin-page-selected.png");

        // Guards: nothing changes during play, and command-line skins are neither edited nor saved.
        gPlaying=true;
        require(handleLr2SkinSetting("lr2-play7","none")&&samePath(gLr2PlayFile,play),"selection changed during play");
        gPlaying=false;gLr2CommandLine=true;
        require(handleLr2SkinSetting("lr2-play7","none")&&samePath(gLr2PlayFile,play),"command-line selection was editable");
        gLr2PlayFile.clear();saveLr2SkinSettings();
        require(samePath(readLr2IniPath(L"Play7"),play),"command-line run overwrote the saved selection");
        gLr2CommandLine=false;

        // Restart: saved files come back; a missing one falls back for that screen only.
        SendMessageW(gSettingsWindow,WM_CLOSE,0,0);pump();
        for(const auto& slot:lr2ScreenSlots())slot.file->clear();
        loadLr2SkinSettings(output);
        require(samePath(gLr2PlayFile,play)&&samePath(gLr2SelectFile,select)&&samePath(gLr2DecideFile,decide)&&
            samePath(gLr2ResultFile,resultSkin)&&samePath(gLr2SkinRoot,output/L"Theme"),"selection was not restored");
        std::filesystem::remove(play);loadLr2SkinSettings(output);
        require(gLr2PlayFile.empty()&&samePath(gLr2SelectFile,select),"a missing skin file must only disable its own screen");

        openSettings();ShowWindow(gSettingsWindow,SW_HIDE);selectSettingsCategory(5);
        require(value("lr2-play7")=="none"&&value("lr2-select")==skinIndex(select),"reopened page did not reflect the restored state");
        require(handleLr2SkinSetting("lr2-clear","")&&gLr2Skins.size()==5&&gLr2SelectFile.empty()&&gLr2ResultFile.empty()&&
            readLr2IniPath(L"Select").empty()&&value("lr2-select")=="none","clearing must disable every LR2 screen");
        require(!handleLr2SkinSetting("key-mode","1"),"non-LR2 controls must pass through");
        using sevenmania::skin::lr2::ScreenType;
        const auto missingInclude=output/L"missing-include.lr2skin";
        writeText(missingInclude,"#INFORMATION,5,Missing include,Test\n#INCLUDE,absent.csv\n");
        sevenmania::skin::lr2::Options includeOptions;
        includeOptions.screen=ScreenType::Select;
        const auto failed=inspectLr2Scene(missingInclude,includeOptions);
        require(!failed.scene&&!failed.diagnostics.empty()&&failed.diagnostics.back().error&&
            failed.diagnostics.back().line==2,"runtime load treats missing INCLUDE as fatal and retains the source line");
        gLr2ResultFile=resultSkin;
        gLr2PlayScene.reset();
        gCharts.emplace_back();
        gActiveChartIndex=static_cast<int>(gCharts.size()-1);
        gCharts.back().durationSeconds=10.;
        gLr2Play.resetTimers(0.);
        gPlayhead=5.;
        sampleLr2ResultGauge();
        require(gLr2Play.gaugeSamples==128&&gLr2Play.gaugeHistory[127]==static_cast<float>(gGrooveGauge.value()),
            "result gauge history is available without an LR2 play skin");
        gLr2Play.resetTimers(0.);
        require(gLr2Play.gaugeSamples==0,"restarting clears the result gauge history");
        SendMessageW(gSettingsWindow,WM_CLOSE,0,0);pump();
        const auto exitSkin=output/L"result-exit.lr2skin";
        writeText(exitSkin,"#INFORMATION,7,Result exit,Test\n#STARTINPUT,500\n#FADEOUT,550\n"
            "#SRC_IMAGE,0,111,0,0,1,1\n#DST_IMAGE,0,0,0,0,10,10,0,255,255,255,255\n");
        sevenmania::skin::lr2::Options exitOptions;
        exitOptions.screen=ScreenType::Result;
        gShowingResult=true;
        syncNativeResultNext();
        require((GetWindowLongPtrW(gResultNext,GWL_STYLE)&WS_VISIBLE)!=0,"native result shows Next");
        gLr2ResultScene=inspectLr2Scene(exitSkin,exitOptions).scene;
        require(bool(gLr2ResultScene),"result exit fixture loads");
        skinVisibility(true);
        require((GetWindowLongPtrW(gResultNext,GWL_STYLE)&WS_VISIBLE)==0,"LR2 result hides the native Next button");
        using namespace std::chrono;
        gLr2ResultStart=steady_clock::now();
        requestResultExit();
        require(gLr2ResultExitStart==steady_clock::time_point{},"early input leaves the result open");
        gLr2ResultStart-=seconds(1);
        requestResultExit();
        const auto exitStart=gLr2ResultExitStart;
        require(exitStart!=steady_clock::time_point{}&&gShowingResult,"accepted input starts the exit animation");
        requestResultExit();
        require(gLr2ResultExitStart==exitStart,"repeated input does not restart the exit animation");
        gLr2ResultExitStart-=seconds(1);
        SendMessageW(gWindow,WM_TIMER,ID_LR2_RESULT_ANIMATION,0);
        require(!gShowingResult&&!gLr2ResultScene,"completed fade returns to song selection");
        gCompositionEnabled=true;
        gLoadResult.index=gActiveChartIndex=static_cast<int>(gCharts.size()-1);
        gCharts.back().mode=bms::ChartMode::SevenKey;
        gLr2PlayFile=play;
        gPlaybackJob.active=true;
        gPlaybackJob.applied=false;
        require(waitingForLr2Play(),"an assigned LR2 play skin owns the loading transition before decode");
        gPlaybackJob.applied=true;
        require(!waitingForLr2Play(),"the native loading preview is available after LR2 load failure");
        gLr2PlayScene=std::make_unique<sevenmania::skin::lr2::PreviewScene>();
        HDC windowDc=GetDC(gWindow);
        RECT windowBounds{};
        GetClientRect(gWindow,&windowBounds);
        const HDC memoryDc=CreateCompatibleDC(windowDc);
        const HBITMAP bitmap=CreateCompatibleBitmap(windowDc,windowBounds.right,windowBounds.bottom);
        const auto oldBitmap=SelectObject(memoryDc,bitmap);
        PatBlt(memoryDc,0,0,windowBounds.right,windowBounds.bottom,WHITENESS);
        gPlaybackJob.painted=false;
        drawPreparingPlayback(memoryDc);
        const auto loadingPixel=GetPixel(memoryDc,0,0);
        SelectObject(memoryDc,oldBitmap);DeleteObject(bitmap);DeleteDC(memoryDc);ReleaseDC(gWindow,windowDc);
        require(loadingPixel==RGB(0,0,0)&&gPlaybackJob.painted,
            "the LR2 transition clears the native preview and still lets playback preparation continue");
        gPlaybackJob.active=false;
        gLr2PlayScene.reset();
        std::cout<<"LR2 skin settings: scan, per-screen selection, custom choices, guards and restart passed.\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    if(gSettingsWindow)SendMessageW(gSettingsWindow,WM_CLOSE,0,0);if(gWindow)DestroyWindow(gWindow);
    CoUninitialize();Gdiplus::GdiplusShutdown(gGdiPlusToken);return result;
}
