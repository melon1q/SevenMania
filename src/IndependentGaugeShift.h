#pragma once

#include "IndependentGauge.h"

namespace sevenmania::independent_gauge {

enum class Mode { None, Continue, SurvivalToGroove, BestClear, SelectToUnder };

class Selector {
public:
    bool configure(Kind selected, Mode mode, Kind bottom) noexcept {
        selected_ = Kind::AssistEasy;
        current_ = selected_;
        bottom_ = Kind::AssistEasy;
        mode_ = Mode::None;
        configured_ = false;
        changed_ = false;
        failed_ = true;
        const auto initial = static_cast<unsigned>(selected);
        const auto lower = static_cast<unsigned>(bottom);
        if (initial > 8 || lower > 5 ||
            (mode != Mode::None && mode != Mode::Continue && mode != Mode::SurvivalToGroove &&
             mode != Mode::BestClear && mode != Mode::SelectToUnder)) return false;
        selected_ = selected;
        current_ = selected;
        bottom_ = bottom;
        mode_ = mode;
        configured_ = true;
        failed_ = false;
        return true;
    }

    void update(const Bank& bank) noexcept {
        changed_ = false;
        if (!configured_) return;
        if (mode_ == Mode::SurvivalToGroove) {
            if (static_cast<unsigned>(current_) <= 5 && bank.value(current_) <= 0.0f)
                current_ = Kind::Normal;
        } else if (mode_ == Mode::BestClear || mode_ == Mode::SelectToUnder) {
            const auto position = static_cast<unsigned>(current_);
            const bool course = static_cast<unsigned>(selected_) >= 6;
            const auto start = course ? 6u : (std::min)(position, static_cast<unsigned>(bottom_));
            const auto end = mode_ == Mode::BestClear ? (course ? 8u : 5u)
                                                      : static_cast<unsigned>(selected_);
            current_ = static_cast<Kind>(start);
            for (auto candidate = start; candidate <= end; ++candidate) {
                const auto kind = static_cast<Kind>(candidate);
                if (bank.qualified(kind)) current_ = kind;
            }
        }
        changed_ = selected_ != current_;
        if (mode_ == Mode::None && bank.value(current_) <= 0.0f) failed_ = true;
    }

    Kind current() const noexcept { return current_; }
    bool changed() const noexcept { return changed_; }
    bool failed() const noexcept { return failed_; }

private:
    Kind selected_ = Kind::AssistEasy;
    Kind current_ = Kind::AssistEasy;
    Kind bottom_ = Kind::AssistEasy;
    Mode mode_ = Mode::None;
    bool configured_ = false;
    bool changed_ = false;
    bool failed_ = true;
};

} // namespace sevenmania::independent_gauge
