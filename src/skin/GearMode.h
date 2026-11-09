#pragma once
#include <string_view>

namespace sevenmania::skin {
// Key modes a skin gear can be authored for. Index 0 (7K) is the base geometry stored in the
// element itself; the other modes are stored per element under extensions.gearModes.<name>
// as {x, y, width, height, visible, fullWidth?}. Values are in skin pixels with the gear
// centered at the left layout position (x center 342), like the base geometry.
inline constexpr int gearModeCount = 5;
inline const wchar_t* gearModeName(int mode) noexcept {
    switch(mode){case 1:return L"5k";case 2:return L"10k";case 3:return L"14k";case 4:return L"pms";default:return L"7k";}
}
// Lane (parser numbering) driven by a gear-key element id, or -1.
//   gear-key-N      : lane N          (PMS: lane N-1, nine buttons)
//   gear-key-p2-N   : lane 8+N        (PMS: N=2 -> lane 7, N=3 -> lane 8; those reuse the
//                                      black/white key textures so the buttons alternate)
inline int gearKeyLane(std::string_view id, int mode) noexcept {
    constexpr std::string_view p1="gear-key-",p2="gear-key-p2-";
    const bool second=id.starts_with(p2);
    if(!second&&(!id.starts_with(p1)||id.starts_with("gear-key-guide-")||id.starts_with("gear-key-p2")))return -1;
    const auto digits=id.substr(second?p2.size():p1.size());
    if(digits.size()!=1||digits[0]<'1'||digits[0]>'7')return -1;
    const int n=digits[0]-'0';
    if(mode==4)return second?(n==2?7:n==3?8:-1):n-1;
    return second?8+n:n;
}
// Lane whose key label a gear-key-guide element shows, or -1.
//   gear-key-guide-N     : lane N (N = 0 is the scratch)  (PMS: lane N-1, N = 1..7)
//   gear-key-guide-p2-N  : lane 8+N                        (PMS: N=2 -> lane 7, N=3 -> lane 8)
inline int gearGuideLane(std::string_view id, int mode) noexcept {
    constexpr std::string_view p1="gear-key-guide-",p2="gear-key-guide-p2-";
    const bool second=id.starts_with(p2);
    if(!second&&!id.starts_with(p1))return -1;
    const auto digits=id.substr(second?p2.size():p1.size());
    if(digits.size()!=1||digits[0]<'0'||digits[0]>'7')return -1;
    const int n=digits[0]-'0';
    if(mode==4)return second?(n==2?7:n==3?8:-1):(n>=1?n-1:-1);
    return second?8+n:n;
}
}
