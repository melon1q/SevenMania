#pragma once

#include "Judgement.h"
#include "BmsParser.h"
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <limits>

enum class HoldResult { None, PGreat, Poor, Bad };

// Starting a note consumes its head, but its body remains active until the tail
// reaches the judgement line and its release window expires, or the player
// releases the key and the release offset can be judged.
class LongNoteHold {
public:
    void start(size_t noteIndex, double endSeconds, double startSeconds = 0, bms::LongNoteMode mode = bms::LongNoteMode::CN, TimingJudgement head = TimingJudgement::PGreat) {
        noteIndex_ = noteIndex; mode_=mode;head_=head;
        nextGaugeTick_=startSeconds+0.2;
        endSeconds_ = endSeconds;
        active_ = true;
        nextComboTickSeconds_ = startSeconds + comboIntervalSeconds_;
    }
    TimingJudgement headJudgement()const{return head_;}
    bool active() const { return active_; }
    size_t noteIndex() const { return noteIndex_; }
    unsigned takeComboTicks(double timeSeconds) {
        if (!active_ || !std::isfinite(timeSeconds)) return 0;
        const double until = std::min(timeSeconds, endSeconds_);
        if (until + 1e-9 < nextComboTickSeconds_) return 0;
        const auto count = static_cast<unsigned>(std::min(
            std::floor((until - nextComboTickSeconds_) / comboIntervalSeconds_ + 1e-8) + 1,
            static_cast<double>(std::numeric_limits<unsigned>::max())));
        nextComboTickSeconds_ += count * comboIntervalSeconds_;
        return count;
    }
    HoldResult update(double timeSeconds, bool pressed, double releaseWindow = LONG_NOTE_RELEASE_WINDOW_SECONDS) {
        if (!active_) return HoldResult::None;
        if(mode_==bms::LongNoteMode::LN && pressed && timeSeconds>=endSeconds_) {active_=false;return HoldResult::PGreat;}
        if(mode_==bms::LongNoteMode::HCN) {
            if(timeSeconds>=endSeconds_) {active_=false;return pressed?HoldResult::PGreat:HoldResult::Poor;}
            return HoldResult::None;
        }
        if (!pressed) return release(timeSeconds, releaseWindow);
        if (timeSeconds >= endSeconds_ + releaseWindow) {
            active_ = false;
            return HoldResult::Bad;
        }
        return HoldResult::None;
    }
    HoldResult release(double timeSeconds, double releaseWindow = LONG_NOTE_RELEASE_WINDOW_SECONDS) {
        if (!active_) return HoldResult::None;
        if(mode_==bms::LongNoteMode::HCN && timeSeconds<endSeconds_) return HoldResult::None;
        active_ = false;
        if (timeSeconds < endSeconds_ - releaseWindow) return HoldResult::Poor;
        if (timeSeconds <= endSeconds_ + releaseWindow) return HoldResult::PGreat;
        return HoldResult::Bad;
    }
    unsigned takeGaugeTicks(double time) {
        if(!active_ || mode_!=bms::LongNoteMode::HCN || time<nextGaugeTick_) return 0;
        const auto count=static_cast<unsigned>(std::max(0.0,std::floor((std::min(time,endSeconds_)-nextGaugeTick_)/0.2+1e-8)+1));
        nextGaugeTick_+=count*0.2;return count;
    }
    int visibleTailY(double timeSeconds, int top, int judgeY, double pixelsPerSecond) const {
        const double y = judgeY - std::max(0.0, endSeconds_ - timeSeconds) * pixelsPerSecond;
        // Clip before integer conversion, including tails far beyond the screen.
        return static_cast<int>(std::clamp(y, static_cast<double>(top), static_cast<double>(judgeY)));
    }
private:
    bool active_{};
    TimingJudgement head_=TimingJudgement::PGreat;
    bms::LongNoteMode mode_=bms::LongNoteMode::CN;
    double nextGaugeTick_{};
    size_t noteIndex_{};
    double endSeconds_{};
    static constexpr double comboIntervalSeconds_ = 0.1;
    double nextComboTickSeconds_{};
};
