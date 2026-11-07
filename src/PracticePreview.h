#pragma once

#include "AudioEngine.h"
#include <mfapi.h>

// Decode once off the UI thread, then reuse the complete chart for every range.
class PracticePreview {
    std::atomic_bool cancelled_{false};
    std::atomic_bool ready_{false};
    std::thread worker_;
    std::thread rateWorker_;
    std::atomic_bool rateCancelled_{false}, rateDone_{false};
    bool rateOkay_{};
    std::wstring rateError_;
    std::unique_ptr<AudioEngine> audio_;
    std::wstring error_;
    double start_{}, end_{}, rate_{1};
    bool requested_{}, pending_{};
    std::chrono::steady_clock::time_point changed_{};
public:
    ~PracticePreview() { reset(); }
    void reset() {
        stop();
        cancelled_ = true;
        if (worker_.joinable()) worker_.join();
        audio_.reset(); ready_ = false; requested_ = pending_ = false; error_.clear();
    }
    void prepare(bms::Chart chart) {
        reset(); cancelled_ = false;
        worker_ = std::thread([this, chart = std::move(chart)]() mutable {
            const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            auto audio = std::make_unique<AudioEngine>();
            for (const auto& note : chart.notes)
                if (!note.sampleId.empty()) chart.bgm.push_back({note.timeSeconds, note.sampleId});
            if (audio->prepare(chart, error_, &cancelled_, true) && !cancelled_) audio_ = std::move(audio);
            ready_.store(true, std::memory_order_release);
            if (SUCCEEDED(com)) CoUninitialize();
        });
    }
    void stop() {
        requested_ = pending_ = false;
        rateCancelled_ = true;
        if (rateWorker_.joinable()) rateWorker_.join();
        if (ready_.load(std::memory_order_acquire) && audio_) audio_->stop();
    }
    void select(double start, double end, double rate) {
        stop(); start_ = start; end_ = end; rate_ = rate;
        requested_ = pending_ = true; changed_ = std::chrono::steady_clock::now();
    }
    void tick(int master, int volume, uint32_t buffer) {
        if (!requested_ || !ready_.load(std::memory_order_acquire) || !audio_) return;
        audio_->setVolumes(master, volume, volume, 0);
        if (pending_ && std::chrono::steady_clock::now() - changed_ >= std::chrono::milliseconds(150)) {
            if (!rateWorker_.joinable()) {
                rateCancelled_ = false; rateDone_ = false; rateError_.clear();
                rateWorker_ = std::thread([this, rate = rate_] {
                    rateOkay_ = audio_->preparePlaybackRate(rate, rateError_, &rateCancelled_);
                    rateDone_.store(true, std::memory_order_release);
                });
                return;
            }
            if (!rateDone_.load(std::memory_order_acquire)) return;
            rateWorker_.join();
            if (!rateOkay_) { error_ = rateError_; requested_ = pending_ = false; return; }
            audio_->setBufferFrames(buffer);
            pending_ = false;
            if (!audio_->startAt(start_, error_, rate_)) requested_ = false;
        }
        if (!pending_ && audio_->isRunning() && audio_->positionSeconds() >= end_) stop();
    }
    bool playing() const { return ready_.load(std::memory_order_acquire) && audio_ && audio_->isRunning(); }
};
