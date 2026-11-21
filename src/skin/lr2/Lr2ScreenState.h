#pragma once
// Clocks and conditions of the LR2 decide and result screens that depend on game events, kept apart
// from drawing so they can be tested. IDs follow beatoraja's SkinProperty / BooleanPropertyFactory
// (exch-bms2/beatoraja), which reproduces LR2's numbering; see docs/Lr2SkinCompatibility.md.
#include <algorithm>
#include <map>
#include <optional>

namespace sevenmania::skin::lr2 {

// LR2 rank of an EX score out of notes*2, in ninths: 1 F (below 2/9) up to 8 AAA (8/9 and above);
// 0 for no score or a score that does not fit the note count.
constexpr int exScoreRank(long long exScore,long long notes) noexcept {
    if(exScore<=0||notes<=0||exScore>2*notes)return 0;
    const long long tier=9*exScore/(2*notes);
    return static_cast<int>(tier<1?1:tier>8?8:tier);
}

// Result #STARTINPUT: input (ms before any key counts), rank (when the score update is shown,
// "ランク表示後") and update (ms after the update before a key leaves, "更新表示後").
struct ResultInput {int input{},rank{},update{};};

// Scene time at which timer 152 (TIMER_RESULT_UPDATESCORE) starts. beatoraja starts it at once when the
// rank time is 0 and otherwise on the first key; SevenMania also starts it by itself once the rank time
// has passed, so a player who presses nothing still sees the update. keyAt is the scene time of that
// first key, or negative.
constexpr double resultUpdateStart(const ResultInput& input,double keyAt) noexcept {
    if(input.rank<=0)return 0;
    return keyAt>=0?std::min<double>(input.rank,keyAt):double(input.rank);
}
constexpr double resultUpdateAge(const ResultInput& input,double elapsed,double keyAt) noexcept {
    const double start=resultUpdateStart(input,keyAt);
    return elapsed>=start?elapsed-start:-1;
}
enum class ResultKey {Ignored,ShowUpdate,Leave};
// What a key pressed at scene time elapsed does: nothing before the input time, then the first key shows
// the update early, and once the update has been up for its time a key starts the closing fade.
constexpr ResultKey resultKey(const ResultInput& input,double elapsed,double keyAt) noexcept {
    if(elapsed<input.input)return ResultKey::Ignored;
    const double start=resultUpdateStart(input,keyAt);
    if(elapsed<start)return ResultKey::ShowUpdate;
    return elapsed>=start+input.update?ResultKey::Leave:ResultKey::Ignored;
}

// Decide screen: #SCENETIME ends it, then timer 2 (fade) runs for #FADEOUT. A key after #SKIP
// (and #STARTINPUT) starts timer 3 instead and the screen ends #FADEOUT later.
// Total length with the fade, or 0 when the skin gives no scene time.
constexpr int decideDuration(int scene,int fadeout) noexcept {return scene>0?scene+std::max(0,fadeout):0;}
constexpr double decideFadeoutAge(int scene,double elapsed,double skipAt) noexcept {
    return skipAt<0&&scene>0&&elapsed>=scene?elapsed-scene:-1;
}
constexpr double decideSkipAge(double elapsed,double skipAt) noexcept {return skipAt>=0&&elapsed>=skipAt?elapsed-skipAt:-1;}
// Whether a key at elapsed may skip: past #SKIP and #STARTINPUT, before the scene ends, not yet skipped.
constexpr bool decideCanSkip(int skip,int input,int scene,double elapsed,double skipAt) noexcept {
    return skip>0&&skipAt<0&&elapsed>=std::max(skip,input)&&(scene<=0||elapsed<scene);
}

// The play a result screen shows and the record as it was before it.
struct ResultState {
    bool cleared{};
    int exScore{},noteCount{},maxCombo{},missCount{};
    // False in practice, diagnostic and autoplay runs, whose plays are not saved: nothing counts as updated.
    bool recordSaved=true;
    // The play was autoplay (options 32 off / 33 on).
    bool autoplay{};
    // The chart had been played before. The best values may still be unknown for an old record.
    bool played{};
    std::optional<int> bestExScore,bestMaxCombo,bestMissCount;
};

// Conditions the result screen decides once when it loads.
inline std::map<int,bool> resultOptions(const ResultState& state) {
    std::map<int,bool> options{
        {90,state.cleared},{91,!state.cleared},
        {80,false},{81,true},{32,!state.autoplay},{33,state.autoplay},
        // No internet ranking (50 offline, 51 online), no battle or double play (11, 13), no result flip
        // (350 off, 351 on), no rival to win, lose or draw against (352-354), no dan trial (333) and no IR
        // rank (334).
        {50,true},{51,false},{11,false},{13,false},{350,true},{351,false},
        {352,false},{353,false},{354,false},{333,false},{334,false},
    };
    const int rank=exScoreRank(state.exScore,state.noteCount); // 0, then 1 F .. 8 AAA.
    for(int i=0;i<8;++i) {
        options[300+i]=rank==8-i;          // This play's rank, AAA .. F.
        options[340+i]=rank==8-i||(rank==0&&i==7); // The rank now, F also for no score.
        options[310+i]=false;              // 2P side.
        options[320+i]=false;              // Best rank before this play, set below.
    }
    options[308]=rank==0; // No score.
    options[318]=true;    // 2P: no score.
    const std::optional<int> previousEx=state.played?state.bestExScore:std::optional(0);
    if(const int best=exScoreRank(previousEx.value_or(0),state.noteCount);best>0)options[328-best]=true;
    // New records (beatoraja update_score, update_maxcombo, update_misscount, update_scorerank). The target
    // is MY BEST, so beating the target (336) is beating the best score.
    const bool scoreUpdate=state.recordSaved&&previousEx&&state.exScore>*previousEx;
    options[330]=scoreUpdate;
    options[336]=scoreUpdate;
    options[331]=state.recordSaved&&(!state.played||state.bestMaxCombo)&&state.maxCombo>state.bestMaxCombo.value_or(0);
    options[332]=state.recordSaved&&(!state.played||(state.bestMissCount&&state.missCount<*state.bestMissCount));
    options[335]=state.recordSaved&&previousEx&&rank>exScoreRank(*previousEx,state.noteCount);
    return options;
}
}
