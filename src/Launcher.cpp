#include "SharedStartupSettings.h"
#include "skin/SkinPackage.h"
#include "UserBackground.h"
#include <wrl/client.h>
#include "SongLibrarySources.h"
#include "SongScanner.h"
#include "SongImportDialog.h"
#include <commctrl.h>
#include <shellapi.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <set>
#include <tuple>

namespace {
std::filesystem::path base,config;HWND window,tab,statusText,startButton;
HFONT launcherFont;HANDLE gameProcess{};std::thread worker;std::atomic_bool cancelScan{},scanning{};
std::mutex resultMutex;std::wstring resultText;
bms::SongLibrarySources sources;std::vector<std::filesystem::path> originalRoots;
struct Monitor{std::wstring name,label;bool primary{};};std::vector<Monitor> monitors;
std::vector<std::pair<int,int>> resolutions;
struct Field{HWND control;std::wstring section,key,baseline;};std::vector<Field> fields;
std::vector<std::pair<HWND,int>> pages;HWND monitorChoice,resolutionChoice,modeChoice,fpsChoice,bgaCheck,bufferChoice,rootList;
sevenmania::StartupSettings initial;
std::wstring widthBaseline,heightBaseline;
constexpr UINT complete=WM_APP+100;
int scaled(int value){return MulDiv(value,int(GetDpiForSystem()),96);}
void scrollTo(int position){SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(window,SB_VERT,&info);int before=info.nPos;info.fMask=SIF_POS;info.nPos=position;SetScrollInfo(window,SB_VERT,&info,TRUE);GetScrollInfo(window,SB_VERT,&info);ScrollWindowEx(window,0,before-info.nPos,nullptr,nullptr,nullptr,nullptr,SW_SCROLLCHILDREN|SW_INVALIDATE|SW_ERASE);}

HWND control(const wchar_t* type,const wchar_t* text,int x,int y,int w,int h,int id,int page,DWORD style=0) {
    HWND c=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,scaled(x),scaled(y),scaled(w),scaled(h),window,reinterpret_cast<HMENU>(INT_PTR(id)),nullptr,nullptr);
    SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(launcherFont),TRUE);if(page>=0)pages.emplace_back(c,page);return c;
}
HWND choice(int x,int y,int id,int page){return control(L"COMBOBOX",L"",x,y,610,240,id,page,CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP);}
void add(HWND c,const std::wstring& text){SendMessageW(c,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
int selected(HWND c){return int(SendMessageW(c,CB_GETCURSEL,0,0));}
void select(HWND c,int i){SendMessageW(c,CB_SETCURSEL,i,0);}
void message(const std::wstring& text){SetWindowTextW(statusText,text.c_str());}
void showPage(){int n=TabCtrl_GetCurSel(tab);for(auto [c,p]:pages)ShowWindow(c,p==n?SW_SHOW:SW_HIDE);}
void label(const wchar_t* text,int y,int page){control(L"STATIC",text,35,y+4,215,24,0,page);}
#include "LauncherSkinSettings.inl"
void refreshRoots(){SendMessageW(rootList,LB_RESETCONTENT,0,0);if(sources.roots.empty())SendMessageW(rootList,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"(없음)"));EnableWindow(GetDlgItem(window,32),!sources.roots.empty());EnableWindow(GetDlgItem(window,33),!sources.roots.empty());for(size_t i=0;i<sources.roots.size();++i){std::error_code ec;auto text=sources.roots[i].wstring()+(std::filesystem::is_directory(sources.roots[i],ec)?L"":L" [연결 끊김]");SendMessageW(rootList,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}SendMessageW(rootList,LB_SETHORIZONTALEXTENT,4000,0);}
void refreshResolutions(int width=0,int height=0){
    resolutions={{0,0}};SendMessageW(resolutionChoice,CB_RESETCONTENT,0,0);add(resolutionChoice,L"자동 (모니터 기본값)");
    const int m=selected(monitorChoice);std::set<std::pair<int,int>> modes;
    if(m>=0&&m<int(monitors.size())) {
        DEVMODEW desktop{};desktop.dmSize=sizeof(desktop);
        if(EnumDisplaySettingsW(monitors[m].name.c_str(),ENUM_CURRENT_SETTINGS,&desktop))
            for(DWORD i=0;i<512;++i){DEVMODEW mode{};mode.dmSize=sizeof(mode);
                if(!EnumDisplaySettingsW(monitors[m].name.c_str(),i,&mode))break;
                if(mode.dmPelsWidth>0&&mode.dmPelsHeight>0&&mode.dmPelsWidth<=desktop.dmPelsWidth&&mode.dmPelsHeight<=desktop.dmPelsHeight)
                    modes.emplace(int(mode.dmPelsWidth),int(mode.dmPelsHeight));
            }
    }
    int index=0;for(auto [w,h]:modes){if(w==width&&h==height)index=int(resolutions.size());resolutions.emplace_back(w,h);add(resolutionChoice,std::to_wstring(w)+L" × "+std::to_wstring(h));}select(resolutionChoice,index);
}
void captureBaseline(){widthBaseline=sevenmania::iniText(config,L"Display",L"ResolutionWidth");heightBaseline=sevenmania::iniText(config,L"Display",L"ResolutionHeight");fields.clear();for(auto [c,section,key]:{std::tuple{monitorChoice,L"Display",L"Monitor"},std::tuple{modeChoice,L"Display",L"ScreenMode"},std::tuple{fpsChoice,L"Display",L"FPS"},std::tuple{bgaCheck,L"Display",L"Brightness"},std::tuple{bufferChoice,L"Sound",L"BufferFrames"}})fields.push_back({c,section,key,sevenmania::iniText(config,section,key)});}
bool save(){
    if(scanning){message(L"인덱싱이 끝나거나 취소된 뒤 적용하세요.");return false;}
    struct Change{std::wstring section,key,expected,value;};std::vector<Change> changes;
    auto change=[&](const wchar_t* section,const wchar_t* key,std::wstring value,bool dirty){if(dirty)changes.push_back({section,key,std::wstring(key)==L"ResolutionWidth"?widthBaseline:heightBaseline,std::move(value)});};
    // Baselines represent what this window originally read. Same-key conflicts
    // reject the transaction; untouched keys always come from the latest file.
    auto field=[&](HWND c,std::wstring value,bool dirty){if(!dirty)return;for(const auto& f:fields)if(f.control==c)changes.push_back({f.section,f.key,f.baseline,std::move(value)});};
    int m=selected(monitorChoice);auto monitor=m>=0&&m<int(monitors.size())?monitors[m].name:L"";
    field(monitorChoice,monitor,monitor!=fields[0].baseline);
    field(modeChoice,std::to_wstring(selected(modeChoice)),selected(modeChoice)!=initial.mode);
    int fps=frameRateValues[std::clamp(selected(fpsChoice),0,int(frameRateValues.size())-1)];if(fps==-2){BOOL valid;fps=GetDlgItemInt(window,69,&valid,FALSE);if(!valid||fps<1||fps>1500){message(L"사용자 FPS는 1~1500입니다.");return false;}}field(fpsChoice,std::to_wstring(fps),fps!=initial.fps);
    const int brightness=SendMessageW(bgaCheck,BM_GETCHECK,0,0)==BST_CHECKED?(initial.brightness?initial.brightness:50):0;
    field(bgaCheck,std::to_wstring(brightness),brightness!=initial.brightness);
    int buffer=sevenmania::startupBuffers[std::clamp(selected(bufferChoice),0,5)];field(bufferChoice,std::to_wstring(buffer),buffer!=initial.buffer);
    auto resolution=resolutions[std::clamp(selected(resolutionChoice),0,int(resolutions.size())-1)];
    change(L"Display",L"ResolutionWidth",std::to_wstring(resolution.first),resolution.first!=initial.width||resolution.second!=initial.height);
    change(L"Display",L"ResolutionHeight",std::to_wstring(resolution.second),resolution.first!=initial.width||resolution.second!=initial.height);
    for(const auto& f:skinFields){wchar_t text[32768]{};GetWindowTextW(f.control,text,32768);std::wstring value=f.choice?std::to_wstring(selected(f.control)):text;
        if(!f.path&&std::wstring(f.key)!=L"CustomBackground"){try{size_t used;int n=std::stoi(value,&used);if(used!=value.size()||n<(std::wstring(f.key)==L"JudgementTextYOffset"?-256:0)||n>(std::wstring(f.key)==L"JudgementTextYOffset"?256:std::wstring(f.key)==L"Background"?3:100))throw 0;}catch(...){message(L"스킨 설정의 숫자 범위를 확인하세요.");return false;}}
        if(f.path)value=value.empty()?L"":skinPathEncode(value);if(value!=f.baseline)changes.push_back({f.section,f.key,f.baseline,value});
    }
    bool ok=sevenmania::atomicSettingsUpdate(config,[&](const auto& temp){
        for(const auto& c:changes){if(sevenmania::iniText(temp,c.section.c_str(),c.key.c_str())!=c.expected)return false;if(!WritePrivateProfileStringW(c.section.c_str(),c.key.c_str(),c.value.c_str(),temp.c_str()))return false;}
        if(sources.roots!=originalRoots){bms::SongLibrarySources current;current.initialize(base/L"Songs",temp);if(current.roots!=originalRoots)return false;current.roots=sources.roots;if(!current.save())return false;}
        return WritePrivateProfileStringW(L"SevenMania",L"SettingsVersion",L"1",temp.c_str())!=FALSE;
    });
    if(!ok){message(L"저장 실패: 게임/다른 인덱싱이 실행 중이거나 같은 설정이 변경되었습니다. 게임 종료 후 런처를 다시 여세요.");return false;}
    const int oldRootIndex=int(SendMessageW(rootList,LB_GETCURSEL,0,0));
    const auto selectedRoot=oldRootIndex>=0&&oldRootIndex<int(sources.roots.size())?sources.roots[size_t(oldRootIndex)]:std::filesystem::path{};
    for(auto& f:skinFields)f.baseline=sevenmania::iniText(config,f.section,f.key);
    initial=sevenmania::readStartupSettings(config);sources.initialize(base/L"Songs",config);originalRoots=sources.roots;
    select(modeChoice,initial.mode);select(fpsChoice,frameRateIndex(initial.fps));
    for(size_t i=0;i<sevenmania::startupBuffers.size();++i)if(sevenmania::startupBuffers[i]==initial.buffer)select(bufferChoice,int(i));
    SendMessageW(bgaCheck,BM_SETCHECK,initial.brightness?BST_CHECKED:BST_UNCHECKED,0);
    const auto currentMonitor=sevenmania::iniText(config,L"Display",L"Monitor");for(size_t i=0;i<monitors.size();++i)if(monitors[i].name==currentMonitor)select(monitorChoice,int(i));
    refreshResolutions(initial.width,initial.height);refreshRoots();
    for(size_t i=0;i<sources.roots.size();++i)if(sources.roots[i]==selectedRoot)SendMessageW(rootList,LB_SETCURSEL,i,0);
    captureBaseline();message(L"적용했습니다. 게임을 시작하면 같은 설정을 사용합니다.");return true;
}
void scan(bool all){
    if(scanning){cancelScan=true;message(L"인덱싱 취소 중...");return;}if(!save())return;
    if(worker.joinable())worker.join();cancelScan=false;scanning=true;
    auto copy=sources;auto root=std::filesystem::path{};const int index=int(SendMessageW(rootList,LB_GETCURSEL,0,0));
    if(!all){if(index<0||index>=int(copy.roots.size())){scanning=false;message(L"재검색할 폴더를 선택하세요.");return;}root=copy.roots[index];}
    message(L"메타데이터 인덱싱 중 · 재검색 버튼을 다시 누르면 취소합니다.");
    worker=std::thread([copy,root]{std::wstring text;try{sevenmania::SettingsGate gate(config);if(!gate.acquire())text=L"게임 또는 다른 인덱싱이 실행 중입니다.";else{
        auto previous=root.empty()?std::vector<bms::Chart>{}:bms::snapshotSongLibraries(copy,&cancelScan);
        auto result=bms::scanSongLibraries(copy,&cancelScan,{},root,std::move(previous));
        size_t errors=result.warnings.size();for(const auto& c:result.charts)if(!c.playable)++errors;
        text=cancelScan?L"인덱싱을 취소했습니다.":L"완료: "+std::to_wstring(result.charts.size())+L"개 채보 · 오류/경고 "+std::to_wstring(errors)+L"개";
    }}catch(...){text=L"인덱싱 실패: 경로와 권한을 확인하세요.";}
    {std::lock_guard lock(resultMutex);resultText=text;}scanning=false;PostMessageW(window,complete,0,0);});
}
void launch(){
    if(gameProcess&&WaitForSingleObject(gameProcess,0)==WAIT_TIMEOUT){message(L"이 런처에서 시작한 게임이 실행 중입니다.");return;}
    if(!save())return;const auto exe=base/L"SevenMania.exe";
    std::wstring command=L"\""+exe.wstring()+L"\"";STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,base.c_str(),&startup,&process)){message(L"게임 실행 실패 · SevenMania.exe와 배포 파일을 확인하세요.");return;}
    CloseHandle(process.hThread);if(gameProcess)CloseHandle(gameProcess);gameProcess=process.hProcess;message(L"게임을 실행했습니다. 런처를 닫아도 게임은 계속 실행됩니다.");
}
LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l){switch(msg){
case WM_CREATE:{window=hwnd;launcherFont=CreateFontW(-scaled(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    tab=control(WC_TABCONTROLW,L"",15,15,900,490,1,-1,WS_TABSTOP);int i=0;for(auto text:{L"비디오",L"오디오",L"스킨",L"곡 라이브러리",L"고급"}){TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(text);TabCtrl_InsertItem(tab,i++,&item);}
    label(L"모니터",65,0);monitorChoice=choice(255,60,10,0);
    DISPLAY_DEVICEW device{};for(DWORD n=0;;++n){device.cb=sizeof(device);if(!EnumDisplayDevicesW(nullptr,n,&device,0))break;if(device.StateFlags&DISPLAY_DEVICE_ATTACHED_TO_DESKTOP){monitors.push_back({device.DeviceName,device.DeviceString,(device.StateFlags&DISPLAY_DEVICE_PRIMARY_DEVICE)!=0});add(monitorChoice,std::wstring(device.DeviceName)+L" · "+device.DeviceString);}}
    int monitorIndex=0;for(size_t n=0;n<monitors.size();++n)if(monitors[n].primary)monitorIndex=int(n);auto wanted=sevenmania::iniText(config,L"Display",L"Monitor");for(size_t n=0;n<monitors.size();++n)if(monitors[n].name==wanted)monitorIndex=int(n);select(monitorChoice,monitorIndex);
    label(L"해상도",115,0);resolutionChoice=choice(255,110,11,0);refreshResolutions(initial.width,initial.height);
    label(L"화면 모드",165,0);modeChoice=choice(255,160,12,0);for(auto text:{L"창 모드",L"테두리 없는 전체 창",L"전체화면 (기존 엔진 방식)"})add(modeChoice,text);select(modeChoice,initial.mode);
    label(L"최대 FPS / VSync",215,0);fpsChoice=choice(255,210,13,0);for(auto text:frameRateLabels)add(fpsChoice,text);select(fpsChoice,frameRateIndex(initial.fps));
    bgaCheck=control(L"BUTTON",L"BGA 출력",255,265,400,30,14,0,BS_AUTOCHECKBOX|WS_TABSTOP);SendMessageW(bgaCheck,BM_SETCHECK,initial.brightness?BST_CHECKED:BST_UNCHECKED,0);
    control(L"STATIC",L"VSync는 기존 FPS 모드와 함께 선택합니다. 무제한은 최대 1500FPS입니다.\n이 값은 현재 게임의 플레이 제한이며, 화면별 FPS는 아직 지원하지 않습니다.",35,330,820,65,0,0);
    label(L"사용자 FPS (1~1500)",405,0);customFps=control(L"EDIT",std::to_wstring(initial.fps>0?initial.fps:1000).c_str(),255,405,250,28,69,0,WS_BORDER|ES_NUMBER|WS_TABSTOP);
    label(L"오디오 버퍼 (frames)",75,1);bufferChoice=choice(255,70,20,1);i=0;for(auto value:sevenmania::startupBuffers){add(bufferChoice,std::to_wstring(value));if(value==initial.buffer)select(bufferChoice,i);++i;}
    control(L"STATIC",L"현재 엔진: WASAPI 공유 모드 / Windows 기본 출력 장치\n장치 선택과 별도 오디오 API 선택은 이 버전에서 제공하지 않습니다.",35,135,820,80,0,1);
    createSkinSettings();
    rootList=control(L"LISTBOX",L"",35,65,830,270,30,3,LBS_NOTIFY|WS_BORDER|WS_VSCROLL|WS_HSCROLL|WS_TABSTOP);
    int x=35;for(auto [text,id]:{std::pair{L"폴더 추가",31},{L"등록 해제",32},{L"선택 재검색 / 취소",33},{L"전체 갱신 / 취소",34}}){control(L"BUTTON",text,x,355,195,35,id,3,WS_TABSTOP);x+=210;}refreshRoots();
    control(L"STATIC",L"외부 파일을 복사하지 않습니다. 메타데이터 캐시는 게임과 공유합니다.\n등록 해제해도 실제 파일과 플레이 기록은 삭제하지 않습니다.",35,410,830,60,0,3);
    control(L"STATIC",(L"공유 설정 파일:\n"+config.wstring()+L"\n\n게임 실행 중에는 설정 저장/캐시 갱신을 차단합니다.\n같은 설정을 다른 런처에서 바꾸면 저장을 거부합니다.\n변경 사항은 적용 또는 게임 시작을 누를 때 저장합니다.").c_str(),35,75,820,260,0,4);
    statusText=control(L"STATIC",L"준비됨",25,520,870,55,0,-1);
    control(L"BUTTON",L"비디오/버퍼 기본값",25,595,210,40,40,-1,WS_TABSTOP);control(L"BUTTON",L"적용",600,595,140,40,41,-1,WS_TABSTOP);startButton=control(L"BUTTON",L"게임 시작",755,595,145,40,42,-1,WS_TABSTOP);
    captureBaseline();showPage();return 0;}
case WM_SIZE:{SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE};info.nMax=scaled(660)-1;info.nPage=HIWORD(l);SetScrollInfo(hwnd,SB_VERT,&info,TRUE);return 0;}
case WM_VSCROLL:{SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(hwnd,SB_VERT,&info);int position=info.nPos;switch(LOWORD(w)){case SB_LINEUP:position-=scaled(30);break;case SB_LINEDOWN:position+=scaled(30);break;case SB_PAGEUP:position-=info.nPage;break;case SB_PAGEDOWN:position+=info.nPage;break;case SB_THUMBTRACK:position=info.nTrackPos;break;}scrollTo(position);return 0;}
case WM_MOUSEWHEEL:{SCROLLINFO info{sizeof(info),SIF_POS};GetScrollInfo(hwnd,SB_VERT,&info);scrollTo(info.nPos-GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA*scaled(60));return 0;}
case WM_NOTIFY:if(reinterpret_cast<NMHDR*>(l)->hwndFrom==tab){showPage();return 0;}break;
case WM_COMMAND:{int id=LOWORD(w);if(id==61||id==68)pickSkinAsset(id==61);if(id==62)SetWindowTextW(GetDlgItem(window,60),L"");if(id==10&&HIWORD(w)==CBN_SELCHANGE)refreshResolutions();
    if(id==31&&!scanning){std::vector<std::filesystem::path> paths;if(bms::ui::chooseSongSources(window,base,paths)){for(auto path:paths){std::error_code ec;if(!std::filesystem::is_directory(path,ec))path=path.parent_path();sources.add(path);}refreshRoots();}}
    if(id==32&&!scanning){int index=int(SendMessageW(rootList,LB_GETCURSEL,0,0));if(index>=0)sources.remove(size_t(index));refreshRoots();}
    if(id==33||id==34)scan(id==34);
    if(id==40){select(modeChoice,0);select(fpsChoice,2);select(bufferChoice,3);select(resolutionChoice,0);SendMessageW(bgaCheck,BM_SETCHECK,BST_CHECKED,0);}
    if(id==41)save();if(id==42)launch();return 0;}
case complete:{if(scanning)return 0;if(worker.joinable())worker.join();std::lock_guard lock(resultMutex);message(resultText);return 0;}
case WM_CLOSE:DestroyWindow(hwnd);return 0;
case WM_DESTROY:cancelScan=true;if(worker.joinable())worker.join();if(gameProcess)CloseHandle(gameProcess);if(launcherFont)DeleteObject(launcherFont);PostQuitMessage(0);return 0;
}return DefWindowProcW(hwnd,msg,w,l);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
    try{SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);base=std::filesystem::path(path).parent_path();config=sevenmania::settingsPath();initial=sevenmania::readStartupSettings(config);sources.initialize(base/L"Songs",config);originalRoots=sources.roots;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_TAB_CLASSES};InitCommonControlsEx(&controls);WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=instance;cls.lpszClassName=L"SevenManiaLauncher";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClassW(&cls);
    auto hwnd=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"SEVENMANIA LAUNCHER",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_VSCROLL,CW_USEDEFAULT,CW_USEDEFAULT,scaled(950),std::min(scaled(700),GetSystemMetrics(SM_CYSCREEN)-50),nullptr,nullptr,instance,nullptr);if(!hwnd)return 1;ShowWindow(hwnd,show);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0)if(!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}CoUninitialize();return int(msg.wParam);
    }catch(...){MessageBoxW(nullptr,L"런처 초기화 실패: 설정 경로와 접근 권한을 확인하세요.",L"SevenMania Launcher",MB_ICONERROR);return 1;}
}
