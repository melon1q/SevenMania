#pragma once
#include <algorithm>
#include <chrono>

class TextPopAnimation {
public:
    using Clock = std::chrono::steady_clock;
    void start(Clock::time_point now = Clock::now()) { started_ = now; active_ = true; }
    void reset() { active_ = false; }
    double scale(Clock::time_point now = Clock::now()) const {
        if (!active_) return 1.0;
        const double age = std::chrono::duration<double>(now - started_).count();
        const double t = std::clamp(age / 0.20, 0.0, 1.0) - 1.0;
        const double easeOutBack = 1.0 + 2.70158 * t * t * t + 1.70158 * t * t;
        return 0.85 + 0.15 * easeOutBack;
    }
private:
    Clock::time_point started_{};
    bool active_{};
};
