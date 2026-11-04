#include "../src/Main.cpp"
#include <iostream>
int wmain(int argc,wchar_t** argv){if(argc!=2)return 2;try{
    const auto directory=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(directory);gSettingsFile=directory/(L"engine-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    auto write=[&](const wchar_t* section,const wchar_t* key,const wchar_t* value){if(!WritePrivateProfileStringW(section,key,value,gSettingsFile.c_str()))throw std::runtime_error("write failed");};
    write(L"Display",L"FPS",L"-1");write(L"Display",L"ScreenMode",L"1");write(L"Display",L"ResolutionWidth",L"1280");write(L"Display",L"ResolutionHeight",L"720");write(L"Display",L"Brightness",L"0");write(L"Sound",L"BufferFrames",L"512");
    loadDisplaySettings();if(gTargetFps!=vsyncFps||gScreenMode!=1||gResolutionWidth!=1280||gResolutionHeight!=720||gBgaBrightness!=0||gSoundBufferFrames!=512)throw std::runtime_error("launcher settings not applied by actual engine loader");
    write(L"Display",L"FPS",L"0");loadDisplaySettings();if(gTargetFps!=0||unlimitedFpsLimit!=1500)throw std::runtime_error("unlimited semantics changed");
    std::cout<<"Actual game loader applied VSync/unlimited, mode, resolution, BGA OFF and buffer from shared INI.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
