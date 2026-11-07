#pragma once

// UI-thread-owned lifecycle. Decoders publish completion through WM_LOAD_FINISHED;
// they never mutate this state. Library indexing is intentionally independent.
namespace bms {
enum class PlaybackLoadPhase { Idle, LoadingEssential, PreparingPlay, ReadyToStart, Playing, LoadFailed, Cancelled };
class PlaybackLoadState {
    PlaybackLoadPhase phase_ = PlaybackLoadPhase::Idle;
    bool loaded_{};
public:
    PlaybackLoadPhase phase() const noexcept { return phase_; }
    void begin() noexcept { phase_=PlaybackLoadPhase::LoadingEssential;loaded_=false; }
    void resourcesLoaded() noexcept { if(phase_==PlaybackLoadPhase::LoadingEssential)loaded_=true; }
    bool awaitingPreparation() const noexcept { return phase_==PlaybackLoadPhase::LoadingEssential&&loaded_; }
    bool beginPreparation() noexcept { if(!awaitingPreparation())return false;phase_=PlaybackLoadPhase::PreparingPlay;return true; }
    bool ready() noexcept { if(phase_!=PlaybackLoadPhase::PreparingPlay)return false;phase_=PlaybackLoadPhase::ReadyToStart;return true; }
    bool canStart() const noexcept { return phase_==PlaybackLoadPhase::ReadyToStart; }
    bool started() noexcept { if(!canStart())return false;phase_=PlaybackLoadPhase::Playing;return true; }
    bool preparing() const noexcept { return phase_==PlaybackLoadPhase::PreparingPlay||canStart(); }
    void fail() noexcept { phase_=PlaybackLoadPhase::LoadFailed;loaded_=false; }
    void cancel() noexcept { phase_=PlaybackLoadPhase::Cancelled;loaded_=false; }
    void reset() noexcept { phase_=PlaybackLoadPhase::Idle;loaded_=false; }
};
}
