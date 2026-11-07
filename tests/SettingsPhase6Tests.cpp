#include "../src/Main.cpp"
#include <fstream>
#include <psapi.h>
#include <dxgi1_4.h>
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
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=3&&argc!=4)return 2;
    const auto output=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(output);
    const auto config=output/(L"phase6-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&gGdiPlusToken,&input,nullptr);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int result=0;
    try {
        std::filesystem::copy_file(argv[1],config);gSettingsFile=config;
        {bms::KeyProfiles profiles;for(int m=0;m<5;++m){profiles.keys[m][0]=VK_F6+m;profiles.save(config,m);}bms::KeyProfiles restored;restored.load(config);for(int m=0;m<5;++m)require(restored.keys[m][0]==UINT(VK_F6+m),"mode key bindings not independent/persistent");}
        for(int mode=0;mode<5;++mode){gBindingMode=mode;beginKeyBindingCapture();for(int lane=0;lane<bms::KeyProfiles::counts[mode];++lane)acceptBindingKey('A'+lane);require(!gCapturingBindings,"mode-specific key capture did not finish");bms::KeyProfiles saved;saved.load(config);require(saved.keys[mode][bms::KeyProfiles::counts[mode]-1]==UINT('A'+bms::KeyProfiles::counts[mode]-1),"captured last key not persisted");}gBindingMode=1;
        {std::wstring error;require(applySettingValue(ID_FPS,800,error)&&gTargetFps==800,"800 FPS not applied");require(applySettingValue(ID_FPS,937,error)&&gTargetFps==937,"custom FPS not applied");require(applySettingValue(ID_JUDGEMENT_PROFILE,3,error)&&gJudgementProfile==JudgementProfile::Normal,"manual judgement still mutable");}
        if(argc==4)gRmlSettingsAssetRoot=std::filesystem::absolute(argv[3]);
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
        registerType(L"Phase6Owner",windowProc);registerType(L"BmsPlayerSettingsWindow",settingsProc);
        registerType(L"BmsPlayerSettingsBody",settingsBodyProc);registerType(L"BmsPlayerSettingsPreviewOverlay",settingsPreviewOverlayProc);
        gWindow=CreateWindowW(L"Phase6Owner",L"Phase6",WS_OVERLAPPEDWINDOW,0,0,1280,860,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(gWindow&&bms::ui::RmlSettingsHost::available(),"default RML host unavailable");
        bms::Chart chart;chart.path=L"synthetic.bms";chart.title=L"Phase6";chart.playable=true;chart.initialBpm=120;
        gCharts={chart};gActiveChartIndex=0;buildSongGroups();rebuildSongRows();
        // Probe the actual permitted route; gameplay (including pause) remains blocked.
        for(bool paused:{false,true}) {gPlaying=true;gPaused=paused;openSettings();require(!gSettingsWindow,"settings unexpectedly opened during gameplay");}
        gPlaying=false;gPaused=false;gLoading=true;gLoadResult.playback=true;openSettings();require(!gSettingsWindow,"playback-loading guard changed");gLoading=false;gLoadResult={};
        openSettings();require(gSettingsWindow&&gSettingsCategory==4&&gRmlSettingsPageActive,"default route did not open RML summary");
        ShowWindow(gSettingsWindow,SW_HIDE);selectSettingsCategory(6);paintPage(output/L"keys.png");selectSettingsCategory(4);paintPage();
        for(const auto& [id,value]:settingsController().snapshot()) {
            (void)value;
            if(id==ID_BROWSE_BACKGROUND||id==ID_JUDGEMENT_PROFILE||id==ID_COMBO_TEXT_Y||id==ID_SKIN_JUDGEMENT_MODE||id==ID_SKIN_COMBO_MODE)continue; // removed user controls; legacy keys retained
            require(std::any_of(rmlSettingBindings().begin(),rmlSettingBindings().end(),[&](const auto& binding){return bms::SettingsController::canonicalId(binding.command)==id;}),"snapshot contains an unmapped setting");
        }
        require(postSetting(ID_SPEED,13),"close queue fixture failed");
        SendMessageW(gSettingsWindow,WM_CLOSE,0,0);
        require(gNoteSpeed==3.25&&!gSettingsWindow&&!gRmlSettingsPage.window(),"close lost queued setting or left UI alive");pump();
        require(!gSettingsPreviewOverlay&&!gSettingsPreviewDc&&!gSettingsPreviewBitmap,"close retained preview resources");
        loadDisplaySettings();require(gNoteSpeed==3.25,"close did not persist final queued value");
        wchar_t unknown[64]{};GetPrivateProfileStringW(L"Phase0Unknown",L"PreserveMe",L"",unknown,64,config.c_str());require(std::wstring(unknown)==L"unchanged","current INI unknown key lost");
        const auto legacy=output/L"legacy.ini";
        {std::ofstream file(legacy);file<<"[Display]\nBGADarkness=25\nDirectComposition=0\n[System]\nMemoryLimitPercent=100\n[UnknownLegacy]\nKeep=yes\n";}
        gSettingsFile=legacy;loadDisplaySettings();saveDisplaySettings();loadDisplaySettings();
        require(gBgaBrightness==80&&!gCompositionEnabled,"legacy brightness/GPU compatibility changed");
        GetPrivateProfileStringW(L"UnknownLegacy",L"Keep",L"",unknown,64,legacy.c_str());require(std::wstring(unknown)==L"yes","legacy unknown key lost");
        gSettingsFile=config;loadDisplaySettings();
        Microsoft::WRL::ComPtr<ID3D11Device> probeDevice;Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
        if(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&probeDevice,nullptr,nullptr))) {
            Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;Microsoft::WRL::ComPtr<IDXGIAdapter> base;
            if(SUCCEEDED(probeDevice.As(&dxgi))&&SUCCEEDED(dxgi->GetAdapter(&base))) base.As(&adapter);
        }
        struct Sample {SIZE_T memory{};DWORD gdi{},user{};UINT64 gpu{};bool gpuValid{};};
        auto sample=[&] {
            Sample s;PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);require(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m)),"memory sample failed");
            s.memory=m.PrivateUsage;s.gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);s.user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
            DXGI_QUERY_VIDEO_MEMORY_INFO video{};s.gpuValid=adapter&&SUCCEEDED(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&video));s.gpu=video.CurrentUsage;return s;
        };
        std::ofstream report(output/L"lifecycle-metrics.csv");report<<"iteration,private_bytes,gdi,user,local_gpu_bytes,gpu_available,open_and_paint_ms,render_frames,render_ms\n";
        Sample baseline{},last{};
        double totalOpen=0,totalRender=0;unsigned frames=0;
        for(int i=0;i<32;++i) {
            const auto begin=std::chrono::steady_clock::now();
            openSettings();require(gSettingsCategory==4&&gRmlSettingsPageActive,"repeat open failed");ShowWindow(gSettingsWindow,SW_HIDE);
            for(int category=0;category<5;++category){selectSettingsCategory(category);paintPage();}
            const auto stats=gRmlSettingsPage.metrics();
            const double openMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
            for(int timer=0;timer<8;++timer)SendMessageW(gRmlSettingsPage.window(),WM_TIMER,1,0);
            require(gRmlSettingsPage.metrics().frames==stats.frames,"hidden settings rendered on timer");
            // Preview uses actual renderer surfaces and must release them on close.
            HDC dc=GetDC(gWindow);ensureSettingsPreviewBuffer(dc,640,360);ReleaseDC(gWindow,dc);
            const HWND oldHost=gRmlSettingsPage.window();SendMessageW(gSettingsWindow,WM_CLOSE,0,0);pump();
            require(!IsWindow(oldHost)&&!gSettingsWindow&&!gCapturingBindings&&!gSettingsPreviewDc&&!gSettingsPreviewOverlay,"repeat close retained UI/capture/preview");
            last=sample();if(i==7)baseline=last;
            report<<i<<','<<last.memory<<','<<last.gdi<<','<<last.user<<','<<last.gpu<<','<<last.gpuValid<<','<<openMs<<','<<stats.frames<<','<<stats.renderMilliseconds<<'\n';
            if(i>=8){totalOpen+=openMs;totalRender+=stats.renderMilliseconds;frames+=stats.frames;}
        }
        require(last.gdi<=baseline.gdi+4&&last.user<=baseline.user+4,"GDI/USER handles grew across repeated close");
        require(last.memory<=baseline.memory+32ull*1024*1024,"private memory grew beyond warm-cache tolerance");
        if(last.gpuValid&&baseline.gpuValid) require(last.gpu<=baseline.gpu+32ull*1024*1024,"local GPU usage grew beyond deferred-release tolerance");
        std::cout<<"Lifecycle after warmup: private delta="<<static_cast<long long>(last.memory)-static_cast<long long>(baseline.memory)
            <<", GDI delta="<<int(last.gdi)-int(baseline.gdi)<<", USER delta="<<int(last.user)-int(baseline.user)
            <<", GPU available="<<last.gpuValid<<", GPU delta="<<static_cast<long long>(last.gpu)-static_cast<long long>(baseline.gpu)
            <<", mean open+5page paint ms="<<totalOpen/24<<", mean render ms="<<(frames?totalRender/frames:0)<<'\n';
        openSettings();ShowWindow(gSettingsWindow,SW_HIDE);paintPage(output/L"settings-default-summary.png");click("native-fallback");
        require(!gRmlSettingsPageActive&&(GetWindowLongPtrW(gSettingsBody,GWL_STYLE)&WS_VISIBLE),"native compatibility switch failed");
        SendMessageW(gSettingsWindow,WM_CLOSE,0,0);openSettings();ShowWindow(gSettingsWindow,SW_HIDE);require(gRmlSettingsPageActive,"next session did not restore default RML UI");
        SendMessageW(gSettingsWindow,WM_CLOSE,0,0);
        gRmlSettingsPageEnabled=false;openSettings();ShowWindow(gSettingsWindow,SW_HIDE);require(!gRmlSettingsPageActive&&!gRmlSettingsPageFailed,"disabled option incorrectly reported document failure");
        SendMessageW(gSettingsWindow,WM_CLOSE,0,0);gRmlSettingsPageEnabled=true;
        std::cout<<"Phase6 default route, current/legacy INI, close queue, idle/render lifecycle, native fallback and option OFF passed.\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    if(gSettingsWindow)SendMessageW(gSettingsWindow,WM_CLOSE,0,0);if(gWindow)DestroyWindow(gWindow);
    CoUninitialize();Gdiplus::GdiplusShutdown(gGdiPlusToken);return result;
}
