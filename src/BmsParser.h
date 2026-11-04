#pragma once

#include <filesystem>
#include <cstdint>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace bms {

inline bool readableAssetExists(const std::filesystem::path& path) {
    std::error_code error;return std::filesystem::is_regular_file(path,error) && !error;
}

enum class LongNoteMode { LN = 1, CN = 2, HCN = 3 };

enum class ChartMode { Unknown, FiveKey, SevenKey, TenKey, FourteenKey, PopnNine, Other };

struct Note {
    int lane{}; // 0 = scratch, 1..7 = keys
    double timeSeconds{};
    double lengthSeconds{};
    std::wstring sampleId;
    std::wstring endSampleId;
    LongNoteMode mode = LongNoteMode::LN;
    bool longNote() const noexcept { return lengthSeconds > 0.0001; }
};

struct HiddenNote { int lane{}; double timeSeconds{}; std::wstring sampleId; };
struct MineNote { int lane{}; double timeSeconds{}; double damage{}; };
struct ValueEvent { double timeSeconds{}, value{}; };
struct TextEvent { double timeSeconds{}; std::wstring text; };
struct BgaColor {int alpha=255,red=0,green=0,blue=0;};
struct ColorEvent {double timeSeconds{};BgaColor color;};
struct SwitchAnimation {
    double frameMilliseconds=100,durationMilliseconds{};int lane{};bool loop{};BgaColor color;
    std::vector<std::wstring> frames;
};
struct SwitchEvent {double timeSeconds{};std::wstring id;};
struct SampleSlice {std::wstring source;double offset{},duration=-1;};
struct SampleEffect { double pitch = 60, volume = 100, durationMilliseconds{}; };

struct BgmEvent {
    double timeSeconds{};
    std::wstring sampleId;
};

struct TempoEvent {
    double timeSeconds{};
    double bpm{};
};

struct ScrollEvent {
    double timeSeconds{};
    double positionSeconds{};
    double speed = 1; // Signed visual velocity; judgement/audio remain on chart time.
    double acceleration{};
};

enum class BgaLayer { Base, Poor, Layer, Layer2 };

struct BgaEvent {
    double timeSeconds{};
    BgaLayer layer{ BgaLayer::Base };
    std::wstring imageId;
};

struct BgaClip {
    std::wstring bitmapId;
    int x{};
    int y{};
    int width{};
    int height{};
    int destinationX{};
    int destinationY{};
    bool cropped{};
};

struct Chart {
    std::wstring catalogGroupKey;
    bool catalogOnly{}; // Selection metadata; reparse before preview/gameplay.
    std::filesystem::path path;
    std::wstring title;
    std::wstring subtitle;
    std::wstring artist;
    std::wstring genre;
    std::wstring playLevel;
    std::wstring subartist, comment, charset;
    std::filesystem::path banner;
    int difficulty{}, objectBase = 36, rank = 2;
    bool rankDefaulted = true;
    std::wstring judgementSpec = L"SM-JUDGE-1";
    double extendedRank = -1, total = -1, referenceBpm{}, wavVolume = 100;
    LongNoteMode longNoteMode = LongNoteMode::LN;
    std::vector<int> randomSelections;
    std::vector<HiddenNote> invisible;
    std::vector<MineNote> mines;
    std::vector<ValueEvent> judgeRanks, speeds;
    std::vector<TextEvent> texts;
    std::unordered_map<std::wstring, SampleEffect> sampleEffects;
    bool nonMonotonicScroll{},halfJudgement{};
    bool bmson{};
    std::unordered_map<std::wstring,SampleSlice> sampleSlices;
    std::unordered_map<std::wstring,std::vector<std::wstring>> sampleGroups;
    double initialBpm{};
    ChartMode mode{ ChartMode::Unknown };
    bool playable{};
    bool partialLoad{};
    bool sharedSampleDefinitions{};
    std::vector<std::wstring> silentSampleIds;
    std::vector<std::filesystem::path> missingSampleFiles;
    std::wstring partialLoadReason;
    std::wstring blockReason;
    std::vector<Note> notes;
    std::vector<BgmEvent> bgm;
    std::vector<TempoEvent> tempos;
    std::vector<ScrollEvent> scroll;
    std::vector<BgaEvent> bga;
    std::array<std::vector<ValueEvent>,4> bgaOpacity;
    std::array<std::vector<ColorEvent>,4> bgaColors;
    std::unordered_map<std::wstring,BgaColor> bitmapColors;
    std::unordered_map<std::wstring,SwitchAnimation> switchAnimations;
    std::vector<SwitchEvent> switchEvents;
    std::vector<std::wstring> options;
    int poorBgaMode{};
    double videoFps{};
    std::unordered_map<std::wstring, std::filesystem::path> samples;
    std::unordered_map<std::wstring, std::filesystem::path> bitmaps;
    std::filesystem::path fallbackVideo;
    std::filesystem::path titleImage;
    std::filesystem::path finishImage;
    std::filesystem::path previewAudio;
    std::vector<std::filesystem::path> previewAudioCandidates;
    bool previewStartDefined{};
    double previewStartSeconds{};
    double previewDurationSeconds = 20;
    std::unordered_map<std::wstring, BgaClip> bgaClips;
    double durationSeconds{};
};

struct ParseResult {
    Chart chart;
    std::vector<std::wstring> errors;
    std::vector<std::wstring> warnings;
};

class BmsParser {
    struct SharedDefinition { std::wstring chartPath, value; };
    struct DirectoryData {
        std::unordered_map<std::wstring, std::filesystem::path> files;
        std::vector<std::filesystem::path> charts, bgaFiles;
        std::unordered_map<std::wstring, std::vector<SharedDefinition>> definitions;
        bool definitionsReady{};
    };
    bool reuseDirectoryData_{};
    uint64_t seed_{};
    bool assetRecovery_ = true;
    mutable std::unordered_map<std::wstring, DirectoryData> directories_;
    DirectoryData& directoryData(const std::filesystem::path& directory) const;
    void loadSharedDefinitions(DirectoryData& data) const;
public:
    // Batch scans use one parser per song folder. A fresh parser snapshots file
    // names once; ordinary calls refresh the snapshot on every parse.
    explicit BmsParser(bool reuseDirectoryData = false, uint64_t seed = 0, bool assetRecovery = true)
        : reuseDirectoryData_(reuseDirectoryData), seed_(seed), assetRecovery_(assetRecovery) {}
    ParseResult parse(const std::filesystem::path& path) const;
    static std::wstring modeLabel(ChartMode mode);
};

std::wstring toLower(std::wstring value);

} // namespace bms
