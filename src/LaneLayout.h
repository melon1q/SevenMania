#pragma once

struct LaneLayout {
    static constexpr int defaultKeyWidth = 68;
    static constexpr int defaultScratchWidth = 104;
    int fieldX{}, keyWidth = defaultKeyWidth, scratchWidth = defaultScratchWidth;
    int keys = 7, players = 1;
    bool popn{};
    int laneCount() const {return popn?9:players==2?16:keys+1;}
    int width(int lane) const {
        if(popn)return keyWidth;
        const int key=lane%8;
        if(key>keys)return 0;
        return key==0?scratchWidth:keyWidth+(key%2==1?0:-8);
    }
    int left(int lane) const {int x=fieldX;for(int prior=0;prior<lane;++prior)x+=width(prior);return x;}
    int right(int lane) const {return left(lane)+width(lane);}
    int totalWidth() const {int total=0;for(int lane=0;lane<laneCount();++lane)total+=width(lane);return total;}
};
