#pragma once
// Persistent best-play records (BmsPlayer-records.ini).
// Owns the record map and its INI file; knows nothing about charts, windows or UI.
// The host maps chart indices to normalized keys and repaints after a save.
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

struct RecordJudgement {std::wstring spec=L"LEGACY";int rank=-1;};
struct PlayRecord {
    bool played{};
    bool cleared{};
    unsigned maxCombo{};
    int bestAccuracyScore{};
    RecordJudgement scoreJudgement,comboJudgement,clearJudgement;
};

// Case-insensitive absolute path used as the record key.
inline std::wstring normalizedRecordPath(const std::filesystem::path& path) {
    if (path.empty()) return {};
    std::error_code ec;
    auto normalized = std::filesystem::absolute(path, ec);
    if (ec) normalized = path;
    std::wstring value = normalized.lexically_normal().wstring();
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}

// FNV-1a over the key's UTF-16 code units. Also used for practice preset sections,
// so the hash must stay stable across versions.
inline std::wstring recordSectionForPath(const std::wstring& path) {
    uint64_t hash = 14695981039346656037ull;
    for (wchar_t ch : path) {
        const uint32_t value = static_cast<uint32_t>(ch);
        for (size_t byte = 0; byte < sizeof(wchar_t); ++byte) {
            hash ^= static_cast<uint8_t>(value >> (byte * 8));
            hash *= 1099511628211ull;
        }
    }
    return L"Play_" + std::to_wstring(hash);
}

struct PlayRecordStore {
    std::filesystem::path file;
    std::unordered_map<std::wstring, PlayRecord> records;

    void load() {
        records.clear();
        if (file.empty()) return;
        DWORD capacity = 4096;
        std::vector<wchar_t> sections;
        DWORD length = 0;
        for (;;) {
            sections.assign(capacity, L'\0');
            length = GetPrivateProfileSectionNamesW(sections.data(), capacity, file.c_str());
            if (length < capacity - 2 || capacity >= 1u << 20) break;
            capacity *= 2;
        }
        for (DWORD offset = 0; offset < length;) {
            const std::wstring section(sections.data() + offset);
            offset += static_cast<DWORD>(section.size()) + 1;
            if (!section.starts_with(L"Play_")) continue;
            std::vector<wchar_t> storedPath(32768, L'\0');
            const DWORD pathLength = GetPrivateProfileStringW(section.c_str(), L"Path", L"", storedPath.data(),
                static_cast<DWORD>(storedPath.size()), file.c_str());
            if (pathLength == 0 || pathLength >= storedPath.size()) continue;
            std::wstring path(storedPath.data(), pathLength);
            const auto key = normalizedRecordPath(path);
            if (key.empty() || recordSectionForPath(key) != section) continue;
            PlayRecord record;
            record.played = GetPrivateProfileIntW(section.c_str(), L"Played", 0, file.c_str()) != 0;
            record.cleared = GetPrivateProfileIntW(section.c_str(), L"Cleared", 0, file.c_str()) != 0;
            record.maxCombo = static_cast<unsigned>(std::max(0, static_cast<int>(GetPrivateProfileIntW(section.c_str(), L"MaxCombo", 0, file.c_str()))));
            record.bestAccuracyScore = std::clamp(static_cast<int>(GetPrivateProfileIntW(section.c_str(), L"AccuracyScore", 0, file.c_str())), 0, 1'000'000);
            for(auto pair:{std::pair{L"Score",&record.scoreJudgement},std::pair{L"Combo",&record.comboJudgement},std::pair{L"Clear",&record.clearJudgement}}) {
                wchar_t value[128]{};const std::wstring prefix=pair.first;
                GetPrivateProfileStringW(section.c_str(),(prefix+L"JudgeSpec").c_str(),L"LEGACY",value,128,file.c_str());
                pair.second->spec=value;
                pair.second->rank=static_cast<int>(GetPrivateProfileIntW(section.c_str(),(prefix+L"JudgeRank").c_str(),-1,file.c_str()));
            }
            records[key] = record;
        }
    }

    // Returns the record only when it has been played at least once.
    const PlayRecord* findPlayed(const std::wstring& key) const {
        const auto found = records.find(key);
        return found == records.end() || !found->second.played ? nullptr : &found->second;
    }

    // Merges a finished play into the best record and writes it through.
    // Returns false (and changes nothing) when there is no key or no file.
    bool save(const std::filesystem::path& chartPath, bool cleared, unsigned maxCombo, int accuracyScore,
        const std::wstring& spec=L"LEGACY", int rank=-1) {
        const auto key = normalizedRecordPath(chartPath);
        if (key.empty() || file.empty()) return false;
        auto& record = records[key];
        const RecordJudgement provenance{spec,rank};
        if(!record.played || accuracyScore>record.bestAccuracyScore)record.scoreJudgement=provenance;
        if(!record.played || maxCombo>record.maxCombo)record.comboJudgement=provenance;
        if(!record.played || (cleared&&!record.cleared))record.clearJudgement=provenance;
        record.played = true;
        record.cleared = record.cleared || cleared;
        record.maxCombo = std::max(record.maxCombo, maxCombo);
        record.bestAccuracyScore = std::max(record.bestAccuracyScore, std::clamp(accuracyScore, 0, 1'000'000));
        const auto section = recordSectionForPath(key);
        WritePrivateProfileStringW(section.c_str(), L"Path", key.c_str(), file.c_str());
        WritePrivateProfileStringW(section.c_str(), L"Played", L"1", file.c_str());
        WritePrivateProfileStringW(section.c_str(), L"Cleared", record.cleared ? L"1" : L"0", file.c_str());
        WritePrivateProfileStringW(section.c_str(), L"MaxCombo", std::to_wstring(record.maxCombo).c_str(), file.c_str());
        WritePrivateProfileStringW(section.c_str(), L"AccuracyScore", std::to_wstring(record.bestAccuracyScore).c_str(), file.c_str());
        for(auto pair:{std::pair{L"Score",&record.scoreJudgement},std::pair{L"Combo",&record.comboJudgement},std::pair{L"Clear",&record.clearJudgement}}) {
            const std::wstring prefix=pair.first;
            WritePrivateProfileStringW(section.c_str(),(prefix+L"JudgeSpec").c_str(),pair.second->spec.c_str(),file.c_str());
            WritePrivateProfileStringW(section.c_str(),(prefix+L"JudgeRank").c_str(),std::to_wstring(pair.second->rank).c_str(),file.c_str());
        }
        return true;
    }
};
