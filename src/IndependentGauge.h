#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace sevenmania::independent_gauge {

enum class Profile { SevenKeys, BeatorajaLR2, LR2oraja };
enum class Kind {
    AssistEasy, Easy, Normal, Hard, ExHard, Hazard, Class, ExClass, ExHardClass
};
enum class Event { PGreat, Great, Good, Bad, Poor, EmptyPoor };

// A bank owns all nine gauges; no chart or player objects are required.
class Bank {
public:
    bool configure(Profile profile, double total, std::size_t notes) noexcept {
        values_.fill(0.0f);
        ready_ = false;
        if ((profile != Profile::SevenKeys && profile != Profile::BeatorajaLR2 && profile != Profile::LR2oraja) ||
            !std::isfinite(total) || total <= 0.0 || notes == 0) {
            return false;
        }
        profile_ = profile;
        totalPerNote_ = total / static_cast<double>(notes);
        // The upper bound also handles an overflow in 2 * total.
        hardGain_ = std::clamp((2.0 * total - 320.0) /
                                   static_cast<double>(notes), 0.0, 0.15) / 0.15;
        lr2Damage_ = profile == Profile::LR2oraja
            ? std::max(lr2orajaTotalFactor(total), lr2orajaNoteFactor(notes))
            : std::max(totalDamageFactor(total), noteDamageFactor(notes));
        for (std::size_t i = 0; i < count; ++i) {
            values_[i] = i < 3 ? 20.0f : 100.0f;
        }
        ready_ = true;
        return true;
    }

    void apply(Event event, float rate = 1.0f) noexcept {
        const auto e = static_cast<std::size_t>(event);
        if (!ready_ || e >= eventCount || !std::isfinite(rate) || rate < 0.0f) return;
        const auto& table = profile_ == Profile::SevenKeys ? sevenKeys : lr2;
        for (std::size_t i = 0; i < count; ++i) {
            if (values_[i] <= 0.0f) continue;
            double delta = static_cast<double>(table[i][e]) * rate;
            if (delta > 0.0) {
                if (i < 3) delta *= totalPerNote_;
                else if (profile_ == Profile::SevenKeys && (i == 3 || i == 4))
                    delta *= hardGain_;
            } else if (delta < 0.0) {
                if (profile_ != Profile::SevenKeys && (i == 3 || i == 4))
                    delta *= lr2Damage_;
                delta *= guts(i, values_[i]);
            }
            update(i, delta);
        }
    }

    void mineDamage(float damage) noexcept {
        if (!ready_ || !std::isfinite(damage) || damage < 0.0f) return;
        for (std::size_t i = 0; i < count; ++i) {
            if (values_[i] > 0.0f) update(i, -static_cast<double>(damage));
        }
    }

    void hcnTick(bool held) noexcept { apply(held ? Event::Great : Event::Bad, 0.5f); }

    float value(Kind kind) const noexcept {
        const auto i = static_cast<std::size_t>(kind);
        return i < count ? values_[i] : 0.0f;
    }

    bool qualified(Kind kind) const noexcept {
        const auto i = static_cast<std::size_t>(kind);
        if (!ready_ || i >= count || values_[i] <= 0.0f) return false;
        const float border = i == 0 ? 60.0f : i < 3 ? 80.0f : 0.0f;
        return values_[i] >= border;
    }

