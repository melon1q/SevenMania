#pragma once
#include <array>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>
#include <windows.h>
namespace bms {
struct KeyProfiles {
    inline static constexpr int modes[]{5,7,9,10,14};
    inline static constexpr int counts[]{6,8,9,12,16};
    std::array<std::array<UINT,16>,5> keys{};
    // Human-readable notes about lanes that were reset to defaults by load().
    std::vector<std::wstring> warnings;
    KeyProfiles(){
        // Slot order per profile: 1P scratch, 1P keys 1-N, then (DP) 2P scratch, 2P keys 1-N.
        keys[0]={VK_LSHIFT,'S','D','F',VK_SPACE,'J'};
        keys[1]={VK_LSHIFT,'S','D','F',VK_SPACE,'J','K','L'};
        keys[2]={'Z','S','X','D','C','F','V','G','B'};
        keys[3]={VK_LSHIFT,'Z','S','X','D','C',VK_RSHIFT,'M','K',VK_OEM_COMMA,'L',VK_OEM_PERIOD};
        keys[4]={VK_LSHIFT,'Z','S','X','D','C','F','V',VK_RSHIFT,'M','K',VK_OEM_COMMA,'L',VK_OEM_PERIOD,VK_OEM_1,VK_OEM_2};
    }
    void warn(const std::wstring& text){warnings.push_back(text);OutputDebugStringW((text+L"\n").c_str());}
    static bool reservedKey(UINT v){
        return v==0||v>255||v==VK_ESCAPE||v==VK_RETURN||v==VK_PRIOR||v==VK_NEXT||v==VK_F1||v==VK_F2||v==VK_F5||v==VK_TAB||v==VK_LWIN||v==VK_RWIN||v==VK_LMENU||v==VK_RMENU;
    }
    // A single invalid or duplicate lane resets only that lane to its default; the rest of the
    // profile is kept. If the defaults then collide with kept keys, the whole mode is reset.
    void load(const std::filesystem::path& file){
        for(int m=0;m<5;++m){
            const auto section=L"KeyBindings"+std::to_wstring(modes[m]);
            const auto defaults=keys[m];auto loaded=defaults;
            for(int i=0;i<counts[m];++i){
                const auto key=L"Lane"+std::to_wstring(i);
                UINT v=GetPrivateProfileIntW(section.c_str(),key.c_str(),defaults[i],file.c_str());
                // Older captures stored the side-less Shift/Ctrl codes, which key events never report
                // (they are normalized to the left/right code), so such a lane could never fire.
                if(v==VK_SHIFT)v=defaults[i]==VK_RSHIFT?VK_RSHIFT:VK_LSHIFT;
                else if(v==VK_CONTROL)v=VK_LCONTROL;
                if(reservedKey(v)||std::find(loaded.begin(),loaded.begin()+i,v)!=loaded.begin()+i){
                    warn(section+L" "+key+L": 사용할 수 없거나 중복된 키라 기본값으로 되돌렸습니다.");
                    v=defaults[i];
                }
                loaded[i]=v;
            }
            bool unique=true;
            for(int i=0;i<counts[m]&&unique;++i)unique=std::find(loaded.begin(),loaded.begin()+i,loaded[i])==loaded.begin()+i;
            if(!unique){warn(section+L": 키가 겹쳐 전체 기본값을 사용합니다.");loaded=defaults;}
            keys[m]=loaded;
        }
    }
    void save(const std::filesystem::path& file,int mode)const{const auto section=L"KeyBindings"+std::to_wstring(modes[mode]);for(int i=0;i<counts[mode];++i){const auto key=L"Lane"+std::to_wstring(i);WritePrivateProfileStringW(section.c_str(),key.c_str(),std::to_wstring(keys[mode][i]).c_str(),file.c_str());}}
};
}
