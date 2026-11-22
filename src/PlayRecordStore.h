#pragma once
// Persistent best-play records (BmsPlayer-records.ini).
// Owns the record map and its INI file; knows nothing about charts, windows or UI.
// The host maps chart indices to record keys and repaints after a save.
//
// Keys: a chart whose file could be hashed is keyed "sha256:<hex>", so moving or renaming
// its folder keeps the record. A key without a hash is the chart's normalized absolute path
// (the only key older builds wrote). adoptLegacyRecord() moves such a path record onto the
// hash key the first time the chart is seen with a hash.
//
// File: INI sections, one per record, named Play_<FNV-1a of the key>. The whole file is read
// once and rewritten as UTF-16LE (with BOM) through a temporary file that replaces the
// original, so a crash mid-save leaves the previous file intact and non-ANSI paths survive.
// Sections this store does not understand are written back unchanged.
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <cwchar>
#include <limits>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

struct RecordJudgement {std::wstring spec=L"LEGACY";int rank=-1;};
// A legacy boolean clear has no recoverable lamp or gauge provenance. Keep it
// independently from bestKnownClear rather than assigning it a guessed rank.
enum class RecordClearLamp : int { Failed, AssistEasy, Easy, Normal, Hard, ExHard, FullCombo, Perfect, Max };
struct RecordClearInfo { RecordClearLamp lamp{RecordClearLamp::Failed}; std::wstring profile, gauge; };
// The conditions a play ran under. mode is bms::ChartMode as an int (-1 unknown); gauge is the
// gauge's display name (EASY, NORMAL, HARD, ...); random lists the chart's #RANDOM/#SWITCH
// branch numbers, comma separated (empty for a chart without random branches); lane is the 1P lane
// option's name (OFF, MIRROR, RANDOM, S-RANDOM; empty when recorded before the option existed).
struct RecordPlayOptions {
    int mode=-1;
    std::wstring gauge, random, lane;
    bool operator==(const RecordPlayOptions&) const = default;
};
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
    // Seconds of song played in every play saved since this field was added (LR2 select numbers 17-19).
    std::optional<int> playSeconds;
    RecordJudgement scoreJudgement,comboJudgement,clearJudgement;
    bool legacyClearUnknown{};
    std::optional<RecordClearInfo> bestKnownClear;
    // Where the chart was last seen and its digests (empty when unknown).
    std::wstring path, md5, sha256;
    // Conditions of the play that set the best score, the best clear, and of the latest play.
    std::optional<RecordPlayOptions> scoreOptions, clearOptions, lastOptions;
};

// Case-insensitive absolute path used as the legacy record key.
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

// Identity of a chart for its record: the key it is stored under plus what is known about it.
struct RecordChartId {
    std::wstring key, path, md5, sha256;
    // The key older builds used for the same chart; empty when it equals key.
    std::wstring legacyKey() const { return key == path ? std::wstring{} : path; }
};
inline bool isRecordSha256(const std::wstring& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](wchar_t c) { return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'); });
}
inline RecordChartId recordChartId(const std::filesystem::path& chartPath, const std::wstring& md5 = {}, const std::wstring& sha256 = {}) {
    RecordChartId id{{}, normalizedRecordPath(chartPath), md5, sha256};
    id.key = isRecordSha256(sha256) ? L"sha256:" + sha256 : id.path;
    if (!isRecordSha256(sha256)) id.sha256.clear();
    return id;
}

// Every record added up: LR2 select numbers 30-37 (beatoraja player_playcount .. player_poor).
// Records written before a count existed add nothing to it.
struct PlayRecordTotals {
    long long playCount{}, clearCount{};
    std::array<long long,5> judgements{};
    long long playSeconds{};
};

