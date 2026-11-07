#pragma once
#include "BmsParser.h"
#include <atomic>
#include <mutex>
#include <memory>
#include <chrono>
#include <array>

// One outstanding job, reusing gLoadWorker. UI flags are never touched by worker.
// Publication uses the mutex for the immutable chart and release/acquire for completion.
struct PlaybackLoadJob {
    using Clock=std::chrono::steady_clock;
    std::mutex mutex;
    std::shared_ptr<const bms::Chart> chart;
    std::atomic_bool done{false};
    std::uintptr_t generation{};
    std::filesystem::path path;
    std::wstring error,partial;
    double parseMs{},audioMs{},rateMs{},videoMs{},imageMs{},gpuMs{},renderMaxMs{};
    size_t renders{};int assetWidth{},assetHeight{};std::array<double,6> sceneSignature{};
    Clock::time_point lastSceneRender{};bool previewDirty=true;
    Clock::time_point selected{},preparing{},ready{},playing{},imageStarted{},lastFrame{};
    std::array<double,4096> frames{};size_t frameCount{},frameTotal{};
    size_t peakWorkingSet{},peakPrivate{},pcmBytes{};
    bool active{},applied{},consumed{},gpuReady{},painted{},reported{},serial{};
    void begin(const std::filesystem::path& file){
        // Previous worker must already be joined before reuse.
        std::lock_guard lock(mutex);chart.reset();done=false;++generation;if(!generation)++generation;
        path=file;error.clear();partial.clear();parseMs=audioMs=rateMs=videoMs=imageMs=gpuMs=0;
        preparing=ready=playing=imageStarted=lastFrame={};selected=Clock::now();frameCount=frameTotal=0;peakWorkingSet=peakPrivate=pcmBytes=0;
        active=true;applied=consumed=gpuReady=painted=reported=false;previewDirty=true;lastSceneRender={};renders=0;renderMaxMs=0;assetWidth=assetHeight=0;sceneSignature={};
        serial=GetEnvironmentVariableW(L"SEVENMANIA_LOAD_SERIAL",nullptr,0)!=0;
    }
    void publish(std::shared_ptr<const bms::Chart> value){std::lock_guard lock(mutex);chart=std::move(value);}
    std::shared_ptr<const bms::Chart> snapshot(){std::lock_guard lock(mutex);return chart;}
    void releaseChart(){std::lock_guard lock(mutex);chart.reset();}
    static double elapsed(Clock::time_point from,Clock::time_point to=Clock::now()){return std::chrono::duration<double,std::milli>(to-from).count();}
    bool complete() const {return done.load(std::memory_order_acquire);}
};
