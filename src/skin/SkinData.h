#pragma once
#include "SkinPackage.h"
#include <array>
#include <bitset>
#include <span>
#include <string_view>

namespace sevenmania::skin {
enum class BindingType { String, Integer, Real, Boolean };
enum class BindingId : std::uint8_t {
    SongTitle, SongArtist, SongGenre, SongBpm, ChartLevel, ChartLevelText, ChartDifficultyColor,
    PlayCombo, PlayScore, PlayGauge, PlayJudgement, PlayJudgementCombo, PlayPaused,
    ResultScore, ResultRank, ResultClearType, ResultMaxCombo, ResultCleared, PlayGaugeMode,
    SelectSpeedText, SelectJudgementText, SelectGaugeMode,
    PlayJudgementGrade, PlayJudgementAge,
    PlayJudgementProfile,
    Count, Invalid=255
};
struct BindingDefinition { BindingId id; std::string_view name; BindingType type; unsigned screens; };
class SkinBindingRegistry {
public:
    static std::span<const BindingDefinition> definitions() noexcept;
    static const BindingDefinition* find(std::string_view name) noexcept;
    static const BindingDefinition* find(BindingId id) noexcept;
    static bool allowed(BindingId id,ScreenKind screen) noexcept;
};
class SkinDataSnapshot {
public:
    ScreenKind screen() const noexcept {return screen_;}
    bool active() const noexcept {return active_;}
    std::uint64_t revision() const noexcept {return revision_;}
    const SkinValue& value(BindingId id) const noexcept;
private:
    friend class SkinDataProvider;
    ScreenKind screen_{};
    bool active_{};
    std::uint64_t revision_{};
    std::array<SkinValue,static_cast<std::size_t>(BindingId::Count)> values_;
};
// Only the host owns this writer. Skins receive a const snapshot, never engine pointers.
// begin/set/end and renderer update run on the same UI owner thread.
class SkinDataProvider {
public:
    void beginFrame(ScreenKind screen);
    bool set(BindingId id,const SkinValue& value);
    bool setWide(BindingId id,std::wstring_view value);
    void endFrame();
    void clear();
    const SkinDataSnapshot& snapshot() const noexcept {return snapshot_;}
private:
    SkinDataSnapshot snapshot_;
    std::bitset<static_cast<std::size_t>(BindingId::Count)> seen_;
    std::bitset<static_cast<std::size_t>(BindingId::Count)> wideValid_;
    std::array<std::wstring,static_cast<std::size_t>(BindingId::Count)> wideCache_;
};
struct BindingDiagnostic { std::string element,binding,message; };
struct CompiledBinding { BindingId id=BindingId::Invalid; bool declared{}; ScreenKind screen{}; };
class SkinBindingResolver {
public:
    static CompiledBinding compile(const SkinElement& element,ScreenKind screen,
        std::vector<BindingDiagnostic>& diagnostics);
    // Reuses output capacity. No name lookup or JSON work after compile().
    static void resolve(const SkinElement& element,CompiledBinding binding,
        const SkinDataSnapshot* snapshot,std::string& output);
};
}
