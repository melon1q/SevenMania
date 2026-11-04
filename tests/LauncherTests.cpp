#include "SharedStartupSettings.h"
#include "SongLibrarySources.h"
#include <iostream>
#include <thread>
#include <atomic>
void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
int wmain(int argc,wchar_t** argv){if(argc!=2)return 2;try{
    auto folder=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(folder);
    auto file=folder/(L"설정 日本語 "+std::to_wstring(GetCurrentProcessId())+L".ini");
    {std::ofstream f(file);f<<"[Unknown]\nKeep=original\n[Display]\nFPS=999999\nScreenMode=99\nBGADarkness=25\n[Sound]\nBufferFrames=999\n";}
    auto s=sevenmania::readStartupSettings(file);require(s.fps==240&&s.mode==2&&s.buffer==256&&s.brightness==80,"legacy/default normalization failed");
    require(sevenmania::atomicSettingsUpdate(file,[&](auto temp){return WritePrivateProfileStringW(L"Display",L"FPS",L"-1",temp.c_str())!=0;}),"atomic settings save failed");
    require(sevenmania::readStartupSettings(file).fps==-1&&sevenmania::iniText(file,L"Unknown",L"Keep")==L"original","VSync roundtrip or unknown fields lost");
    require(!sevenmania::atomicSettingsUpdate(file,[&](auto temp){WritePrivateProfileStringW(L"Display",L"FPS",L"60",temp.c_str());return false;}),"failed transaction committed");
    require(sevenmania::readStartupSettings(file).fps==-1,"failed edit changed live config");
    std::atomic_bool ready{},finish{},acquired{};std::thread game([&]{sevenmania::SettingsGate gate(file);acquired=gate.acquire();ready=true;while(!finish)std::this_thread::yield();});
    while(!ready)std::this_thread::yield();bool blocked=!sevenmania::atomicSettingsUpdate(file,[](auto){return true;});finish=true;game.join();require(acquired&&blocked,"game/launcher concurrency gate failed");
    require(sevenmania::atomicSettingsUpdate(file,[&](auto temp){bms::SongLibrarySources sources;sources.initialize(folder/L"Songs",temp);sources.add(folder/L"外部 曲");return sources.save();}),"source transaction failed");
    bms::SongLibrarySources sources;sources.initialize(folder/L"Songs",file);require(sources.roots.size()==1,"shared sources lost after restart");
    SetEnvironmentVariableW(L"BMS_PLAYER_CONFIG_PATH",file.c_str());require(sevenmania::settingsPath()==file,"game/launcher config resolution differs");
    require(sevenmania::iniText(file,L"Unknown",L"Keep")==L"original","source save overwrote existing keys");
    std::cout<<"Shared launcher settings: legacy defaults, VSync, atomic rollback, game exclusion, Unicode source persistence and shared path passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
