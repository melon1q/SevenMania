#pragma once
// BMS difficulty tables (the format beatoraja and LR2 table tools read): a page whose
// <meta name="bmstable" content="header.json"> points at a header {name, symbol, data_url, level_order},
// whose data_url is an array of {md5, sha256, level, title, artist}. A table URL may also point at the
// header JSON directly. This file parses and caches; DifficultyTableFetch.h downloads.
#include "JsonValue.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace bms::table {
struct Entry { std::wstring md5, sha256, level, title, artist; };
struct Table {
    std::wstring url;        // what the player entered
    std::wstring headerUrl;  // where the header came from (data_url resolves against it)
    std::wstring name, symbol, dataUrl;
    std::vector<std::wstring> levelOrder; // from the header; may be empty
    std::vector<Entry> entries;
};

inline std::wstring lowerAscii(std::wstring text) {
    for (auto& c : text) if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    return text;
}
inline std::wstring trimmed(const std::wstring& text) {
    size_t first = 0, last = text.size();
    while (first < last && std::iswspace(text[first])) ++first;
    while (last > first && std::iswspace(text[last - 1])) --last;
    return text.substr(first, last - first);
}

// UTF-8 (with or without BOM); bytes that are not valid UTF-8 are read as Shift_JIS (CP932), which
// many Japanese table pages still use.
inline std::wstring decodeText(const std::string& bytes) {
    size_t skip = bytes.size() >= 3 && bytes.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
    const int size = static_cast<int>(std::min<size_t>(bytes.size() - skip, 0x7fffffff));
    if (size <= 0) return {};
    for (const UINT codePage : {UINT(CP_UTF8), UINT(932)}) {
        const DWORD flags = codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
        const int length = MultiByteToWideChar(codePage, flags, bytes.data() + skip, size, nullptr, 0);
        if (length <= 0) continue;
        std::wstring text(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(codePage, flags, bytes.data() + skip, size, text.data(), length);
        return text;
    }
    return {};
}

// content of <meta name="bmstable" content="...">, attributes in any order and either quote style.
inline std::optional<std::wstring> findBmstableMeta(const std::wstring& html) {
    const auto lower = lowerAscii(html);
    for (size_t at = lower.find(L"<meta"); at != std::wstring::npos; at = lower.find(L"<meta", at + 5)) {
        const size_t end = lower.find(L'>', at);
        if (end == std::wstring::npos) break;
        const auto tag = lower.substr(at, end - at), original = html.substr(at, end - at);
        const auto attribute = [&](const std::wstring& name) -> std::optional<std::wstring> {
            for (size_t p = tag.find(name); p != std::wstring::npos; p = tag.find(name, p + 1)) {
                if (p > 0 && !std::iswspace(tag[p - 1])) continue;
                size_t q = p + name.size();
                while (q < tag.size() && std::iswspace(tag[q])) ++q;
                if (q >= tag.size() || tag[q] != L'=') continue;
                ++q;
                while (q < tag.size() && std::iswspace(tag[q])) ++q;
                if (q >= tag.size()) return std::nullopt;
                const wchar_t quote = tag[q] == L'"' || tag[q] == L'\'' ? tag[q] : 0;
                const size_t start = quote ? q + 1 : q;
                size_t stop = start;
                while (stop < tag.size() && (quote ? tag[stop] != quote : !std::iswspace(tag[stop]) && tag[stop] != L'/')) ++stop;
                return original.substr(start, stop - start);
            }
            return std::nullopt;
        };
        if (const auto name = attribute(L"name"); name && lowerAscii(*name) == L"bmstable")
            if (const auto content = attribute(L"content"); content && !trimmed(*content).empty()) return trimmed(*content);
    }
    return std::nullopt;
}

// A reference from a page or header, resolved against the URL it came from.
inline std::wstring resolveUrl(const std::wstring& base, const std::wstring& reference) {
    const auto ref = trimmed(reference);
    const auto scheme = ref.find(L"://");
    if (scheme != std::wstring::npos && ref.find_first_of(L"/?#") > scheme) return ref;
    const auto baseScheme = base.find(L"://");
    if (baseScheme == std::wstring::npos) return ref;
    if (ref.starts_with(L"//")) return base.substr(0, baseScheme + 1) + ref;
    const size_t hostEnd = base.find(L'/', baseScheme + 3);
    const std::wstring origin = hostEnd == std::wstring::npos ? base : base.substr(0, hostEnd);
    if (ref.starts_with(L"/")) return origin + ref;
    std::wstring directory = hostEnd == std::wstring::npos ? L"/" : base.substr(hostEnd);
    if (const auto query = directory.find_first_of(L"?#"); query != std::wstring::npos) directory.resize(query);
    directory.resize(directory.rfind(L'/') + 1);
    std::vector<std::wstring> parts;
    const auto push = [&](const std::wstring& path) {
        size_t start = 0;
        while (start <= path.size()) {
            const size_t slash = path.find(L'/', start);
            const auto part = path.substr(start, slash == std::wstring::npos ? std::wstring::npos : slash - start);
            if (part == L"..") { if (!parts.empty()) parts.pop_back(); }
            else if (part != L"." && !part.empty()) parts.push_back(part);
            if (slash == std::wstring::npos) break;
            start = slash + 1;
        }
    };
    push(directory); push(ref);
    std::wstring out = origin;
    for (const auto& part : parts) out += L"/" + part;
    if ((ref.empty() || ref.ends_with(L"/")) && !out.ends_with(L"/")) out += L"/";
    return out;
}

// A JSON level may be a string or a number; numbers print without a trailing ".0".
inline std::wstring levelText(const JsonValue& value) {
    if (value.type == JsonValue::Type::String) return trimmed(value.string);
    if (value.type != JsonValue::Type::Number) return {};
    if (value.number == std::floor(value.number) && std::abs(value.number) < 1e15) return std::to_wstring(static_cast<long long>(value.number));
    auto text = std::to_wstring(value.number);
    while (!text.empty() && text.back() == L'0') text.pop_back();
    if (!text.empty() && text.back() == L'.') text.pop_back();
    return text;
}

// Fills name, symbol, data_url and level_order. False when the text is not a table header.
inline bool parseHeader(const std::wstring& json, Table& table) {
    try {
        const auto root = JsonReader(json).read();
        if (root.type != JsonValue::Type::Object) return false;
        table.name = trimmed(root[L"name"].str());
        table.symbol = trimmed(root[L"symbol"].str());
        table.dataUrl = trimmed(root[L"data_url"].str());
        table.levelOrder.clear();
        for (const auto& level : root[L"level_order"].array) if (auto text = levelText(level); !text.empty()) table.levelOrder.push_back(text);
        return !table.dataUrl.empty();
    } catch (...) { return false; }
}

inline std::wstring lowerHex(std::wstring text) { return lowerAscii(trimmed(std::move(text))); }

// The chart list. Entries without an MD5 or SHA-256 cannot be matched and are dropped.
inline bool parseData(const std::wstring& json, Table& table) {
    try {
        const auto root = JsonReader(json).read();
        if (root.type != JsonValue::Type::Array) return false;
        table.entries.clear();
        for (const auto& item : root.array) {
            if (item.type != JsonValue::Type::Object) continue;
            Entry entry{lowerHex(item[L"md5"].str()), lowerHex(item[L"sha256"].str()), levelText(item[L"level"]),
                trimmed(item[L"title"].str()), trimmed(item[L"artist"].str())};
            if (entry.md5.size() != 32 && entry.sha256.size() != 64) continue;
            if (entry.level.empty()) entry.level = L"?";
            table.entries.push_back(std::move(entry));
        }
        return true;
    } catch (...) { return false; }
}

// Levels in display order: the header's level_order first, then any other level the data uses,
// numerically where both are numbers, otherwise by text.
inline std::vector<std::wstring> levels(const Table& table) {
    std::vector<std::wstring> out;
    std::set<std::wstring> seen;
    for (const auto& level : table.levelOrder) if (seen.insert(level).second) out.push_back(level);
    std::vector<std::wstring> rest;
    for (const auto& entry : table.entries) if (seen.insert(entry.level).second) rest.push_back(entry.level);
    const auto number = [](const std::wstring& text) -> std::optional<double> {
        try { size_t used{}; const double value = std::stod(text, &used); if (used == text.size()) return value; } catch (...) {}
        return std::nullopt;
    };
    std::stable_sort(rest.begin(), rest.end(), [&](const std::wstring& a, const std::wstring& b) {
        const auto x = number(a), y = number(b);
        if (x && y) return *x < *y;
        if (x != y) return x.has_value(); // numbers before text
        return a < b;
    });
    out.insert(out.end(), rest.begin(), rest.end());
    return out;
}

// ---- cache: one folder per table, keyed by the entered URL -----------------------------------
inline std::wstring cacheKey(const std::wstring& url) {
    uint64_t hash = 14695981039346656037ull;
    for (const wchar_t c : url) { hash ^= static_cast<uint16_t>(c); hash *= 1099511628211ull; }
    wchar_t text[17]{};
    swprintf_s(text, L"%016llx", static_cast<unsigned long long>(hash));
    return text;
}
inline bool writeFile(const std::filesystem::path& path, const std::string& bytes) {
    auto temp = path; temp += L".tmp";
    { std::ofstream out(temp, std::ios::binary | std::ios::trunc); out.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); if (!out) return false; }
    return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}
