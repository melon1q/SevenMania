#pragma once
// Opt-in only. No file I/O or growing containers in the measured interval.
#ifdef BMS_HUD_FRAME_PROBE
#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
namespace hudprobe {
using Clock=std::chrono::steady_clock;
enum Stage { HudUpdate,HudBitmapGroup,HudMask,HudLayer,Animations,ResourcePrepare,PrepareCheck,SkinDraw,D2DBegin,D2DEnd,Present,Copy,CompositionDraw,Readback,ScoreUpdate,GaugeUpdate,GamePaint,Backdrop,LegacyIcons,LegacyDeck,GdiSync,ReflexSleep,PacerWait,InputPump,InputDown,InputUp,AudioPosition,VideoUpdate,BgaStateUpdate,GameLogic,PaintDispatch,WindowPaint,PresenterPrepare,SkinHostPrepare,Snapshot,ResourceStats,FinalGdiFlush,PresenterTotal,GpuQueryCollect,UploadHybrid,UploadFrame,SwapBufferGet,MarkerSimStart,MarkerSimEnd,MarkerRenderStart,MarkerRenderEnd,MarkerPresentStart,MarkerPresentEnd,MarkerInput,LatencyRequest,UploadBackdrop,UploadOverlay,UploadRegion0,UploadRegion1,UploadRegion2,BackdropCompare,BackdropCopy,BackdropCreate,PauseTransition,ResumeTransition,BackdropRetain,VideoFrameFree,Stages };
enum Count { AtlasBuilds,SkinPrepares,TargetChanges,PrepareTargetChanges,HudBitmaps,HudMasks,BitmapSwitches,TextUploads,Readbacks,Submits,HudTextUploads,QueueDeferred,PresentCalls,InputPaint,PausedPaint,UnfocusedPaint,DeferredInputPaint,AutoResumes,Region0Bytes,Region1Bytes,Region2Bytes,BackdropComparedRows,BackdropCopyCalls,VideoFrameFrees,VideoFrameFreedBytes,BackdropUploadBytes,Counts };
inline constexpr const char* stageNames[]={"hud_update_cpu_ms","hud_bitmap_group_cpu_ms","hud_mask_cpu_ms","hud_layer_cpu_ms","animation_cpu_ms","resource_prepare_cpu_ms","prepare_check_cpu_ms","skin_draw_cpu_ms","d2d_begin_cpu_ms","d2d_end_cpu_ms","present_cpu_ms","copy_cpu_ms","composition_draw_cpu_ms","readback_cpu_ms","score_update_cpu_ms","gauge_update_cpu_ms","game_paint_cpu_ms","backdrop_cpu_ms","legacy_icons_cpu_ms","legacy_deck_cpu_ms","gdi_sync_cpu_ms","reflex_sleep_cpu_ms","pacer_wait_cpu_ms","inputpump_cpu_ms","inputdown_cpu_ms","inputup_cpu_ms","audioposition_cpu_ms","videoupdate_cpu_ms","bgastateupdate_cpu_ms","gamelogic_cpu_ms","paintdispatch_cpu_ms","windowpaint_cpu_ms","presenterprepare_cpu_ms","skinhostprepare_cpu_ms","snapshot_cpu_ms","resourcestats_cpu_ms","finalgdiflush_cpu_ms","presentertotal_cpu_ms","gpuquerycollect_cpu_ms","uploadhybrid_cpu_ms","uploadframe_cpu_ms","swapbufferget_cpu_ms","markersimstart_cpu_ms","markersimend_cpu_ms","markerrenderstart_cpu_ms","markerrenderend_cpu_ms","markerpresentstart_cpu_ms","markerpresentend_cpu_ms","markerinput_cpu_ms","latencyrequest_cpu_ms","upload_backdrop_cpu_ms","upload_overlay_cpu_ms","upload_region0_cpu_ms","upload_region1_cpu_ms","upload_region2_cpu_ms","backdrop_compare_cpu_ms","backdrop_copy_cpu_ms","backdrop_create_cpu_ms","pause_transition_cpu_ms","resume_transition_cpu_ms","backdrop_retain_cpu_ms","video_frame_free_cpu_ms"};
inline constexpr const char* countNames[]={"atlas_builds","skin_prepares","target_changes","skin_target_changes","hud_bitmaps","hud_masks","bitmap_switches","text_uploads","readbacks","submits","hud_text_uploads","queue_deferred","present_calls","input_paints","paused_paints","unfocused_paints","deferred_input_paints","auto_resumes","region0_bytes","region1_bytes","region2_bytes","backdrop_compared_rows","backdrop_copy_calls","video_frame_frees","video_frame_freed_bytes","backdrop_upload_bytes"};
struct Span{std::uint64_t start{},end{};unsigned stage{},depth{};};
struct Row{std::uint64_t serial{};double wall{},chart{},interval{},cpu{};std::uint64_t cycles{};unsigned spanSize{},spanDropped{};std::array<Span,128> spans{};std::array<double,Stages> ns{};std::array<unsigned,Counts> counts{};};
struct Gpu{std::uint64_t serial{};double ms{};};
struct Submit{std::uint64_t serial{};double wall{};};
struct Recorder{
    static constexpr size_t capacity=65536;
    std::unique_ptr<Row[]> rows;std::unique_ptr<Gpu[]> gpu;std::unique_ptr<Submit[]> submits;
    unsigned depth{};std::uint64_t startCycles{};bool cyclesStarted{};
    Row row,pending;size_t size{},gpuSize{},submitSize{},dropped{};bool enabled{},active{},detailed=true,forceLegacyIcons{},legacyReflex{};
    Clock::time_point origin{},previous{},began{};const void* lastBitmap{};
    void allocate(){rows=std::make_unique<Row[]>(capacity);gpu=std::make_unique<Gpu[]>(capacity);submits=std::make_unique<Submit[]>(capacity);}
    void start(bool details){size=gpuSize=submitSize=dropped=0;row={};pending={};previous={};origin=Clock::now();depth=0;detailed=details;enabled=true;active=false;}
    void begin(){if(!enabled)return;const auto now=Clock::now();row=pending;pending={};row.serial=size+1;row.wall=std::chrono::duration<double>(now-origin).count();row.interval=previous==Clock::time_point{}?0:std::chrono::duration<double,std::milli>(now-previous).count();began=previous=now;cyclesStarted=QueryThreadCycleTime(GetCurrentThread(),&startCycles)!=0;active=true;}
    void finish(double chart){if(!enabled||!active)return;row.chart=chart;std::uint64_t cycles{};const bool valid=QueryThreadCycleTime(GetCurrentThread(),&cycles)!=0;row.cycles=cyclesStarted&&valid&&cycles>=startCycles?cycles-startCycles:0;row.cpu=std::chrono::duration<double,std::milli>(Clock::now()-began).count();active=false;if(size<capacity)rows[size++]=row;else ++dropped;}
    void count(Count c,unsigned n=1){if(enabled)(active?row:pending).counts[c]+=n;}
    void bitmap(const void* p,unsigned calls){if(!enabled)return;count(HudBitmaps,calls);if(lastBitmap!=p){count(BitmapSwitches);lastBitmap=p;}}
    void gpuSample(std::uint64_t serial,double ms){if(enabled&&gpuSize<capacity)gpu[gpuSize++]={serial,ms};}
    void submit(){if(!enabled)return;count(Submits);if(submitSize<capacity)submits[submitSize++]={active?row.serial:0,std::chrono::duration<double>(Clock::now()-origin).count()};}
    void save(const std::filesystem::path& prefix){enabled=false;active=false;std::ofstream f(prefix.wstring()+L".frames.csv");f.precision(12);f<<"serial,wall_s,chart_s,interval_ms,frame_cpu_ms";for(auto name:stageNames)f<<','<<name;for(auto name:countNames)f<<','<<name;f<<",ui_thread_cycles,span_dropped\n";
        for(size_t i=0;i<size;++i){const auto& r=rows[i];f<<r.serial<<','<<r.wall<<','<<r.chart<<','<<r.interval<<','<<r.cpu;for(auto n:r.ns)f<<','<<n/1e6;for(auto n:r.counts)f<<','<<n;f<<','<<r.cycles<<','<<r.spanDropped<<'\n';}
        std::unique_ptr<bool[]> selected=std::make_unique<bool[]>(size);
        for(size_t i=0;i<size;++i)if(rows[i].cpu>=1.25||rows[i].interval>=1.5)for(size_t j=i>2?i-2:0;j<size&&j<=i+2;++j)selected[j]=true;
        std::ofstream spans(prefix.wstring()+L".spans.csv");spans<<"serial,stage,depth,start_ns,end_ns,elapsed_ns\n";
        for(size_t i=0;i<size;++i)if(selected[i])for(unsigned j=0;j<rows[i].spanSize;++j){const auto& s=rows[i].spans[j];spans<<rows[i].serial<<','<<stageNames[s.stage]<<','<<s.depth<<','<<s.start<<','<<s.end<<','<<s.end-s.start<<'\n';}
        std::ofstream g(prefix.wstring()+L".gpu.csv");g.precision(12);g<<"serial,frame_gpu_bracket_ms\n";for(size_t i=0;i<gpuSize;++i)g<<gpu[i].serial<<','<<gpu[i].ms<<'\n';
        std::ofstream p(prefix.wstring()+L".submits.csv");p.precision(12);p<<"serial,wall_s\n";for(size_t i=0;i<submitSize;++i)p<<submits[i].serial<<','<<submits[i].wall<<'\n';
    }
};
inline Recorder recorder;
struct Scope{Stage stage;bool on;Clock::time_point start;unsigned depth{};
    explicit Scope(Stage s,bool condition=true):stage(s),on(condition&&recorder.enabled&&recorder.detailed){if(on){start=Clock::now();depth=recorder.depth++;}}
    ~Scope(){if(on){const auto end=Clock::now();auto& row=recorder.active?recorder.row:recorder.pending;row.ns[stage]+=std::chrono::duration<double,std::nano>(end-start).count();--recorder.depth;
        if(row.spanSize<row.spans.size())row.spans[row.spanSize++]={std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(start-recorder.origin).count()),std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(end-recorder.origin).count()),unsigned(stage),depth};else ++row.spanDropped;
    }}
};
}
#define HUD_JOIN_(a,b) a##b
#define HUD_JOIN(a,b) HUD_JOIN_(a,b)
#define HUD_SCOPE(stage) hudprobe::Scope HUD_JOIN(hudScope,__LINE__)(hudprobe::stage)
#define HUD_SCOPE_IF(stage,condition) hudprobe::Scope HUD_JOIN(hudScope,__LINE__)(hudprobe::stage,condition)
#define HUD_COUNT(value) hudprobe::recorder.count(hudprobe::value)
#define HUD_BITMAP(pointer,calls) hudprobe::recorder.bitmap(pointer,calls)
#define HUD_REGION(index,bytes) hudprobe::Scope HUD_JOIN(hudRegion,__LINE__)(static_cast<hudprobe::Stage>(hudprobe::UploadRegion0+std::min(unsigned(index),2u)));hudprobe::recorder.count(static_cast<hudprobe::Count>(hudprobe::Region0Bytes+std::min(unsigned(index),2u)),bytes);++index
#define HUD_MARKER(marker) hudprobe::Scope HUD_JOIN(hudMarker,__LINE__)(static_cast<hudprobe::Stage>(hudprobe::MarkerSimStart+static_cast<int>(marker)))
#define HUD_FRAME_BEGIN() hudprobe::recorder.begin()
#define HUD_FRAME_END(chart) hudprobe::recorder.finish(chart)
#else
#define HUD_REGION(index,bytes) ((void)0)
#define HUD_MARKER(marker) ((void)0)
#define HUD_SCOPE(stage) ((void)0)
#define HUD_SCOPE_IF(stage,condition) ((void)0)
#define HUD_COUNT(value) ((void)0)
#define HUD_BITMAP(pointer,calls) ((void)0)
#define HUD_FRAME_BEGIN() ((void)0)
#define HUD_FRAME_END(chart) ((void)0)
#endif

#ifdef BMS_HUD_FRAME_PROBE
#define HUD_LEGACY_REFLEX() (hudprobe::recorder.legacyReflex)
#else
#define HUD_LEGACY_REFLEX() false
#endif
