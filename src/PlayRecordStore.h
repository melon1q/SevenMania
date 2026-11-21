#pragma once
// Persistent best-play records (BmsPlayer-records.ini).
// Owns the record map and its INI file; knows nothing about charts, windows or UI.
// The host maps chart indices to normalized keys and repaints after a save.
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cwctype>
#include <cwchar>
#include <limits>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

struct RecordJudgement {std::wstring spec=L"LEGACY";int rank=-1;};
// A legacy boolean clear has no recoverable lamp or gauge provenance. Keep it
// independently from bestKnownClear rather than assigning it a guessed rank.
enum class RecordClearLamp : int { Failed, AssistEasy, Easy, Normal, Hard, ExHard, FullCombo, Perfect, Max };
struct RecordClearInfo { RecordClearLamp lamp{RecordClearLamp::Failed}; std::wstring profile, gauge; };
struct PlayRecord {
    bool played{};
    bool cleared{};
    unsigned maxCombo{};
    int bestAccuracyScore{};
    std::optional<int> bestExScore;
    // Fields added after the first record format. Records written before them leave the field empty
    // ("not recorded") rather than guessing. bestJudgements are PG, GR, GD, BD, PR of the play that set
    // bestExScore; minMissCount is the lowest BD+PR (plus unreached notes) of any play since. Play and
    // clear counts start with the first play saved in this format; noteCount is the chart's notes then.
    std::optional<std::array<int,5>> bestJudgements;
    // PG, GR, GD, BD, PR of every play saved since this field was added, added up (LR2 select numbers 33-37).
    std::optional<std::array<int,5>> totalJudgements;
    std::optional<int> minMissCount, playCount, clearCount, noteCount;
    RecordJudgement scoreJudgement,comboJudgement,clearJudgement;
    bool legacyClearUnknown{};
    std::optional<RecordClearInfo> bestKnownClear;
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

// Every record added up: LR2 select numbers 30-37 (beatoraja player_playcount .. player_poor).
// Records written before a count existed add nothing to it.
struct PlayRecordTotals {
    long long playCount{}, clearCount{};
    std::array<long long,5> judgements{};
};

struct PlayRecordStore {
    std::filesystem::path file;
    std::unordered_map<std::wstring, PlayRecord> records;

    // Sums every record once and keeps the answer until the records change (load or save).
    const PlayRecordTotals& totals() const {
        if(!cachedTotals_) {
            PlayRecordTotals sum;
            for(const auto& [key,record]:records) {
                if(!record.played)continue;
                sum.playCount+=record.playCount.value_or(0);
                sum.clearCount+=record.clearCount.value_or(0);
                if(record.totalJudgements)
                    for(std::size_t grade=0;grade<sum.judgements.size();++grade)sum.judgements[grade]+=(*record.totalJudgements)[grade];
            }
            cachedTotals_=sum;
            ++totalsComputed_;
        }
        return *cachedTotals_;
    }
    // Number of times totals() summed the records; lets tests see the cache at work.
    std::size_t totalsComputed() const noexcept {return totalsComputed_;}

    void load() {
        records.clear();
        cachedTotals_.reset();
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
            record.bestExScore=readCount(section,L"BestExScore");
            record.minMissCount=readCount(section,L"MinMissCount");
            record.playCount=readCount(section,L"PlayCount");
            record.clearCount=readCount(section,L"ClearCount");
            record.noteCount=readCount(section,L"NoteCount");
            if(record.clearCount&&(!record.playCount||*record.clearCount>*record.playCount))record.clearCount.reset();
            record.bestJudgements=readJudgements(section);
            record.totalJudgements=readJudgements(section,L"TotalJudgements");
            const int lamp=static_cast<int>(GetPrivateProfileIntW(section.c_str(),L"BestClearLamp",-1,file.c_str()));
            if(lamp>=static_cast<int>(RecordClearLamp::Failed) && lamp<=static_cast<int>(RecordClearLamp::Max)) {
                wchar_t profile[128]{},gauge[128]{};
                GetPrivateProfileStringW(section.c_str(),L"BestClearProfile",L"",profile,128,file.c_str());
                GetPrivateProfileStringW(section.c_str(),L"BestClearGauge",L"",gauge,128,file.c_str());
                record.bestKnownClear=RecordClearInfo{static_cast<RecordClearLamp>(lamp),profile,gauge};
            }
            const int unknown=static_cast<int>(GetPrivateProfileIntW(section.c_str(),L"LegacyClearUnknown",-1,file.c_str()));
            record.legacyClearUnknown=unknown<0?(record.cleared&&!record.bestKnownClear):unknown!=0;
            for(auto pair:{std::pair{L"Score",&record.scoreJudgement},std::pair{L"Combo",&record.comboJudgement},std::pair{L"Clear",&record.clearJudgement}}) {
                wchar_t value[128]{};const std::wstring prefix=pair.first;
                GetPrivateProfileStringW(section.c_str(),(prefix+L"JudgeSpec").c_str(),L"LEGACY",value,128,file.c_str());
                pair.second->spec=value;
                pair.second->rank=static_cast<int>(GetPrivateProfileIntW(section.c_str(),(prefix+L"JudgeRank").c_str(),-1,file.c_str()));
            }
            records[key] = record;
        }
    }

