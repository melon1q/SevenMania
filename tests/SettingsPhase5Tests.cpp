#include "../src/Main.cpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

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
HWND createSettings() {
    HWND window = CreateWindowExW(WS_EX_CONTROLPARENT, L"BmsPlayerSettingsWindow", L"Phase 3 settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VSCROLL | WS_CLIPCHILDREN,
        0,0,780,820,gWindow,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(window, "settings creation failed"); EnableWindow(gWindow, FALSE); return window;
}
void paintPage(const std::filesystem::path& output = {});
void click(const std::string& id) {
    paintPage();
    gRmlSettingsPage.focus(id);
    paintPage();
    const auto r = gRmlSettingsPage.bounds(id);
    if(r.right<=r.left || r.bottom<=r.top) throw std::runtime_error("RML control bounds missing: "+id);
    const LPARAM point = MAKELPARAM((r.left+r.right)/2,(r.top+r.bottom)/2);
    const HWND window = gRmlSettingsPage.window();
    SendMessageW(window, WM_MOUSEMOVE, 0, point); SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, point);
    SendMessageW(window, WM_LBUTTONUP, 0, point); pump();
}
void paintPage(const std::filesystem::path& output) {
    RECT client{}; GetClientRect(gRmlSettingsPage.window(), &client);
    HDC dc = CreateCompatibleDC(nullptr); BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=client.right; info.bmiHeader.biHeight=-client.bottom;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    void* pixels{}; HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    require(bitmap && pixels,"settings capture allocation failed"); const auto old=SelectObject(dc,bitmap);
    SendMessageW(gRmlSettingsPage.window(),WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),0); GdiFlush();
    if (!output.empty()) {
        const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
        Gdiplus::Bitmap image(bitmap,nullptr); require(image.Save(output.c_str(),&png)==Gdiplus::Ok,"settings capture save failed");
    }
    SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc);
}
uint64_t gameInfoHash() {
    ensureBackBuffer(nullptr,gWindowWidth,gWindowHeight);
    const bool oldPlaying=gPlaying; gPlaying=true; gGpuFrame=false; drawGameScreen(gBackBufferDc); GdiFlush(); gPlaying=oldPlaying;
    uint64_t hash=14695981039346656037ull;
    const auto* pixels=static_cast<const uint32_t*>(gBackBufferPixels);
    for(int y=gWindowHeight-64;y<gWindowHeight-8;++y) for(int x=26;x<300;++x) hash=(hash^(pixels[size_t(y)*gWindowWidth+x]&0xffffffu))*1099511628211ull;
    return hash;
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=3)return 2;
    const auto output=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(output);
    const auto config=output/(L"phase5-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&gGdiPlusToken,&input,nullptr);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int result=0;
    try {
        std::filesystem::copy_file(argv[1],config);gSettingsFile=config;
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
        registerType(L"Phase5Owner",windowProc);registerType(L"BmsPlayerSettingsWindow",settingsProc);
        registerType(L"BmsPlayerSettingsBody",settingsBodyProc);registerType(L"BmsPlayerSettingsPreviewOverlay",settingsPreviewOverlayProc);
        gWindow=CreateWindowW(L"Phase5Owner",L"Phase5",WS_OVERLAPPEDWINDOW,0,0,1280,860,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(gWindow,"owner creation failed");
        bms::Chart chart;chart.path=L"synthetic.bms";chart.title=L"Phase5";chart.playable=true;chart.initialBpm=120;chart.playLevel=L"9";chart.difficulty=3;
        gCharts={chart};gActiveChartIndex=0;
        HWND settings=createSettings();selectSettingsCategory(0);paintPage();require(gRmlSettingsPageActive,"gameplay page failed");
        bool controllerOpened=false;
        const auto controllerTimer=SetTimer(nullptr,0,100,[](HWND,UINT,UINT_PTR,DWORD){
            if(const auto dialog=FindWindowW(L"SevenManiaControllerSettings",nullptr))
                PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);
        });
        require(controllerTimer!=0,"controller dialog test timer failed");
        const auto originalController=gController.config;
        controllerOpened=gRmlSettingsPage.bounds("controller-settings").right>0;
        click("controller-settings");KillTimer(nullptr,controllerTimer);
        require(controllerOpened&&!FindWindowW(L"SevenManiaControllerSettings",nullptr)&&!gControllerConfiguring&&IsWindowEnabled(settings),"controller settings did not return to RML page");
        require(gController.config.enabled==originalController.enabled&&gController.config.keys==originalController.keys,"controller cancel modified mappings");
        gSettingsPreviewOverlay=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,
            L"BmsPlayerSettingsPreviewOverlay",L"",WS_POPUP,0,0,1,1,gWindow,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(gSettingsPreviewOverlay,"preview window creation failed");SetLayeredWindowAttributes(gSettingsPreviewOverlay,0,255,LWA_ALPHA);
        const auto originalKeys=gLaneKeys;
        click("bind-keys");require(gCapturingBindings,"RML bind button did not start physical capture");
        require(handleSettingsCaptureMessage(WM_KEYDOWN,VK_TAB,0)&&gCaptureLane==0,"reserved navigation key escaped capture");
        handleSettingsCaptureMessage(WM_KEYUP,VK_TAB,0);
        handleSettingsCaptureMessage(WM_KEYDOWN,'Q',0);require(gCaptureLane==1&&gCaptureAwaitRelease,"capture did not await release");
        handleSettingsCaptureMessage(WM_KEYDOWN,'W',0);require(gCaptureLane==1,"held capture key advanced another lane");
        handleSettingsCaptureMessage(WM_KEYUP,'Q',0);handleSettingsCaptureMessage(WM_KEYDOWN,'Q',0);
        require(gCaptureLane==1,"duplicate physical key accepted");
        handleSettingsCaptureMessage(WM_SYSKEYDOWN,VK_MENU,0);require(gCaptureLane==1,"Alt entered binding");
        handleSettingsCaptureMessage(WM_KEYDOWN,VK_ESCAPE,0);require(!gCapturingBindings&&gLaneKeys==originalKeys&&gSettingsWindow==settings,"Escape lost original bindings or closed settings");
        click("bind-keys");SendMessageW(settings,WM_ACTIVATE,WA_INACTIVE,0);
        require(!gCapturingBindings&&gLaneKeys==originalKeys,"focus loss failed to cancel");
        click("bind-keys");selectSettingsCategory(1);require(!gCapturingBindings,"navigation left binding capture active");
        selectSettingsCategory(0);paintPage();click("bind-keys");
        const std::array<UINT,8> keys{'Q','W','E','R','T','Y','U','I'};
        for(UINT key:keys){handleSettingsCaptureMessage(WM_KEYDOWN,key,0);handleSettingsCaptureMessage(WM_KEYUP,key,0);}
        require(!gCapturingBindings&&gLaneKeys==keys,"complete binding capture did not apply");loadKeyBindings();require(gLaneKeys==keys,"binding INI save failed");
        click("reset-keys");require(gLaneKeys==DEFAULT_KEYS,"RML default keys failed");
        click("game-preview");require(gSettingsPreviewShowing,"RML preview did not show existing renderer");
        require(postSetting(ID_JUDGE_POSITION,-30)&&postSetting(ID_COMBO_TEXT_Y,24)&&postSetting(ID_JUDGEMENT_TEXT_Y,-24)&&postSetting(ID_GEAR_POSITION,2),"preview values rejected");
        pump();paintPage(output/L"phase5-gameplay.png");
        require(gJudgeLineOffset==-30&&gComboTextYOffset==24&&gJudgementTextYOffset==-24&&gGearPosition==2,"preview values not applied");
        {
            ensureBackBuffer(nullptr,gWindowWidth,gWindowHeight);
            const bool oldPlaying=gPlaying;gPlaying=true;gGpuFrame=false;
            drawGameScreen(gBackBufferDc);gPlaying=oldPlaying;GdiFlush();
            require(gBackBufferBitmap,"gameplay surface missing");
            const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
            Gdiplus::Bitmap preview(gBackBufferBitmap,nullptr);
            require(preview.Save((output/L"gameplay-preview.png").c_str(),&png)==Gdiplus::Ok,"gameplay preview capture failed");
        }
        selectSettingsCategory(1);require(!gSettingsPreviewShowing,"page switch left preview showing");paintPage();
        // Native backend rejects an invalid resolution without altering the last valid state.
        const int resolution=readSettingValue(ID_RESOLUTION);require(postSetting(ID_RESOLUTION,int(gResolutionOptions.size())+10),"invalid request not queued");pump();
        require(readSettingValue(ID_RESOLUTION)==resolution,"failed resolution changed actual value");
        click("gpu-enabled");paintPage();require(gRmlSettingsPage.value("gpu-enabled")==std::to_string(int(gCompositionEnabled)),"GPU request did not synchronize actual value");
        click("gpu-enabled");paintPage();
        require(postSetting(ID_SCREEN_MODE,1),"borderless request rejected");pump();paintPage();
        require(gScreenMode==1&&gRmlSettingsPageActive,"borderless transition lost settings context");
        // Selecting a low resolution must not shrink a borderless window to a
        // small centered rectangle. Exercise the real queued settings backend.
        const auto savedResolutions=gResolutionOptions;
        MONITORINFOEXW monitor{};require(getMonitorDisplayInfo(gWindow,monitor),"monitor query failed");
        const int monitorWidth=monitor.rcMonitor.right-monitor.rcMonitor.left;
        const int monitorHeight=monitor.rcMonitor.bottom-monitor.rcMonitor.top;
        gResolutionOptions.push_back({std::max(1,monitorWidth/2),std::max(1,monitorHeight/2)});
        require(postSetting(ID_RESOLUTION,int(gResolutionOptions.size())),"low resolution request rejected");pump();paintPage();
        RECT fullscreenBounds{};GetWindowRect(gWindow,&fullscreenBounds);
        require(EqualRect(&fullscreenBounds,&monitor.rcMonitor)&&gScreenMode==1,"low resolution left borderless smaller than the monitor");
        require(postSetting(ID_RESOLUTION,0),"automatic resolution request rejected");pump();
        GetWindowRect(gWindow,&fullscreenBounds);
        require(EqualRect(&fullscreenBounds,&monitor.rcMonitor),"automatic resolution did not cover monitor");
        gResolutionOptions=savedResolutions;
        require(postSetting(ID_DIRECT_COMPOSITION,0)&&postSetting(ID_LOW_LATENCY,0)&&postSetting(ID_SCREEN_MODE,0)&&postSetting(ID_RESOLUTION,0),"device values not queued");pump();paintPage();
        require(gScreenMode==0&&!gResolutionConfigured&&!gDirectCompositionEnabled&&!gReflexRequested,"device values failed to apply");
        require(gRmlSettingsPageActive&&gRmlSettingsPage.value("screen-mode")=="0"&&gRmlSettingsPage.value("summary-screen")=="0","device operation broke RML context/summary");
        selectSettingsCategory(2);paintPage();require(gRmlSettingsPage.focus("sound-buffer"),"buffer focus failed");
        SendMessageW(gRmlSettingsPage.window(),WM_KEYDOWN,VK_DOWN,0);SendMessageW(gRmlSettingsPage.window(),WM_KEYUP,VK_DOWN,0);pump();paintPage();
        require(std::stoi(gRmlSettingsPage.value("sound-buffer"))==gSoundBufferFrames,"audio buffer UI/backend diverged");
        const int buffer=gSoundBufferFrames;
        selectSettingsCategory(0);paintPage();click("game-preview");require(gSettingsPreviewShowing,"preview reopen failed");
        SendMessageW(settings,WM_SIZE,SIZE_RESTORED,0);require(!gSettingsPreviewShowing,"resize did not hide preview");
        click("bind-keys");SendMessageW(settings,WM_CLOSE,0,0);
        require(!gCapturingBindings&&!gSettingsPreviewShowing&&!gSettingsWindow&&!gRmlSettingsPage.window(),"close left capture/preview/context active");
        loadDisplaySettings();require(gSoundBufferFrames==buffer&&gComboTextYOffset==24&&gGearPosition==2,"restart lost stage5 values");
        settings=createSettings();selectSettingsCategory(1);paintPage(output/L"phase5-graphics.png");require(gRmlSettingsPageActive,"reopen after device operations failed");
        SendMessageW(settings,WM_CLOSE,0,0);
        std::cout<<"Phase5 physical capture/cancel/release/duplicates, preview lifecycle, queued devices/failure/summary, audio buffer and reopen passed.\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    if(gSettingsWindow)SendMessageW(gSettingsWindow,WM_CLOSE,0,0);
    if(gWindow)DestroyWindow(gWindow);
    CoUninitialize();Gdiplus::GdiplusShutdown(gGdiPlusToken);return result;
}
