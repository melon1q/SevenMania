#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

namespace bms {
// Estimates the player's FAST/SLOW bias from raw hit offsets.
// Samples are "raw": (heardTime - noteTime) in real milliseconds BEFORE the judge offset is applied,
// so changing the judge offset never feeds back into the history. Positive = late (SLOW).
class BiasEstimator {
public:
    static constexpr size_t capacity = 100;
    static constexpr size_t minimumSamples = 30;
    void clear() noexcept { count_ = 0; head_ = 0; }
    void add(double ms) noexcept {
        ring_[head_] = ms; head_ = (head_ + 1) % capacity; if (count_ < capacity) ++count_;
    }
    size_t size() const noexcept { return count_; }
    bool ready() const noexcept { return count_ >= minimumSamples; }
    // Trimmed mean (20% each side) after MAD outlier rejection. Empty until ready().
    std::optional<double> estimate() const noexcept {
        if (!ready()) return std::nullopt;
        std::array<double, capacity> v{}; copyChronological(v);
        const double median = medianOf(v.data(), count_);
        std::array<double, capacity> dev{};
        for (size_t i = 0; i < count_; ++i) dev[i] = std::abs(v[i] - median);
        const double mad = medianOf(dev.data(), count_);
        const double limit = std::max(3.0 * 1.4826 * mad, 15.0);
        std::array<double, capacity> kept{}; size_t n = 0;
        for (size_t i = 0; i < count_; ++i) if (std::abs(v[i] - median) <= limit) kept[n++] = v[i];
        if (n == 0) return median;
        std::sort(kept.begin(), kept.begin() + static_cast<std::ptrdiff_t>(n));
        const size_t trim = n / 5;
        double sum = 0; size_t used = 0;
        for (size_t i = trim; i + trim < n; ++i) { sum += kept[i]; ++used; }
        return used ? sum / static_cast<double>(used) : kept[n / 2];
    }
    // Medians of the older and newer halves of the history (for persistence checks).
    std::optional<std::pair<double, double>> halves() const noexcept {
        if (!ready()) return std::nullopt;
        std::array<double, capacity> v{}; copyChronological(v);
        const size_t mid = count_ / 2;
        return std::pair{medianOf(v.data(), mid), medianOf(v.data() + mid, count_ - mid)};
    }
private:
    static double medianOf(double* data, size_t n) noexcept {
        if (n == 0) return 0;
        std::sort(data, data + static_cast<std::ptrdiff_t>(n));
        return n % 2 ? data[n / 2] : (data[n / 2 - 1] + data[n / 2]) * 0.5;
    }
    void copyChronological(std::array<double, capacity>& out) const noexcept {
        const size_t start = count_ < capacity ? 0 : head_;
        for (size_t i = 0; i < count_; ++i) out[i] = ring_[(start + i) % capacity];
    }
    std::array<double, capacity> ring_{};
    size_t count_{}, head_{};
};

// Moves the judge offset toward the estimated bias in small, damped steps.
class AutoJudgeAdjuster {
public:
    enum Mode { Off = 0, Suggest = 1, Realtime = 2 };
    static constexpr double deadbandMs = 3.0;
    static constexpr int maxStepMs = 2;
    static constexpr int updateInterval = 20;
    static constexpr int limitMs = 100;
    void reset() noexcept { estimator_.clear(); sinceUpdate_ = 0; }
    void feed(double rawMs) noexcept { estimator_.add(rawMs); ++sinceUpdate_; }
    size_t samples() const noexcept { return estimator_.size(); }
    // Next judge offset for the realtime mode; returns the current value when nothing should change.
    int step(int currentMs) noexcept {
        if (sinceUpdate_ < updateInterval) return currentMs;
        sinceUpdate_ = 0;
        const auto target = suggestion(currentMs);
        if (!target) return currentMs;
        const int diff = *target - currentMs;
        const int delta = std::clamp(static_cast<int>(std::lround(diff * 0.5)), -maxStepMs, maxStepMs);
        return std::clamp(currentMs + (delta ? delta : (diff > 0 ? 1 : -1)), -limitMs, limitMs);
    }
    // Full-correction target (rounded ms) when the bias is persistent and larger than the deadband.
    std::optional<int> suggestion(int currentMs) const noexcept {
        const auto t = estimator_.estimate(); const auto h = estimator_.halves();
        if (!t || !h) return std::nullopt;
        const double diff = *t - currentMs;
        if (std::abs(diff) < deadbandMs) return std::nullopt;
        const double older = h->first - currentMs, newer = h->second - currentMs;
        if (std::abs(older) < deadbandMs || std::abs(newer) < deadbandMs || (older > 0) != (diff > 0) || (newer > 0) != (diff > 0))
            return std::nullopt;
        return std::clamp(static_cast<int>(std::lround(*t)), -limitMs, limitMs);
    }
private:
    BiasEstimator estimator_;
    int sinceUpdate_{};
};
} // namespace bms