    // A non-negative decimal value, or nothing when the key is missing or malformed.
    std::optional<int> readCount(const std::wstring& section,const wchar_t* key) const {
        wchar_t text[16]{};
        const DWORD length=GetPrivateProfileStringW(section.c_str(),key,L"",text,16,file.c_str());
        if(length==0||length>10||!std::all_of(text,text+length,[](wchar_t digit){return digit>=L'0'&&digit<=L'9';}))
            return std::nullopt;
        const auto value=std::wcstoll(text,nullptr,10);
        if(value>std::numeric_limits<int>::max())return std::nullopt;
        return static_cast<int>(value);
    }
    // Five comma-separated counts; anything else is treated as not recorded.
    std::optional<std::array<int,5>> readJudgements(const std::wstring& section,const wchar_t* key=L"BestJudgements") const {
        wchar_t text[64]{};
        const DWORD length=GetPrivateProfileStringW(section.c_str(),key,L"",text,64,file.c_str());
        if(length==0||length>=63)return std::nullopt;
        std::array<int,5> counts{};
        const wchar_t* cursor=text;
        for(std::size_t grade=0;grade<counts.size();++grade) {
            const wchar_t* start=cursor;
            while(*cursor>=L'0'&&*cursor<=L'9')++cursor;
            if(cursor==start||cursor-start>10)return std::nullopt;
            const auto value=std::wcstoll(start,nullptr,10);
            if(value>std::numeric_limits<int>::max())return std::nullopt;
            counts[grade]=static_cast<int>(value);
            if(grade+1<counts.size()&&*cursor++!=L',')return std::nullopt;
        }
        if(*cursor)return std::nullopt;
        return counts;
    }

    // Returns the record only when it has been played at least once.
    const PlayRecord* findPlayed(const std::wstring& key) const {
        const auto found = records.find(key);
        return found == records.end() || !found->second.played ? nullptr : &found->second;
    }

