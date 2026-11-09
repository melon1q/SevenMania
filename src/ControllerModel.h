#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace bms::controller {
struct Config {
    bool enabled=false;
    std::array<int,7> keys{0,1,2,3,4,5,6};
    // Second-player buttons (DP lanes 8-15, PMS lanes 7-8). -1 = unassigned.
    std::array<int,7> keys2p{-1,-1,-1,-1,-1,-1,-1};
    int scratchCw=7,scratchCcw=8,axis=0,mode=1,threshold=256,holdMs=80,menu=-1;
    int scratch2pCw=-1,scratch2pCcw=-1;
};
struct Sample {
    bool connected=false;
    std::array<bool,128> buttons{};
    std::array<int,8> axes{};
};
// lanes use DP numbering: 0 = 1P scratch, 1-7 = 1P keys, 8 = 2P scratch, 9-15 = 2P keys.
struct State {std::array<bool,16> lanes{};bool scratchPulse=false,menuPressed=false;};
// Pure mapping: one clock domain in milliseconds, independent of render FPS.
class Mapper {
    bool initialized_=false,menuBefore_=false,menuBlocked_=false;
    std::array<bool,16> blocked_{};
    int lastAxis_=0,travel_=0,rotationDirection_=0;
    std::uint64_t movingUntil_=0;
public:
    void reset() noexcept {initialized_=false;blocked_.fill(false);travel_=0;rotationDirection_=0;movingUntil_=0;menuBefore_=false;}
    State update(const Config& config,const Sample& sample,std::uint64_t now,bool active) noexcept {
        State out;
        if(!config.enabled||!sample.connected||!active){reset();return out;}
        const auto button=[&](int index){return index>=0&&index<128&&sample.buttons[index];};
        std::array<bool,16> held{};
        held[0]=button(config.scratchCw)||button(config.scratchCcw);
        for(int i=0;i<7;++i)held[i+1]=button(config.keys[i]);
        held[8]=button(config.scratch2pCw)||button(config.scratch2pCcw);
        for(int i=0;i<7;++i)held[i+9]=button(config.keys2p[i]);
        const bool hasAxis=config.axis>=0&&config.axis<8&&config.mode>0;
        const int axis=hasAxis?std::clamp(sample.axes[config.axis],0,65535):0;
        const bool menu=button(config.menu);
        if(!initialized_){initialized_=true;lastAxis_=axis;if(hasAxis&&config.mode==2)held[0]=held[0]||std::abs(axis-32767)>=std::clamp(config.threshold,1,32767);blocked_=held;menuBlocked_=menu;menuBefore_=menu;return out;}
        bool moving=false;
        if(hasAxis&&config.mode==1){
            if(rotationDirection_&&now>=movingUntil_){rotationDirection_=0;travel_=0;}
            int delta=axis-lastAxis_;if(delta>32767)delta-=65536;else if(delta< -32768)delta+=65536;
            lastAxis_=axis;travel_=std::clamp(travel_+delta,-65536,65536);
            // Slow continuous movement must not time out between threshold crossings.
            if(rotationDirection_&&delta)movingUntil_=now+std::clamp(config.holdMs,20,250);
            if(std::abs(travel_)>=std::clamp(config.threshold,1,8192)){
                const int direction=travel_>0?1:-1;
                // Continued rotation extends the hold, not another key press.
                // A new gesture or a qualified reversal emits one edge.
                out.scratchPulse=direction!=rotationDirection_;rotationDirection_=direction;
                movingUntil_=now+std::clamp(config.holdMs,20,250);travel_=0;
            }
            moving=now<movingUntil_;
        } else if(hasAxis&&config.mode==2){moving=std::abs(axis-32767)>=std::clamp(config.threshold,1,32767);}
        held[0]=held[0]||moving;
        for(int i=0;i<16;++i){blocked_[i]=blocked_[i]&&held[i];out.lanes[i]=held[i]&&!blocked_[i];}
        menuBlocked_=menuBlocked_&&menu;out.menuPressed=menu&&!menuBefore_&&!menuBlocked_;menuBefore_=menu;
        return out;
    }
};
inline bool laneHeld(bool keyboard,bool controller) noexcept {return keyboard||controller;}
}
