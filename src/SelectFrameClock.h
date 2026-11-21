#pragma once

#include <algorithm>
#include <chrono>

// Frame cadence of the song select screen while it animates, separate from the play FramePacer.
// The select screen is message-driven, so it only needs "is a frame due" and "how long may I sleep".
class SelectFrameClock {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr int defaultFps = 60;
    static constexpr bool validFps(int fps) noexcept { return fps == 60 || fps == 120; }
    void setFps(int fps) noexcept {
        fps_ = validFps(fps) ? fps : defaultFps;
        period_ = std::chrono::nanoseconds(1'000'000'000LL / fps_);
        next_ = {};
    }
    // Any rate from 30 to 360 FPS, for screens that follow the monitor (LR2 song entry, loading, result).
    void setRate(int fps) noexcept {
        fps_ = std::clamp(fps, 30, 360);
        period_ = std::chrono::nanoseconds(1'000'000'000LL / fps_);
        next_ = {};
    }
    int fps() const noexcept { return fps_; }
    Clock::duration period() const noexcept { return period_; }
    bool due(Clock::time_point now) const noexcept { return now >= next_; }
    // Schedule the next frame one period after the previous deadline; after a stall, from now.
    void advance(Clock::time_point now) noexcept {
        next_ = next_ == Clock::time_point{} || now - next_ > period_ ? now + period_ : next_ + period_;
    }
    // Whole milliseconds to wait for the next frame (rounded down, so a frame is never late by a tick).
    unsigned waitMilliseconds(Clock::time_point now) const noexcept {
        if (now >= next_) return 0;
        return static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(next_ - now).count());
    }
private:
    int fps_ = defaultFps;
    Clock::duration period_ = std::chrono::nanoseconds(1'000'000'000LL / defaultFps);
    Clock::time_point next_{};
};
