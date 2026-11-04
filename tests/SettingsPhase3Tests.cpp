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
    const auto output=std::filesystem::absolute(argv[2]); std::filesystem::create_directories(output);
    const auto config=output/(L"settings-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    Gdiplus::GdiplusStartupInput input; Gdiplus::GdiplusStartup(&gGdiPlusToken,&input,nullptr); CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int result=0;
    try {
        std::filesystem::copy_file(argv[1],config); gSettingsFile=config; gCpuPerformanceMode=false;
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES|ICC_BAR_CLASSES}; InitCommonControlsEx(&controls);
        registerType(L"BmsSettingsPhase3Owner",windowProc); registerType(L"BmsPlayerSettingsWindow",settingsProc);
        registerType(L"BmsPlayerSettingsBody",settingsBodyProc);
        gWindow=CreateWindowW(L"BmsSettingsPhase3Owner",L"Phase3",WS_OVERLAPPEDWINDOW,0,0,1280,860,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(gWindow,"main fixture creation failed");
        bms::Chart chart; chart.path=L"synthetic.bms"; chart.title=L"Fixture"; chart.playable=true; chart.initialBpm=120;
        gCharts={chart}; gActiveChartIndex=0;
        ensureBackBuffer(nullptr,gWindowWidth,gWindowHeight);
        bms::ui::SongListVisualRow listRow;listRow.title=L"Shared runtime fixture";listRow.level=L"7";listRow.height=58;
        require(!gRmlSongList.enabled()||gRmlSongList.paint(gBackBufferDc,320,180,{listRow}),"initial list render failed");
        HWND settings=createSettings(); selectSettingsCategory(3);
        if (!gRmlSettingsPageActive) {
            wchar_t reason[1024]{}; GetWindowTextW(gSettingsStatus,reason,1024); std::wcerr<<reason<<L'\n';
        }
        require(gRmlSettingsPageActive,"migrated Other page did not open");
        paintPage();
        listRow.selected=true;
        require(!gRmlSongList.enabled()||gRmlSongList.paint(gBackBufferDc,320,180,{listRow}),"settings stylesheet reload broke the active list");
        require(GetParent(gRmlSettingsPage.window())==settings,"RML page is not embedded in the same settings HWND");
        require(!(GetWindowLongPtrW(gSettingsBody,GWL_STYLE)&WS_VISIBLE),"native body overlaps migrated page");
        require(gRmlSettingsPage.value("resource-usage")=="1","initial persisted checkbox value was not displayed");
        require(settingsController().drain().empty(),"state synchronization enqueued a write");
        click("resource-usage"); paintPage(output/L"settings-other-off.png");
        require(!gShowResourceUsage && GetPrivateProfileIntW(L"System",L"ShowUsage",9,config.c_str())==0,"RML checkbox did not apply/save off");
        const auto offHash=gameInfoHash();
        click("resource-usage"); paintPage(output/L"settings-other-on.png");
        require(gShowResourceUsage && GetPrivateProfileIntW(L"System",L"ShowUsage",9,config.c_str())==1,"RML checkbox did not apply/save on");
        require(gameInfoHash()!=offHash,"FPS/BPM/speed area did not follow migrated checkbox");
        // Phase 4: every migrated control reads the canonical snapshot, including aliases.
        for(int category=0;category<5;++category) {
            selectSettingsCategory(category); paintPage(output/(L"settings-category-"+std::to_wstring(category)+L".png"));
            require(gRmlSettingsPageActive,"migrated category failed");
            const auto snapshot=settingsController().snapshot();
            for(const auto& binding:rmlSettingBindings()) {
                const auto expected=std::to_string(snapshot.at(bms::SettingsController::canonicalId(binding.command)));
                if(std::stod(gRmlSettingsPage.value(binding.element))!=std::stod(expected)) throw std::runtime_error(std::string("snapshot mismatch: ")+binding.element);
            }
        }
        selectSettingsCategory(2); paintPage();
        const int oldVolume=gVolumes[0];
        require(gRmlSettingsPage.focus("master-volume"),"volume focus failed"); paintPage();
        SendMessageW(gRmlSettingsPage.window(),WM_KEYDOWN,VK_LEFT,0);
        SendMessageW(gRmlSettingsPage.window(),WM_KEYUP,VK_LEFT,0); pump(); paintPage();
        require(gVolumes[0]<oldVolume,"range keyboard did not apply real volume");
        const auto slider=gRmlSettingsPage.bounds("master-volume/sliderbar");
        const HWND rmlWindow=gRmlSettingsPage.window();
        const LPARAM start=MAKELPARAM((slider.left+slider.right)/2,(slider.top+slider.bottom)/2);
        const LPARAM end=MAKELPARAM(slider.left-50,(slider.top+slider.bottom)/2);
        SendMessageW(rmlWindow,WM_MOUSEMOVE,0,start);SendMessageW(rmlWindow,WM_LBUTTONDOWN,MK_LBUTTON,start);
        SendMessageW(rmlWindow,WM_MOUSEMOVE,MK_LBUTTON,end);SendMessageW(rmlWindow,WM_LBUTTONUP,0,end);pump();paintPage();
        require(gVolumes[0]<oldVolume-1,"mouse drag did not apply volume");
        const int changedVolume=gVolumes[0];
        selectSettingsCategory(4);paintPage();
        require(std::stod(gRmlSettingsPage.value("summary-master"))==changedVolume,"summary volume did not synchronize");
        selectSettingsCategory(0);paintPage();
        require(gRmlSettingsPage.focus("game-speed"),"dropdown focus failed");paintPage();
        const auto oldSpeed=gRmlSettingsPage.value("game-speed");
        SendMessageW(gRmlSettingsPage.window(),WM_KEYDOWN,VK_DOWN,0);
        SendMessageW(gRmlSettingsPage.window(),WM_KEYUP,VK_DOWN,0);pump();paintPage();
        require(gRmlSettingsPage.value("game-speed")!=oldSpeed,"dropdown keyboard failed");
        const auto changedSpeed=gRmlSettingsPage.value("game-speed");
        click("game-speed");paintPage();
        const auto popup=gRmlSettingsPage.bounds("game-speed/selectbox");
        RECT viewport{};GetClientRect(gRmlSettingsPage.window(),&viewport);
        require(popup.bottom>popup.top && popup.top>=0 && popup.bottom<=viewport.bottom,"dropdown escaped settings viewport");
        SendMessageW(gRmlSettingsPage.window(),WM_KEYDOWN,VK_ESCAPE,0);
        SendMessageW(gRmlSettingsPage.window(),WM_KEYUP,VK_ESCAPE,0);pump();paintPage();
        selectSettingsCategory(4);paintPage();
        require(gRmlSettingsPage.value("summary-speed")==changedSpeed,"summary speed diverged");
        selectSettingsCategory(3);paintPage();
        runSettingsImagePicker([](HWND){});paintPage();
        require(gRmlSettingsPage.focused()=="browse-image","cancelled picker did not restore focus");
        const auto fixedNav=gRmlSettingsPage.bounds("nav-other"),fixedClose=gRmlSettingsPage.bounds("settings-close");
        gRmlSettingsPage.focus("native-other");paintPage();
        require(gRmlSettingsPage.bounds("nav-other").top==fixedNav.top && gRmlSettingsPage.bounds("settings-close").top==fixedClose.top,"body scroll moved fixed navigation/footer");
        click("nav-sound"); require(gSettingsWindow==settings && gSettingsCategory==2 && gRmlSettingsPageActive,"category switch did not preserve embedded window");
        click("native-sound");
        require(GetWindowLongPtrW(gSettingsBody,GWL_STYLE)&WS_VISIBLE,"native sound page did not return");
        SendMessageW(settings,WM_COMMAND,MAKEWPARAM(ID_CATEGORY_BASE+3,BN_CLICKED),0); paintPage();
        click("native-other"); require(gSettingsCategory==3&&!gRmlSettingsPageActive,"advanced Other settings unavailable");
        SendMessageW(gResourceUsageChoice,BM_SETCHECK,BST_UNCHECKED,0);
        SendMessageW(settings,WM_COMMAND,MAKEWPARAM(ID_RESOURCE_USAGE,BN_CLICKED),reinterpret_cast<LPARAM>(gResourceUsageChoice));
        SendMessageW(settings,WM_COMMAND,MAKEWPARAM(ID_CATEGORY_BASE+3,BN_CLICKED),0); paintPage();
        require(gRmlSettingsPage.value("resource-usage")=="0","native and RML views diverged");
        SetWindowPos(settings,nullptr,0,0,780,400,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE); paintPage(output/L"settings-small.png");
        RECT smallClient{}; GetClientRect(gRmlSettingsPage.window(),&smallClient);
        const auto closeBounds=gRmlSettingsPage.bounds("settings-close");
        require(closeBounds.top>=0 && closeBounds.bottom<=smallClient.bottom,"small settings window clipped the close footer");
        selectSettingsCategory(0);paintPage();click("game-speed");paintPage();
        const auto smallPopup=gRmlSettingsPage.bounds("game-speed/selectbox");
        require(smallPopup.top>=0&&smallPopup.bottom<=smallClient.bottom,"small-window dropdown escaped viewport");
        SendMessageW(gRmlSettingsPage.window(),WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA),MAKELPARAM(700,200));pump();paintPage();
        require(gRmlSettingsPage.value("game-speed")==changedSpeed,"body wheel changed selected speed");
        SendMessageW(gRmlSettingsPage.window(),WM_DPICHANGED,MAKEWPARAM(192,192),0);paintPage();
        const auto dpiClose=gRmlSettingsPage.bounds("settings-close");
        require(dpiClose.top>=0&&dpiClose.bottom<=smallClient.bottom,"high-DPI update clipped footer");
        SendMessageW(gRmlSettingsPage.window(),WM_DPICHANGED,MAKEWPARAM(96,96),0);
        selectSettingsCategory(3);paintPage();
        SetWindowPos(settings,nullptr,0,0,780,820,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        SendMessageW(settings,WM_RML_SETTINGS_FALLBACK,0,0);
        require(!gRmlSettingsPageActive&&gRmlSettingsPageFailed&&!gShowResourceUsage,"runtime fallback lost applied state");
        const auto beforeReload=settingsController().snapshot();
        SendMessageW(settings,WM_CLOSE,0,0); gShowResourceUsage=true; loadDisplaySettings();
        const auto afterReload=settingsController().snapshot();
        for(const auto& binding:rmlSettingBindings()) {
            const int id=bms::SettingsController::canonicalId(binding.command);
            require(beforeReload.at(id)==afterReload.at(id),"migrated setting did not survive INI reload");
        }
        require(!gShowResourceUsage,"restart did not retain migrated setting");
        settings=createSettings(); selectSettingsCategory(3); paintPage();
        require(gRmlSettingsPage.value("resource-usage")=="0","reopened migrated page lost saved value");
        click("native-fallback"); require(!gRmlSettingsPageActive&&gRmlSettingsPageFailed,"explicit native fallback failed");
        SendMessageW(settings,WM_CLOSE,0,0);
        const auto broken=output/L"broken-assets"; std::filesystem::create_directories(broken);
        std::filesystem::copy_file(executableFolder()/L"Assets/UI/settings.rml",broken/L"settings.rml",std::filesystem::copy_options::overwrite_existing);
        std::error_code ignored; std::filesystem::remove(broken/L"settings.rcss",ignored); gRmlSettingsAssetRoot=broken;
        settings=createSettings(); selectSettingsCategory(3);
        require(gRmlSettingsPageFailed&&!gRmlSettingsPageActive&&(GetWindowLongPtrW(gSettingsBody,GWL_STYLE)&WS_VISIBLE),"missing CSS did not restore native controls");
        SendMessageW(settings,WM_CLOSE,0,0);
        {std::ofstream bad(broken/L"settings.rcss");bad<<"body { unsupported-phase3-property: 1; }";}
        settings=createSettings();selectSettingsCategory(3);
        require(gRmlSettingsPageFailed&&!gRmlSettingsPageActive&&!gShowResourceUsage,"invalid CSS did not fail safely");
        SendMessageW(settings,WM_CLOSE,0,0);
        settings=createSettings();selectSettingsCategory(3);
        require(gRmlSettingsPageFailed&&!gRmlSettingsPageActive,"cached invalid CSS bypassed validation on reopen");
        SendMessageW(settings,WM_CLOSE,0,0);
        std::filesystem::copy_file(executableFolder()/L"Assets/UI/settings.rcss",broken/L"settings.rcss",std::filesystem::copy_options::overwrite_existing);
        std::filesystem::remove(broken/L"settings.rml",ignored);
        settings=createSettings();selectSettingsCategory(3);
        require(gRmlSettingsPageFailed&&!gRmlSettingsPageActive,"missing RML did not fail safely");
        SendMessageW(settings,WM_CLOSE,0,0);
        {std::ofstream incomplete(broken/L"settings.rml");incomplete<<"<rml><head><link type=\"text/rcss\" href=\"settings.rcss\" /></head><body><div>Incomplete</div></body></rml>";}
        settings=createSettings();selectSettingsCategory(3);
        require(gRmlSettingsPageFailed&&!gRmlSettingsPageActive&&!gShowResourceUsage,"missing required control did not fail safely");
        SendMessageW(settings,WM_CLOSE,0,0);gRmlSettingsAssetRoot.clear();
        settings=createSettings();selectSettingsCategory(3);require(gRmlSettingsPageActive,"valid settings assets did not recover after failed sessions");
        SendMessageW(settings,WM_CLOSE,0,0);
        std::cout<<"Phase3: embedded Other page, real setting/save, gameplay info, native navigation, reopen, and missing/invalid CSS fallback passed.\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    if(gSettingsWindow)SendMessageW(gSettingsWindow,WM_CLOSE,0,0);
    if(gWindow)DestroyWindow(gWindow);
    CoUninitialize();Gdiplus::GdiplusShutdown(gGdiPlusToken);return result;
}
