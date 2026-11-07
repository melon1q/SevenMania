#include "BmsParser.h"
#include "RustBackend.h"
#include "PreviewTiming.h"
#include "BmsControlFlow.h"
#include "BmsText.h"
#include "BmsonParser.h"
#include "ChartOptions.h"
#include <random>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stack>
#include <windows.h>

namespace bms {
namespace {

struct RawObject {
    int measure{};
    double position{};
    int channel{};
    std::wstring id;
    int line{};
    double endPosition{};
};

struct LaneObject {
    RawObject raw;
    int lane{};
    bool longChannel{};
};

struct TimedObject {
    double time{};
    int lane{-1};
    int channel{};
    std::wstring id;
};

struct Condition {
    bool parentActive{};
    bool branchMatched{};
};

std::wstring trim(std::wstring value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](wchar_t c) { return std::iswspace(c) != 0; });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t c) { return std::iswspace(c) != 0; }).base();
    if (first >= last) return {};
    return std::wstring(first, last);
}

std::vector<std::wstring> splitLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    if (rust_backend::splitLines(text, lines)) return lines;
    std::wistringstream input(text);
    std::wstring line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        lines.push_back(std::move(line));
    }
    return lines;
}

std::wstring decodeBytes(const std::vector<char>& bytes,const std::filesystem::path& directory) { return decodeBmsText(bytes,directory); }

std::wstring readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("파일을 열 수 없습니다.");
    const auto size = in.tellg();
    if (size < 0 || size > std::numeric_limits<int>::max()) throw std::runtime_error("invalid chart size");
    std::vector<char> bytes(static_cast<size_t>(size));
    in.seekg(0);
    if (!bytes.empty() && !in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) throw std::runtime_error("chart read failed");
    return decodeBytes(bytes,path.parent_path());
}

std::wstring koreanArchiveFilename(const std::wstring& filename) {
    // Older Korean ZIP extractors can interpret Shift-JIS filename bytes as
    // CP949. Only use a lossless, reversible alias; never guess by similarity.
    if (std::all_of(filename.begin(), filename.end(), [](wchar_t c) { return c < 128; })) return {};
    const auto encode = [](const std::wstring& value, UINT codePage) {
        BOOL substituted = FALSE;
        const int size = WideCharToMultiByte(codePage, WC_NO_BEST_FIT_CHARS, value.data(),
            static_cast<int>(value.size()), nullptr, 0, nullptr, &substituted);
        if (size <= 0 || substituted) return std::string{};
        std::string bytes(static_cast<size_t>(size), '\0');
        if (!WideCharToMultiByte(codePage, WC_NO_BEST_FIT_CHARS, value.data(),
            static_cast<int>(value.size()), bytes.data(), size, nullptr, &substituted) || substituted) return std::string{};
        return bytes;
    };
    const auto bytes = encode(filename, 932);
    if (bytes.empty()) return {};
    const int size = MultiByteToWideChar(949, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring alias(static_cast<size_t>(size), L'\0');
    if (!MultiByteToWideChar(949, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), alias.data(), size)) return {};
    if (alias == filename || encode(alias, 949) != bytes) return {};
    return alias;
}

int parseChannel(const std::wstring& value) {
    const auto channelName = toLower(value);
    if (channelName == L"sc") return 1000;
    if (channelName == L"sp") return 1001;
    if (channelName.size() == 2 && channelName[0] == L'0' && channelName[1] >= L'b' && channelName[1] <= L'e') return 2000 + channelName[1] - L'b';
    try {
        size_t used = 0;
        int channel = std::stoi(value, &used, 10);
        if (used == value.size()) return channel;
    } catch (...) {}
    try {
        size_t used = 0;
        int channel = std::stoi(value, &used, 16);
        if (used == value.size()) return channel;
    } catch (...) {}
    return 0;
}

bool parseDouble(const std::wstring& value, double& out) {
    try {
        size_t used = 0;
        out = std::stod(value, &used);
        return used == value.size() && std::isfinite(out);
    } catch (...) { return false; }
}

bool parseInt(const std::wstring& value, int& out) {
    try {
        size_t used = 0;
        out = std::stoi(value, &used);
        return used == value.size();
    } catch (...) { return false; }
}

bool tryDataLine(const std::wstring& line, int& measure, int& channel, std::wstring& data) {
    const auto value = trim(line);
    if (value.size() < 8 || value[0] != L'#' || value[6] != L':') return false;
    if (!std::all_of(value.begin() + 1, value.begin() + 4, [](wchar_t c) { return c >= L'0' && c <= L'9'; })) return false;
    measure = std::stoi(value.substr(1, 3));
    channel = parseChannel(value.substr(4, 2));
    data = value.substr(7);
    return !data.empty();
}

bool mapLane(int channel, int& lane, bool& isLong) {
    isLong = channel >= 51 && channel <= 69;
    int c = isLong ? channel-40 : channel;
    if (c>=31 && c<=49) c-=20;
    if (c>=0xd1 && c<=0xd9) c=11+(c-0xd1);
    if (c>=0xe1 && c<=0xe9) c=21+(c-0xe1);
    const int side=c>=21 && c<=29 ? 8 : 0;
    if(side) c-=10;
    if(c==16) lane=side;
    else if(c>=11 && c<=15) lane=side+c-10;
    else if(c==18) lane=side+6;
    else if(c==19) lane=side+7;
    else return false;
    return true;
}

bool isPlayerOneChannel(int c) { return (c>=11&&c<=19)||(c>=31&&c<=39)||(c>=51&&c<=59)||(c>=0xd1&&c<=0xd9); }
bool isPlayerTwoChannel(int c) { return (c>=21&&c<=29)||(c>=41&&c<=49)||(c>=61&&c<=69)||(c>=0xe1&&c<=0xe9); }

ChartMode determineMode(const std::filesystem::path& path, int player, const std::set<int>& channels) {
    const auto ext = toLower(path.extension().wstring());
    if (ext == L".pms") return ChartMode::PopnNine;
    bool p1 = std::any_of(channels.begin(), channels.end(), isPlayerOneChannel);
    bool p2 = std::any_of(channels.begin(), channels.end(), isPlayerTwoChannel);
    if (player == 3 || p2) {
        const bool seven = ext==L".bme" || channels.contains(38) || channels.contains(39) || channels.contains(48) || channels.contains(49) || channels.contains(0xd8) || channels.contains(0xd9) || channels.contains(0xe8) || channels.contains(0xe9) || channels.contains(18) || channels.contains(19) || channels.contains(28) || channels.contains(29) || channels.contains(58) || channels.contains(59) || channels.contains(68) || channels.contains(69);
        return seven ? ChartMode::FourteenKey : ChartMode::TenKey;
    }
    if (player != 0 && player != 1 && player != 2) return ChartMode::Other;
    if (ext == L".bme") return ChartMode::SevenKey;
    const bool extension = channels.contains(0xd8) || channels.contains(0xd9) || channels.contains(18) || channels.contains(19) || channels.contains(38) || channels.contains(39) || channels.contains(58) || channels.contains(59);
    if (extension) return ChartMode::SevenKey;
    if (p1) return ChartMode::FiveKey;
    return ChartMode::Unknown;
}

struct ConditionEvent { double beat{}; int channel{}; int lane{-1}; std::wstring id; };

