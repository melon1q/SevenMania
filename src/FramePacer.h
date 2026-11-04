#pragma once

#include <windows.h>
#include <chrono>
#include <algorithm>
#include "FrameRateSettings.h"
#include "HudFrameProbe.h"

// One-shot waits preserve fractional frame periods (144 Hz = 6.944... ms).
// Unlike WM_TIMER, this timer has no 10 ms minimum.
class FramePacer {
    friend struct FramePacerTestAccess;
public:
    FramePacer() {
        timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (!timer_) timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }
    ~FramePacer() { if (timer_) CloseHandle(timer_); }
    FramePacer(const FramePacer&) = delete;
    FramePacer& operator=(const FramePacer&) = delete;
    void reset(int fps) {
        // VSync has its own presentation clock. Keep its input/update loop, and
        // the unsynchronized "unlimited" mode, bounded at 1500 Hz.
        const int limit = fps <= 0 ? unlimitedFpsLimit : fps;
        cappedUnlimited_ = fps == 0;
        minimumStartInterval_ = fps == 0 || fps >= 500;
        period_ = std::chrono::nanoseconds((1'000'000'000LL + limit - 1) / limit);
        deadline_ = Clock::now();
    }
    bool due() {
        const auto now = Clock::now();
        if (now < deadline_) return false;
        frameStarted_ = now;
        return true;
    }
    void advance() {
        if (HUD_LEGACY_REFLEX()?cappedUnlimited_:minimumStartInterval_) {
            // A ceiling is a minimum start-to-start interval, not a fixed grid.
            // Expensive frames already paid this interval; do not skip another slot.
            deadline_ = std::max(frameStarted_ + period_, Clock::now());
            return;
        }
        deadline_ += period_;
        const auto now = Clock::now();
        if (deadline_ <= now) {
            const auto missed = (now - deadline_) / period_ + 1;
            deadline_ += period_ * missed; // Skip late frames instead of a catch-up burst.
        }
    }
    bool pollBeforeDeadline() const {
        // Sub-millisecond sleeps may overshoot the 1500 Hz budget. The caller
        // keeps pumping input messages during this short, bounded polling period.
        return (HUD_LEGACY_REFLEX()?cappedUnlimited_:(minimumStartInterval_&&period_<=std::chrono::milliseconds(2))) && deadline_ - Clock::now() < std::chrono::milliseconds(1);
    }
    HANDLE arm() {
        if (!timer_) return nullptr;
        auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline_ - Clock::now()).count();
        if(!HUD_LEGACY_REFLEX()&&minimumStartInterval_&&period_<=std::chrono::milliseconds(2))remaining-=1'000'000; // Wake early; pump input during the final short poll.
        LARGE_INTEGER dueTime{};
        dueTime.QuadPart = -std::max<long long>(1, remaining / 100);
        if (!SetWaitableTimer(timer_, &dueTime, 0, nullptr, nullptr, FALSE)) return nullptr;
        return timer_;
    }
private:
    using Clock = std::chrono::steady_clock;
    HANDLE timer_{};
    bool cappedUnlimited_{},minimumStartInterval_{};
    Clock::time_point deadline_{ Clock::now() };
    Clock::time_point frameStarted_{ deadline_ };
    std::chrono::nanoseconds period_{ 1'000'000'000LL / 240 };
};