    // Intended for controlled state setup. A failed gauge cannot be revived.
    void setValue(Kind kind, float value) noexcept {
        const auto i = static_cast<std::size_t>(kind);
        if (!ready_ || i >= count || values_[i] <= 0.0f || !std::isfinite(value)) return;
        values_[i] = std::clamp(value, minimum(i), 100.0f);
        applyFailThreshold(i);
    }

private:
    static constexpr std::size_t count = 9;
    static constexpr std::size_t eventCount = 6;
    using Table = std::array<std::array<float, eventCount>, count>;
    inline static constexpr Table sevenKeys{{
        {{1, 1, .5f, -1.5f, -3, -.5f}},
        {{1, 1, .5f, -1.5f, -4.5f, -1}},
        {{1, 1, .5f, -3, -6, -2}},
        {{.15f, .12f, .03f, -5, -10, -5}},
        {{.15f, .06f, 0, -8, -16, -8}},
        {{.15f, .06f, 0, -100, -100, -10}},
        {{.15f, .12f, .06f, -1.5f, -3, -1.5f}},
        {{.15f, .12f, .03f, -3, -6, -3}},
        {{.15f, .06f, 0, -5, -10, -5}}
    }};
    inline static constexpr Table lr2{{
        {{1.2f, 1.2f, .6f, -3.2f, -4.8f, -1.6f}},
        {{1.2f, 1.2f, .6f, -3.2f, -4.8f, -1.6f}},
        {{1, 1, .5f, -4, -6, -2}},
        {{.1f, .1f, .05f, -6, -10, -2}},
        {{.1f, .1f, .05f, -12, -20, -2}},
        {{.15f, .06f, 0, -100, -100, -10}},
        {{.1f, .1f, .05f, -2, -3, -2}},
        {{.1f, .1f, .05f, -6, -10, -2}},
        {{.1f, .1f, .05f, -12, -20, -2}}
    }};

    static constexpr float minimum(std::size_t i) noexcept { return i < 3 ? 2.0f : 0.0f; }

    static double totalDamageFactor(double total) noexcept {
        constexpr std::array<double, 9> thresholds{{240, 230, 210, 200, 180, 160, 150, 130, 120}};
        constexpr std::array<double, 9> factors{{1, 1.11, 1.25, 1.5, 1.666, 2, 2.5, 3.333, 5}};
        for (std::size_t i = 0; i < thresholds.size(); ++i)
            if (total >= thresholds[i]) return factors[i];
        return 10;
    }

    static double noteDamageFactor(std::size_t notes) noexcept {
        double factor = 1.0;
        double weight = 0.002;
        for (std::size_t current = 1000; current > 1; current /= 2, weight *= 2) {
            const auto lower = current / 2;
            const auto position = std::max(notes, lower);
            factor += weight * (static_cast<double>(current) - static_cast<double>(position));
        }
        return factor;
    }

    static double lr2orajaTotalFactor(double total) noexcept {
        return 10.0 / std::clamp(std::floor(total / 16.0) - 5.0, 1.0, 10.0);
    }

    static double lr2orajaNoteFactor(std::size_t notes) noexcept {
        if (notes <= 20) return 10.0;
        if (notes < 30) return 8.0 + 0.2 * (30.0 - static_cast<double>(notes));
        if (notes < 60) return 5.0 + 0.2 * (60.0 - static_cast<double>(notes)) / 3.0;
        if (notes < 125) return 4.0 + (125.0 - static_cast<double>(notes)) / 65.0;
        if (notes < 250) return 3.0 + 0.008 * (250.0 - static_cast<double>(notes));
        if (notes < 500) return 2.0 + 0.004 * (500.0 - static_cast<double>(notes));
        if (notes < 1000) return 1.0 + 0.002 * (1000.0 - static_cast<double>(notes));
        return 1.0;
    }

    double guts(std::size_t i, float before) const noexcept {
        if (profile_ == Profile::SevenKeys && (i == 3 || i == 6)) {
            const float step = i == 3 ? 10.0f : 5.0f;
            for (int n = 1; n <= 5; ++n)
                if (before < step * n) return 0.3 + 0.1 * n;
        }
        if (profile_ != Profile::SevenKeys && (i == 3 || i == 6 || i == 7) &&
            before < (profile_ == Profile::LR2oraja ? 32.0f : 30.0f))
            return 0.6;
        return 1.0;
    }

    void update(std::size_t i, double delta) noexcept {
        const double next = static_cast<double>(values_[i]) + delta;
        values_[i] = static_cast<float>(std::clamp(next, static_cast<double>(minimum(i)), 100.0));
        applyFailThreshold(i);
    }

    void applyFailThreshold(std::size_t i) noexcept {
        if (profile_ == Profile::LR2oraja && i >= 3 && values_[i] < 2.0f) values_[i] = 0.0f;
    }

    std::array<float, count> values_{};
    Profile profile_ = Profile::SevenKeys;
    double totalPerNote_ = 0.0;
    double hardGain_ = 0.0;
    double lr2Damage_ = 1.0;
    bool ready_ = false;
};

} // namespace sevenmania::independent_gauge
