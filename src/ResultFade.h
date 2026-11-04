#pragma once
#include <algorithm>
#include <chrono>

// Presentation-only clock: never changes judgement or audio time.
class ResultFade {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto outDuration = std::chrono::milliseconds(200);
    static constexpr auto inDuration = std::chrono::milliseconds(250);
    void start(Clock::time_point now = Clock::now()) { started_ = now; active_ = true; }
    void reset() { active_ = false; }
    bool active() const { return active_; }
    bool outgoing(Clock::time_point now) const { return active_ && now - started_ < outDuration; }
    bool finished(Clock::time_point now) const { return active_ && now - started_ >= outDuration + inDuration; }
    double darkness(Clock::time_point now) const {
        if (!active_) return 0;
        const auto elapsed = now - started_;
        const double t = elapsed < outDuration ?
            std::chrono::duration<double>(elapsed).count() / std::chrono::duration<double>(outDuration).count() :
            1.0 - std::chrono::duration<double>(elapsed - outDuration).count() / std::chrono::duration<double>(inDuration).count();
        const double x = std::clamp(t, 0.0, 1.0);
        return x * x * (3 - 2 * x);
    }
private:
    Clock::time_point started_{};
    bool active_{};
};
