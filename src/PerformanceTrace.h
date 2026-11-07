#pragma once
#include "ExperimentProfile.h"

// The profiling target uses the real player loop. The shipping target compiles
// these probes out so collecting diagnostic CSV files is always opt-in.
#ifdef BMS_PERF_DIAGNOSTICS
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>
#include <atomic>

namespace perf {
using Clock = std::chrono::steady_clock;
struct Frame {
    double intervalMs{}, positionMs{}, bgaMs{}, hudMs{}, syncMs{}, notesMs{}, judgementMs{}, presentMs{};
    double decodeMs{}, imageMs{}, shadeMs{}, tintMs{};
    double videoMs{}, rawPositionSeconds{};
    double resourceMs{}, overlayMs{}, deckMs{};
    double beamsMs{}, feedbackMs{}, textMs{}, bgaCopyMs{}, backplateBuildMs{}, uploadMs{}, gpuDrawMs{}, gpuQueueMs{};
    double inputMs{},judgeLogicMs{},triggerLockMs{},waitMs{},latencyWaitMs{};
    size_t keyDown{},keyUp{},judgements{},textMisses{};
    size_t visibleNotes{};
    uint64_t backdropSavedBytes{};
    bool rebuiltBga{};
    bool presentDeferred{};
};
inline bool detailed=true;
struct Allocations {uint64_t calls{},bytes{},frees{};};
inline thread_local Allocations allocations;
inline std::atomic<uint64_t> audioMixNs{},audioLockNs{},audioCalls{};
struct Event {double wall{},chart{};int kind{},value{};};
struct Row {Frame frame;double playhead{},duration{},wall{};uint64_t allocations{},bytes{},frees{},audioNs{},audioLock{},audioCount{};};
struct Trace {
    Frame frame,pending;
    std::ofstream file;
    std::filesystem::path path;
    std::vector<Row> rows;
    std::vector<Event> events;
    Clock::time_point start{},previous{};
    Allocations allocationStart{};
    size_t dropped{};bool active{};
    double& timer(double Frame::*member){return active?frame.*member:pending.*member;}
    size_t& count(size_t Frame::*member){return active?frame.*member:pending.*member;}
    void event(int kind,int value,double chart){if((detailed || kind==4) && file.is_open() && events.size()<32768)events.push_back({std::chrono::duration<double>(Clock::now().time_since_epoch()).count(),chart,kind,value});}
    void session(const std::filesystem::path& output) {
        save();path=output;file.open(path,std::ios::trunc);rows.clear();events.clear();rows.reserve(65536);events.reserve(32768);
        previous={};frame={};pending={};active=false;dropped=0;allocationStart=allocations;
    }
    void begin(){const auto now=Clock::now();frame=pending;pending={};if(previous!=Clock::time_point{})frame.intervalMs=std::chrono::duration<double,std::milli>(now-previous).count();start=previous=now;active=true;}
    void finish(double playhead){
        const auto now=Clock::now();active=false;
        if(!file.is_open())return;
        Row row{frame,playhead,std::chrono::duration<double,std::milli>(now-start).count(),std::chrono::duration<double>(now.time_since_epoch()).count(),
            allocations.calls-allocationStart.calls,allocations.bytes-allocationStart.bytes,allocations.frees-allocationStart.frees,
            audioMixNs.exchange(0),audioLockNs.exchange(0),audioCalls.exchange(0)};
        allocationStart=allocations;if(rows.size()<65536)rows.push_back(row);else ++dropped;
    }
    void save(){
        if(!file.is_open())return;
        file<<"playhead_s,interval_ms,frame_ms,position_ms,bga_ms,hud_ms,sync_ms,notes_ms,judgement_ms,present_ms,visible_notes,bga_rebuilt,decode_ms,image_ms,shade_ms,tint_ms,video_ms,raw_position_s,resource_ms,overlay_ms,deck_ms,beams_ms,feedback_ms,text_ms,bga_copy_ms,backplate_build_ms,upload_ms,gpu_draw_ms,gpu_queue_ms,present_deferred,input_ms,judge_logic_ms,trigger_lock_ms,wait_ms,latency_wait_ms,key_down,key_up,judgements,text_misses,cpp_allocations,cpp_bytes,cpp_frees,audio_mix_ms,audio_lock_ms,audio_calls,wall_s,backdrop_saved_bytes\n";
        file.precision(12);
        for(const auto& r:rows){const auto& f=r.frame;
            file<<r.playhead<<','<<f.intervalMs<<','<<r.duration<<','<<f.positionMs<<','<<f.bgaMs<<','<<f.hudMs<<','<<f.syncMs<<','<<f.notesMs<<','<<f.judgementMs<<','<<f.presentMs<<','<<f.visibleNotes<<','<<f.rebuiltBga
                <<','<<f.decodeMs<<','<<f.imageMs<<','<<f.shadeMs<<','<<f.tintMs<<','<<f.videoMs<<','<<f.rawPositionSeconds<<','<<f.resourceMs<<','<<f.overlayMs<<','<<f.deckMs<<','<<f.beamsMs<<','<<f.feedbackMs<<','<<f.textMs<<','<<f.bgaCopyMs<<','<<f.backplateBuildMs<<','<<f.uploadMs<<','<<f.gpuDrawMs<<','<<f.gpuQueueMs<<','<<f.presentDeferred
                <<','<<f.inputMs<<','<<f.judgeLogicMs<<','<<f.triggerLockMs<<','<<f.waitMs<<','<<f.latencyWaitMs<<','<<f.keyDown<<','<<f.keyUp<<','<<f.judgements<<','<<f.textMisses<<','<<r.allocations<<','<<r.bytes<<','<<r.frees<<','<<r.audioNs/1e6<<','<<r.audioLock/1e6<<','<<r.audioCount<<','<<r.wall<<','<<f.backdropSavedBytes<<'\n';
        }file.close();
        std::ofstream eventFile(path.wstring()+L".events.csv");eventFile.precision(12);eventFile<<"wall_s,chart_s,kind,value\n";for(const auto&e:events)eventFile<<e.wall<<','<<e.chart<<','<<e.kind<<','<<e.value<<'\n';
    }
    ~Trace(){save();}
};
inline Trace trace;
struct Scope {
    double& elapsed;
    Clock::time_point start;
    explicit Scope(double& value) : elapsed(value),start(detailed?Clock::now():Clock::time_point{}) {}
    ~Scope() { if(detailed)elapsed += std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }
};
}
#define BMS_PERF_JOIN2(a, b) a##b
#define BMS_PERF_JOIN(a, b) BMS_PERF_JOIN2(a, b)
#define BMS_PERF_SCOPE(field) BMS_TRACE_ZONE(#field); perf::Scope BMS_PERF_JOIN(perfScope, __LINE__)(perf::trace.timer(&perf::Frame::field))
#define BMS_PERF_SET(field, value) (perf::trace.frame.field = (value))
#define BMS_PERF_COUNT(field) (++perf::trace.count(&perf::Frame::field))
#define BMS_PERF_EVENT(kind,value,time) perf::trace.event(kind,value,time)
#define BMS_PERF_BEGIN() perf::trace.begin()
#define BMS_PERF_FINISH(time) perf::trace.finish(time)
#define BMS_PERF_SESSION(path) perf::trace.session(path)
#else
#define BMS_PERF_SCOPE(field) BMS_TRACE_ZONE(#field)
#define BMS_PERF_SET(field, value) ((void)0)
#define BMS_PERF_COUNT(field) ((void)0)
#define BMS_PERF_EVENT(kind,value,time) ((void)0)
#define BMS_PERF_BEGIN() ((void)0)
#define BMS_PERF_FINISH(time) ((void)0)
#define BMS_PERF_SESSION(path) ((void)0)
#endif
