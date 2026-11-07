#pragma once
#include "SongResourceIdentity.h"

#include "BmsParser.h"
#include "RustBackend.h"
#include <cwctype>
#include <unordered_map>

namespace bms {

inline std::wstring trimSongTitle(std::wstring title) {
    while (!title.empty() && std::iswspace(title.back())) title.pop_back();
    const auto first = title.find_first_not_of(L" \t\r\n");
    return first == std::wstring::npos ? L"" : title.substr(first);
}

inline std::wstring songTitleWithoutChartLabelCpp(const std::wstring& title) {
    auto base = trimSongTitle(title);
    bool stripped = false;
    // BMS titles also use Japanese or full-width brackets around chart labels.
    // Normalize only for grouping; preserve the original display metadata.
    for (auto& character : base) {
        if (character >= L'！' && character <= L'～') character -= 0xfee0;
        if (character == L'　') character = L' ';
        if (character == L'－' || character == L'–' || character == L'—' || character == L'−') character = L'-';
        if (character == L'【' || character == L'［') character = L'[';
        else if (character == L'】' || character == L'］') character = L']';
        else if (character == L'（') character = L'(';
        else if (character == L'）') character = L')';
    }
    // Old charts often put the chart label in #TITLE instead of #SUBTITLE.
    while (!base.empty() && (base.back() == L']' || base.back() == L')')) {
        const bool round = base.back() == L')';
        const wchar_t closing = round ? L')' : L']', opener = round ? L'(' : L'[';
        int depth = 0;
        size_t opening = std::wstring::npos;
        for (size_t i = base.size(); i-- > 0;) {
            if (base[i] == closing) ++depth;
            else if (base[i] == opener && --depth == 0) { opening = i; break; }
        }
        if (opening == std::wstring::npos || opening == 0) break;
        if (round) {
            auto label = toLower(trimSongTitle(base.substr(opening + 1, base.size() - opening - 2)));
            if (label.starts_with(L"sp ") || label.starts_with(L"dp ")) label = trimSongTitle(label.substr(3));
            bool difficulty = false;
            for (const auto* name : {L"beginner", L"basic", L"easy", L"light", L"normal", L"hyper", L"another", L"insane"}) {
                const std::wstring word(name);
                if (label == word || (label.starts_with(word) && label.size() > word.size() &&
                    (std::iswspace(label[word.size()]) || std::iswdigit(label[word.size()])))) difficulty = true;
            }
            // Preserve titles such as Song (Remix) without a corroborating base song.
            if (!difficulty) break;
        }
        auto candidate = trimSongTitle(base.substr(0, opening));
        if (candidate.empty()) break;
        base = std::move(candidate);
        stripped = true;
    }
    // A bare base title is often absent: "Song -SP BEGINNER-" / "Song -SP HYPER-".
    // Accept only a complete difficulty suffix, never an arbitrary title prefix.
    auto difficultySuffix = [](std::wstring label) {
        label = toLower(trimSongTitle(label));
        std::wstring compact;
        for (const auto c : label) {
            if (std::iswspace(c) || c == L'-' || c == L'_' || c == L':' || c == L'~') continue;
            compact += c;
        }
        if (compact.starts_with(L"sp") || compact.starts_with(L"dp")) compact.erase(0, 2);
        for (const auto* name : {L"beginner", L"basic", L"easy", L"light", L"normal", L"hyper", L"another",
                L"insane", L"leggendaria", L"extra", L"expert", L"starter"}) {
            const std::wstring word(name);
            if (!compact.starts_with(word)) continue;
            auto tail = compact.substr(word.size());
            if (tail.starts_with(L"lv")) tail.erase(0, 2);
            while (!tail.empty() && std::iswdigit(tail.front())) tail.erase(0, 1);
            return tail.empty() || tail == L"key" || tail == L"keys";
        }
        return false;
    };
    for (size_t i = 1; i < base.size(); ++i) {
        const auto c = base[i];
        if (!std::iswspace(c) && c != L'-' && c != L'_' && c != L':' && c != L'~') continue;
        if (!difficultySuffix(base.substr(i))) continue;
        auto candidate = trimSongTitle(base.substr(0, i));
        if (!candidate.empty()) return songTitleWithoutChartLabelCpp(candidate);
    }
    return stripped ? base : trimSongTitle(title);
}

inline std::wstring songTitleWithoutChartLabel(const std::wstring& title) {
    std::wstring normalized;
    if (rust_backend::normalizeTitle(title, normalized)) return normalized;
    return songTitleWithoutChartLabelCpp(title);
}

inline std::wstring songDirectoryIdentity(const Chart& chart) {
    return toLower(chart.path.parent_path().lexically_normal().generic_wstring());
}

inline std::wstring songCollectionIdentity(const Chart& chart) {
    namespace fs = std::filesystem;
    const auto chartDirectory = chart.path.parent_path().lexically_normal();
    for (auto ancestor = chartDirectory; !ancestor.empty(); ancestor = ancestor.parent_path()) {
        if (toLower(ancestor.filename().wstring()) == L"songs") {
            const auto relative = chartDirectory.lexically_relative(ancestor);
            if (relative.empty() || relative == L".") return toLower(ancestor.generic_wstring());
            return toLower((ancestor / *relative.begin()).lexically_normal().generic_wstring());
        }
        if (ancestor == ancestor.root_path()) break;
    }
    // Keep the old grouping boundary for charts scanned outside a Songs folder.
    return songDirectoryIdentity(chart);
}

inline std::vector<std::wstring> canonicalSongTitles(const std::vector<Chart>& charts) {
    std::vector<std::wstring> titles;
    titles.reserve(charts.size());
    std::unordered_map<std::wstring, std::vector<size_t>> siblings;
    for (size_t i = 0; i < charts.size(); ++i) {
        titles.push_back(songTitleWithoutChartLabel(charts[i].title));
        siblings[songCollectionIdentity(charts[i])].push_back(i);
    }
    const auto candidates = titles;
    for (const auto& [directory, indices] : siblings) {
        for (size_t i : indices) {
            const auto full = toLower(candidates[i]);
            for (size_t j : indices) {
                const auto& base = candidates[j];
                if (base.empty() || base.size() >= titles[i].size() || !full.starts_with(toLower(base))) continue;
                if (!std::iswspace(full[base.size()])) continue;
                const auto suffix = trimSongTitle(full.substr(base.size()));
                // Parenthesized and dash labels need a corroborating sibling
                // base title. Never merge ordinary prefixes such as Song / Song 2.
                const bool roundLabel = suffix.size() > 2 && suffix.front() == L'(' && suffix.back() == L')';
                const bool dashLabel = suffix.size() > 2 && suffix[0] == L'-' && std::iswspace(suffix[1]);
                if (roundLabel || dashLabel) titles[i] = base;
            }
        }
    }
    return titles;
}

inline std::wstring songGroupIdentity(const Chart& chart, const std::wstring& title, bool collectVariants=false) {
    const auto song=songDirectoryIdentity(chart)+L"|"+toLower(title);
    return collectVariants?song:song+L"|"+songResourceIdentity(chart);
}

} // namespace bms::ui
