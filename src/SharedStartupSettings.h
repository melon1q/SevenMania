#pragma once
#include "FrameRateSettings.h"
#include <filesystem>
#include <functional>
#include <fstream>
#include <windows.h>
#include <string>
#include <cwctype>
#include <cstdint>
#include <stdexcept>

namespace sevenmania {
inline std::filesystem::path settingsPath() {
    wchar_t text[32768]{};
    auto n=GetEnvironmentVariableW(L"BMS_PLAYER_CONFIG_PATH",text,32768);
    if(n&&n<32768&&std::filesystem::path(text).is_absolute())return text;
    n=GetEnvironmentVariableW(L"APPDATA",text,32768);
    std::filesystem::path folder;
    if(n&&n<32768&&std::filesystem::path(text).is_absolute())folder=std::filesystem::path(text)/L"BmsPlayer";
    else {if(!GetModuleFileNameW(nullptr,text,32768))throw std::runtime_error("executable path unavailable");folder=std::filesystem::path(text).parent_path()/L"Config";}
    std::error_code error;std::filesystem::create_directories(folder,error);return folder/L"keybindings.ini";
}
inline std::wstring iniText(const std::filesystem::path& file,const wchar_t* section,const wchar_t* key) {
    std::wstring out(32768,L'\0');out.resize(GetPrivateProfileStringW(section,key,L"",out.data(),DWORD(out.size()),file.c_str()));return out;
}
// Game owns this mutex for its lifetime. Launcher takes it only for saves or
// cache writes. Other launchers and directly started games use the same gate.
class SettingsGate {
    HANDLE handle_{};bool owned_{};
public:
    explicit SettingsGate(const std::filesystem::path& file) {
        uint64_t hash=14695981039346656037ull;
        for(auto c:std::filesystem::absolute(file.empty()?std::filesystem::path(L"default-settings"):file).lexically_normal().wstring())hash=(hash^uint16_t(towlower(c)))*1099511628211ull;
        handle_=CreateMutexW(nullptr,FALSE,(L"Local\\SevenMania.Settings."+std::to_wstring(hash)).c_str());
    }
    bool acquire(){if(owned_)return true;if(!handle_)return false;auto r=WaitForSingleObject(handle_,0);return owned_=r==WAIT_OBJECT_0||r==WAIT_ABANDONED;}
    ~SettingsGate(){if(owned_)ReleaseMutex(handle_);if(handle_)CloseHandle(handle_);}
    SettingsGate(const SettingsGate&)=delete;
};
inline constexpr std::array<int,6> startupBuffers{32,64,128,256,512,1024};
struct StartupSettings {
    int fps=240,mode=0,width=0,height=0,brightness=50,buffer=256;
};
inline StartupSettings readStartupSettings(const std::filesystem::path& file) {
    StartupSettings s;
    auto get=[&](const wchar_t* section,const wchar_t* key,int fallback){return int(GetPrivateProfileIntW(section,key,fallback,file.c_str()));};
    s.fps=get(L"Display",L"FPS",240);if(std::find(frameRateValues.begin(),frameRateValues.end(),s.fps)==frameRateValues.end())s.fps=240;
    s.mode=std::clamp(get(L"Display",L"ScreenMode",0),0,2);
    s.width=get(L"Display",L"ResolutionWidth",0);s.height=get(L"Display",L"ResolutionHeight",0);
    if(s.width<=0||s.height<=0){s.width=s.height=0;}
    auto old=std::clamp(get(L"Display",L"BGADarkness",50),0,100);
    s.brightness=(std::clamp(get(L"Display",L"Brightness",100-old),0,100)+5)/10*10;
    s.buffer=get(L"Sound",L"BufferFrames",256);
    if(std::find(startupBuffers.begin(),startupBuffers.end(),s.buffer)==startupBuffers.end())s.buffer=256;
    return s;
}
inline bool atomicSettingsUpdate(const std::filesystem::path& file,const std::function<bool(const std::filesystem::path&)>& edit) {
    SettingsGate gate(file);if(!gate.acquire())return false;
    auto temp=file;temp+=L".launcher-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".ini";
    bool success=false;
    try {
        std::filesystem::create_directories(file.parent_path());
        WritePrivateProfileStringW(nullptr,nullptr,nullptr,file.c_str());
        if(std::filesystem::exists(file))std::filesystem::copy_file(file,temp);
        else {std::ofstream stream(temp,std::ios::binary);stream.write("\xff\xfe",2);}
        if(edit(temp)){
            WritePrivateProfileStringW(nullptr,nullptr,nullptr,temp.c_str());
            success=MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
            WritePrivateProfileStringW(nullptr,nullptr,nullptr,file.c_str());
        }
    }catch(...){}
    std::error_code error;std::filesystem::remove(temp,error);return success;
}
inline void placeConfiguredMonitor(HWND window,const std::filesystem::path& file) {
    struct Context{HWND window;std::wstring name;};Context context{window,iniText(file,L"Display",L"Monitor")};
    if(context.name.empty())return;
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM data)->BOOL{
        auto& c=*reinterpret_cast<Context*>(data);MONITORINFOEXW info{};info.cbSize=sizeof(info);
        if(GetMonitorInfoW(monitor,&info)&&c.name==info.szDevice){SetWindowPos(c.window,nullptr,info.rcWork.left,info.rcWork.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);return FALSE;}return TRUE;
    },reinterpret_cast<LPARAM>(&context));
}
}