namespace play_record_detail {
struct IniSection {
    std::wstring name;
    std::vector<std::pair<std::wstring,std::wstring>> entries; // as written, in order
    // First value of a key (keys compare case-insensitively, as the profile API does), or nullptr.
    const std::wstring* find(std::wstring_view key) const {
        for (const auto& [entryName, value] : entries)
            if (entryName.size() == key.size() && _wcsnicmp(entryName.data(), key.data(), key.size()) == 0) return &value;
        return nullptr;
    }
};
inline std::wstring trimmed(std::wstring_view text) {
    size_t first = 0, last = text.size();
    while (first < last && (text[first] == L' ' || text[first] == L'\t')) ++first;
    while (last > first && (text[last-1] == L' ' || text[last-1] == L'\t' || text[last-1] == L'\r')) --last;
    return std::wstring(text.substr(first, last - first));
}
// Decodes the file like the profile API reads it: UTF-16LE with a BOM, UTF-8 with a BOM, else the ANSI code page.
inline std::wstring decodeIni(const std::string& bytes) {
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xff && static_cast<unsigned char>(bytes[1]) == 0xfe) {
        std::wstring text((bytes.size() - 2) / 2, L'\0');
        if (!text.empty()) std::memcpy(text.data(), bytes.data() + 2, text.size() * sizeof(wchar_t));
        return text;
    }
    UINT codePage = CP_ACP; size_t skip = 0;
    if (bytes.size() >= 3 && bytes.compare(0, 3, "\xef\xbb\xbf") == 0) { codePage = CP_UTF8; skip = 3; }
    const int size = static_cast<int>(std::min<size_t>(bytes.size() - skip, static_cast<size_t>(std::numeric_limits<int>::max())));
    if (size <= 0) return {};
    const int length = MultiByteToWideChar(codePage, 0, bytes.data() + skip, size, nullptr, 0);
    std::wstring text(static_cast<size_t>(std::max(0, length)), L'\0');
    if (length > 0) MultiByteToWideChar(codePage, 0, bytes.data() + skip, size, text.data(), length);
    return text;
}
// Sections in file order. A repeated section name keeps the first one, as the profile API does.
inline std::vector<IniSection> parseIni(const std::wstring& text) {
    std::vector<IniSection> sections;
    std::unordered_map<std::wstring, size_t> seen;
    IniSection* current = nullptr;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        const auto line = trimmed(std::wstring_view(text).substr(start, end - start));
        start = end + 1;
        if (line.empty() || line[0] == L';') continue;
        if (line.front() == L'[') {
            const size_t close = line.find(L']');
            std::wstring name = trimmed(std::wstring_view(line).substr(1, close == std::wstring::npos ? std::wstring::npos : close - 1));
            std::wstring lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            if (const auto found = seen.find(lower); found != seen.end()) { current = &sections[found->second]; continue; }
            seen.emplace(std::move(lower), sections.size());
            sections.push_back({std::move(name), {}});
            current = &sections.back();
            continue;
        }
        const size_t equals = line.find(L'=');
        if (!current || equals == std::wstring::npos) continue;
        current->entries.emplace_back(trimmed(std::wstring_view(line).substr(0, equals)), trimmed(std::wstring_view(line).substr(equals + 1)));
    }
    return sections;
}
// GetPrivateProfileInt-like: optional sign and leading digits, else the fallback.
inline int iniInt(const std::wstring* value, int fallback) {
    if (!value || value->empty()) return fallback;
    const wchar_t* text = value->c_str();
    wchar_t* end = nullptr;
    const long long parsed = std::wcstoll(text, &end, 10);
    if (end == text) return fallback;
    return static_cast<int>(static_cast<uint32_t>(parsed));
}
// A record value never holds a line break; keep it on one line if one slips in.
inline std::wstring iniValue(std::wstring value) {
    std::replace_if(value.begin(), value.end(), [](wchar_t c) { return c == L'\r' || c == L'\n'; }, L' ');
    return value;
}
inline std::wstring joinCounts(const std::array<int,5>& counts) {
    std::wstring text;
    for (const int count : counts) text += (text.empty() ? L"" : L",") + std::to_wstring(count);
    return text;
}
} // namespace play_record_detail

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
                sum.playSeconds+=record.playSeconds.value_or(0);
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
    // Whether the last write reached the disk.
    bool lastWriteSucceeded() const noexcept {return lastWriteSucceeded_;}

    void load() {
        using namespace play_record_detail;
        records.clear();
        foreign_.clear();
        cachedTotals_.reset();
        if (file.empty()) return;
        std::string bytes;
        {
            std::ifstream in(file, std::ios::binary);
            if (!in) return;
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        for (auto& section : parseIni(decodeIni(bytes))) {
            if (!loadSection(section)) foreign_.push_back(std::move(section));
        }
    }

    // Returns the record only when it has been played at least once.
    const PlayRecord* findPlayed(const std::wstring& key) const {
        const auto found = records.find(key);
        return found == records.end() || !found->second.played ? nullptr : &found->second;
    }

    // Moves the record older builds stored under id's path onto id's hash key, merging it with a
    // record already there. Updates the remembered path. Returns whether anything changed; the caller
    // writes the file once with flush() after adopting a batch.
    bool adoptLegacyRecord(const RecordChartId& id) {
        if (id.key.empty()) return false;
        bool changed = false;
        if (const auto legacy = id.legacyKey(); !legacy.empty()) {
            if (const auto found = records.find(legacy); found != records.end()) {
                PlayRecord old = std::move(found->second);
                records.erase(found);
                auto existing = records.find(id.key);
                if (existing == records.end()) records.emplace(id.key, std::move(old));
                else mergeRecord(existing->second, old);
                changed = true;
                cachedTotals_.reset();
            }
        }
        if (const auto found = records.find(id.key); found != records.end()) {
            auto& record = found->second;
            if (!id.path.empty() && record.path != id.path) { record.path = id.path; changed = true; }
            if (!id.md5.empty() && record.md5 != id.md5) { record.md5 = id.md5; changed = true; }
            if (!id.sha256.empty() && record.sha256 != id.sha256) { record.sha256 = id.sha256; changed = true; }
        }
        return changed;
    }

    // Merges a finished play into the best record and writes it through.
    // Returns false (and changes nothing) when there is no key or no file.
    bool save(const std::filesystem::path& chartPath, bool cleared, unsigned maxCombo, int accuracyScore,
        const std::wstring& spec=L"LEGACY", int rank=-1, const std::optional<RecordClearInfo>& clearInfo=std::nullopt,
        std::optional<int> exScore=std::nullopt,const std::optional<std::array<int,5>>& judgements=std::nullopt,
        std::optional<int> missCount=std::nullopt,std::optional<int> noteCount=std::nullopt) {
        return saveChart(recordChartId(chartPath), cleared, maxCombo, accuracyScore, spec, rank, clearInfo, exScore, judgements, missCount, noteCount);
    }
    bool saveChart(const RecordChartId& id, bool cleared, unsigned maxCombo, int accuracyScore,
        const std::wstring& spec=L"LEGACY", int rank=-1, const std::optional<RecordClearInfo>& clearInfo=std::nullopt,
        std::optional<int> exScore=std::nullopt,const std::optional<std::array<int,5>>& judgements=std::nullopt,
        std::optional<int> missCount=std::nullopt,std::optional<int> noteCount=std::nullopt,
        const std::optional<RecordPlayOptions>& options=std::nullopt, std::optional<int> playSeconds=std::nullopt) {
        if (id.key.empty() || file.empty()) return false;
        adoptLegacyRecord(id);
        cachedTotals_.reset();
        auto& record = records[id.key];
        if (!id.path.empty()) record.path = id.path;
        if (!id.md5.empty()) record.md5 = id.md5;
        if (!id.sha256.empty()) record.sha256 = id.sha256;
        const bool validLamp=clearInfo && static_cast<int>(clearInfo->lamp)>=static_cast<int>(RecordClearLamp::Failed)
            && static_cast<int>(clearInfo->lamp)<=static_cast<int>(RecordClearLamp::Max)
            && (cleared==(clearInfo->lamp!=RecordClearLamp::Failed));
        if(cleared&&!validLamp)record.legacyClearUnknown=true;
        bool clearImproved = !record.played || (cleared&&!record.cleared);
        if(validLamp && (!record.bestKnownClear || static_cast<int>(clearInfo->lamp)>static_cast<int>(record.bestKnownClear->lamp))) {
            record.bestKnownClear=*clearInfo;
            clearImproved = true;
        }
        const RecordJudgement provenance{spec,rank};
        const bool scoreImproved = !record.played || accuracyScore>record.bestAccuracyScore;
        if(scoreImproved)record.scoreJudgement=provenance;
        if(!record.played || maxCombo>record.maxCombo)record.comboJudgement=provenance;
        if(!record.played || (cleared&&!record.cleared))record.clearJudgement=provenance;
        if(options) {
            record.lastOptions=*options;
            if(scoreImproved)record.scoreOptions=*options;
            if(clearImproved)record.clearOptions=*options;
        }
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
        if(validJudgements) record.totalJudgements=addedCounts(record.totalJudgements.value_or(std::array<int,5>{}),*judgements);
        if(missCount&&*missCount>=0)record.minMissCount=std::min(record.minMissCount.value_or(*missCount),*missCount);
        if(noteCount&&*noteCount>=0)record.noteCount=*noteCount;
        // A record from before these counts starts counting here; its earlier plays stay unknown.
        record.playCount=record.playCount.value_or(0)+1;
        record.clearCount=record.clearCount.value_or(0)+(cleared?1:0);
        if(playSeconds&&*playSeconds>=0)
            record.playSeconds=static_cast<int>(std::min<long long>(std::numeric_limits<int>::max(),(long long)record.playSeconds.value_or(0)+*playSeconds));
        flush();
        return true;
    }

    // Writes every record (and the sections this store kept untouched) to the file in one replace.
    bool flush() {
        lastWriteSucceeded_ = false;
        if (file.empty()) return false;
        std::wstring text;
        std::map<std::wstring, std::pair<const std::wstring*, const PlayRecord*>> ordered; // stable file order
        for (const auto& [key, record] : records) ordered.emplace(recordSectionForPath(key), std::pair{&key, &record});
        for (const auto& [section, entry] : ordered) writeSection(text, section, *entry.first, *entry.second);
        for (const auto& section : foreign_) {
            if (ordered.contains(section.name)) continue; // a record now owns this section
            text += L"[" + section.name + L"]\r\n";
            for (const auto& [name, value] : section.entries) text += name + L"=" + value + L"\r\n";
        }
        lastWriteSucceeded_ = replaceFile(text);
        return lastWriteSucceeded_;
    }

private:
    mutable std::optional<PlayRecordTotals> cachedTotals_;
    mutable std::size_t totalsComputed_{};
    std::vector<play_record_detail::IniSection> foreign_;
    bool lastWriteSucceeded_{};

    static std::array<int,5> addedCounts(const std::array<int,5>& a, const std::array<int,5>& b) {
        std::array<int,5> total{};
        for(std::size_t grade=0;grade<total.size();++grade)
            total[grade]=static_cast<int>(std::min<long long>(std::numeric_limits<int>::max(),(long long)a[grade]+b[grade]));
        return total;
    }

    // Folds another record of the same chart into this one, keeping the best of each.
    static void mergeRecord(PlayRecord& into, const PlayRecord& from) {
        if (!from.played) return;
        if (!into.played) {
            const auto path = into.path, md5 = into.md5, sha = into.sha256;
            into = from;
            if (!path.empty()) into.path = path;
            if (!md5.empty()) into.md5 = md5;
            if (!sha.empty()) into.sha256 = sha;
            return;
        }
        if (from.bestAccuracyScore > into.bestAccuracyScore) {
            into.bestAccuracyScore = from.bestAccuracyScore; into.scoreJudgement = from.scoreJudgement; into.scoreOptions = from.scoreOptions;
        }
        if (from.maxCombo > into.maxCombo) { into.maxCombo = from.maxCombo; into.comboJudgement = from.comboJudgement; }
        if (from.cleared && !into.cleared) { into.cleared = true; into.clearJudgement = from.clearJudgement; into.clearOptions = from.clearOptions; }
        if (from.bestKnownClear && (!into.bestKnownClear || from.bestKnownClear->lamp > into.bestKnownClear->lamp)) {
            into.bestKnownClear = from.bestKnownClear; into.clearOptions = from.clearOptions;
        }
        into.legacyClearUnknown = into.legacyClearUnknown || from.legacyClearUnknown;
        if (from.bestExScore && (!into.bestExScore || *from.bestExScore > *into.bestExScore)) {
            into.bestExScore = from.bestExScore; into.bestJudgements = from.bestJudgements;
        }
        if (from.totalJudgements) into.totalJudgements = addedCounts(into.totalJudgements.value_or(std::array<int,5>{}), *from.totalJudgements);
        if (from.minMissCount) into.minMissCount = std::min(into.minMissCount.value_or(*from.minMissCount), *from.minMissCount);
        const auto add = [](std::optional<int>& a, const std::optional<int>& b) {
            if (b) a = static_cast<int>(std::min<long long>(std::numeric_limits<int>::max(), (long long)a.value_or(0) + *b));
        };
        add(into.playCount, from.playCount); add(into.clearCount, from.clearCount); add(into.playSeconds, from.playSeconds);
        if (!into.noteCount) into.noteCount = from.noteCount;
        if (!into.lastOptions) into.lastOptions = from.lastOptions;
    }

    // A non-negative decimal value, or nothing when the key is missing or malformed.
    static std::optional<int> readCount(const play_record_detail::IniSection& section,const wchar_t* key) {
        const auto* text=section.find(key);
        if(!text||text->empty()||text->size()>10||!std::all_of(text->begin(),text->end(),[](wchar_t digit){return digit>=L'0'&&digit<=L'9';}))
            return std::nullopt;
        const auto value=std::wcstoll(text->c_str(),nullptr,10);
        if(value>std::numeric_limits<int>::max())return std::nullopt;
        return static_cast<int>(value);
    }
    // Five comma-separated counts; anything else is treated as not recorded.
    static std::optional<std::array<int,5>> readJudgements(const play_record_detail::IniSection& section,const wchar_t* key=L"BestJudgements") {
        const auto* value=section.find(key);
        if(!value||value->empty()||value->size()>=63)return std::nullopt;
        std::array<int,5> counts{};
        const wchar_t* cursor=value->c_str();
        for(std::size_t grade=0;grade<counts.size();++grade) {
            const wchar_t* start=cursor;
            while(*cursor>=L'0'&&*cursor<=L'9')++cursor;
            if(cursor==start||cursor-start>10)return std::nullopt;
            const auto parsed=std::wcstoll(start,nullptr,10);
            if(parsed>std::numeric_limits<int>::max())return std::nullopt;
            counts[grade]=static_cast<int>(parsed);
            if(grade+1<counts.size()&&*cursor++!=L',')return std::nullopt;
        }
        if(*cursor)return std::nullopt;
        return counts;
    }
    static std::optional<RecordPlayOptions> readOptions(const play_record_detail::IniSection& section,const std::wstring& prefix) {
        const auto* mode=section.find(prefix+L"Mode");
        const auto* gauge=section.find(prefix+L"Gauge");
        const auto* random=section.find(prefix+L"Random");
        const auto* lane=section.find(prefix+L"Lane");
        if(!mode&&!gauge&&!random&&!lane)return std::nullopt;
        RecordPlayOptions options;
        options.mode=std::clamp(play_record_detail::iniInt(mode,-1),-1,64);
        if(gauge)options.gauge=*gauge;
        if(random)options.random=*random;
        if(lane)options.lane=lane->substr(0,32);
        return options;
    }

    // Builds a record from a Play_ section. False leaves the section for the file as it was.
    bool loadSection(const play_record_detail::IniSection& section) {
        using play_record_detail::iniInt;
        if (!section.name.starts_with(L"Play_")) return false;
        const auto* storedPath = section.find(L"Path");
        const auto* storedKey = section.find(L"Key");
        std::wstring key;
        if (storedKey) {
            if (!storedKey->starts_with(L"sha256:") || !isRecordSha256(storedKey->substr(7))) return false;
            key = *storedKey;
        } else {
            if (!storedPath || storedPath->empty() || storedPath->size() >= 32767) return false;
            key = normalizedRecordPath(*storedPath);
        }
        if (key.empty() || recordSectionForPath(key) != section.name) return false;
        PlayRecord record;
        if (storedPath) record.path = storedKey ? *storedPath : key;
        if (const auto* md5 = section.find(L"Md5")) record.md5 = *md5;
        if (storedKey) record.sha256 = storedKey->substr(7);
        record.played = iniInt(section.find(L"Played"), 0) != 0;
        record.cleared = iniInt(section.find(L"Cleared"), 0) != 0;
        record.maxCombo = static_cast<unsigned>(std::max(0, iniInt(section.find(L"MaxCombo"), 0)));
        record.bestAccuracyScore = std::clamp(iniInt(section.find(L"AccuracyScore"), 0), 0, 1'000'000);
        record.bestExScore=readCount(section,L"BestExScore");
        record.minMissCount=readCount(section,L"MinMissCount");
        record.playCount=readCount(section,L"PlayCount");
        record.clearCount=readCount(section,L"ClearCount");
        record.noteCount=readCount(section,L"NoteCount");
        record.playSeconds=readCount(section,L"PlaySeconds");
        if(record.clearCount&&(!record.playCount||*record.clearCount>*record.playCount))record.clearCount.reset();
        record.bestJudgements=readJudgements(section);
        record.totalJudgements=readJudgements(section,L"TotalJudgements");
        const int lamp=iniInt(section.find(L"BestClearLamp"),-1);
        if(lamp>=static_cast<int>(RecordClearLamp::Failed) && lamp<=static_cast<int>(RecordClearLamp::Max)) {
            const auto text=[&](const wchar_t* name){const auto* v=section.find(name);return v?v->substr(0,127):std::wstring{};};
            record.bestKnownClear=RecordClearInfo{static_cast<RecordClearLamp>(lamp),text(L"BestClearProfile"),text(L"BestClearGauge")};
        }
        const int unknown=iniInt(section.find(L"LegacyClearUnknown"),-1);
        record.legacyClearUnknown=unknown<0?(record.cleared&&!record.bestKnownClear):unknown!=0;
        for(auto pair:{std::pair{L"Score",&record.scoreJudgement},std::pair{L"Combo",&record.comboJudgement},std::pair{L"Clear",&record.clearJudgement}}) {
            const std::wstring prefix=pair.first;
            const auto* spec=section.find(prefix+L"JudgeSpec");
            pair.second->spec=spec?spec->substr(0,127):L"LEGACY";
            pair.second->rank=iniInt(section.find(prefix+L"JudgeRank"),-1);
        }
        record.scoreOptions=readOptions(section,L"Score");
        record.clearOptions=readOptions(section,L"Clear");
        record.lastOptions=readOptions(section,L"Last");
        records[key] = std::move(record);
        return true;
    }

    static void writeSection(std::wstring& out, const std::wstring& section, const std::wstring& key, const PlayRecord& record) {
        using play_record_detail::iniValue;
        const auto line = [&](const std::wstring& name, const std::wstring& value) { out += name + L"=" + iniValue(value) + L"\r\n"; };
        out += L"[" + section + L"]\r\n";
        const bool hashed = key.starts_with(L"sha256:");
        if (hashed) line(L"Key", key);
        // Older builds identify a section by its Path; a hashed record's section never matches it, so they skip it.
        line(L"Path", hashed ? record.path : key);
        if (!record.md5.empty()) line(L"Md5", record.md5);
        line(L"Played", record.played ? L"1" : L"0");
        line(L"Cleared", record.cleared ? L"1" : L"0");
        line(L"MaxCombo", std::to_wstring(record.maxCombo));
        line(L"AccuracyScore", std::to_wstring(record.bestAccuracyScore));
        for(auto pair:{std::pair{L"BestExScore",&record.bestExScore},std::pair{L"MinMissCount",&record.minMissCount},
                       std::pair{L"PlayCount",&record.playCount},std::pair{L"ClearCount",&record.clearCount},
                       std::pair{L"NoteCount",&record.noteCount},std::pair{L"PlaySeconds",&record.playSeconds}})
            if(*pair.second)line(pair.first,std::to_wstring(**pair.second));
        for(auto pair:{std::pair{L"BestJudgements",&record.bestJudgements},std::pair{L"TotalJudgements",&record.totalJudgements}})
            if(*pair.second)line(pair.first,play_record_detail::joinCounts(**pair.second));
        line(L"LegacyClearUnknown",record.legacyClearUnknown?L"1":L"0");
        if(record.bestKnownClear) {
            line(L"BestClearLamp",std::to_wstring(static_cast<int>(record.bestKnownClear->lamp)));
            line(L"BestClearProfile",record.bestKnownClear->profile);
            line(L"BestClearGauge",record.bestKnownClear->gauge);
        }
        for(auto pair:{std::pair{L"Score",&record.scoreJudgement},std::pair{L"Combo",&record.comboJudgement},std::pair{L"Clear",&record.clearJudgement}}) {
            const std::wstring prefix=pair.first;
            line(prefix+L"JudgeSpec",pair.second->spec);
            line(prefix+L"JudgeRank",std::to_wstring(pair.second->rank));
        }
        for(auto pair:{std::pair{L"Score",&record.scoreOptions},std::pair{L"Clear",&record.clearOptions},std::pair{L"Last",&record.lastOptions}}) {
            if(!*pair.second)continue;
            const std::wstring prefix=pair.first;
            line(prefix+L"Mode",std::to_wstring((*pair.second)->mode));
            line(prefix+L"Gauge",(*pair.second)->gauge);
            line(prefix+L"Random",(*pair.second)->random);
            if(!(*pair.second)->lane.empty())line(prefix+L"Lane",(*pair.second)->lane);
        }
    }

    // Writes to a temporary file next to the target, flushes it, then swaps it in.
    bool replaceFile(const std::wstring& text) const {
        std::error_code ec;
        if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
        auto temp = file; temp += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
        HANDLE handle = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return false;
        std::string bytes("\xff\xfe", 2);
        bytes.append(reinterpret_cast<const char*>(text.data()), text.size() * sizeof(wchar_t));
        DWORD written{};
        const bool ok = bytes.size() <= std::numeric_limits<DWORD>::max() &&
            WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size() &&
            FlushFileBuffers(handle);
        CloseHandle(handle);
        if (ok && MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
        DeleteFileW(temp.c_str());
        return false;
    }
};