inline std::optional<std::string> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
inline std::string utf8(const std::wstring& text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(std::max(0, size)), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}
// Saves the downloaded header and data as they came, so a later version can parse them again.
inline bool saveCache(const std::filesystem::path& directory, const Table& table, const std::string& header, const std::string& data) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    const auto base = directory / cacheKey(table.url);
    auto headerPath = base; headerPath += L".header.json";
    auto dataPath = base; dataPath += L".data.json";
    auto metaPath = base; metaPath += L".source.txt";
    return writeFile(headerPath, header) && writeFile(dataPath, data) && writeFile(metaPath, utf8(table.url + L"\n" + table.headerUrl + L"\n"));
}
inline std::optional<Table> loadCache(const std::filesystem::path& directory, const std::wstring& url) {
    const auto base = directory / cacheKey(url);
    auto headerPath = base; headerPath += L".header.json";
    auto dataPath = base; dataPath += L".data.json";
    auto metaPath = base; metaPath += L".source.txt";
    const auto header = readFile(headerPath), data = readFile(dataPath), meta = readFile(metaPath);
    if (!header || !data || !meta) return std::nullopt;
    Table table;
    table.url = url;
    const auto lines = decodeText(*meta);
    const auto first = lines.find(L'\n');
    if (first == std::wstring::npos || lines.substr(0, first) != url) return std::nullopt;
    table.headerUrl = trimmed(lines.substr(first + 1));
    if (!parseHeader(decodeText(*header), table) || !parseData(decodeText(*data), table)) return std::nullopt;
    return table;
}
inline void removeCache(const std::filesystem::path& directory, const std::wstring& url) {
    const auto base = directory / cacheKey(url);
    std::error_code ec;
    for (const wchar_t* suffix : {L".header.json", L".data.json", L".source.txt"}) { auto path = base; path += suffix; std::filesystem::remove(path, ec); }
}

