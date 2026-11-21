#pragma once

#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

// Named note sheets in <skin folder>/Notes: "<name>-NoteGroup.png" (e.g. 000-NoteGroup.png).
// The name is the file name without the "-NoteGroup" suffix; the launcher lists these names.
// The optional layout file sits next to the sheet: <name>-NoteGroup.ini.
inline std::vector<std::pair<std::wstring, std::filesystem::path>> skinNoteGroupFiles(const std::filesystem::path& root) {
    constexpr wchar_t suffix[] = L"-NoteGroup";
    constexpr size_t suffixLength = sizeof(suffix) / sizeof(wchar_t) - 1;
    std::vector<std::pair<std::wstring, std::filesystem::path>> files;
    std::error_code error;
    for (std::filesystem::directory_iterator it(root / L"Notes", error), end; !error && it != end; it.increment(error)) {
        const auto path = it->path();
        if (_wcsicmp(path.extension().c_str(), L".png") != 0 || !it->is_regular_file(error)) continue;
        const auto stem = path.stem().wstring();
        if (stem.size() <= suffixLength || _wcsicmp(stem.c_str() + stem.size() - suffixLength, suffix) != 0) continue;
        files.emplace_back(stem.substr(0, stem.size() - suffixLength), path);
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return _wcsicmp(a.first.c_str(), b.first.c_str()) < 0;
    });
    // Two files that differ only by case cannot be told apart; keep the first.
    files.erase(std::unique(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return _wcsicmp(a.first.c_str(), b.first.c_str()) == 0;
    }), files.end());
    return files;
}

// Setting values can be non-ASCII while the INI may be ANSI, so they are stored as "utf8:<hex>".
inline std::wstring skinSettingEncode(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string bytes(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), bytes.data(), size, nullptr, nullptr);
    std::wstring out = L"utf8:";
    for (unsigned char c : bytes) { out += L"0123456789abcdef"[c >> 4]; out += L"0123456789abcdef"[c & 15]; }
    return out;
}

inline std::wstring skinSettingDecode(const std::wstring& value) {
    if (!value.starts_with(L"utf8:")) return value;
    if ((value.size() - 5) % 2) return {};
    const auto digit = [](wchar_t c) { return c >= L'0' && c <= L'9' ? c - L'0' : c >= L'a' && c <= L'f' ? c - L'a' + 10 : -1; };
    std::string bytes;
    for (size_t i = 5; i < value.size(); i += 2) {
        const int hi = digit(value[i]), lo = digit(value[i + 1]);
        if (hi < 0 || lo < 0) return {};
        bytes.push_back(static_cast<char>((hi << 4) | lo));
    }
    if (bytes.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), out.data(), size);
    return out;
}
