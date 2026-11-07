// Replays real application selection/loading/paint/exit paths; no production hooks.
#include "../src/Main.cpp"
#include <tlhelp32.h>
#include <psapi.h>
#include <fstream>
#include <iostream>

namespace {
using Clock=std::chrono::steady_clock;
std::filesystem::path output;
std::vector<std::filesystem::path> paths;
std::ofstream csv;
Clock::time_point began=Clock::now(),due=began;
int rounds=3,roundNo=0,step=0,state=0,result=1,warm=0;
bool busy=false,initialized=false;
unsigned enters=0,exits=0,songs=0,variants=0;
const char* phase="startup";
unsigned threads(){
    unsigned count=0;const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if(snapshot==INVALID_HANDLE_VALUE)throw std::runtime_error("thread snapshot failed");
    THREADENTRY32 entry{};entry.dwSize=sizeof(entry);
    if(Thread32First(snapshot,&entry))do{if(entry.th32OwnerProcessID==GetCurrentProcessId())++count;}while(Thread32Next(snapshot,&entry));
    CloseHandle(snapshot);return count;
}
void sample(const char* name,int operation){
    const auto threadCount=threads();const auto audio=gAudio.decodedBytes();
    DWORD handles{};GetProcessHandleCount(GetCurrentProcess(),&handles);
    PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
    if(!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)))throw std::runtime_error("memory query failed");
    csv<<roundNo<<','<<name<<','<<operation<<','<<std::chrono::duration<double>(Clock::now()-began).count()<<','<<memory.PrivateUsage<<','<<memory.WorkingSetSize<<','<<memory.PeakWorkingSetSize<<','<<handles<<','<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<','<<GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)<<','<<threadCount<<','<<audio<<','<<gBgaVideos.residentPlayers()<<','<<selectedIndex()<<','<<gPlaying<<','<<gGpuUnavailable<<','<<gPaused<<'\n';csv.flush();
    if(operation%10==0||operation==1)std::cout<<"round="<<roundNo<<" phase="<<name<<" n="<<operation<<" private_MiB="<<memory.PrivateUsage/1048576.0<<" handles="<<handles<<" threads="<<threadCount<<std::endl;
}
void selectRow(int row){
    ListView_SetItemState(gList,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
    ListView_SetItemState(gList,row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    if(!selectSongGroupFromListRow(row))throw std::runtime_error("song selection failed");
    updateSelection();InvalidateRect(gWindow,nullptr,FALSE);UpdateWindow(gWindow);
}
void chooseChart(int iteration){
    selectRow((iteration/2)%2);
    const int wanted=iteration%2;
    const auto group=gSongSelection.selectedGroupIndex();
    if(gSongSelection.groups().at(group).selectedVariant!=size_t(wanted)&&!cycleSelectedVariant(wanted?1:-1))throw std::runtime_error("variant selection failed");
}
bool toggleVariant(){
    const auto group=gSongSelection.selectedGroupIndex();
    return group>=0&&cycleSelectedVariant(gSongSelection.groups().at(group).selectedVariant==0?1:-1);
}
void startPlay(int iteration){
    chooseChart(iteration);beginSelectedChart();
    if(!gLoading)throw std::runtime_error("loading did not start");
}
void CALLBACK tick(HWND,UINT,UINT_PTR timer,DWORD){
    if(busy||!gWindow||!gList)return;
    busy=true;
    try{
        if(Clock::now()-began>std::chrono::minutes(20))throw std::runtime_error("20-minute timeout");
        if(Clock::now()<due){busy=false;return;}
        if(!initialized){
            if(gLoading){busy=false;return;}
            stopBrowseMedia();gSongPreview.clearCache();gCharts.clear();
            for(const auto& path:paths){auto parsed=bms::BmsParser(false,0x20261030).parse(path);if(!parsed.chart.playable)throw std::runtime_error("unplayable test chart");gCharts.push_back(std::move(parsed.chart));}
            buildSongGroups();rebuildSongRows(0);
            if(gSongSelection.groups().size()!=2)throw std::runtime_error("provide two song folders with two variants each");
            for(const auto& group:gSongSelection.groups())if(group.chartIndices.size()!=2)throw std::runtime_error("each song must have two variants");
            gTargetFps=60;gCompositionEnabled=true;gDirectCompositionEnabled=false;gReflexRequested=false;
            gVolumes[0]=0;gPreviewVolume=0;gAudio.setVolumes(0,100,100,0);
            SetWindowTextW(gWindow,L"SevenMania memory transition test");
            initialized=true;phase="warmup";state=1;warm=0;sample("startup",0);startPlay(warm);due=Clock::now()+std::chrono::milliseconds(100);busy=false;return;
        }
        switch(state){
        case 1: // Warm each chart's real loading/rendering path once.
            if(gLoading){due=Clock::now()+std::chrono::milliseconds(30);break;}
            if(!gPlaying)throw std::runtime_error("warmup playback failed");
            if(gPaused)resumePlayback();state=2;due=Clock::now()+std::chrono::milliseconds(600);break;
        case 2:
            showTimingJudgement(TimingJudgement::PGreat);showBrowseView();sample("warm_exit",warm+1);
            if(++warm<4){startPlay(warm);state=1;due=Clock::now()+std::chrono::milliseconds(50);}
            else {selectRow(0);state=3;due=Clock::now()+std::chrono::seconds(3);}break;
        case 3:
            roundNo=1;step=0;sample("baseline",0);state=4;phase="song";selectRow(1);due=Clock::now()+std::chrono::milliseconds(600);break;
        case 4:
            sample("song",++step);++songs;
            if(step<100){selectRow((step+1)%2);due=Clock::now()+std::chrono::milliseconds(600);}
            else {step=0;state=5;phase="variant";if(!toggleVariant())throw std::runtime_error("variant did not change");due=Clock::now()+std::chrono::milliseconds(200);}break;
        case 5:
            sample("variant",++step);++variants;
            if(step<50){if(!toggleVariant())throw std::runtime_error("variant did not change");due=Clock::now()+std::chrono::milliseconds(200);}
            else {step=0;state=6;phase="enter";startPlay(step);due=Clock::now()+std::chrono::milliseconds(50);}break;
        case 6:
            if(gLoading){due=Clock::now()+std::chrono::milliseconds(30);break;}
            if(!gPlaying)throw std::runtime_error("playback entry failed");
            if(gPaused)resumePlayback();state=7;due=Clock::now()+std::chrono::milliseconds(700);break;
        case 7:
            if(gPaused)resumePlayback();
            showTimingJudgement(TimingJudgement::PGreat);InvalidateRect(gWindow,nullptr,FALSE);UpdateWindow(gWindow);
            if(gGpuUnavailable)throw std::runtime_error("GPU fallback during memory test");
            sample("enter",step+1);++enters;showBrowseView();state=8;phase="exit";due=Clock::now()+std::chrono::milliseconds(700);break;
        case 8:
            if(gPlaying||gLoading)throw std::runtime_error("playback exit failed");
            sample("exit",++step);++exits;
            if(step<30){startPlay(step);state=6;phase="enter";due=Clock::now()+std::chrono::milliseconds(50);}
            else {state=9;phase="idle";due=Clock::now()+std::chrono::seconds(5);}break;
        case 9:
            sample("round_idle",30);
            if(roundNo<rounds){++roundNo;step=0;state=4;phase="song";selectRow(1);due=Clock::now()+std::chrono::milliseconds(600);}
            else {stopBrowseMedia();gSongPreview.pauseWarmup(true);gSongPreview.clearCache();state=10;phase="quiescent";due=Clock::now()+std::chrono::seconds(5);}break;
        case 10:
            sample("quiescent",0);
            if(songs!=unsigned(rounds*100)||variants!=unsigned(rounds*50)||enters!=unsigned(rounds*30)||exits!=unsigned(rounds*30))throw std::runtime_error("operation counts mismatch");
            std::cout<<"COMPLETE songs="<<songs<<" variants="<<variants<<" enters="<<enters<<" exits="<<exits<<std::endl;
            result=0;KillTimer(nullptr,timer);DestroyWindow(gWindow);break;
        }
    }catch(const std::exception& error){std::cerr<<"FAILED phase="<<phase<<": "<<error.what()<<std::endl;sample("failure",step);KillTimer(nullptr,timer);DestroyWindow(gWindow);}
    busy=false;
}
}
int wmain(int argc,wchar_t** argv){
    if(argc!=7){std::wcerr<<L"Usage: BmsMemoryTransitionTests output-directory rounds song1a song1b song2a song2b\n";return 2;}
    output=std::filesystem::absolute(argv[1]);rounds=std::stoi(argv[2]);if(rounds<1||rounds>10)return 2;
    std::filesystem::create_directories(output/L"EmptyLibrary");for(int i=3;i<7;++i)paths.push_back(std::filesystem::absolute(argv[i]));
    const auto config=output/L"memory-test.ini";
    WritePrivateProfileStringW(L"Library",L"LastFolder",(output/L"EmptyLibrary").c_str(),config.c_str());
    WritePrivateProfileStringW(L"Display",L"ScreenMode",L"0",config.c_str());
    WritePrivateProfileStringW(L"Display",L"ResolutionWidth",L"1280",config.c_str());
    WritePrivateProfileStringW(L"Display",L"ResolutionHeight",L"720",config.c_str());
    WritePrivateProfileStringW(L"Display",L"DirectComposition",L"0",config.c_str());
    SetEnvironmentVariableW(L"BMS_PLAYER_CONFIG_PATH",config.c_str());
    csv.open(output/L"samples.csv");csv<<"round,phase,operation,seconds,private_bytes,working_set_bytes,peak_working_set_bytes,handles,gdi,user,threads,audio_bytes,video_readers,chart,playing,gpu_fallback,paused\n";
    const auto timer=SetTimer(nullptr,0,30,tick);if(!timer)return 1;
    wWinMain(GetModuleHandleW(nullptr),nullptr,nullptr,SW_SHOWNOACTIVATE);KillTimer(nullptr,timer);
    sample("after_shutdown",0);csv.close();return result;
}
