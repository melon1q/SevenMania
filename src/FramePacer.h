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
    using Clock = std::chrono::steady_clock;
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
        vsyncMode_ = fps < 0;
        alignedActive_ = false; lastVblankTarget_ = {}; suspendedFrames_ = 0;
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
    // Optional vblank alignment (VSync, or a cap at/below the refresh rate): start each
    // frame `lead` before a vblank instead of on a free-running grid that slowly drifts
    // across the refresh and periodically lands just after a vblank (missed refresh).
    void alignToVblank(Clock::time_point vblank, std::chrono::nanoseconds refresh, std::chrono::nanoseconds lead) {
        vblank_ = vblank; refresh_ = refresh; lead_ = lead; hasVblank_ = refresh.count() > 0;
    }
    void clearVblank() { hasVblank_ = false; alignedActive_ = false; lastVblankTarget_ = {}; }
    // Fall back to the free-running grid (and ready-signal painting) for `frames` frames.
    void suspendVblank(int frames) { suspendedFrames_ = std::max(suspendedFrames_, frames); lastVblankTarget_ = {}; }
    int suspendedFrames() const { return suspendedFrames_; }
    bool vblankAligned() const { return alignedActive_; }
    void advance() {
        if (suspendedFrames_ > 0) --suspendedFrames_;
        alignedActive_ = hasVblank_ && suspendedFrames_ == 0 && (vsyncMode_ || nearRefreshMultiple());
        if (alignedActive_) { advanceToVblank(Clock::now()); return; }
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
    // A cap within 5% of 1x/2x/3x... the refresh period (240 or 120 fps at 240 Hz).
    // Other caps (144 fps at 240 Hz) keep the free-running grid.
    bool nearRefreshMultiple() const {
        if (refresh_.count() <= 0 || period_ * 20 < refresh_ * 19) return false;
        const long long step = (period_ + refresh_ / 2) / refresh_;
        const auto difference = period_ > refresh_ * step ? period_ - refresh_ * step : refresh_ * step - period_;
        return difference * 20 <= refresh_;
    }
    void advanceToVblank(Clock::time_point now) {
        // Refreshes per frame: 1 for VSync, round(period/refresh) for a cap (120 fps at 240 Hz = 2).
        const long long step = vsyncMode_ ? 1 : std::max<long long>(1, (period_ + refresh_ / 2) / refresh_);
        const auto ahead = now + lead_ - vblank_;
        long long index = ahead.count() <= 0 ? 0 : (ahead + refresh_ - std::chrono::nanoseconds(1)) / refresh_;
        auto target = vblank_ + refresh_ * index;
        if (target - lead_ <= now) target += refresh_;
        // Never run two frames for one cap slot when the previous frame finished early.
        if (lastVblankTarget_ != Clock::time_point{} && target < lastVblankTarget_ + refresh_ * step - refresh_ / 2)
            target = lastVblankTarget_ + refresh_ * step;
        lastVblankTarget_ = target;
        deadline_ = target - lead_;
    }
    bool pollBeforeDeadline() const {
        if (alignedActive_) return deadline_ - Clock::now() < std::chrono::microseconds(500);
        // Sub-millisecond sleeps may overshoot the 1500 Hz budget. The caller
        // keeps pumping input messages during this short, bounded polling period.
        return (HUD_LEGACY_REFLEX()?cappedUnlimited_:(minimumStartInterval_&&period_<=std::chrono::milliseconds(2))) && deadline_ - Clock::now() < std::chrono::milliseconds(1);
    }
    HANDLE arm() {
        if (!timer_) return nullptr;
        auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline_ - Clock::now()).count();
        if(!HUD_LEGACY_REFLEX()&&minimumStartInterval_&&period_<=std::chrono::milliseconds(2))remaining-=1'000'000; // Wake early; pump input during the final short poll.
        if(alignedActive_)remaining-=500'000; // Timer wake jitter: finish the last 0.5 ms polling.
        LARGE_INTEGER dueTime{};
        dueTime.QuadPart = -std::max<long long>(1, remaining / 100);
        if (!SetWaitableTimer(timer_, &dueTime, 0, nullptr, nullptr, FALSE)) return nullptr;
        return timer_;
    }
private:
    HANDLE timer_{};
    bool cappedUnlimited_{},minimumStartInterval_{},vsyncMode_{};
    bool hasVblank_{},alignedActive_{};
    int suspendedFrames_{};
    Clock::time_point vblank_{}, lastVblankTarget_{};
    std::chrono::nanoseconds refresh_{}, lead_{};
    Clock::time_point deadline_{ Clock::now() };
    Clock::time_point frameStarted_{ deadline_ };
    std::chrono::nanoseconds period_{ 1'000'000'000LL / 240 };
};
