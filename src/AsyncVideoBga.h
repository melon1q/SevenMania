#pragma once
#include "VideoBga.h"
#include "BgaRaster.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <array>
#include "HudFrameProbe.h"

#ifdef BMS_HUD_FRAME_PROBE
// Cross-thread totals only; the per-frame recorder belongs to the UI thread.
struct VideoFrameMetrics {
    inline static std::atomic<uint64_t> created{}, reused{}, overflow{}, pixelAllocations{}, liveBytes{}, peakBytes{}, destroyed{};
    inline static std::atomic<DWORD> uiThread{};
    static void capacity(size_t before, size_t after) {
        if (after <= before) return;
        ++pixelAllocations;
        const auto bytes=liveBytes.fetch_add((after-before)*4)+(after-before)*4;
        auto peak=peakBytes.load();while(peak<bytes&&!peakBytes.compare_exchange_weak(peak,bytes)){}
    }
};
#endif

// Decode and scale off the UI thread. Only the latest completed frame is kept,
// so video cannot build an unbounded queue or block the note/input loop.
class AsyncVideoBga {
public:
    struct Frame {
        int width{}, height{},sourceWidth{},sourceHeight{}; uint64_t generation{}; double timeSeconds{},requestedSeconds{}; std::vector<uint32_t> pixels;
#ifdef BMS_HUD_FRAME_PROBE
        Frame(){++VideoFrameMetrics::created;}
        ~Frame(){
            const auto bytes=pixels.capacity()*4;
            if(GetCurrentThreadId()==VideoFrameMetrics::uiThread.load()){
                HUD_SCOPE(VideoFrameFree);
                hudprobe::recorder.count(hudprobe::VideoFrameFrees);
                hudprobe::recorder.count(hudprobe::VideoFrameFreedBytes,unsigned(bytes));
                std::vector<uint32_t>().swap(pixels); // Measure the actual vector deallocation, not only refcount decrement.
            }
            VideoFrameMetrics::liveBytes.fetch_sub(bytes);++VideoFrameMetrics::destroyed;
        }
#endif
    };
private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::thread worker_;
    bool stopping_{true}, finished_{};
    double desiredTime_{};
    int desiredWidth_{}, desiredHeight_{};
    uint64_t request_{}, completed_{}, observed_{};
    std::shared_ptr<const Frame> latest_;
public:
    ~AsyncVideoBga() { reset(); }
    void reset() {
        { std::lock_guard lock(mutex_); stopping_ = true; }
        changed_.notify_all();
        if (worker_.joinable()) worker_.join();
        std::lock_guard lock(mutex_);
        latest_.reset(); desiredTime_ = 0; desiredWidth_ = desiredHeight_ = 0; request_ = completed_ = observed_ = 0;
        finished_ = false;
    }
    bool open(const std::filesystem::path& path, int width = 1280, int height = 720, bool loop = true, double overrideFps = 0) {
        reset();
        { std::lock_guard lock(mutex_); stopping_ = false; desiredWidth_ = width; desiredHeight_ = height; ++request_; }
        worker_ = std::thread([this, path, loop, overrideFps] {
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (SUCCEEDED(initialized)) try {
                VideoBga video;
                VideoFrameScaler scaler;
                if (video.open(path)) {
                    // Worker-owned strong references keep normal frame destruction
                    // off the rendering thread. Never write into a published frame:
                    // use_count==1 means this worker's pool is the only owner.
                    // Three slots, at most 8MiB each. Larger frames and exhausted
                    // slots retain the old allocation path; no wait or stale frame.
                    std::array<std::shared_ptr<Frame>,3> frames;
                    constexpr size_t slotBytes=8*1024*1024;
#ifdef BMS_HUD_FRAME_PROBE
                    const bool legacyFrames=GetEnvironmentVariableW(L"BMS_PROBE_LEGACY_VIDEO_FRAMES",nullptr,0)!=0;
#else
                    constexpr bool legacyFrames=false;
#endif
                    uint64_t consumed = 0, generation = 0;
                    int scaledWidth = 0, scaledHeight = 0;
                    for (;;) {
                        double time; int width, height;
                        {
                            std::unique_lock lock(mutex_);
                            changed_.wait(lock, [&] { return stopping_ || request_ != consumed; });
                            if (stopping_) break;
                            consumed = request_; time = desiredTime_; width = desiredWidth_; height = desiredHeight_;
                        }
                        const bool updated = video.update(time*(overrideFps>0?overrideFps/video.frameRate():1),loop);
                        std::shared_ptr<Frame> frame;
                        if (video.ready() && (updated || width != scaledWidth || height != scaledHeight)) {
                            if(!legacyFrames&&width>0&&height>0&&uint64_t(width)*height*4<=slotBytes){
                                for(auto& slot:frames){
                                    if(slot&&slot.use_count()!=1)continue;
                                    // Pair the observed final consumer refcount
                                    // release with an acquire before overwriting.
                                    // MSVC's count read itself is not acquire.
                                    if(slot)std::atomic_thread_fence(std::memory_order_acquire);
                                    // Discard an exclusive old-size slot on this worker;
                                    // vector growth cannot inflate retained pool capacity.
                                    if(slot&&(slot->width!=width||slot->height!=height))slot.reset();
#ifdef BMS_HUD_FRAME_PROBE
                                    if(slot)++VideoFrameMetrics::reused;
#endif
                                    if(!slot)slot=std::make_shared<Frame>();
                                    frame=slot;break;
                                }
                            }
                            if(!frame){
#ifdef BMS_HUD_FRAME_PROBE
                                if(!legacyFrames)++VideoFrameMetrics::overflow;
#endif
                                frame=std::make_shared<Frame>();
                            }
                            frame->width=width;frame->height=height;frame->sourceWidth=video.width();frame->sourceHeight=video.height();frame->timeSeconds=video.positionSeconds()/(overrideFps>0?overrideFps/video.frameRate():1);frame->requestedSeconds=time;
#ifdef BMS_HUD_FRAME_PROBE
                            const auto oldCapacity=frame->pixels.capacity();
#endif
                            const bool scaled=scaler.scale(video.width(), video.height(), video.pixels(), width, height, frame->pixels);
#ifdef BMS_HUD_FRAME_PROBE
                            VideoFrameMetrics::capacity(oldCapacity,frame->pixels.capacity());
#endif
                            // Enforce the retention limit even if a library's vector
                            // implementation reserves more than the requested size.
                            if(frame->pixels.capacity()*4>slotBytes)for(auto& slot:frames)if(slot==frame)slot.reset();
                            if (scaled) {
                                frame->generation = ++generation;
                                scaledWidth = width; scaledHeight = height;
                            } else frame.reset();
                        }
                        {
                            std::lock_guard lock(mutex_);
                            if (stopping_) break;
                            if (frame) latest_ = std::move(frame);
                            completed_ = consumed;
                        }
                        changed_.notify_all();
                    }
                }
            } catch (...) { /* An optional broken video must not abort song loading. */ }
            if (SUCCEEDED(initialized)) CoUninitialize();
            { std::lock_guard lock(mutex_); finished_ = true; }
            changed_.notify_all();
        });
        return true;
    }
    void request(double time, int width, int height) {
        { std::lock_guard lock(mutex_); desiredTime_ = time; desiredWidth_ = width; desiredHeight_ = height; ++request_; }
        changed_.notify_all();
    }
    bool update(double time) {
        bool changed;
        { std::lock_guard lock(mutex_); desiredTime_ = time; ++request_;
          const uint64_t generation = latest_ ? latest_->generation : 0;
          changed = generation != observed_; observed_ = generation; }
        changed_.notify_all(); return changed;
    }
    // Called by the loading worker (or before restarting audio), never by the
    // gameplay frame loop. Opening the reader alone does not prepare a frame.
    bool waitUntilPrepared(const std::atomic_bool* cancelled = nullptr) {
        std::unique_lock lock(mutex_);
        const auto requested = request_;
        while (!stopping_ && !finished_ && completed_ < requested) {
            if (cancelled && cancelled->load()) return false;
            changed_.wait_for(lock, std::chrono::milliseconds(20));
        }
        return !stopping_ && latest_ && !(cancelled && cancelled->load());
    }
    std::shared_ptr<const Frame> frame() const { std::lock_guard lock(mutex_); return latest_; }
    bool ready() const { return static_cast<bool>(frame()); }
};