int eventPriority(int channel) {
    if (channel == 3 || channel == 8) return 0;
    // Objects on the STOP beat occur at its beginning, not after its delay.
    if (channel == 9 || channel==1002) return 3;
    if (channel == 1) return 1;
    return 2;
}

} // namespace

std::wstring toLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}

std::wstring BmsParser::modeLabel(ChartMode mode) {
    switch (mode) {
    case ChartMode::FiveKey: return L"5키";
    case ChartMode::SevenKey: return L"7+1키";
    case ChartMode::TenKey: return L"10키";
    case ChartMode::FourteenKey: return L"14키";
    case ChartMode::PopnNine: return L"PMS 9키";
    case ChartMode::Other: return L"기타";
    default: return L"판별 불가";
    }
}

BmsParser::DirectoryData& BmsParser::directoryData(const std::filesystem::path& directory) const {
    const auto absolute = std::filesystem::absolute(directory).lexically_normal();
    const auto key = toLower(absolute.wstring());
    if (auto found = directories_.find(key); found != directories_.end()) return found->second;
    DirectoryData data;
    const std::set<std::wstring> chartExtensions{ L".bms", L".bme", L".bml", L".pms", L".bmson", L".mbm" };
    const std::set<std::wstring> bgaExtensions{ L".bmp", L".png", L".jpg", L".jpeg", L".gif", L".mp4", L".m4v", L".mov", L".wmv", L".avi", L".mpeg", L".mpg", L".m1v", L".m2v" };
    std::error_code ec;
    for (std::filesystem::directory_iterator it(absolute, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec) || ec) { ec.clear(); continue; }
        const auto file = it->path();
        data.files.try_emplace(toLower(file.filename().wstring()), file);
        const auto extension = toLower(file.extension().wstring());
        if (chartExtensions.contains(extension)) data.charts.push_back(file);
        if (bgaExtensions.contains(extension)) data.bgaFiles.push_back(file);
    }
    std::sort(data.bgaFiles.begin(), data.bgaFiles.end(), [](const auto& a, const auto& b) {
        return toLower(a.filename().wstring()) < toLower(b.filename().wstring());
    });
    return directories_.emplace(key, std::move(data)).first->second;
}

void BmsParser::loadSharedDefinitions(DirectoryData& data) const {
    if (data.definitionsReady) return;
    for (const auto& candidate : data.charts) {
        try {
            std::unordered_map<std::wstring, std::wstring> fileDefinitions;
            for (const auto& line : splitLines(readText(candidate))) {
                std::wistringstream row(line);
                std::wstring key, value;
                if (!(row >> key)) continue;
                key = toLower(std::move(key));
                if (key.size() != 6 || key.rfind(L"#wav", 0) != 0) continue;
                std::getline(row, value);
                value = trim(std::move(value));
                if (!value.empty()) fileDefinitions.try_emplace(key.substr(4), std::move(value));
            }
            const auto chartKey = toLower(candidate.lexically_normal().wstring());
            for (auto& [id, value] : fileDefinitions) data.definitions[id].push_back({chartKey, std::move(value)});
        } catch (...) { /* A broken sibling must not interrupt the scan. */ }
    }
    data.definitionsReady = true;
}

