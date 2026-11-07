#pragma once

#include "BmsParser.h"
#include "LatestEffectRequest.h"

#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <set>
#include <windows.h>
#include <mmsystem.h>
#include <audioclient.h>
#include <chrono>

class AudioEngine {
public:
    AudioEngine() = default;
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool prepare(const bms::Chart& chart, std::wstring& error, const std::atomic_bool* cancelled = nullptr,
        bool tolerateDecodeFailures = false, size_t* decodeFailureCount = nullptr);
    bool loadEffect(const std::wstring& effectId, const std::filesystem::path& path, std::wstring& error);
    void loadDiagnosticBeep();
    bool start(std::wstring& error);
    bool startAt(double seconds, std::wstring& error, double playbackRate = 1.0);
    bool preparePlaybackRate(double rate, std::wstring& error, const std::atomic_bool* cancelled = nullptr);
    double playbackRate() const noexcept { return playbackRate_; }
    bool startEffectsOnly(std::wstring& error);
    bool pause() noexcept;
    bool resume() noexcept;
    bool isPaused() const noexcept { return paused_.load(); }
    void stop();
    void releaseSong();
    void setWasapiExclusiveEnabled(bool enabled) noexcept { exclusiveOutputRequested_.store(enabled, std::memory_order_relaxed); }
    bool wasapiExclusiveEnabled() const noexcept { return exclusiveOutputRequested_.load(std::memory_order_relaxed); }
    bool usingWasapiExclusive() const noexcept { return exclusiveModeActive_.load(std::memory_order_relaxed); }
    bool fellBackFromWasapiExclusive() const noexcept { return exclusiveModeFallback_.load(std::memory_order_relaxed); }
    void trigger(const std::wstring& sampleId);
    bool triggerEffect(const std::wstring& effectId);
    void setVolumes(int master, int music, int keys, int effects) noexcept;
    void setMusicFadeIn(uint32_t durationMs) noexcept;
    bool setBufferFrames(uint32_t frames) noexcept;
    uint32_t bufferFrames() const noexcept { return requestedBufferFrames_.load(); }
    uint32_t deviceBufferFrames() const noexcept { return deviceBufferFrames_; }
    uint32_t sampleRate() const noexcept { return sampleRate_; }
    double positionSeconds() const;
    double lastRawPositionSeconds() const { return lastRawPositionSeconds_; }
    double durationSeconds() const noexcept { return durationSeconds_; }
    size_t decodedBytes() const {
        size_t bytes = 0;
        std::vector<const AudioData*> counted;
        for (const auto* bank : {&samples_, &playbackSamples_})
            for (const auto& [id, sample] : *bank) {
                if (std::find(counted.begin(), counted.end(), sample.get()) != counted.end()) continue;
                counted.push_back(sample.get()); bytes += sample->pcm.size() * sizeof(int16_t);
            }
        return bytes;
    }
    bool isRunning() const noexcept { return running_.load(); }

private:
    friend struct AudioEngineTestAccess;
#ifdef BMS_PERF_DIAGNOSTICS
    std::atomic<uint64_t> emptyPaddingEvents_{}, renderFailures_{};
#endif
    const std::atomic_bool* prepareCancelled_{};
    struct AudioData { std::vector<int16_t> pcm; };
    struct BgmFrame { uint64_t frame{}; std::wstring id; };
    struct Voice { const AudioData* data{}; uint64_t startFrame{}; bool keySound{}; bool effectSound{}; };

    uint32_t sampleRate_ = 44100;
    bool decodeRateReady_{};
    bool outputFloat_{};
    uint32_t outputChannels_ = 2;
    static constexpr uint32_t channels_ = 2;
    std::atomic<uint32_t> requestedBufferFrames_{256};
    uint32_t framesPerBuffer_ = 256;

    bool decodeFile(const std::filesystem::path& path, AudioData& data, std::wstring& error) const;
    void configureDecodeRate();
    void workerLoop();
    void fillBuffer(int16_t* target, uint32_t frames);
    bool renderAvailable();
    bool startOutput(std::wstring& error, bool effectsOnly, double startSeconds = 0, double playbackRate = 1.0);
    double playbackRate_ = 1.0;
    uint64_t transportStartFrame_{};

    std::unordered_map<std::wstring, std::shared_ptr<AudioData>> samples_;
    std::unordered_map<std::wstring, std::shared_ptr<AudioData>> playbackSamples_;
    double preparedRate_ = 1.0;
    std::unordered_map<std::wstring, std::shared_ptr<AudioData>> effects_;
    std::vector<BgmFrame> bgm_;
    double durationSeconds_{};
    size_t nextBgm_{};
    std::vector<Voice> voices_;
    std::mutex triggerMutex_;
    std::vector<std::wstring> pendingTriggers_;
    LatestEffectRequest pendingEffect_;
    bool effectsOnly_{};
    bool slicedChart_{};
    std::set<std::wstring> triggeredSlices_;
    std::atomic_bool running_{ false };
    std::atomic_bool paused_{ false };
    std::atomic_bool exclusiveOutputRequested_{ false };
    std::atomic_bool exclusiveModeActive_{ false }, exclusiveModeFallback_{ false };
    mutable std::mutex deviceMutex_;
    double pausedPositionSeconds_{};
    std::chrono::steady_clock::time_point pausedAt_{};
    std::atomic<uint64_t> nextFrame_{ 0 };
    std::atomic<uint64_t> musicFadeDurationFrames_{};
    uint64_t musicFadeStartFrame_{}, musicFadeEndFrame_{};
    std::atomic<float> masterVolume_{ 1.0f }, musicVolume_{ 1.0f }, keyVolume_{ 1.0f }, effectVolume_{ 1.0f };
    IAudioClient* audioClient_{};
    IAudioRenderClient* renderClient_{};
    IAudioClock* audioClock_{};
    uint64_t clockFrequency_{};
    uint64_t qpcFrequency_{};
    uint32_t deviceBufferFrames_{};
    std::chrono::steady_clock::time_point playbackEpoch_{};
    HANDLE audioEvent_{};
    HANDLE stopEvent_{};
    std::thread worker_;
    std::vector<float> mixBuffer_;
    mutable double lastRawPositionSeconds_{};
    mutable double lastPositionSeconds_{};
    mutable bool useWallClock_{};
};
