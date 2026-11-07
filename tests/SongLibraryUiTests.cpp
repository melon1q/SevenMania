#include "../src/Main.cpp"
#include <shlobj.h>
#include <fstream>
#include <iostream>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void registerType(const wchar_t* name,WNDPROC proc){WNDCLASSW type{};type.lpfnWndProc=proc;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=name;require(RegisterClassW(&type),"window registration failed");}
void pump(){MSG message{};for(int i=0;i<500&&PeekMessageW(&message,nullptr,0,0,PM_REMOVE);++i)if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}
void paint(const std::filesystem::path& output={}){
    RECT bounds{};GetClientRect(gRmlSettingsPage.window(),&bounds);HDC dc=CreateCompatibleDC(nullptr);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=bounds.right;info.bmiHeader.biHeight=-bounds.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* pixels{};HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);require(bitmap&&pixels,"settings surface failed");auto old=SelectObject(dc,bitmap);
    SendMessageW(gRmlSettingsPage.window(),WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),0);GdiFlush();
    if(!output.empty()){const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};Gdiplus::Bitmap image(bitmap,nullptr);require(image.Save(output.c_str(),&png)==Gdiplus::Ok,"capture failed");}
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
void selectLast(const char* id){require(gRmlSettingsPage.focus(id),"library control missing");paint();for(auto key:{VK_DOWN,VK_RETURN}){SendMessageW(gRmlSettingsPage.window(),WM_KEYDOWN,key,0);SendMessageW(gRmlSettingsPage.window(),WM_KEYUP,key,0);}pump();paint();}
void drop(const std::filesystem::path& path){
    const auto value=path.wstring();auto memory=GlobalAlloc(GHND,sizeof(DROPFILES)+(value.size()+2)*sizeof(wchar_t));require(memory,"drop allocation failed");
    auto* data=static_cast<DROPFILES*>(GlobalLock(memory));data->pFiles=sizeof(DROPFILES);data->fWide=TRUE;
    memcpy(reinterpret_cast<char*>(data)+sizeof(DROPFILES),value.data(),value.size()*sizeof(wchar_t));GlobalUnlock(memory);
    SendMessageW(gWindow,WM_DROPFILES,reinterpret_cast<WPARAM>(memory),0);
}
void waitIndex(){for(int i=0;i<1000&&(gSongIndexWorker.running()||gSongIndexWorker.hasPending());++i){pump();Sleep(2);}pump();require(!gSongIndexWorker.running()&&!gSongIndexWorker.hasPending(),"background index did not publish");}
}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;const auto output=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(output);
    const auto primary=output/L"Songs",external=output/L"外部 라이브러리";std::filesystem::create_directories(primary/L"Event"/L"Song A");std::filesystem::create_directories(external);
    for(auto name:{L"normal.bms",L"another.bms"}){std::ofstream f(primary/L"Event"/L"Song A"/name);f<<"#PLAYER 1\n#TITLE Library UI\n#BPM 120\n#PLAYLEVEL 3\n#WAV01 missing.wav\n#00011:01\n";}
    {std::ofstream f(primary/L"Event"/L"Song A"/L"missing.wav");f<<"shared resource fixture";}
    {std::ofstream f(external/L"external.bms");f<<"#PLAYER 1\n#TITLE external\n#BPM 120\n#00011:01\n";}
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&gGdiPlusToken,&input,nullptr);
    int result=0;
    try{
        gSettingsFile=output/(L"profile-"+std::to_wstring(GetCurrentProcessId())+L".ini");gLibrarySources.initialize(primary,gSettingsFile);gLibrarySources.add(primary);gLibrarySources.originalFolders=false;gLibrarySources.individualCharts=false;
        gLibrarySources.add(external);gLibrarySources.save();gCurrentFolder=primary;
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
        registerType(L"Library2Owner",windowProc);registerType(L"BmsPlayerSettingsWindow",settingsProc);registerType(L"BmsPlayerSettingsBody",settingsBodyProc);registerType(L"BmsPlayerSettingsPreviewOverlay",settingsPreviewOverlayProc);
        gWindow=CreateWindowW(L"Library2Owner",L"Library 2 UI Test",WS_OVERLAPPEDWINDOW,0,0,1280,900,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);require(gWindow,"owner failed");
        auto index=bms::scanSongLibraries(gLibrarySources);gCharts=std::move(index.charts);gSongFolders.knownFolders=std::move(index.folders);rebuildChartRecordKeys();buildSongGroups();rebuildSongRows();
        require(gCharts.size()==3&&gSongSelection.groups().size()==2,"UI index/group fixture failed");
        openSettings();require(gRmlSettingsPageActive,"actual RML settings did not open");ShowWindow(gSettingsWindow,SW_HIDE);selectSettingsCategory(3);paint();
        for(auto id:{"library-sources","library-add","library-remove","library-rescan","library-refresh","library-navigation","library-grouping","library-status"}){auto r=gRmlSettingsPage.bounds(id);require(r.right>r.left&&r.bottom>r.top,"library UI control has no layout");}
        selectLast("library-navigation");require(gLibrarySources.originalFolders,"original mode selection event did not apply");
        selectLast("library-grouping");require(gLibrarySources.individualCharts&&gSongSelection.groups().size()==3,"individual mode selection event did not apply");
        selectLast("library-grouping");require(gLibrarySources.collectVariants&&!gLibrarySources.individualCharts&&gSongSelection.groups().size()==2,"collect mode event did not rebuild groups");
        bms::SongLibrarySources collectedSettings;collectedSettings.initialize(primary,gSettingsFile);require(collectedSettings.collectVariants&&!collectedSettings.individualCharts,"collection preference did not persist");
        gLibrarySources.individualCharts=true;gLibrarySources.collectVariants=false;applyLibraryView();syncLibrarySettings();
        gRmlSettingsPage.focus("library-refresh");paint();
        paint(output/L"library-management.png");
        // A completed worker must not replace charts under an open settings UI.
        startLibraryValidation();for(int i=0;i<1000&&gSongIndexWorker.running();++i){pump();Sleep(2);}require(gSongIndexWorker.hasPending(),"settings did not defer scan publication");
        SendMessageW(gSettingsWindow,WM_CLOSE,0,0);SendMessageW(gWindow,WM_LIBRARY_INDEX,0,0);waitIndex();
        require(gCharts.size()==3&&gLibrarySources.originalFolders&&gLibrarySources.individualCharts,"background refresh lost view/state");
        gLibrarySourceSelection=1;SendMessageW(gWindow,WM_LIBRARY_ACTION,2,0);waitIndex();require(gCharts.size()==2&&std::filesystem::exists(external/L"external.bms"),"unregister lost file or retained chart");
        drop(external);waitIndex();require(gLibrarySources.roots.size()==2&&gCharts.size()==3,"Explorer folder drop did not register/index");
        drop(external/L"external.bms");waitIndex();require(gLibrarySources.roots.size()==2&&gCharts.size()==3,"chart drop duplicated registered directory");
        {std::ofstream f(output/L"unsupported.txt");f<<"not BMS";}drop(output/L"unsupported.txt");require(gLibrarySources.roots.size()==2,"unsupported drop registered a library");
        gSongFolders.restore(external,gCharts);rebuildSongRows(restoreLibraryChart(external/L"external.bms"));
        std::filesystem::rename(external/L"external.bms",external/L"external.hold");
        beginSelectedChart(false);require(!gLoading,"stale cached chart entered playback loading");
        std::filesystem::rename(external/L"external.hold",external/L"external.bms");
        startLibraryValidation();gSongFolders.restore(primary/L"Event",gCharts);rebuildSongRows();waitIndex();
        require(gSongFolders.location()==primary/L"Event","background publication lost navigation performed during scan");
        bms::SongLibrarySources restored;restored.initialize(primary,gSettingsFile);require(restored.roots.size()==2&&restored.originalFolders&&restored.individualCharts,"UI settings failed restart roundtrip");
        while(!gLibrarySources.roots.empty()){gLibrarySourceSelection=0;SendMessageW(gWindow,WM_LIBRARY_ACTION,2,0);waitIndex();}
        require(gCharts.empty()&&ListView_GetItemCount(gList)==0,"last source removal exposed cached songs");
        bms::SongLibrarySources noSources;noSources.initialize(primary,gSettingsFile);require(noSources.roots.empty(),"empty state did not persist");
        require(std::filesystem::exists(primary/L"Event"/L"Song A"/L"normal.bms"),"last source removal deleted files");
        std::cout<<"Library UI: real RML mode events, worker deferral/publication, unregister, Explorer folder/chart drops and persisted settings passed.\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    gSongIndexWorker.cancel();if(gSettingsWindow)SendMessageW(gSettingsWindow,WM_CLOSE,0,0);if(gWindow)DestroyWindow(gWindow);
    CoUninitialize();Gdiplus::GdiplusShutdown(gGdiPlusToken);return result;
}
