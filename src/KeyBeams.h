#pragma once
#include "LaneLayout.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

class KeyBeams {
    struct State { double from{}, target{}, changedAt{}; };
    std::array<State, 16> states_{};
    int keys_=7,players_=1;bool popn_{};
public:
    void configure(const LaneLayout& layout){keys_=layout.keys;players_=layout.players;popn_=layout.popn;}
    void reset() { states_ = {}; }
    double widthFraction(int lane, double time) const {
        if (lane < 0 || lane >= 16) return 0;
        const auto& state = states_[static_cast<size_t>(lane)];
        const double travel = std::clamp((time - state.changedAt) / 0.02, 0.0, 1.0);
        return state.target > state.from ? std::min(state.target, state.from + travel)
                                        : std::max(state.target, state.from - travel);
    }
    void setPressed(int lane, bool pressed, double time) {
        if (lane < 0 || lane >= 16) return;
        const double target = pressed ? 1.0 : 0.0;
        auto& state = states_[static_cast<size_t>(lane)];
        if (state.target == target) return;
        state = { widthFraction(lane, time), target, time };
    }
    void draw(uint32_t* pixels, int width, int height, int fieldX, int laneWidth, int judgeY, double time, int scratchWidth = 0) const {
        if (!pixels || width <= 0 || height <= 0 || laneWidth <= 0 || judgeY <= 0) return;
        const int beamHeight = std::max(1, judgeY / 2);
        const int beamTop = judgeY - beamHeight;
        const LaneLayout layout{ fieldX, laneWidth, scratchWidth > 0 ? scratchWidth : laneWidth,keys_,players_,popn_ };
        for (int lane = 0; lane < layout.laneCount(); ++lane) {
            const double fraction = widthFraction(lane, time);
            const int beamWidth = static_cast<int>(std::lround(layout.width(lane) * fraction));
            if (beamWidth <= 0) continue;
            const int left = layout.left(lane) + (layout.width(lane) - beamWidth) / 2;
            const int firstX = std::max(0, left), lastX = std::min(width, left + beamWidth);
            if (firstX >= lastX) continue;
            for (int y = std::max(0, beamTop); y < std::min(height, judgeY + 1); ++y) {
                const unsigned alpha = static_cast<unsigned>(190.0 * (y - beamTop) / beamHeight);
                if (!alpha) continue;
                const unsigned inverse = 255 - alpha;
                auto* output = pixels + static_cast<size_t>(y) * width;
                for (int x = firstX; x < lastX; ++x) {
                    const uint32_t before = output[x];
                    const unsigned r = (56 * alpha + ((before >> 16) & 255) * inverse + 127) / 255;
                    const unsigned g = (189 * alpha + ((before >> 8) & 255) * inverse + 127) / 255;
                    const unsigned b = (248 * alpha + (before & 255) * inverse + 127) / 255;
                    output[x] = 0xff000000u | (r << 16) | (g << 8) | b;
                }
            }
        }
    }
};
