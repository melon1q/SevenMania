#pragma once
#include "AudioEngine.h"
#include <condition_variable>
#include <optional>
#include <set>
#include <deque>
#include <mfapi.h>

// Sequential library prefetch and foreground requests share one decoder worker.
class SongPreview {
    friend struct SongPreviewTestAccess;
    struct Prepared {
        std::unique_ptr<AudioEngine> audio;
        std::shared_ptr<std::atomic_bool> cancellation;
        double start{}, length{}, chartOffset{};
        uint64_t age{};
    };
    std::mutex mutex_;
    std::condition_variable changed_;
    std::thread worker_;
    std::optional<bms::Chart> requested_;
    std::deque<bms::Chart> warmup_;
    std::unordered_map<std::wstring, Prepared> cache_;
    std::shared_ptr<std::atomic_bool> warmCancelled_ = std::make_shared<std::atomic_bool>(false);
    std::wstring desiredKey_, activeKey_, inFlightKey_;
    uint64_t age_{};
    bool warmupPaused_{};
    std::shared_ptr<std::atomic_bool> cancelled_;
    std::shared_ptr<std::atomic_bool> inFlightCancellation_;
    std::optional<Prepared> active_;
    std::wstring error_;
    bool stopping_{};
    void store(const std::wstring& key, Prepared result) {
        result.age = ++age_;
        cache_.insert_or_assign(key, std::move(result));
        size_t bytes = 0;
        for (const auto& [name, item] : cache_) bytes += item.audio->decodedBytes();
        while (!cache_.empty() && (cache_.size() > 64 || bytes > 256ull * 1024 * 1024)) {
            auto oldest = cache_.end();
            for (auto it = cache_.begin(); it != cache_.end(); ++it) {
                if (it->first == desiredKey_) continue;
                if (oldest == cache_.end() || it->second.age < oldest->second.age) oldest = it;
            }
            if (oldest == cache_.end()) break;
            bytes -= oldest->second.audio->decodedBytes(); cache_.erase(oldest);
        }
    }
    void workerLoop() {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(com)) return;
        for (;;) {
            bms::Chart chart;
            std::shared_ptr<std::atomic_bool> cancellation;
            {
                std::unique_lock lock(mutex_);
                changed_.wait(lock, [&] { return stopping_ || requested_.has_value() || (!warmupPaused_ && !warmup_.empty()); });
                if (stopping_) break;
                if (requested_) { chart = std::move(*requested_); requested_.reset(); cancellation = cancelled_; }
                else { chart = std::move(warmup_.front()); warmup_.pop_front(); cancellation = warmCancelled_; }
                if (cache_.contains(chart.path.wstring()) || (!activeKey_.empty() && chart.path.wstring() == activeKey_)) continue;
                inFlightKey_ = chart.path.wstring();
                inFlightCancellation_ = cancellation;
            }
            std::wstring error;
            try {
                if(chart.catalogOnly)chart=bms::BmsParser().parse(chart.path).chart;
                Prepared result; result.audio = std::make_unique<AudioEngine>(); result.cancellation = cancellation;
                result.start = chart.previewStartSeconds; result.length = chart.previewDurationSeconds;
                auto candidates = chart.previewAudioCandidates;
                if (!chart.previewAudio.empty() && std::find(candidates.begin(), candidates.end(), chart.previewAudio) == candidates.end())
                    candidates.insert(candidates.begin(), chart.previewAudio);
                bool prepared = false;
                for (const auto& file : candidates) {
                    if (cancellation->load()) break;
                    bms::Chart previewChart;
                    previewChart.samples[L"preview"] = file;
                    previewChart.bgm.push_back({0, L"preview"});
                    if (result.audio->prepare(previewChart, error, cancellation.get())) {
                        result.chartOffset = chart.previewStartSeconds;
                        result.start = 0; result.length = result.audio->durationSeconds();
                        prepared = true; break;
                    }
                }
                if (!prepared && !cancellation->load()) {
                    for (const auto& note : chart.notes) {
                        if(!note.sampleId.empty())chart.bgm.push_back({note.timeSeconds,note.sampleId});
                        if(note.longNote() && !note.endSampleId.empty())chart.bgm.push_back({note.timeSeconds+note.lengthSeconds,note.endSampleId});
                    }
                    // Only decode samples which can contribute before the preview ends.
                    std::set<std::wstring> used;
                    std::erase_if(chart.bgm, [&](const bms::BgmEvent& event) {
                        if (event.timeSeconds >= result.start + result.length) return true;
                        used.insert(event.sampleId); return false;
                    });
                    if(!chart.bmson)std::erase_if(chart.samples,[&](const auto& entry){return !used.contains(entry.first);});
                    prepared = result.audio->prepare(chart, error, cancellation.get(), true);
                }
                if (prepared && !cancellation->load()) {
                    if (result.start >= result.audio->durationSeconds()) result.start = 0;
                    result.length = std::min(result.length, result.audio->durationSeconds() - result.start);
                    std::lock_guard lock(mutex_);
                    if (!stopping_ && !cancellation->load()) store(chart.path.wstring(), std::move(result));
                }
            } catch (...) { error = L"미리보기 음원을 준비하지 못했습니다."; }
            { std::lock_guard lock(mutex_);
              if (!error.empty() && !cancellation->load() && chart.path.wstring() == desiredKey_) error_ = error;
              inFlightKey_.clear(); inFlightCancellation_.reset(); }
        }
        CoUninitialize();
    }
