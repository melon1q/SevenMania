#include "../src/ControllerDialog.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
int checks=0;
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
void mapping(){
    using namespace bms::controller;
    Config c;c.enabled=true;Mapper m;Sample s;s.connected=true;s.axes.fill(32767);
    auto read=[&](std::uint64_t time=100){return m.update(c,s,time,true);};
    require(!read().lanes[1],"initial neutral");
    for(int i=0;i<7;++i){s.buttons[i]=true;require(read().lanes[i+1],"key mapping");s.buttons[i]=false;require(!read().lanes[i+1],"key release");}
    s.buttons[7]=true;s.buttons[8]=true;require(read().lanes[0],"scratch buttons");s.buttons[7]=false;require(read().lanes[0],"scratch OR");s.buttons[8]=false;require(!read().lanes[0],"scratch release");
    c.axis=0;s.axes[0]=65500;m.reset();read();s.axes[0]=100;require(!read().scratchPulse,"wrapped small rotation");s.axes[0]=300;require(read().scratchPulse,"wrapped rotary threshold");require(read(179).lanes[0],"scratch hold");require(!read(180).lanes[0],"scratch timeout");
    s.axes[0]=100;read(200);s.axes[0]=65400;require(read(201).scratchPulse,"reverse wrap");
    m.reset();s.axes[0]=30000;read();s.axes[0]+=10;require(!read().scratchPulse,"small jitter");s.axes[0]-=10;require(!read().scratchPulse,"jitter cancellation");
    s.buttons[0]=true;require(read().lanes[1],"held before focus loss");require(!m.update(c,s,100,false).lanes[1],"focus release");require(!read().lanes[1],"focus held suppression");s.buttons[0]=false;read();s.buttons[0]=true;require(read().lanes[1],"focus rearmed");
    s.connected=false;require(!read().lanes[1],"disconnect release");s.connected=true;require(!read().lanes[1],"reconnect held suppression");
    c.mode=2;m.reset();s.axes[0]=60000;read();require(!read().lanes[0],"centered held suppression");s.axes[0]=32767;read();s.axes[0]=40000;require(read().lanes[0],"centered deflection");s.axes[0]=32767;require(!read().lanes[0],"centered release");
    c.menu=15;s.buttons[15]=false;read();s.buttons[15]=true;require(read().menuPressed,"menu edge");require(!read().menuPressed,"menu no repeat");
    c.keys[0]=128;c.axis=8;c.menu=128;read();require(true,"invalid mapping safe");
    c.enabled=false;require(!read().lanes[1],"disabled release");
    require(laneHeld(true,false)&&laneHeld(false,true)&&laneHeld(true,true)&&!laneHeld(false,false),"keyboard/controller union");
    for(const int interval:{0,1,4,16}){
        Config rotary;rotary.enabled=true;Mapper turntable;Sample input;input.connected=true;input.axes[0]=65500;
        std::uint64_t now=1000;turntable.update(rotary,input,now,true);
        int pulses=0,attacks=0;bool down=false;
        const auto tick=[&](){const auto state=turntable.update(rotary,input,now,true);pulses+=state.scratchPulse;
            if(state.lanes[0]&&(!down||state.scratchPulse))++attacks;down=state.lanes[0];return state;};
        // One full wrapped rotation, including multiple buffered events at one timestamp.
        for(int i=0;i<64;++i){now+=interval;input.axes[0]=(input.axes[0]+1024)%65536;require(tick().lanes[0],"continuous rotation lost hold");}
        require(pulses==1&&attacks==1,"one rotation repeatedly pressed scratch");
        now+=1;input.axes[0]=(input.axes[0]+65536-1024)%65536;require(tick().scratchPulse,"qualified reversal did not retrigger");
        require(attacks==2,"reversal emitted wrong number of attacks");
        for(int i=0;i<20;++i){now+=1;input.axes[0]=(input.axes[0]+65536-1024)%65536;require(!tick().scratchPulse,"continued reverse rotation retriggered");}
        now+=rotary.holdMs;require(!tick().lanes[0],"stopped rotation did not release");
        now+=1;input.axes[0]=(input.axes[0]+1024)%65536;require(tick().scratchPulse&&attacks==3,"new gesture did not rearm");
    }
    {Config rotary;rotary.enabled=true;Mapper slow;Sample input;input.connected=true;input.axes[0]=1000;
    slow.update(rotary,input,0,true);int pulses=0;
    for(int i=1;i<=1024;++i){++input.axes[0];const auto state=slow.update(rotary,input,i*4,true);pulses+=state.scratchPulse;if(i>=rotary.threshold)require(state.lanes[0],"slow continuous rotation timed out");}
    require(pulses==1,"slow rotation repeatedly pressed scratch");require(!slow.update(rotary,input,4096+rotary.holdMs,true).lanes[0],"slow rotation never released");}
}
bool saveDialog=false,dialogSeen=false;
void CALLBACK operateDialog(HWND,UINT,UINT_PTR,DWORD){
    auto dialog=FindWindowW(L"SevenManiaControllerSettings",nullptr);if(!dialog)return;
    dialogSeen=GetDlgItem(dialog,130)&&GetDlgItem(dialog,119)&&GetDlgItem(dialog,140);
    if(saveDialog){SendDlgItemMessageW(dialog,100,BM_SETCHECK,BST_CHECKED,0);SendDlgItemMessageW(dialog,110,CB_SETCURSEL,42,0);}
    PostMessageW(dialog,WM_COMMAND,saveDialog?IDOK:IDCANCEL,0);
}
void native(const std::filesystem::path& folder){
    using namespace bms::controller;
    std::filesystem::create_directories(folder);const auto ini=(folder/L"controller-test.ini").wstring();
    DeleteFileW(ini.c_str());
    const auto owner=CreateWindowExW(0,L"STATIC",L"Controller test owner",WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(owner!=nullptr,"owner window");
    Device device;require(device.initialize(owner),"DirectInput initialization");device.scan();
    device.config.enabled=false;device.config.keys[0]=23;require(device.save(ini),"INI save");
    Device restored;restored.load(ini);require(restored.config.keys[0]==23&&!restored.config.enabled,"INI readback");
    const auto timer=SetTimer(nullptr,0,100,operateDialog);require(timer!=0,"dialog driver timer");
    SettingsDialog cancel(device,owner,ini);require(!cancel.show(owner)&&dialogSeen,"dialog cancel and controls");
    require(!device.config.enabled&&device.config.keys[0]==23&&IsWindowEnabled(owner),"cancel restores state and owner");
    saveDialog=true;SettingsDialog save(device,owner,ini);require(save.show(owner),"dialog save");KillTimer(nullptr,timer);
    restored.load(ini);require(restored.config.enabled==device.config.enabled&&restored.config.keys[0]==41,"dialog persisted mappings");
    // A nonexistent saved GUID must turn off even if unrelated controllers exist.
    device.config.enabled=true;device.deviceId=L"{00000000-0000-0000-0000-000000000000}";
    require(device.disableIfUnavailable()&&!device.config.enabled,"missing registered controller stayed enabled");
    require(!device.disableIfUnavailable(),"disabled controller repeated availability transition");
    require(device.save(ini),"auto-disabled controller save failed");restored.load(ini);
    require(!restored.config.enabled&&restored.config.keys[0]==41,"auto-disable lost persistence/mappings");
    device.config.enabled=true;device.reconnect(owner);int releases=0;
    const auto missing=device.poll(true,[&](const State& state){++releases;require(std::none_of(state.lanes.begin(),state.lanes.end(),[](bool held){return held;}),"missing device retained a pressed lane");});
    require(!missing.connected&&!device.config.enabled&&releases==1,"poll did not disable missing device and release inputs");
    Device uninitialized;uninitialized.config.enabled=true;
    require(uninitialized.disableIfUnavailable()&&!uninitialized.config.enabled,"uninitialized controller stayed enabled");
    device.config.enabled=false;require(!device.poll(false,[](const State&){}).connected,"disabled device safe");
    device.reconnect(nullptr);DestroyWindow(owner);DeleteFileW(ini.c_str());
}
}
int wmain(int argc,wchar_t** argv){
    try{mapping();if(argc>1)native(std::filesystem::absolute(argv[1]));std::cout<<checks<<" controller checks passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
