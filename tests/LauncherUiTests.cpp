#include "../src/Launcher.cpp"
#include <iostream>
#include <objidl.h>
#include <gdiplus.h>
void require(bool condition,const char* error){if(!condition)throw std::runtime_error(error);}
void pump(){MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE))if(msg.message!=WM_QUIT){TranslateMessage(&msg);DispatchMessageW(&msg);}}
void confirmTestClose(DWORD pid){EnumWindows([](HWND h,LPARAM pid)->BOOL{DWORD owner{};GetWindowThreadProcessId(h,&owner);wchar_t type[64]{};GetClassNameW(h,type,64);if(owner==DWORD(pid)&&std::wstring(type)==L"#32770"&&GetDlgItem(h,IDYES))PostMessageW(h,WM_COMMAND,IDYES,0);return TRUE;},pid);}
int wmain(int argc,wchar_t** argv){if(argc!=3)return 2;HANDLE child{};DWORD childPid=0;try{
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    base=std::filesystem::absolute(argv[1])/(L"런처 日本語 "+std::to_wstring(GetCurrentProcessId()));std::filesystem::create_directories(base/L"Songs");
    const auto release=std::filesystem::absolute(argv[2]);
    std::filesystem::copy_file(release/L"SevenMania.exe",base/L"SevenMania.exe");
    std::filesystem::copy(release/L"Assets",base/L"Assets",std::filesystem::copy_options::recursive);
    config=base/L"settings.ini";SetEnvironmentVariableW(L"BMS_PLAYER_CONFIG_PATH",config.c_str());
    WritePrivateProfileStringW(L"Sound",L"SongPreview",L"0",config.c_str());WritePrivateProfileStringW(L"Unknown",L"Keep",L"yes",config.c_str());
    {std::ofstream f(base/L"Songs"/L"test.bms");f<<"#PLAYER 1\n#TITLE launcher fixture\n#BPM 120\n#00011:01\n";}
    initial=sevenmania::readStartupSettings(config);sources.initialize(base/L"Songs",config);originalRoots=sources.roots;require(sources.roots.empty(),"launcher automatically registered Songs");
    INITCOMMONCONTROLSEX init{sizeof(init),ICC_TAB_CLASSES};InitCommonControlsEx(&init);WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"LauncherTest";RegisterClassW(&cls);
    window=CreateWindowExW(0,cls.lpszClassName,L"Launcher test",WS_OVERLAPPEDWINDOW,0,0,950,700,nullptr,nullptr,cls.hInstance,nullptr);require(window,"launcher native window failed");
    wchar_t emptyLabel[128]{};SendMessageW(rootList,LB_GETTEXT,0,reinterpret_cast<LPARAM>(emptyLabel));require(std::wstring(emptyLabel)==L"(없음)","launcher empty default label missing");
    select(fpsChoice,0);select(modeChoice,0);select(resolutionChoice,0);SendMessageW(bgaCheck,BM_SETCHECK,BST_UNCHECKED,0);select(bufferChoice,4);
    SendMessageW(window,WM_COMMAND,41,0);auto read=sevenmania::readStartupSettings(config);require(read.fps==60&&read.brightness==0&&read.buffer==512,"GUI changes not persisted to engine schema");
    WritePrivateProfileStringW(L"Sound",L"BufferFrames",L"1024",config.c_str());
    require(save()&&selected(bufferChoice)==5,"untouched concurrent setting not merged/reflected in GUI");
    // Only dirty settings may be saved. Same-field changes by another launcher
    // must be detected, rather than silently overwritten.
    select(fpsChoice,1);WritePrivateProfileStringW(L"Display",L"FPS",L"400",config.c_str());require(!save(),"same-key conflict overwrote another window");
    initial=sevenmania::readStartupSettings(config);select(fpsChoice,frameRateIndex(initial.fps));captureBaseline();
    sources.add(base/L"Songs"); // Explicit registration, never a startup default.
    auto external=base/L"外部 曲";std::filesystem::create_directory(external);{std::ofstream f(external/L"other.bms");f<<"#TITLE external\n#BPM 120\n#00011:01\n";}
    sources.add(external);require(save(),"external root save failed");refreshRoots();SendMessageW(window,WM_COMMAND,34,0);
    for(int i=0;i<3000&&scanning;++i){pump();Sleep(2);}pump();require(!scanning,"launcher indexing did not complete");
    require(bms::snapshotSongLibraries(sources).size()==2,"launcher cache not reusable by game");
    SendMessageW(rootList,LB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,32,0);require(save()&&std::filesystem::exists(external/L"other.bms"),"unregister removed physical song");
    ULONG_PTR token{};Gdiplus::GdiplusStartupInput gdiplus;Gdiplus::GdiplusStartup(&token,&gdiplus,nullptr);
    {RECT rect{};GetWindowRect(window,&rect);HDC dc=CreateCompatibleDC(nullptr);HDC screen=GetDC(nullptr);HBITMAP bitmap=CreateCompatibleBitmap(screen,rect.right-rect.left,rect.bottom-rect.top);ReleaseDC(nullptr,screen);auto old=SelectObject(dc,bitmap);ShowWindow(window,SW_SHOWNOACTIVATE);UpdateWindow(window);PrintWindow(window,dc,0);GdiFlush();
    const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};{Gdiplus::Bitmap image(bitmap,nullptr);image.Save((base/L"launcher-video.png").c_str(),&png);}SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ShowWindow(window,SW_HIDE);}
    Gdiplus::GdiplusShutdown(token);
    SendMessageW(window,WM_COMMAND,42,0);require(gameProcess,"launcher did not create game process");childPid=GetProcessId(gameProcess);DuplicateHandle(GetCurrentProcess(),gameProcess,GetCurrentProcess(),&child,0,FALSE,DUPLICATE_SAME_ACCESS);
    WaitForInputIdle(child,10000);Sleep(1000);require(WaitForSingleObject(child,0)==WAIT_TIMEOUT,"game exited at startup");
    require(!save(),"launcher modified settings while game was running");
    SendMessageW(window,WM_CLOSE,0,0);require(!IsWindow(window),"launcher close confirmation was not removed");require(WaitForSingleObject(child,0)==WAIT_TIMEOUT,"closing launcher terminated game");
    std::cout<<"Native launcher Apply/conflict, Unicode paths, async cache reuse, unregister, actual SevenMania launch and launcher-independent child lifetime passed.\n";
    EnumWindows([](HWND h,LPARAM pid)->BOOL{DWORD owner{};GetWindowThreadProcessId(h,&owner);if(owner==DWORD(pid))PostMessageW(h,WM_CLOSE,0,0);return TRUE;},childPid);
    for(int i=0;i<500&&WaitForSingleObject(child,0)==WAIT_TIMEOUT;++i){confirmTestClose(childPid);Sleep(20);}
    require(WaitForSingleObject(child,0)==WAIT_OBJECT_0,"test game did not close normally");CloseHandle(child);child=nullptr;
    bms::SongLibrarySources cleared;cleared.initialize(base/L"Songs",config);while(!cleared.roots.empty())require(cleared.remove(0),"cannot clear final root");require(cleared.save(),"cannot persist empty roots");
    require(std::filesystem::exists(base/L"Songs"/L"test.bms"),"unregister deleted original chart");
    std::wstring command=L"\""+(base/L"SevenMania.exe").wstring()+L"\"";STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi{};
    require(CreateProcessW((base/L"SevenMania.exe").c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,base.c_str(),&si,&pi),"standalone empty game launch failed");CloseHandle(pi.hThread);child=pi.hProcess;childPid=pi.dwProcessId;
    WaitForInputIdle(child,10000);Sleep(1000);
    struct Probe{DWORD pid;HWND list{};};Probe probe{childPid};
    EnumWindows([](HWND h,LPARAM data)->BOOL{auto& p=*reinterpret_cast<Probe*>(data);DWORD pid{};GetWindowThreadProcessId(h,&pid);if(pid==p.pid){p.list=FindWindowExW(h,nullptr,L"SysListView32",nullptr);if(p.list)return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&probe));
    const bool emptyGame=WaitForSingleObject(child,0)==WAIT_TIMEOUT&&probe.list&&SendMessageW(probe.list,LVM_GETITEMCOUNT,0,0)==0;
    EnumWindows([](HWND h,LPARAM pid)->BOOL{DWORD owner{};GetWindowThreadProcessId(h,&owner);if(owner==DWORD(pid))PostMessageW(h,WM_CLOSE,0,0);return TRUE;},childPid);
    for(int i=0;i<500&&WaitForSingleObject(child,0)==WAIT_TIMEOUT;++i){confirmTestClose(childPid);Sleep(20);}require(WaitForSingleObject(child,0)==WAIT_OBJECT_0,"empty game did not close");CloseHandle(child);child=nullptr;
    require(emptyGame,"game automatically loaded existing Songs after all roots were removed");std::cout<<"Standalone empty-library game: zero song rows despite an existing Songs chart; final root removal and original file preservation passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';if(childPid)EnumWindows([](HWND h,LPARAM pid)->BOOL{DWORD owner{};GetWindowThreadProcessId(h,&owner);if(owner==DWORD(pid))PostMessageW(h,WM_CLOSE,0,0);return TRUE;},childPid);if(child)CloseHandle(child);if(window&&IsWindow(window))DestroyWindow(window);return 1;}}