    // Merges a finished play into the best record and writes it through.
    // Returns false (and changes nothing) when there is no key or no file.
    bool save(const std::filesystem::path& chartPath, bool cleared, unsigned maxCombo, int accuracyScore,
        const std::wstring& spec=L"LEGACY", int rank=-1, const std::optional<RecordClearInfo>& clearInfo=std::nullopt,
        std::optional<int> exScore=std::nullopt,const std::optional<std::array<int,5>>& judgements=std::nullopt,
        std::optional<int> missCount=std::nullopt,std::optional<int> noteCount=std::nullopt) {
        const auto key = normalizedRecordPath(chartPath);
        if (key.empty() || file.empty()) return false;
        cachedTotals_.reset();
        auto& record = records[key];
        const bool validLamp=clearInfo && static_cast<int>(clearInfo->lamp)>=static_cast<int>(RecordClearLamp::Failed)
            && static_cast<int>(clearInfo->lamp)<=static_cast<int>(RecordClearLamp::Max)
            && (cleared==(clearInfo->lamp!=RecordClearLamp::Failed));
        if(cleared&&!validLamp)record.legacyClearUnknown=true;
        if(validLamp && (!record.bestKnownClear || static_cast<int>(clearInfo->lamp)>static_cast<int>(record.bestKnownClear->lamp)))
            record.bestKnownClear=*clearInfo;
        const RecordJudgement provenance{spec,rank};
        if(!record.played || accuracyScore>record.bestAccuracyScore)record.scoreJudgement=provenance;
        if(!record.played || maxCombo>record.maxCombo)record.comboJudgement=provenance;
        if(!record.played || (cleared&&!record.cleared))record.clearJudgement=provenance;
        record.played = true;
        record.cleared = record.cleared || cleared;
        record.maxCombo = std::max(record.maxCombo, maxCombo);
        record.bestAccuracyScore = std::max(record.bestAccuracyScore, std::clamp(accuracyScore, 0, 1'000'000));
        const auto previousBestEx=record.bestExScore;
        if(exScore&&*exScore>=0)record.bestExScore=std::max(record.bestExScore.value_or(0),*exScore);
        // The judgements follow the play that set the best EX score. An equal score fills them in only when
        // the best was recorded without them, so they never come from a different play than the score.
        const bool validJudgements=judgements&&std::all_of(judgements->begin(),judgements->end(),[](int count){return count>=0;});
        if(validJudgements&&exScore&&*exScore>=0&&
           (!previousBestEx||*exScore>*previousBestEx||(!record.bestJudgements&&*exScore==*previousBestEx)))
            record.bestJudgements=*judgements;
        // Totals saturate at the largest int instead of wrapping.
        if(validJudgements) {
            auto total=record.totalJudgements.value_or(std::array<int,5>{});
            for(std::size_t grade=0;grade<total.size();++grade)
                total[grade]=static_cast<int>(std::min<long long>(std::numeric_limits<int>::max(),(long long)total[grade]+(*judgements)[grade]));
            record.totalJudgements=total;
        }
        if(missCount&&*missCount>=0)record.minMissCount=std::min(record.minMissCount.value_or(*missCount),*missCount);
        if(noteCount&&*noteCount>=0)record.noteCount=*noteCount;
        // A record from before these counts starts counting here; its earlier plays stay unknown.
        record.playCount=record.playCount.value_or(0)+1;
        record.clearCount=record.clearCount.value_or(0)+(cleared?1:0);
        const auto section = recordSectionForPath(key);
        WritePrivateProfileStringW(section.c_str(), L"Path", key.c_str(), file.c_str());
        WritePrivateProfileStringW(section.c_str(), L"Played", L"1", file.c_str());
        WritePrivateProfileStringW(section.c_str(), L"Cleared", record.cleared ? L"1" : L"0", file.c_str());
        WritePrivateProfileStringW(section.c_str(), L"MaxCombo", std::to_wstring(record.maxCombo).c_str(), file.c_str());
        WritePrivateProfileStringW(section.c_str(), L"AccuracyScore", std::to_wstring(record.bestAccuracyScore).c_str(), file.c_str());
        for(auto pair:{std::pair{L"BestExScore",&record.bestExScore},std::pair{L"MinMissCount",&record.minMissCount},
                       std::pair{L"PlayCount",&record.playCount},std::pair{L"ClearCount",&record.clearCount},
                       std::pair{L"NoteCount",&record.noteCount}})
            if(*pair.second)WritePrivateProfileStringW(section.c_str(),pair.first,std::to_wstring(**pair.second).c_str(),file.c_str());
        for(auto pair:{std::pair{L"BestJudgements",&record.bestJudgements},std::pair{L"TotalJudgements",&record.totalJudgements}}) {
            if(!*pair.second)continue;
            std::wstring counts;
            for(const int count:**pair.second)counts+=(counts.empty()?L"":L",")+std::to_wstring(count);
            WritePrivateProfileStringW(section.c_str(),pair.first,counts.c_str(),file.c_str());
        }
        WritePrivateProfileStringW(section.c_str(),L"LegacyClearUnknown",record.legacyClearUnknown?L"1":L"0",file.c_str());
        if(record.bestKnownClear) {
            WritePrivateProfileStringW(section.c_str(),L"BestClearLamp",std::to_wstring(static_cast<int>(record.bestKnownClear->lamp)).c_str(),file.c_str());
            WritePrivateProfileStringW(section.c_str(),L"BestClearProfile",record.bestKnownClear->profile.c_str(),file.c_str());
            WritePrivateProfileStringW(section.c_str(),L"BestClearGauge",record.bestKnownClear->gauge.c_str(),file.c_str());
        }
        for(auto pair:{std::pair{L"Score",&record.scoreJudgement},std::pair{L"Combo",&record.comboJudgement},std::pair{L"Clear",&record.clearJudgement}}) {
            const std::wstring prefix=pair.first;
            WritePrivateProfileStringW(section.c_str(),(prefix+L"JudgeSpec").c_str(),pair.second->spec.c_str(),file.c_str());
            WritePrivateProfileStringW(section.c_str(),(prefix+L"JudgeRank").c_str(),std::to_wstring(pair.second->rank).c_str(),file.c_str());
        }
        return true;
    }
private:
    mutable std::optional<PlayRecordTotals> cachedTotals_;
    mutable std::size_t totalsComputed_{};
};
