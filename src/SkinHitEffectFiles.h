#pragma once

#include <windows.h>
#include <filesystem>
#include <map>
#include <climits>
#include <string>
#include <vector>

// Numbered PNGs in <skin folder>/HitEffects. Rejected names are reported through "issues".
inline std::map<int, std::filesystem::path> skinHitEffectFiles(const std::filesystem::path& root,
    std::vector<std::wstring>* issues = nullptr) {
    std::map<int, std::filesystem::path> files;
    std::error_code error;
    for (std::filesystem::directory_iterator it(root / L"HitEffects", error), end;
        !error && it != end; it.increment(error)) {
        const auto path = it->path();
        if (_wcsicmp(path.extension().c_str(), L".png") != 0 || !it->is_regular_file(error)) continue;
        const auto stem = path.stem().wstring();
        if (stem.empty() || stem.front() == L'0' || stem.find_first_not_of(L"0123456789") != std::wstring::npos) {
            if (issues) issues->push_back(L"HitEffects/" + path.filename().wstring() +
                L" ignored: the name must be a number without leading zeros (1.png, 2.png, ...).");
            continue;
        }
        const auto number = std::wcstoll(stem.c_str(), nullptr, 10);
        if (number > INT_MAX) {
            if (issues) issues->push_back(L"HitEffects/" + path.filename().wstring() + L" ignored: the number is too large.");
            continue;
        }
        if (!files.emplace(static_cast<int>(number), path).second && issues)
            issues->push_back(L"HitEffects/" + path.filename().wstring() + L" ignored: another file already uses this number.");
    }
    return files;
}

// Frame count of a hit effect sheet: Frames in an optional sibling <number>.ini
// ([HitEffect] Frames=8), otherwise 6. The result is not range-checked.
inline int skinHitEffectFrames(const std::filesystem::path& png) {
    auto ini = png;
    ini.replace_extension(L".ini");
    std::error_code error;
    if (!std::filesystem::is_regular_file(ini, error)) return 6;
    const auto absolute = std::filesystem::absolute(ini, error);
    return static_cast<int>(GetPrivateProfileIntW(L"HitEffect", L"Frames", 6, absolute.c_str()));
}