ParseResult BmsParser::parse(const std::filesystem::path& path) const {
    if (!reuseDirectoryData_) directories_.clear();
    ParseResult result;
    auto& chart = result.chart;
    chart.path = path;
    chart.title = path.stem().wstring();

    std::wstring text;
    try { text = readText(path); }
    catch (const std::exception&) {
        chart.blockReason = L"파일을 읽을 수 없음";
        return result;
    }

    if(toLower(path.extension().wstring())==L".bmson")return parseBmson(path,text);
    std::unordered_map<std::wstring, std::wstring> headers;
    std::unordered_map<std::wstring, std::wstring> sampleNames;
    std::unordered_map<std::wstring, std::wstring> bitmapNames;
    std::unordered_map<std::wstring, BgaClip> bgaClips;
    std::unordered_map<std::wstring, double> bpmDefinitions, stopDefinitions;
    std::vector<RawObject> objects;
    std::map<int, double> measureScales;
    std::set<int> channels;
    int player = 0, lnType = 1;
    static const uint64_t sessionSeed = (uint64_t(std::random_device{}()) << 32) ^ std::random_device{}();
    BmsControlFlow control((seed_ ? seed_ : sessionSeed) ^ std::hash<std::wstring>{}(toLower(std::filesystem::absolute(path).lexically_normal().wstring())));
    int objectBase = 36;
    const auto idKey = [&](std::wstring id) { return objectBase == 62 ? id : toLower(std::move(id)); };
    const auto unquote = [](std::wstring value) {
        value = trim(std::move(value));
        if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') value = value.substr(1,value.size()-2);
        return value;
    };
    std::unordered_map<std::wstring,double> scrollDefinitions, speedDefinitions, rankDefinitions;
    std::unordered_map<std::wstring,std::wstring> textDefinitions;
    std::unordered_map<std::wstring,BgaColor> colorDefinitions;
    std::vector<RawObject> stpEvents;
    std::wstring wavFolder;

    double baseBpm = 0;
    std::wstring lnObj;
    std::set<std::wstring> lnObjects;
    bool hasLnObj = false, unsupportedLn = false, malformed = false;

    int lineNumber = 0;

    // BASE is a global header, including when declared after indexed assets.
    {
        auto preflight=control;std::vector<std::wstring> ignored;
        for(const auto& raw:splitLines(text)) {
            const auto line=trim(raw);if(line.empty() || line[0]!=L'#')continue;
            const auto at=line.find_first_of(L" \t");
            const auto key=toLower(line.substr(1,at==std::wstring::npos?at:at-1));
            const auto value=at==std::wstring::npos?L"":trim(line.substr(at+1));
            if(preflight.consume(key,value,ignored))continue;
            if(preflight.active() && key==L"base") {int base{};if(parseInt(value,base) && (base==16 || base==36 || base==62))objectBase=base;}
        }
    }
    for (const auto& rawLine : splitLines(text)) {
        ++lineNumber;
        const auto line = trim(rawLine);
        if (line.empty() || line[0] == L';') continue;

        int measure = 0, channel = 0;
        std::wstring data;
        if (tryDataLine(line, measure, channel, data)) {
            if (!control.active()) continue;
            if (channel == 2) {
                double scale = 0;
                if (!parseDouble(data, scale) || scale <= 0 || scale > 1e6) {
                    malformed = true;
                    result.errors.push_back(L"" + std::to_wstring(lineNumber) + L"행: 마디 길이 데이터가 잘못되었습니다.");
                } else measureScales[measure] = scale;
                continue;
            }
            const auto dataEnd=data.find_first_of(L" \t;");
            if(dataEnd!=std::wstring::npos)data=data.substr(0,dataEnd);
            if (data.size() % 2 != 0) {
                malformed = true;
                result.errors.push_back(std::to_wstring(lineNumber) + L"행: 오브젝트 데이터 길이가 짝수가 아닙니다.");
                continue;
            }
            channels.insert(channel);
            const size_t slots = data.size() / 2;
            for (size_t i = 0; i < slots; ++i) {
                std::wstring id = data.substr(i * 2, 2);
                const bool hex=objectBase==16 || channel==3 || (channel>=2000 && channel<=2003);
                const bool valid=std::all_of(id.begin(),id.end(),[&](wchar_t c){return (c>=L'0'&&c<=L'9') || (c>=L'A'&&c<=(hex?L'F':L'Z')) || (c>=L'a'&&c<=(hex?L'f':L'z'));});
                if(!valid){result.errors.push_back(std::to_wstring(lineNumber)+L"행: 잘못된 오브젝트 ID "+id);continue;}
                if (objectBase != 62) std::transform(id.begin(), id.end(), id.begin(), ::towupper);
                if (id == L"00" && !(channel >= 51 && channel <= 69) && !(channel>=2000 && channel<=2003)) continue;
                objects.push_back({ measure, static_cast<double>(i) / slots, channel, std::move(id), lineNumber, static_cast<double>(i+1)/slots });
            }
            continue;
        }

        if (line[0] != L'#') continue;
        size_t split = line.find_first_of(L" \t");
        std::wstring key = toLower(line.substr(1, split == std::wstring::npos ? split : split - 1));
        std::wstring value = split == std::wstring::npos ? L"" : trim(line.substr(split + 1));
        if (control.consume(key,value,result.errors)) continue;
        if (!control.active()) continue;
        if (key == L"rank") {
            int rank{};
            if(parseInt(value,rank) && rank>=0 && rank<=3) {chart.rank=rank;chart.rankDefaulted=false;}
            else {result.warnings.push_back(std::to_wstring(lineNumber)+L"행: 잘못된 #RANK (0~3 정수만 지원), 이전 유효값을 유지합니다.");
                OutputDebugStringW((result.warnings.back()+L"\n").c_str());}
            continue;
        }
        const auto originalKey = line.substr(1, split == std::wstring::npos ? split : split-1);
        const auto suffix = [&](size_t prefix) { return idKey(originalKey.substr(prefix)); };
        if (key == L"base") {
            int base{}; if (parseInt(value,base) && (base==16 || base==36 || base==62)) {}
            else result.errors.push_back(L"잘못된 #BASE 값입니다.");
            continue;
        }
        const auto parseColor=[](std::wstring text,BgaColor& color) {
            std::replace(text.begin(),text.end(),L',',L' ');std::wistringstream in(text);
            if(!(in>>color.alpha>>color.red>>color.green>>color.blue))return false;
            for(int value:{color.alpha,color.red,color.green,color.blue})if(value<0 || value>255)return false;
            return true;
        };
        if(key==L"option") {chart.options.push_back(value);continue;}
        if(key==L"poorbga") {parseInt(value,chart.poorBgaMode);continue;}
        if(key==L"videof/s") {parseDouble(value,chart.videoFps);continue;}
        if(key.size()==6 && key.starts_with(L"argb")) {BgaColor c;if(parseColor(value,c))colorDefinitions[suffix(4)]=c;else result.errors.push_back(L"잘못된 ARGB 값");continue;}
        if(key.size()==7 && key.starts_with(L"exbmp")) {
            const auto space=value.find_first_of(L" \t");BgaColor c;
            if(space!=std::wstring::npos && parseColor(value.substr(0,space),c)) {
                bitmapNames[suffix(5)]=unquote(value.substr(space+1));chart.bitmapColors[suffix(5)]=c;
            } else result.errors.push_back(L"잘못된 EXBMP 값");
            continue;
        }
        if(key.size()==7 && key.starts_with(L"swbga")) {
            const auto space=value.find_first_of(L" \t");auto params=value.substr(0,space);std::vector<std::wstring> fields;size_t begin=0;
            for(size_t i=0;i<=params.size();++i)if(i==params.size() || params[i]==L':'){fields.push_back(params.substr(begin,i-begin));begin=i+1;}
            SwitchAnimation animation;int boundChannel{},loop{};bool longChannel=false;
            if(fields.size()==5 && parseDouble(fields[0],animation.frameMilliseconds) && animation.frameMilliseconds>0 &&
                parseDouble(fields[1],animation.durationMilliseconds) && animation.durationMilliseconds>=0 && parseInt(fields[2],boundChannel) &&
                mapLane(boundChannel,animation.lane,longChannel) && parseInt(fields[3],loop) && parseColor(fields[4],animation.color) && space!=std::wstring::npos) {
                animation.loop=loop!=0;auto frames=trim(value.substr(space+1));
                for(size_t i=0;i+1<frames.size();i+=2)animation.frames.push_back(objectBase==62?frames.substr(i,2):toLower(frames.substr(i,2)));
                chart.switchAnimations[suffix(5)]=std::move(animation);
            } else result.errors.push_back(L"잘못된 SWBGA 값");continue;
        }
        if(key==L"text"){chart.texts.push_back({0,unquote(value)});continue;}
        if (key == L"path_wav") { wavFolder=unquote(value); continue; }
        if (key == L"wavcmd") {
            std::wistringstream args(value); int command{}; std::wstring id; double n{};
            if (args >> command >> id >> n) {
                auto& fx=chart.sampleEffects[idKey(id)];
                if (command==0) fx.pitch=std::clamp(n,0.0,127.0);
                else if (command==1) fx.volume=std::max(0.0,n);
                else if (command==2) fx.durationMilliseconds=std::max(0.0,n*0.5);
                else result.errors.push_back(L"잘못된 WAVCMD 명령입니다.");
            } else result.errors.push_back(L"잘못된 WAVCMD 인수입니다.");
            continue;
        }
        if (key.size()==8 && key.starts_with(L"scroll")) { double n{}; if(parseDouble(value,n)) scrollDefinitions[suffix(6)]=n; continue; }
        if (key.size()==7 && key.starts_with(L"speed")) { double n{}; if(parseDouble(value,n)) speedDefinitions[suffix(5)]=n; continue; }
        if (key.size()==8 && key.starts_with(L"exrank")) { double n{}; if(parseDouble(value,n) && n>0) rankDefinitions[suffix(6)]=n; continue; }
        if (key.size()==6 && key.starts_with(L"text")) { textDefinitions[suffix(4)]=unquote(value); continue; }
        if (key==L"stp") {
            std::wistringstream args(value); std::wstring location; double milliseconds{};
            if (args>>location>>milliseconds) {
                const auto dot=location.find(L'.'); int m{}, part{};
                if(parseInt(location.substr(0,dot),m) && m>=0 && m<=999 &&
                    (dot==std::wstring::npos || parseInt(location.substr(dot+1),part)) && part>=0 && part<1000 && milliseconds>=0)
                    stpEvents.push_back({m,part/1000.0,1002,std::to_wstring(milliseconds),lineNumber});
                else result.errors.push_back(L"잘못된 STP 위치입니다.");
            } else result.errors.push_back(L"잘못된 STP 인수입니다.");
            continue;
        }

        if (key.size() == 5 && key.rfind(L"wav", 0) == 0) sampleNames[suffix(3)] = unquote(value);
        else if (key.size() == 5 && key.rfind(L"bmp", 0) == 0) bitmapNames[suffix(3)] = unquote(value);
        else if ((key.size()==5 && key.starts_with(L"bga")) || (key.size()==6 && key.starts_with(L"@bga"))) {
            std::wistringstream args(value);
            std::wstring bitmapToken;
            int x1 = 0, y1 = 0, x2 = 0, y2 = 0, dx = 0, dy = 0;
            if (args >> bitmapToken >> x1 >> y1 >> x2 >> y2 >> dx >> dy) {
                int bitmapNumber = 0;
                BgaClip clip;
                if (!key.starts_with(L"@") && parseInt(bitmapToken, bitmapNumber) && bitmapNumber >= 0 && bitmapNumber <= 255) {
                    wchar_t bitmapId[3]{};
                    swprintf_s(bitmapId, L"%02X", bitmapNumber);
                    clip.bitmapId = bitmapId;
                } else {
                    clip.bitmapId = bitmapToken;
                    std::transform(clip.bitmapId.begin(), clip.bitmapId.end(), clip.bitmapId.begin(), ::towupper);
                }
                clip.x=key.starts_with(L"@")?x1:std::min(x1,x2);
                clip.y=key.starts_with(L"@")?y1:std::min(y1,y2);
                clip.width=key.starts_with(L"@")?std::max(0,x2):std::abs(x2-x1);
                clip.height=key.starts_with(L"@")?std::max(0,y2):std::abs(y2-y1);
                clip.destinationX = dx;
                clip.destinationY = dy;
                clip.cropped=true;
                if(clip.width<=0 || clip.height<=0)result.errors.push_back(L"BGA 자르기 영역이 비어 있습니다.");
                bgaClips[suffix(key.starts_with(L"@")?4:3)]=std::move(clip);
            }
        }
        else if (key.size() == 5 && key.rfind(L"bpm", 0) == 0) { double n{}; if (parseDouble(value, n) && n > 0) bpmDefinitions[suffix(3)] = n; }
        else if (key.size() == 6 && key.rfind(L"stop", 0) == 0) { double n{}; if (parseDouble(value, n) && std::isfinite(n)) stopDefinitions[suffix(4)] = std::abs(n); }
        else if (key.size() == 7 && key.starts_with(L"exbpm")) { double n{}; if(parseDouble(value,n) && n>0) bpmDefinitions[suffix(5)]=n; }
        else if (key == L"player") parseInt(value, player);
        else if (key == L"title" || key == L"subtitle" || key == L"artist" || key == L"genre" || key == L"playlevel" || key == L"difficulty" || key == L"bpm" ||
            key == L"stagefile" || key == L"backbmp" || key == L"preview" || key == L"previewpoint" ||
            key == L"previewstart" || key == L"previewduration" || key==L"rank" || key==L"defexrank" || key==L"exrank" || key==L"total" ||
            key==L"lnmode" || key==L"basebpm" || key==L"subartist" || key==L"comment" || key==L"banner" || key==L"charset" ||
            key==L"volwav" || key==L"videofile" || key==L"materialswav" || key==L"materialsbmp" || key==L"option") headers[key] = value;
        else if (key == L"lnobj") { lnObj = value; if(objectBase!=62) std::transform(lnObj.begin(), lnObj.end(), lnObj.begin(), ::towupper); hasLnObj = true; std::wistringstream ids(lnObj);std::wstring id;while(ids>>id)lnObjects.insert(idKey(id)); }
        else if (key == L"lntype") { parseInt(value, lnType); unsupportedLn = lnType != 1 && lnType != 2; }

    }

    control.finish(result.errors);
    chart.randomSelections = control.selections;
    chart.objectBase=objectBase;
    chart.subartist=headers[L"subartist"]; chart.comment=headers[L"comment"]; chart.charset=headers[L"charset"];
    parseInt(headers[L"difficulty"],chart.difficulty);
    parseDouble(headers.contains(L"defexrank")?headers[L"defexrank"]:headers[L"exrank"],chart.extendedRank);
    parseDouble(headers[L"total"],chart.total); parseDouble(headers[L"basebpm"],chart.referenceBpm);
    parseDouble(headers[L"volwav"],chart.wavVolume); chart.wavVolume=std::max(0.0,chart.wavVolume);
    int lnMode=1; parseInt(headers[L"lnmode"],lnMode);
    if(lnMode<0 || lnMode>3){result.errors.push_back(L"LNMODE 값은 0부터 3까지여야 합니다.");lnMode=1;}
    chart.longNoteMode=static_cast<LongNoteMode>(std::clamp(lnMode,1,3));
    if (headers.contains(L"banner")) chart.banner=std::filesystem::absolute(path.parent_path()/unquote(headers[L"banner"]));

    if (headers.contains(L"bpm")) parseDouble(headers[L"bpm"], baseBpm);
    chart.title = headers.contains(L"title") ? headers[L"title"] : path.stem().wstring();
    chart.subtitle = headers[L"subtitle"];
    chart.artist = headers[L"artist"];
    chart.genre = headers[L"genre"];
    double previewStart{}, previewDuration{};
    const auto previewPosition = headers.contains(L"previewstart") ? headers[L"previewstart"] : headers[L"previewpoint"];
    if (parseDouble(previewPosition, previewStart) && std::isfinite(previewStart) && previewStart >= 0) {
        chart.previewStartSeconds = previewStart;
        chart.previewStartDefined = true;
    }
    if (parseDouble(headers[L"previewduration"], previewDuration) && std::isfinite(previewDuration) && previewDuration > 0)
        chart.previewDurationSeconds = std::clamp(previewDuration, 1.0, 120.0);
    // #DIFFICULTY is a chart category (Beginner/Normal/Hyper/etc.), not its
    // numeric play level. Preserve #PLAYLEVEL verbatim and never infer it.
    chart.playLevel = headers.contains(L"playlevel") ? headers[L"playlevel"] : L"";
    chart.initialBpm = baseBpm > 0 ? baseBpm : 120;
    chart.mode = determineMode(path, player, channels);

    const bool hasLongChannel = std::any_of(objects.begin(),objects.end(),[](const RawObject& o){return o.channel>=51 && o.channel<=69;});
    std::wstring modeError;
    if(chart.mode==ChartMode::Unknown || chart.mode==ChartMode::Other) modeError=L"지원하지 않는 플레이 방식입니다.";
    if(unsupportedLn) modeError=L"유효하지 않은 LNTYPE입니다.";
    std::vector<LaneObject> laneObjects;
    for (const auto& o : objects) {
        int lane = -1; bool isLong = false;
        if (mapLane(o.channel, lane, isLong)) {
            if(chart.mode==ChartMode::PopnNine) {
                int c=o.channel; if(c>=51 && c<=69)c-=40;else if(c>=31 && c<=49)c-=20;
                else if(c>=0xd1 && c<=0xd9)c=c-0xd0+10;else if(c>=0xe1 && c<=0xe9)c=c-0xe0+20;
                if(c>=11 && c<=15) lane=c-11;
                else if(c>=22 && c<=25) lane=c-17;
                else continue;
            }
            laneObjects.push_back({o,lane,isLong});
        }
    }

    int maxMeasure = 0;
    for (const auto& o : objects) maxMeasure = std::max(maxMeasure, o.measure);
    for (const auto& [measure,scale] : measureScales) maxMeasure=std::max(maxMeasure,measure);
    for (const auto& o : stpEvents) maxMeasure=std::max(maxMeasure,o.measure);
    std::vector<double> measureStart(static_cast<size_t>(maxMeasure + 2), 0), measureSpan(static_cast<size_t>(maxMeasure + 1), 4);
    for (int m = 0; m <= maxMeasure; ++m) {
        const auto scale = measureScales.find(m);
        measureSpan[static_cast<size_t>(m)] = 4.0 * (scale == measureScales.end() ? 1.0 : scale->second);
        measureStart[static_cast<size_t>(m + 1)] = measureStart[static_cast<size_t>(m)] + measureSpan[static_cast<size_t>(m)];
    }

    const auto beatOf = [&](const RawObject& o) { return measureStart[o.measure]+o.position*measureSpan[o.measure]; };
    std::vector<ConditionEvent> timeline;
    for(const auto& o:objects) {
        if(o.channel==1 || o.channel==3 || o.channel==4 || o.channel==6 || o.channel==7 || o.channel==8 || o.channel==9 || o.channel==10 ||
           o.channel==1000 || o.channel==1001 || (o.channel>=0xa0 && o.channel<=0xa5) || o.channel==99 || (o.channel>=2000 && o.channel<=2003))
            timeline.push_back({beatOf(o),o.channel,-1,o.id});
    }
    for(const auto& o:stpEvents) timeline.push_back({beatOf(o),1002,-1,o.id});
    for(const auto& o:laneObjects) {
        if(o.longChannel && lnType==2) continue;
        if(o.raw.id==L"00") continue;
        timeline.push_back({beatOf(o.raw),o.raw.channel,o.lane,o.raw.id});
    }
    if(lnType==2) {
        // MGQ encodes covered slots rather than alternating start/end markers.
        // Merge adjacent intervals across bar boundaries; 00/gaps close the run.
        for(int lane=0;lane<16;++lane) {
            struct Run {double first,last; std::wstring id; int channel;}; std::vector<Run> runs;
            for(const auto& o:laneObjects) if(o.longChannel && o.lane==lane && o.raw.id!=L"00")
                runs.push_back({beatOf(o.raw),measureStart[o.raw.measure]+o.raw.endPosition*measureSpan[o.raw.measure],o.raw.id,o.raw.channel});
            std::stable_sort(runs.begin(),runs.end(),[](const Run&a,const Run&b){return a.first<b.first;});
            for(size_t i=0;i<runs.size();) {
                auto r=runs[i++];
                while(i<runs.size() && runs[i].first<=r.last+1e-9) {r.last=std::max(r.last,runs[i].last);++i;}
                timeline.push_back({r.first,r.channel,lane,r.id}); timeline.push_back({r.last,r.channel,lane,L""});
            }
        }
    }
    std::stable_sort(timeline.begin(),timeline.end(),[](const ConditionEvent&a,const ConditionEvent&b){
        return a.beat!=b.beat?a.beat<b.beat:eventPriority(a.channel)<eventPriority(b.channel);
    });
    // Non-BGM timing declarations at a position use the last authored value.
    for(size_t i=0;i<timeline.size();++i) {
        auto& e=timeline[i];
        if(e.channel!=9) continue;
        for(size_t j=i+1;j<timeline.size() && timeline[j].beat==e.beat;++j)
            if(timeline[j].channel==e.channel) { e.channel=-1; break; }
    }
    std::vector<TimedObject> longEvents;
    double bpm = chart.initialBpm, seconds = 0, previousBeat = 0;
    chart.tempos.push_back({ 0, bpm });
    const double reference=chart.referenceBpm>0?chart.referenceBpm:chart.initialBpm;
    double scrollFactor=1;
    chart.scroll.push_back({0,0,bpm/reference});
    const auto scrollEvent = [&](double time, double, double speed) {
        const auto prev=chart.scroll.back(); const double dt=time-prev.timeSeconds;
        const ScrollEvent event{time,prev.positionSeconds+dt*prev.speed,speed};
        if(speed<0) chart.nonMonotonicScroll=true;
        if(chart.scroll.back().timeSeconds==time) chart.scroll.back()=event; else chart.scroll.push_back(event);
    };
    for (const auto& o : timeline) {
        if(o.channel<0) continue;
        seconds += (o.beat - previousBeat) * 60.0 / bpm;
        previousBeat = o.beat;
        if (o.channel == 3) {
            try { size_t used{}; int value = std::stoi(o.id, &used, 16); if (used==o.id.size() && value > 0) { bpm = value; chart.tempos.push_back({ seconds, bpm }); } else result.errors.push_back(L"잘못된 직접 BPM 값입니다."); }
            catch (...) { result.errors.push_back(L"잘못된 직접 BPM 값입니다."); }
            scrollEvent(seconds, o.beat, bpm / reference * scrollFactor);
        } else if (o.channel == 8) {
            const auto id = idKey(o.id);
            if (bpmDefinitions.contains(id)) { bpm = bpmDefinitions[id]; chart.tempos.push_back({ seconds, bpm }); }
            else result.errors.push_back(L"BPM 정의가 없습니다: #BPM" + o.id);
            scrollEvent(seconds, o.beat, bpm / reference * scrollFactor);
        } else if (o.channel == 9 || o.channel == 1002) {
            const auto id = idKey(o.id);
            const double stopBegin = seconds;
            if(o.channel==1002) seconds+=std::stod(o.id)/1000.0;
            else if (stopDefinitions.contains(id)) seconds += stopDefinitions[id] * 60.0 / bpm / 48.0;
            else result.errors.push_back(L"STOP 정의가 없습니다: #STOP"+o.id);
            if (seconds > stopBegin) {
                scrollEvent(stopBegin, o.beat, 0);
                scrollEvent(seconds, o.beat, bpm / reference * scrollFactor);
            }
        } else if (o.channel==1000) {
            if(scrollDefinitions.contains(idKey(o.id))) {scrollFactor=scrollDefinitions[idKey(o.id)];scrollEvent(seconds,o.beat,bpm/reference*scrollFactor);}
            else result.errors.push_back(L"SCROLL 정의가 없습니다: "+o.id);
        } else if(o.channel==1001) {
            if(speedDefinitions.contains(idKey(o.id))) chart.speeds.push_back({seconds,speedDefinitions[idKey(o.id)]});
            else result.errors.push_back(L"SPEED 정의가 없습니다: "+o.id);
        } else if(o.channel==0xa0) {
            if(rankDefinitions.contains(idKey(o.id))) chart.judgeRanks.push_back({seconds,rankDefinitions[idKey(o.id)]});
            else result.errors.push_back(L"EXRANK 정의가 없습니다: "+o.id);
        } else if(o.channel>=0xa1 && o.channel<=0xa4) {
            constexpr int layers[]{0,2,3,1};
            if(colorDefinitions.contains(idKey(o.id)))chart.bgaColors[layers[o.channel-0xa1]].push_back({seconds,colorDefinitions[idKey(o.id)]});
            else result.errors.push_back(L"ARGB 정의가 없습니다: "+o.id);
        } else if(o.channel==0xa5) {
            chart.switchEvents.push_back({seconds,idKey(o.id)});
        } else if(o.channel==99) {
            if(textDefinitions.contains(idKey(o.id))) chart.texts.push_back({seconds,textDefinitions[idKey(o.id)]});
        } else if(o.channel>=2000 && o.channel<=2003) {
            // Filled by the BGA opacity timeline below.
            constexpr int layers[]{0,2,3,1};
            chart.bgaOpacity[layers[o.channel-2000]].push_back({seconds,static_cast<double>(std::stoi(o.id,nullptr,16))/255.0});
        } else if(o.channel>=31 && o.channel<=49) {
            chart.invisible.push_back({o.lane,seconds,o.id});
        } else if((o.channel>=0xd1 && o.channel<=0xd9) || (o.channel>=0xe1 && o.channel<=0xe9)) {
            int value=0; for(wchar_t c:o.id) {c=static_cast<wchar_t>(towupper(c));value=value*36+(c<=L'9'?c-L'0':c-L'A'+10);}
            chart.mines.push_back({o.lane,seconds,value/2.0});
        } else if (o.channel == 1) {
            chart.bgm.push_back({ seconds, o.id });
        } else if (o.channel == 4 || o.channel == 6 || o.channel == 7 || o.channel == 10) {
            const BgaLayer layer = o.channel == 4 ? BgaLayer::Base : o.channel == 6 ? BgaLayer::Poor : o.channel == 7 ? BgaLayer::Layer : BgaLayer::Layer2;
            chart.bga.push_back({ seconds, layer, o.id });
        } else {
            if (o.channel >= 51 && o.channel <= 69) {
                longEvents.push_back({ seconds, o.lane, o.channel, o.id });
            } else {
                chart.notes.push_back({ o.lane, seconds, 0, o.id });
            }
        }
    }
    chart.durationSeconds = seconds;

    if (hasLnObj) {
        for (int lane = 0; lane < 16; ++lane) {
            std::vector<Note> row;
            for (const auto& n : chart.notes) if (n.lane == lane) row.push_back(n);
            std::sort(row.begin(), row.end(), [](const Note& a, const Note& b) { return a.timeSeconds < b.timeSeconds; });
            std::vector<Note> replacement;
            int pending = -1;
            for (const auto& note : row) {
                if (lnObjects.contains(idKey(note.sampleId))) {
                    if(pending<0) {
                        size_t count=0;int channel=51;
                        for(const auto& event:longEvents)if(event.lane==lane && event.time<note.timeSeconds){++count;channel=event.channel;}
                        if(count%2)longEvents.push_back({note.timeSeconds,lane,channel,L""});
                        else result.errors.push_back(L"LNOBJ 종료 오브젝트 앞에 시작 노트가 없습니다.");
                    }
                    else { replacement[static_cast<size_t>(pending)].lengthSeconds = std::max(0.0, note.timeSeconds - replacement[static_cast<size_t>(pending)].timeSeconds);
                        if(sampleNames.contains(idKey(note.sampleId))) {
                            if(lnObjects.size()>1)replacement[static_cast<size_t>(pending)].endSampleId=note.sampleId;
                            else chart.bgm.push_back({note.timeSeconds,note.sampleId});
                        }
                        pending=-1;
                    }
                    continue;
                }
                replacement.push_back(note);
                pending = static_cast<int>(replacement.size()) - 1;
            }
            chart.notes.erase(std::remove_if(chart.notes.begin(), chart.notes.end(), [lane](const Note& n) { return n.lane == lane; }), chart.notes.end());
            chart.notes.insert(chart.notes.end(), replacement.begin(), replacement.end());
        }
    }

    if (hasLongChannel) {
        for (int lane = 0; lane < 16; ++lane) {
            std::vector<TimedObject> row;
            for (const auto& o : longEvents) if (o.lane == lane) row.push_back(o);
            std::sort(row.begin(), row.end(), [](const TimedObject& a, const TimedObject& b) { return a.time < b.time; });
            for (size_t i = 0; i + 1 < row.size(); i += 2) {
                const auto start = row[i], end = row[i + 1];
                for(const auto& n:chart.notes)if(n.lane==lane && n.timeSeconds>=start.time && n.timeSeconds<=end.time && n.sampleId!=start.id && n.sampleId!=end.id)
                    chart.bgm.push_back({n.timeSeconds,n.sampleId});
                chart.notes.erase(std::remove_if(chart.notes.begin(), chart.notes.end(), [&](const Note& n) { return n.lane == lane && std::abs(n.timeSeconds - start.time) < 1e-6; }), chart.notes.end());
                chart.notes.erase(std::remove_if(chart.notes.begin(), chart.notes.end(), [&](const Note& n) { return n.lane == lane && std::abs(n.timeSeconds - end.time) < 1e-6; }), chart.notes.end());
                chart.notes.push_back({ lane, start.time, std::max(0.0, end.time - start.time), start.id, start.id==end.id?L"":end.id, chart.longNoteMode });
            }
            if (row.size() % 2) result.errors.push_back(L"롱노트 시작·종료 오브젝트 수가 맞지 않습니다.");
        }
    }

    for(auto& note:chart.notes) note.mode=chart.longNoteMode;
    applyStaticOptions(chart,(seed_?seed_:sessionSeed)^std::hash<std::wstring>{}(path.wstring()),result.errors);
    std::set<std::wstring> needed;
    for (const auto& n : chart.notes) {needed.insert(n.sampleId);if(!n.endSampleId.empty()) needed.insert(n.endSampleId);}
    for(const auto& n:chart.invisible) needed.insert(n.sampleId);
    if(!chart.mines.empty() && sampleNames.contains(L"00")) needed.insert(L"00");
    std::stable_sort(chart.bgm.begin(),chart.bgm.end(),[](const BgmEvent&a,const BgmEvent&b){return a.timeSeconds<b.timeSeconds;});
    for (const auto& e : chart.bgm) needed.insert(e.sampleId);
    for(const auto& id:lnObjects)if(!sampleNames.contains(idKey(id)))needed.erase(id);
    needed.erase(L"");
    if (chart.notes.empty()) result.errors.push_back(L"플레이 가능한 노트가 없습니다.");
    auto& chartDirectory = directoryData(path.parent_path());
    const auto ownPath = toLower(std::filesystem::absolute(path).lexically_normal().wstring());
    const auto materialPath=[&](const std::wstring& name,const std::wstring& material,const std::wstring& folder) {
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
        const auto base=std::filesystem::path(executable).parent_path();
        if(name.starts_with(L"<")) {
            const auto end=name.find(L'>');if(end!=std::wstring::npos)return base/L"Materials"/name.substr(1,end-1)/name.substr(end+1);
        }
        if(!material.empty()) {
            auto first=base/L"Materials"/unquote(material)/name;
            if(readableAssetExists(first))return first;
            return base/L"_datafiles"/L"materials"/unquote(material)/name;
        }
        return path.parent_path()/folder/name;
    };
    const auto resolveSamplePath = [&](const std::filesystem::path& requested) {
        auto& files = directoryData(requested.parent_path()).files;
        if (const auto exact = files.find(toLower(requested.filename().wstring())); exact != files.end())
            return std::pair{exact->second, true};
        // Preserve the declared file first; otherwise resolve WAV/OGG/MP3 by stem.
        const auto stem = toLower(requested.stem().wstring());
        for (const auto* extension : { L".wav", L".ogg", L".mp3" }) {
            const auto candidate = files.find(stem + extension);
            if (candidate != files.end()) return std::pair{candidate->second, true};
        }
        // Keep normal Unicode filenames ahead of archive-encoding fallbacks.
        const auto archiveName = koreanArchiveFilename(requested.filename().wstring());
        if (!archiveName.empty()) {
            const auto exact = files.find(toLower(archiveName));
            if (exact != files.end()) return std::pair{exact->second, true};
            const auto archiveStem = toLower(std::filesystem::path(archiveName).stem().wstring());
            for (const auto* extension : { L".wav", L".ogg", L".mp3" }) {
                const auto candidate = files.find(archiveStem + extension);
                if (candidate != files.end()) return std::pair{candidate->second, true};
            }
        }
        // Some zero-based GM sample sets omit gm_001 while keeping gm_000 and
        // gm_002. Map that one missing entry to the available first sample so
        // the chart remains playable instead of failing on the missing file.
        if (assetRecovery_ && stem == L"gm_001") {
            bool hasFirst = false, hasSecond = false;
            for (const auto* extension : { L".wav", L".ogg", L".mp3" }) {
                hasFirst |= files.contains(std::wstring(L"gm_000") + extension);
                hasSecond |= files.contains(std::wstring(L"gm_002") + extension);
            }
            if (hasFirst && hasSecond) {
                for (const auto* extension : { L".wav", L".ogg", L".mp3" }) {
                    const auto candidate = files.find(std::wstring(L"gm_000") + extension);
                    if (candidate != files.end()) {
                        result.errors.push_back(L"호환 복구: gm_001 대신 gm_000 음원을 사용합니다.");
                        return std::pair{candidate->second,true};
                    }
                }
            }
        }
        return std::pair{requested, false};
    };
    for (const auto& id : needed) {
        auto found = sampleNames.find(idKey(id));
        if (found == sampleNames.end()) found = sampleNames.find(id);
        if (found == sampleNames.end() && assetRecovery_ && objectBase!=62) {
            loadSharedDefinitions(chartDirectory);
            const auto shared = chartDirectory.definitions.find(toLower(id));
            if (shared != chartDirectory.definitions.end()) {
                for (const auto& donor : shared->second) {
                    if (donor.chartPath == ownPath) continue;
                    found = sampleNames.emplace(toLower(id), donor.value).first;
                    chart.sharedSampleDefinitions = true;
                    break;
                }
            }
        }
        if (found == sampleNames.end()) {
            // An omitted #WAV definition is a silent object in BMS. Keep the
            // note/BGM event and its timing, but do not prepare an audio sample.
            chart.silentSampleIds.push_back(id);
            continue;
        }
        const auto [samplePath, exists] = resolveSamplePath(materialPath(found->second,headers[L"materialswav"],wavFolder));
        const auto absoluteSample = std::filesystem::absolute(samplePath);
        if (exists) chart.samples[id] = absoluteSample;
        else if (std::find(chart.missingSampleFiles.begin(), chart.missingSampleFiles.end(), absoluteSample) == chart.missingSampleFiles.end())
            chart.missingSampleFiles.push_back(absoluteSample);
    }
    // Incomplete sample sets still keep every note and its timing. Missing
    // sounds stay silent instead of blocking all otherwise available audio.
    if (chart.samples.empty() && !chart.missingSampleFiles.empty())
        result.errors.push_back(L"음원 파일을 찾을 수 없습니다: " + chart.missingSampleFiles.front().wstring());

    chart.bgaClips = std::move(bgaClips);
    const std::set<std::wstring> imageExtensions{L".bmp", L".png", L".jpg", L".jpeg", L".gif"};
    const auto resolveImage = [&](const std::wstring& declared, std::initializer_list<const wchar_t*> names) {
        if (!declared.empty()) {
            const auto requested = path.parent_path() / std::filesystem::path(declared);
            if (readableAssetExists(requested)) return std::filesystem::absolute(requested);
            const auto found = chartDirectory.files.find(toLower(requested.filename().wstring()));
            if (found != chartDirectory.files.end()) return std::filesystem::absolute(found->second);
        }
        for (const auto* name : names) {
            for (const auto& candidate : chartDirectory.bgaFiles) {
                if (!imageExtensions.contains(toLower(candidate.extension().wstring()))) continue;
                if (toLower(candidate.stem().wstring()) == name) return std::filesystem::absolute(candidate);
            }
        }
        return std::filesystem::path{};
    };
    chart.titleImage = resolveImage(headers[L"stagefile"], {L"title", L"stagefile"});
    const std::vector<std::wstring> previewExtensions{L".wav", L".ogg", L".mp3", L".flac", L".m4a", L".aac", L".wma", L".opus", L".aiff", L".aif"};
    const auto addPreview = [&](const std::filesystem::path& file) {
        const auto absolute = std::filesystem::absolute(file).lexically_normal();
        if (std::none_of(chart.previewAudioCandidates.begin(), chart.previewAudioCandidates.end(), [&](const auto& item) {
            return toLower(item.wstring()) == toLower(absolute.wstring());
        })) chart.previewAudioCandidates.push_back(absolute);
    };
    auto declaredPreview = trim(headers[L"preview"]);
    if (declaredPreview.size() >= 2 && declaredPreview.front() == L'"' && declaredPreview.back() == L'"')
        declaredPreview = declaredPreview.substr(1, declaredPreview.size() - 2);
    if (!declaredPreview.empty()) {
        const auto requested = (path.parent_path() / declaredPreview).lexically_normal();
        auto& files = directoryData(requested.parent_path()).files;
        if (const auto exact = files.find(toLower(requested.filename().wstring())); exact != files.end()) addPreview(exact->second);
        for (const auto& extension : previewExtensions) {
            const auto found = files.find(toLower(requested.stem().wstring()) + extension);
            if (found != files.end()) addPreview(found->second);
        }
    }
    std::vector<std::filesystem::path> previews;
    const auto previewRank = [](const auto& file) {
        auto stem = toLower(file.stem().wstring());
        while (!stem.empty() && (stem.front() == L'_' || stem.front() == L'-' || stem.front() == L' ')) stem.erase(stem.begin());
        if (stem == L"preview") return 0;
        if (stem.starts_with(L"preview")) return 1;
        if (stem == L"highlight" || stem == L"demo" || stem == L"trailer") return 2;
        return 3;
    };
    for (const auto& [name, file] : chartDirectory.files) {
        const auto extension = toLower(file.extension().wstring());
        if (previewRank(file) < 3 && std::find(previewExtensions.begin(), previewExtensions.end(), extension) != previewExtensions.end()) previews.push_back(file);
    }
    std::sort(previews.begin(), previews.end(), [&](const auto& a, const auto& b) {
        const int rankA = previewRank(a), rankB = previewRank(b);
        return rankA != rankB ? rankA < rankB : toLower(a.filename().wstring()) < toLower(b.filename().wstring());
    });
    for (const auto& file : previews) addPreview(file);
    if (!chart.previewAudioCandidates.empty()) chart.previewAudio = chart.previewAudioCandidates.front();
    chart.finishImage = resolveImage(headers[L"backbmp"], {L"finish", L"result", L"backbmp"});
    if (chart.finishImage.empty()) chart.finishImage = chart.titleImage;
    if (!chart.titleImage.empty()) chart.bitmaps[L"__titlecard"] = chart.titleImage;
    if (!chart.finishImage.empty()) chart.bitmaps[L"__finishcard"] = chart.finishImage;

    for (const auto& [id, name] : bitmapNames) {
        auto bitmapPath=materialPath(name,headers[L"materialsbmp"],L"");
        if(!readableAssetExists(bitmapPath)) {
            for(const auto* ext:{L".png",L".bmp",L".jpg",L".jpeg",L".mp4",L".mpeg",L".mpg",L".avi",L".wmv",L".m1v",L".m2v"}) {
                auto candidate=bitmapPath;candidate.replace_extension(ext);
                if(readableAssetExists(candidate)){bitmapPath=candidate;break;}
            }
        }
        chart.bitmaps[id]=std::filesystem::absolute(bitmapPath);
    }
    std::set<std::wstring> neededImages;
    for(const auto& event:chart.bga)neededImages.insert(idKey(event.imageId));
    if(bitmapNames.contains(L"00"))neededImages.insert(L"00");
    for(const auto& [id,animation]:chart.switchAnimations)for(const auto& frame:animation.frames)neededImages.insert(idKey(frame));
    for(auto id:neededImages) {
        if(chart.bgaClips.contains(id))id=idKey(chart.bgaClips.at(id).bitmapId);
        const auto found=chart.bitmaps.find(id);
        if(found==chart.bitmaps.end() || !readableAssetExists(found->second))result.errors.push_back(L"BGA 파일을 찾을 수 없습니다: "+id);
    }
    if(headers.contains(L"videofile")) chart.fallbackVideo=std::filesystem::absolute(path.parent_path()/unquote(headers[L"videofile"]));

    // A same-basename BGA is the preferred fallback; otherwise pick a stable
    // image/video candidate from the chart folder. Explicit BMS events still
    // render above this fallback.
    const std::set<std::wstring> videoExtensions{ L".mp4", L".m4v", L".mov", L".wmv", L".avi", L".mpeg", L".mpg", L".m1v", L".m2v" };
    auto bgaFiles = chartDirectory.bgaFiles;
    std::erase_if(bgaFiles, [&](const auto& candidate) {
        const auto stem = toLower(candidate.stem().wstring());
        const auto absolute = std::filesystem::absolute(candidate).lexically_normal();
        return imageExtensions.contains(toLower(candidate.extension().wstring())) &&
            (absolute == chart.titleImage || absolute == chart.finishImage || stem == L"title" || stem == L"stagefile" ||
                stem == L"finish" || stem == L"result" || stem == L"backbmp");
    });
    if (!bgaFiles.empty() && chart.fallbackVideo.empty()) {
        const auto matching = std::find_if(bgaFiles.begin(), bgaFiles.end(), [&](const auto& candidate) {
            return _wcsicmp(candidate.stem().c_str(), path.stem().c_str()) == 0;
        });
        auto fallbackIt = matching;
        if (fallbackIt == bgaFiles.end()) {
            const auto tagged = std::find_if(bgaFiles.begin(), bgaFiles.end(), [](const auto& candidate) {
                return toLower(candidate.stem().wstring()).find(L"bga") != std::wstring::npos;
            });
            if (tagged != bgaFiles.end()) {
                fallbackIt = tagged;
                const auto rank = [](const auto& file) {
                    const auto ext = toLower(file.extension().wstring());
                    if (ext == L".mp4") return 0;
                    if (ext == L".mpeg") return 1;
                    if (ext == L".mpg") return 2;
                    if (ext == L".m4v") return 3;
                    if (ext == L".mov") return 4;
                    if (ext == L".wmv") return 5;
                    if (ext == L".avi") return 6;
                    return 10;
                };
                for (auto it = std::next(tagged); it != bgaFiles.end(); ++it) {
                    if (toLower(it->stem().wstring()).find(L"bga") == std::wstring::npos) continue;
                    const int rankNext = rank(*it), rankBest = rank(*fallbackIt);
                    if (rankNext < rankBest || (rankNext == rankBest && toLower(it->filename().wstring()) < toLower(fallbackIt->filename().wstring()))) fallbackIt = it;
                }
            } else {
                const auto firstVideo = std::find_if(bgaFiles.begin(), bgaFiles.end(), [&](const auto& file) {
                    return videoExtensions.contains(toLower(file.extension().wstring()));
                });
                fallbackIt = firstVideo == bgaFiles.end() ? bgaFiles.begin() : firstVideo;
            }
        }
        const auto& fallback = *fallbackIt;
        const auto extension = toLower(fallback.extension().wstring());
        if (videoExtensions.contains(extension)) chart.fallbackVideo = std::filesystem::absolute(fallback);
        else {
            chart.bitmaps[L"__fallback"] = std::filesystem::absolute(fallback);
            chart.bga.insert(chart.bga.begin(), { 0.0, BgaLayer::Base, L"__fallback" });
        }
    }

    if (!chart.notes.empty()) {
        const auto last = std::max_element(chart.notes.begin(), chart.notes.end(), [](const Note& a, const Note& b) { return a.timeSeconds + a.lengthSeconds < b.timeSeconds + b.lengthSeconds; });
        chart.durationSeconds = std::max(chart.durationSeconds, last->timeSeconds + last->lengthSeconds);
    }
    std::sort(chart.notes.begin(), chart.notes.end(), [](const Note& a, const Note& b) { return a.timeSeconds < b.timeSeconds; });
    if (!chart.previewStartDefined) chart.previewStartSeconds = automaticPreviewStart(chart);
    // Unsupported key modes, charts with no usable notes, and unsupported long
    // note encodings remain blocked. Other parse/sample problems can leave a
    // usable chart, so preserve its notes and let the UI identify it as partial.
    if (modeError.empty() && unsupportedLn)
        modeError = L"LNTYPE은 1 또는 2여야 합니다.";
    const bool hasRunnableNotes = !chart.notes.empty();
    chart.playable = modeError.empty() && hasRunnableNotes;
    chart.partialLoad = chart.playable && (!result.errors.empty() || malformed ||
        !chart.missingSampleFiles.empty() || !chart.silentSampleIds.empty() || chart.sharedSampleDefinitions);
    chart.partialLoadReason.clear();
    if (chart.partialLoad) {
        std::vector<std::wstring> reasons;
        if(chart.sharedSampleDefinitions)reasons.push_back(L"다른 채보의 WAV 선언으로 호환 복구했습니다.");
        if (!chart.missingSampleFiles.empty()) {
            std::wstring reason = L"누락된 음원 파일: " + chart.missingSampleFiles.front().filename().wstring();
            if (chart.missingSampleFiles.size() > 1)
                reason += L" 외 " + std::to_wstring(chart.missingSampleFiles.size() - 1) + L"개";
            reasons.push_back(std::move(reason));
        }
        if (!chart.silentSampleIds.empty())
            reasons.push_back(L"정의되지 않은 음원 채널 " + std::to_wstring(chart.silentSampleIds.size()) + L"개");
        for (const auto& error : result.errors) {
            if (!chart.missingSampleFiles.empty() && error.starts_with(L"음원 파일을 찾을 수 없습니다:")) continue;
            if (std::find(reasons.begin(), reasons.end(), error) == reasons.end()) reasons.push_back(error);
            if (reasons.size() >= 3) break;
        }
        if (reasons.empty()) reasons.push_back(L"일부 채보 데이터를 읽지 못했습니다.");
        for (const auto& reason : reasons) {
            if (!chart.partialLoadReason.empty()) chart.partialLoadReason += L" · ";
            chart.partialLoadReason += reason;
        }
    }
    chart.blockReason = chart.playable ? L"" : modeError.empty() ? L"플레이 가능한 노트가 없습니다." : modeError;
    if(chart.extendedRank>0 || !chart.judgeRanks.empty() || chart.halfJudgement)chart.judgementSpec=L"LEGACY-EXTENDED";
    return result;
}

} // namespace bms