// ---- matching the library --------------------------------------------------------------------
// For one table: each level's entries, and how many of them the library has.
struct LevelView { std::wstring level; std::set<std::wstring> md5, sha256; size_t total{}, owned{}; };
inline std::vector<LevelView> levelViews(const Table& table, const std::set<std::wstring>& libraryMd5, const std::set<std::wstring>& librarySha256) {
    std::vector<LevelView> out;
    std::map<std::wstring, size_t> at;
    for (const auto& level : levels(table)) { at[level] = out.size(); out.push_back({level, {}, {}, 0, 0}); }
    for (const auto& entry : table.entries) {
        auto& view = out[at[entry.level]];
        ++view.total;
        if (!entry.md5.empty()) view.md5.insert(entry.md5);
        if (!entry.sha256.empty()) view.sha256.insert(entry.sha256);
        if ((!entry.md5.empty() && libraryMd5.contains(entry.md5)) || (!entry.sha256.empty() && librarySha256.contains(entry.sha256))) ++view.owned;
    }
    return out;
}
inline bool levelHas(const LevelView& view, const std::wstring& md5, const std::wstring& sha256) {
    return (!md5.empty() && view.md5.contains(md5)) || (!sha256.empty() && view.sha256.contains(sha256));
}
} // namespace bms::table