public:
    static constexpr uint32_t fadeDurationMs = 150;
    ~SongPreview() { shutdown(); }
    bool playing() const { return active_ && active_->audio->isRunning(); }
    std::wstring lastError() { std::lock_guard lock(mutex_); return error_; }
    double chartTime(double fallback) const { return playing() ? active_->chartOffset + active_->audio->positionSeconds() : fallback; }
    bool cached(const std::filesystem::path& path) { std::lock_guard lock(mutex_); return cache_.contains(path.wstring()) || (!activeKey_.empty() && activeKey_ == path.wstring()); }
    void prewarm(std::vector<bms::Chart> charts) {
        clearCache();
        { std::lock_guard lock(mutex_); for (auto& chart : charts) warmup_.push_back(std::move(chart)); warmupPaused_ = false; }
        if (!worker_.joinable()) worker_ = std::thread(&SongPreview::workerLoop, this);
        changed_.notify_one();
    }
    void pauseWarmup(bool paused) { { std::lock_guard lock(mutex_); warmupPaused_ = paused; } changed_.notify_one(); }
    void clearCache() {
        reset();
        std::lock_guard lock(mutex_); *warmCancelled_ = true;
        warmCancelled_ = std::make_shared<std::atomic_bool>(false); warmup_.clear(); cache_.clear();
    }
    void reset() {
        if (active_) active_->audio->stop();
        { std::lock_guard lock(mutex_); if (cancelled_) *cancelled_ = true; requested_.reset(); error_.clear();
          desiredKey_.clear();
          if (active_) store(activeKey_, std::move(*active_));
          activeKey_.clear(); }
        active_.reset();
    }
    void select(const bms::Chart& chart) {
        reset();
        { std::lock_guard lock(mutex_); desiredKey_ = chart.path.wstring(); cancelled_ = std::make_shared<std::atomic_bool>(false);
          if (!cache_.contains(desiredKey_) && (inFlightKey_ != desiredKey_ || !inFlightCancellation_ || inFlightCancellation_->load())) requested_ = chart; }
        if (!worker_.joinable()) worker_ = std::thread(&SongPreview::workerLoop, this);
        changed_.notify_one();
    }
    void tick(int master, int volume, int buffer, uint32_t fadeInMs = 600) {
        std::optional<Prepared> ready;
        { std::lock_guard lock(mutex_); if (!active_) {
            auto found = cache_.find(desiredKey_);
            if (found != cache_.end()) { ready = std::move(found->second); cache_.erase(found); activeKey_ = desiredKey_; }
        } }
        if (ready) {
            active_ = std::move(ready);
            active_->audio->setVolumes(master, volume, volume, 0);
            active_->audio->setMusicFadeIn(fadeInMs);
            active_->audio->setBufferFrames(buffer);
            std::wstring error;
            if (active_->length <= 0 || !active_->audio->startAt(active_->start, error)) {
                active_.reset(); std::lock_guard lock(mutex_);
                activeKey_.clear();
                error_ = error.empty() ? L"재생할 미리보기 음원이 없습니다." : error;
            }
        }
        if (!active_) return;
        active_->audio->setVolumes(master, volume, volume, 0);
        if (active_->audio->positionSeconds() >= active_->start + active_->length) {
            std::wstring error;
            active_->audio->setBufferFrames(buffer);
            if (!active_->audio->startAt(active_->start, error)) active_.reset();
        }
    }
    void shutdown() {
        reset();
        { std::lock_guard lock(mutex_); stopping_ = true; *warmCancelled_ = true; cache_.clear(); warmup_.clear(); }
        changed_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
};
