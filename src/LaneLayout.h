#pragma once

struct LaneLayout {
    static constexpr int defaultKeyWidth = 68;
    static constexpr int defaultScratchWidth = 104;
    int fieldX{}, keyWidth = defaultKeyWidth, scratchWidth = defaultScratchWidth;
    int keys = 7, players = 1;
    bool popn{};
    int laneCount() const {return popn?9:players==2?16:keys+1;}
    // Lane numbers are logical (parser/judgement); position is the left-to-right screen order.
    // DP draws the second player's scratch on the right edge: SC 1-7 | 1-7 SC.
    int laneAt(int position) const {
        if(popn||players!=2||position<8||position>15)return position;
        return position==15?8:position+1;
    }
    int position(int lane) const {
        if(popn||players!=2||lane<8||lane>15)return lane;
        return lane==8?15:lane-1;
    }
    int width(int lane) const {
        if(popn)return keyWidth;
        const int key=lane%8;
        if(key>keys)return 0;
        return key==0?scratchWidth:keyWidth+(key%2==1?0:-8);
    }
    int left(int lane) const {int x=fieldX;for(int prior=0,end=position(lane);prior<end;++prior)x+=width(laneAt(prior));return x;}
    int right(int lane) const {return left(lane)+width(lane);}
    int totalWidth() const {int total=0;for(int lane=0;lane<laneCount();++lane)total+=width(lane);return total;}
    // Index (0..6) of the user-configurable separator drawn on the left edge of this lane,
    // or -1 if there is none (leftmost lane, or a lane that is not shown).
    int separatorIndex(int lane) const {
        if(width(lane)<=0||position(lane)<=0)return -1;
        if(popn)return position(lane)-1<6?position(lane)-1:6;
        if(players==2&&lane>=8)return lane==8?0:lane-9;
        return lane-1;
    }
    // Slot (0 = scratch color, 1..7 = key colors) used to color a lane.
    // PMS has no scratch: its nine buttons mirror around the center button.
    int colorSlot(int lane) const {
        if(popn){static constexpr int slots[9]{1,2,3,4,0,4,3,2,1};return lane>=0&&lane<9?slots[lane]:1;}
        return lane%8;
    }
};
