#pragma once
#include <array>
#include <algorithm>
#include <filesystem>
#include <string>
#include <windows.h>
namespace bms {
struct KeyProfiles {
    inline static constexpr int modes[]{5,7,9,10,14};
    inline static constexpr int counts[]{6,8,9,12,16};
    std::array<std::array<UINT,16>,5> keys{};
    KeyProfiles(){for(auto& row:keys)row={VK_LSHIFT,'S','D','F',VK_SPACE,'J','K','L',VK_RSHIFT,'Q','W','E','R','T','Y','U'};keys[2]={'Z','S','X','D','C','F','V','G','B'};}
    void load(const std::filesystem::path& file){for(int m=0;m<5;++m){const auto section=L"KeyBindings"+std::to_wstring(modes[m]);auto loaded=keys[m];bool valid=true;
        for(int i=0;i<counts[m];++i){const auto key=L"Lane"+std::to_wstring(i);UINT v=GetPrivateProfileIntW(section.c_str(),key.c_str(),keys[m][i],file.c_str());
            if(v==0||v>255||v==VK_ESCAPE||v==VK_RETURN||v==VK_PRIOR||v==VK_NEXT||v==VK_F1||v==VK_F2||v==VK_F5||v==VK_TAB||v==VK_LWIN||v==VK_RWIN||v==VK_LMENU||v==VK_RMENU||std::find(loaded.begin(),loaded.begin()+i,v)!=loaded.begin()+i){valid=false;break;}loaded[i]=v;
        }if(valid)keys[m]=loaded;
    }}
    void save(const std::filesystem::path& file,int mode)const{const auto section=L"KeyBindings"+std::to_wstring(modes[mode]);for(int i=0;i<counts[mode];++i){const auto key=L"Lane"+std::to_wstring(i);WritePrivateProfileStringW(section.c_str(),key.c_str(),std::to_wstring(keys[mode][i]).c_str(),file.c_str());}}
};
}
