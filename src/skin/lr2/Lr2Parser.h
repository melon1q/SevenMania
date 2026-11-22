#pragma once
#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace sevenmania::skin::lr2 {
enum class ScreenType { Play7Key=0, Play5Key=1, Play14Key=2, Play10Key=3, Play9Key=4, Select=5, Decide=6, Result=7 };
constexpr bool isPlayScreen(int type) noexcept {return type>=0&&type<=4;}
// LR2 note lanes: 0 is the 1P scratch, 1-7 its keys, 10 the 2P scratch and 11-17 its keys.
// 9KEY uses 1-9 for its nine buttons.
inline constexpr int maxPlayLanes=20;
// True for an LR2 lane that a play screen of this type must place.
// The 2P side (lanes 10-19) of a single-play skin (7KEY, 5KEY). LR2 half skins declare both sides in one
// file; the side a single-play mode never uses is skipped without a warning.
constexpr bool playLaneOtherSide(int type,int lane) noexcept {
    return (type==0||type==1)&&lane>=10&&lane<=19;
}
constexpr bool playLaneUsed(int type,int lane) noexcept {
    switch(type) {
        case 0: return lane>=0&&lane<=7;
        case 1: return lane>=0&&lane<=5;
        case 2: return (lane>=0&&lane<=7)||(lane>=10&&lane<=17);
        case 3: return (lane>=0&&lane<=5)||(lane>=10&&lane<=15);
        case 4: return lane>=1&&lane<=9;
    }
    return false;
}
// Lanes a play screen must place. 0 for the other screens.
constexpr int playLaneCount(int type) noexcept {
    int count=0;
    for(int lane=0;lane<maxPlayLanes;++lane)count+=playLaneUsed(type,lane);
    return count;
}
// SevenMania's lane (5/7/10/14 keys: x%8 is the key, x/8 the side; 9 keys: 0-8) to the LR2 lane, or -1.
constexpr int lr2Lane(int type,int lane) noexcept {
    if(lane<0||lane>=16)return -1;
    const int mapped=type==4?lane+1:lane/8*10+lane%8;
    return playLaneUsed(type,mapped)?mapped:-1;
}
// The side (0 = 1P, 1 = 2P) whose judgement a SevenMania lane shows; only double play has a 2P side.
constexpr int lr2Side(int type,int lane) noexcept {return (type==2||type==3)&&lane>=8?1:0;}
// Keys of the play type: 7, 5, 14, 10 or 9, or 0.
constexpr int playKeys(int type) noexcept {
    constexpr int keys[]{7,5,14,10,9};
    return isPlayScreen(type)?keys[type]:0;
}
// Strings are UTF-8 after strict Windows-31J decoding; fields include the #word.
struct Command {
    std::filesystem::path source;
    std::size_t line{};
    std::string name;
    std::vector<std::string> fields;
    bool enabled{};
};
// `selected` is the choice in effect after defaults, host options and user choices.
struct CustomOption { std::string name; int firstId{}; std::vector<std::string> choices; int selected{}; };
// `selected` is the user's filename stem for the '*'; empty uses `defaultName`.
struct CustomFile { std::string name, pattern, defaultName, selected; };
// `values` are x, y, w, h, rotation, alpha with disabled components forced to zero.
struct CustomOffset { std::string name; int id{}; std::array<bool,6> enabled{true,true,true,true,true,true}; std::array<int,6> values{}; };
// User selections keyed by the names a skin declares in its header.
struct SkinChoices {
    std::map<std::string,std::string> options;            // CUSTOMOPTION name -> choice name
    std::map<std::string,std::string> files;              // CUSTOMFILE name -> filename stem
    std::map<std::string,std::array<int,6>> offsets;      // CUSTOMOFFSET name -> x, y, w, h, r, a
};
struct Header {
    int type = -1;
    std::string name, author;
    unsigned width = 640, height = 480;
    std::vector<CustomOption> options;
    std::vector<CustomFile> files;
    std::vector<CustomOffset> offsets;
};
struct Diagnostic { std::filesystem::path source; std::size_t line{}; std::string message; bool error{}; };
struct Document {
    Header header;
    std::vector<Command> commands;
    std::vector<Diagnostic> diagnostics;
    std::filesystem::path root;
    std::map<int,bool> optionValues;
    // Every CUSTOMOFFSET ID the header declares, with the user's values (zero by default).
    std::map<int,std::array<int,6>> offsetValues;
};
struct Limits {
    std::size_t fileBytes = 4u<<20, totalBytes = 32u<<20, commands = 100000, includeDepth = 16;
};
struct Options {
    // Permitted root containing LR2files; empty discovers the LR2files ancestor
    // or restricts all files to the input file's directory.
    std::filesystem::path root;
    std::map<int,bool> initialOptions;
    // Applied after the header is read. A named choice overrides `initialOptions` for that option's IDs.
    SkinChoices choices;
    // The signed condition ID is passed unchanged to the game-state provider.
    std::function<std::optional<bool>(int)> condition;
    Limits limits;
    // The preview can display available layers when a skin distribution omits
    // an include. It also picks a stable match for a single-directory *.csv.
    bool previewIncludes{};
    // Read the header of any screen type and stop: no body commands, no INCLUDE files.
    bool headerOnly{};
    ScreenType screen=ScreenType::Play7Key;
};
struct Result {
    std::optional<Document> document;
    std::vector<Diagnostic> diagnostics;
    explicit operator bool() const noexcept { return document.has_value(); }
};
Result parse(const std::filesystem::path& file,const Options& options = {}) noexcept;
// Resolves a literal path using the same containment policy as INCLUDE.
std::filesystem::path resolveAssetPath(const std::filesystem::path& root,
                                       const std::filesystem::path& declaringFile,
                                       const std::string& value);
// Also accepts one '*' in the filename: a CUSTOMFILE with the same pattern supplies the
// selected file, then its default; otherwise the first match in case-insensitive name order is used.
std::filesystem::path resolveWildcardAsset(const std::filesystem::path& root,
                                           const std::filesystem::path& declaringFile,
                                           const std::string& value,const std::vector<CustomFile>& files);
// Filename stems a CUSTOMFILE pattern can take, in case-insensitive name order.
std::vector<std::string> listWildcardChoices(const std::filesystem::path& root,
                                             const std::filesystem::path& declaringFile,
                                             const std::string& pattern);
std::filesystem::path resolveAssetDirectory(const std::filesystem::path& root,
                                            const std::filesystem::path& declaringFile,
                                            const std::string& value);
}
