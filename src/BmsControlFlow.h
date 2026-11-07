#pragma once
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <random>
#include <string>
#include <vector>

namespace bms {
// Control flow is evaluated before any asset/timing declaration is collected.
// One seed per session/chart keeps scanning, previews and reloads on the same branch.
class BmsControlFlow {
    struct Random { int value{}; bool enabled{}; };
    struct Block {
        bool isSwitch{}, parent{}, active{}, matched{}, skipped{}, sawElse{};
        int selection{};
    };
    std::vector<Random> randoms_;
    std::vector<Block> blocks_;
    std::mt19937_64 rng_;
    static bool number(const std::wstring& text, int& n) {
        try { size_t used{}; n = std::stoi(text, &used); return used == text.size(); }
        catch (...) { return false; }
    }
public:
    std::vector<int> selections;
    explicit BmsControlFlow(uint64_t seed) : rng_(seed) {}
    bool active() const { return blocks_.empty() || blocks_.back().active; }
    bool consume(const std::wstring& key, const std::wstring& value, std::vector<std::wstring>& errors) {
        int n{};
        const auto error = [&] { errors.push_back(L"잘못된 조건문: #" + key + L" " + value); };
        if (key == L"random" || key == L"setrandom") {
            if (!number(value, n) || n <= 0) { error(); n = 1; }
            const bool enabled = active();
            const int selected = enabled ? (key == L"setrandom" ? n : std::uniform_int_distribution<int>(1, n)(rng_)) : 0;
            randoms_.push_back({selected, enabled});
            if (enabled) selections.push_back(selected);
        } else if (key == L"endrandom") {
            if (randoms_.empty()) error(); else randoms_.pop_back();
        } else if (key == L"if") {
            if (!number(value, n) || randoms_.empty()) error();
            const bool parent = active();
            const bool match = !randoms_.empty() && randoms_.back().enabled && randoms_.back().value == n;
            blocks_.push_back({false, parent, parent && match, match});
        } else if (key == L"elseif" || key == L"else") {
            if (blocks_.empty() || blocks_.back().isSwitch || blocks_.back().sawElse) { error(); return true; }
            auto& b = blocks_.back();
            bool match = !b.matched;
            if (key == L"elseif") {
                if (!number(value, n) || randoms_.empty()) { error(); match = false; }
                else match = match && randoms_.back().value == n;
            } else b.sawElse = true;
            b.active = b.parent && match; b.matched |= match;
        } else if (key == L"endif") {
            if (blocks_.empty() || blocks_.back().isSwitch) error(); else blocks_.pop_back();
        } else if (key == L"switch" || key == L"setswitch") {
            if (!number(value, n) || n <= 0) { error(); n = 1; }
            const bool parent = active();
            const int selected = parent ? (key == L"setswitch" ? n : std::uniform_int_distribution<int>(1, n)(rng_)) : 0;
            blocks_.push_back({true, parent, false, false, false, false, selected});
            if (parent) selections.push_back(selected);
        } else if (key == L"case" || key == L"def" || key == L"skip") {
            if (blocks_.empty() || !blocks_.back().isSwitch) { error(); return true; }
            auto& b = blocks_.back();
            if (key == L"skip") { if (b.active) b.skipped = true; b.active = false; }
            else {
                bool match = key == L"def" ? !b.matched : number(value, n) && n == b.selection;
                b.matched |= match;
                b.active = b.parent && !b.skipped && b.matched; // CASE fallthrough is deliberate.
            }
        } else if (key == L"endsw" || key == L"endswitch") {
            if (blocks_.empty() || !blocks_.back().isSwitch) error(); else blocks_.pop_back();
        } else return false;
        return true;
    }
    void finish(std::vector<std::wstring>& errors) const {
        if (!blocks_.empty()) errors.push_back(L"닫히지 않은 #IF 또는 #SWITCH 블록이 있습니다.");
        // ENDRANDOM is optional in widely distributed legacy charts.
    }
};
} // namespace bms
