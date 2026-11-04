#include "../src/Main.cpp"
#include <iostream>
#include <stdexcept>
#include <fstream>

namespace {
unsigned checks=0;
void require(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
void type(const wchar_t* name,WNDPROC proc){WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=name;c.lpfnWndProc=proc;require(RegisterClassW(&c)!=0,"window class registration failed");}
void dispatchRestarts(){MSG m{};unsigned count=0;while(PeekMessageW(&m,gWindow,WM_ACCURACY_RESTART,WM_ACCURACY_RESTART,PM_REMOVE)){require(++count<10,"restart message loop");DispatchMessageW(&m);}}
void policy(){
    for(int setting=0;setting<5;++setting)for(int grade=0;grade<5;++grade)
        require(belowAccuracyLimit(static_cast<AccuracyLimit>(setting),static_cast<TimingJudgement>(grade))==(setting>0&&grade>=setting),"accuracy threshold or equality incorrect");
    require(!belowAccuracyLimit(AccuracyLimit::None,TimingJudgement::Poor),"disabled limit active");
}
void settings(){
    openSettings();require(gSettingsWindow!=nullptr,"settings did not open");
    require(SendMessageW(gAccuracyLimitChoice,CB_GETCOUNT,0,0)==5&&SendMessageW(gSummaryAccuracyLimit,CB_GETCOUNT,0,0)==5,"native tabs lack accuracy options");
    for(int value=0;value<5;++value){
        require(postSetting(ID_SUMMARY_ACCURACY_LIMIT,value),"summary setting rejected");
        SendMessageW(gWindow,WM_APPLY_SETTINGS,0,0);
        require(int(gAccuracyLimit)==value&&GetPrivateProfileIntW(L"Gameplay",L"AccuracyLimit",99,gSettingsFile.c_str())==static_cast<UINT>(value),"accuracy setting not persisted");
        selectSettingsCategory(0);syncRmlSettingsState();
        require(gRmlSettingsPage.value("game-accuracy")==std::to_string(value),"RML gameplay differs from summary");
        selectSettingsCategory(4);syncRmlSettingsState();
        require(gRmlSettingsPage.value("summary-accuracy")==std::to_string(value),"RML summary differs from gameplay");
    }
    SendMessageW(gSettingsWindow,WM_RML_SETTINGS_NAVIGATE,0,1);
    require(applyNativeSetting(ID_ACCURACY_LIMIT,2),"native accuracy selection failed");
    require(SendMessageW(gSummaryAccuracyLimit,CB_GETCURSEL,0,0)==2,"native summary not refreshed");
    gAccuracyLimit=AccuracyLimit::None;loadDisplaySettings();require(gAccuracyLimit==AccuracyLimit::BelowGreat,"saved accuracy limit not reloaded");
    SendMessageW(gSettingsWindow,WM_CLOSE,0,0);
    require(reservedBinding(VK_F5),"F5 can be assigned to a lane");
}
void playback(){
    bms::Chart chart;chart.title=L"Accuracy fixture";chart.path=L"accuracy-fixture.bms";chart.mode=bms::ChartMode::SevenKey;chart.playable=true;chart.initialBpm=120;chart.durationSeconds=8;
    const auto silent=gSettingsFile.parent_path()/L"silence.wav";
    {
        std::ofstream file(silent,std::ios::binary);
        const auto u16=[&](unsigned v){file.put(char(v));file.put(char(v>>8));};
        const auto u32=[&](unsigned v){u16(v&65535);u16(v>>16);};
        file.write("RIFF",4);u32(36+8820);file.write("WAVEfmt ",8);u32(16);u16(1);u16(1);
        u32(44100);u32(88200);u16(2);u16(16);file.write("data",4);u32(8820);
        const std::array<char,8820> zeros{};file.write(zeros.data(),zeros.size());
    }
    chart.samples[L"01"]=silent;
    bms::Note note;note.lane=1;note.timeSeconds=1;chart.notes.push_back(note);note.timeSeconds=3;chart.notes.push_back(note);
    gCharts={chart};gActiveChartIndex=0;gPlaying=true;gPaused=false;gShowingResult=false;gLoading=false;gStartupSplash=false;gPractice.enabled=false;
    gAccuracyLimit=AccuracyLimit::None;std::wstring error;
    require(gAudio.prepare(chart,error),"silent chart audio prepare failed");
    const bool started=gAudio.startAt(0,error);if(!started)std::wcerr<<error<<L'\n';
    require(started,"test audio output unavailable");
    restartPlayback();require(gAudio.isRunning()&&!gPaused&&gHit.size()==2,"active restart did not initialize playback");
    gHit[0]=true;gFeedback.addCombo(4);gResultScore.apply(TimingJudgement::PGreat);
    auto generation=gPlaybackGeneration;SendMessageW(gWindow,WM_KEYDOWN,VK_F5,0);
    require(gPlaybackGeneration!=generation&&!gHit[0]&&gFeedback.combo()==0&&gResultScore.value()==0,"F5 failed to reset active play");
    generation=gPlaybackGeneration;SendMessageW(gWindow,WM_KEYDOWN,VK_F5,LPARAM(1LL<<30));require(gPlaybackGeneration==generation,"held F5 repeatedly restarted");
    require(gAudio.pause(),"pause transport failed");gPaused=true;
    SendMessageW(gWindow,WM_KEYDOWN,VK_F5,0);require(!gPaused&&!gAudio.isPaused(),"paused F5 did not restart");
    gAccuracyLimit=AccuracyLimit::BelowPGreat;
    require(gAudio.startAt(1.04,error),"input test transport seek failed");
    SendMessageW(gWindow,WM_KEYDOWN,laneKey(1),LPARAM(1LL<<30));require(!gHit[0]&&!gAccuracyRestartPending,"held lane key replayed after restart");
    pressLane(1);require(gHit[0]&&gAccuracyRestartPending,"actual off-perfect note input bypassed limit");dispatchRestarts();
    gAccuracyLimit=AccuracyLimit::BelowBad;require(gAudio.startAt(1.5,error),"miss test transport seek failed");
    updatePlaybackFrame();require(gHit[0]&&gAccuracyRestartPending,"actual missed note bypassed limit");dispatchRestarts();
    gAccuracyLimit=AccuracyLimit::BelowPGreat;
    showTimingJudgement(TimingJudgement::PGreat);require(!gAccuracyRestartPending,"PGREAT rejected itself");
    gHit[0]=true;generation=gPlaybackGeneration;
    showTimingJudgement(TimingJudgement::Great);
    require(gAccuracyRestartPending&&gPlaybackGeneration==generation&&gHit[0],"restart ran inside judgement iteration");
    showTimingJudgement(TimingJudgement::Poor);dispatchRestarts();
    require(!gAccuracyRestartPending&&gPlaybackGeneration==generation+1&&!gHit[0]&&gResultScore.value()==0,"queued auto restart did not reset state once");
    gAccuracyLimit=AccuracyLimit::BelowGreat;
    showTimingJudgement(TimingJudgement::Great);require(!gAccuracyRestartPending,"GREAT rejected equality");
    showTimingJudgement(TimingJudgement::Good);require(gAccuracyRestartPending,"GOOD did not trigger below GREAT");dispatchRestarts();
    gAccuracyLimit=AccuracyLimit::BelowBad;showTimingJudgement(TimingJudgement::Bad);require(!gAccuracyRestartPending,"BAD rejected equality");
    showTimingJudgement(TimingJudgement::Poor);require(gAccuracyRestartPending,"POOR did not trigger below BAD");dispatchRestarts();
    gAccuracyLimit=AccuracyLimit::BelowPGreat;showTimingJudgement(TimingJudgement::Great,0);require(gAccuracyRestartPending,"non-scoring hold head bypassed limit");dispatchRestarts();
    gAccuracyLimit=AccuracyLimit::BelowGood;showHoldJudgement(HoldResult::Bad);require(gAccuracyRestartPending,"hold failure bypassed limit");dispatchRestarts();
    gGrooveGaugeMode=GrooveGaugeMode::Hard;gGrooveGauge.configure(2,GrooveGaugeMode::Hard);gGrooveGauge.damage(99);
    showTimingJudgement(TimingJudgement::Poor);require(gAccuracyRestartPending&&gPlaying&&!gShowingResult,"gauge failure beat automatic restart");dispatchRestarts();
    showTimingJudgement(TimingJudgement::Poor);generation=gPlaybackGeneration;clearAccuracyRestart();gAccuracyLimit=AccuracyLimit::None;
    SendMessageW(gWindow,WM_ACCURACY_RESTART,static_cast<WPARAM>(generation),0);require(gPlaybackGeneration==generation+1,"stale restart affected a later run");dispatchRestarts();
    gGrooveGaugeMode=GrooveGaugeMode::Normal;gGrooveGauge.configure(2,GrooveGaugeMode::Normal);
    showTimingJudgement(TimingJudgement::Poor);require(!gAccuracyRestartPending,"disabled accuracy limit restarted");
    gAccuracyLimit=AccuracyLimit::BelowPGreat;gPractice={};gPractice.enabled=true;gPractice.startSeconds=2;gPractice.endSeconds=6;
    restartPlayback();require(gPlayhead==2&&gAudio.positionSeconds()>=2,"practice restart lost selected start");
    updatePlaybackFrame();require(!gAccuracyRestartPending&&gHit[0]&&gResultScore.value()==0,"notes before practice range triggered auto restart");
    gPractice.enabled=false;gAccuracyLimit=AccuracyLimit::None;
    gAudio.stop();gPlaying=false;
}
}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;const auto folder=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(folder);
    Gdiplus::GdiplusStartupInput gdiplus;Gdiplus::GdiplusStartup(&gGdiPlusToken,&gdiplus,nullptr);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);MFStartup(MF_VERSION);
    int result=0;
    try{
        policy();gSettingsFile=folder/(L"accuracy-"+std::to_wstring(GetCurrentProcessId())+L".ini");
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
        type(L"AccuracyOwner",windowProc);type(L"BmsPlayerSettingsWindow",settingsProc);type(L"BmsPlayerSettingsBody",settingsBodyProc);type(L"BmsPlayerSettingsPreviewOverlay",settingsPreviewOverlayProc);
        gWindow=CreateWindowW(L"AccuracyOwner",L"Accuracy restart tests",WS_OVERLAPPEDWINDOW,0,0,1280,860,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(gWindow!=nullptr,"owner creation failed");settings();playback();
        std::cout<<checks<<" accuracy/F5/settings/transport checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    gAudio.stop();if(IsWindow(gSettingsWindow))SendMessageW(gSettingsWindow,WM_CLOSE,0,0);if(IsWindow(gWindow))DestroyWindow(gWindow);gWindow=nullptr;
    MFShutdown();CoUninitialize();Gdiplus::GdiplusShutdown(gGdiPlusToken);return result;
}
