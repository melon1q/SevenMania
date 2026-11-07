#pragma once
#include <algorithm>
#include <cmath>

// Keep a continuous clock between coarse driver reports. Drift is corrected by
// changing the rate slightly, never by jumping the displayed note position.
class AudioPositionClock {
public:
    void reset() { *this = AudioPositionClock{}; }
    double update(double rawSeconds, double wallSeconds) {
        if (!started_) {
            if (rawSeconds <= 0) return 0;
            started_ = true;
            lastRaw_ = lastResult_ = rawSeconds;
            lastWall_ = wallSeconds;
            return rawSeconds;
        }
        double predicted = lastResult_ + std::max(0.0, wallSeconds - lastWall_) * rate_;
        if (rawSeconds > lastRaw_) {
            aheadLimit_ = std::clamp(std::max(aheadLimit_, (rawSeconds - lastRaw_) * 1.5), 0.125, 0.5);
            const double error = rawSeconds - predicted;
            rate_ = std::clamp(1.0 + error * 0.2, 0.98, 1.02);
            lastRaw_ = rawSeconds;
        }
        predicted = std::min(predicted, rawSeconds + aheadLimit_);
        lastResult_ = std::max(lastResult_, predicted);
        lastWall_ = wallSeconds;
        return lastResult_;
    }
private:
    bool started_{};
    double lastWall_{}, lastRaw_{}, lastResult_{}, rate_ = 1.0, aheadLimit_ = 0.125;
};
