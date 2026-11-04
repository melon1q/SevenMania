#pragma once
#include <array>
#include "JsonValue.h"
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <variant>

namespace sevenmania::skin {
enum class ScreenKind { Select, Play, Result };
enum class ElementKind { Image, Text, Number, Sprite, Box };
enum class ResourceKind { Png, Jpeg, Font, AnimationData };
enum class ErrorCode {
    Io, InvalidZip, UnsafePath, LimitExceeded, InvalidJson, InvalidManifest,
    UnsupportedVersion, MissingFile, UnsupportedResource, InvalidImage,
    InvalidScreen, DuplicateId, NotFound, InvalidModel, AllocationFailed
};
struct SkinError { ErrorCode code{}; std::string path, message; };
struct Resolution { std::uint32_t width{}, height{}; };
struct ResourcePath { std::string path; bool required = true; };
struct SkinManifest {
    std::string format, id, name, author, skinVersion, description;
    std::uint32_t formatVersion{};
    Resolution resolution{1920,1080};
    std::vector<ScreenKind> supportedScreens;
    std::map<std::string, ResourcePath> resourcePaths;
};
using Bytes = std::vector<std::uint8_t>;
struct SkinResource {
    std::string id, path;
    ResourceKind kind{};
    std::shared_ptr<const Bytes> bytes;
    std::uint32_t width{}, height{}; // metadata only; no texture or registered font
};
struct SpriteGrid { std::uint32_t frameWidth{}, frameHeight{}, columns{}, rows{}; };
struct SkinRect { double x{}, y{}, width{}, height{}; };
struct SkinTint { std::uint8_t r=255, g=255, b=255, a=255; };
using SkinValue = std::variant<std::monostate,std::string,std::int64_t,double,bool>;
enum class TextAlign { Left, Center, Right };
enum class TextVerticalAlign { Top, Center, Bottom };
struct SkinTextStyle {
    std::string font = "Malgun Gothic"; // system/private font family, no filesystem access
    unsigned fontSize = 32;
    TextAlign align = TextAlign::Left;
    TextVerticalAlign verticalAlign = TextVerticalAlign::Top;
    bool clip = true;
};
struct SkinNumberFormat { unsigned decimals{}, minDigits{}; bool grouping{}; };
// Optional combined HUD style. Resource IDs resolve only inside the package.
struct SkinHudStyle {
    std::string judgementMode="font",comboMode="font";
    std::array<std::string,6> judgements; // PGREAT, GREAT, GOOD, BAD, POOR, existing MINE display
    std::array<std::string,10> digits;
    double judgementWidth=320,judgementHeight=80,digitWidth=48,digitHeight=80,spacing=0,gap=16,displayMs=850;
    bool images() const noexcept {return judgementMode=="image"||comboMode=="image";}
};
struct SkinElement {
    std::string id, resource, text, binding;
    std::string boxMode = "solid";
    ElementKind kind{};
    double x{}, y{}, width{}, height{}, opacity = 1;
    double rotation{}; // clockwise degrees about destination centre
    SkinTint tint;
    bool visible = true;
    std::optional<SkinRect> source; // source image pixels
    std::optional<SkinRect> clip;   // unrotated screen design coordinates
    SkinTextStyle textStyle;
    SkinNumberFormat numberFormat;
    SkinHudStyle hud;
    SkinValue defaultValue;
    std::int32_t z{};
    std::optional<SpriteGrid> sprite;
    // Reserved declarative data only. No callbacks, scripts, or animation executor.
    bms::JsonValue extensions;
};
struct SkinScreen {
    ScreenKind kind{};
    Resolution resolution;
    std::vector<SkinElement> elements; // stable layer order after prepare()
    bms::JsonValue extensions;
};
// Container-neutral IR: a future LR2 converter constructs this same model.
struct SkinPackage {
    SkinManifest manifest;
    std::map<std::string, SkinResource> resources;
    std::map<ScreenKind, SkinScreen> screens;
};
struct Limits {
    std::uint64_t archiveBytes = 64ull << 20, totalBytes = 128ull << 20;
    std::uint32_t fileBytes = 16u << 20, jsonBytes = 1u << 20;
    std::uint32_t files = 1024, compressionRatio = 200;
    std::uint32_t imageDimension = 8192, imagePixels = 16u << 20;
    std::uint64_t totalDecodedImageBytes = 256ull << 20;
    std::uint32_t jsonDepth = 64, jsonNodes = 100000, elements = 4096;
};
struct LoadResult {
    std::shared_ptr<const SkinPackage> package;
    std::optional<SkinError> error;
    explicit operator bool() const noexcept { return package != nullptr && !error; }
};
const char* screenName(ScreenKind kind) noexcept;
class SkinLoader {
public:
    explicit SkinLoader(Limits limits = {}) : limits_(limits) {}
    LoadResult load(const std::filesystem::path& file) const noexcept;
    // Same validator for native packages and future LR2 converter output.
    LoadResult prepare(SkinPackage model) const noexcept;
private:
    Limits limits_;
};
// One owner thread; callers keep returned shared_ptrs during transitions.
class SkinManager {
public:
    explicit SkinManager(Limits limits = {}) : loader_(limits) {}
    std::optional<SkinError> registerFile(const std::filesystem::path& file);
    std::optional<SkinError> registerModel(SkinPackage model);
    std::optional<SkinError> select(ScreenKind screen, const std::string& id);
    std::shared_ptr<const SkinPackage> selected(ScreenKind screen) const;
    std::shared_ptr<const SkinPackage> find(const std::string& id) const;
    bool unregisterSkin(const std::string& id);
    std::size_t size() const noexcept { return packages_.size(); }
private:
    std::optional<SkinError> add(LoadResult loaded);
    SkinLoader loader_;
    std::map<std::string, std::shared_ptr<const SkinPackage>> packages_;
    std::map<ScreenKind, std::string> selections_;
};
}
