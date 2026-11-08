#pragma once
// Per-play presentation diagnostics: classifies what a "tearing" report most
// likely is (GDI fallback, VSync frame misses, or unsynchronized flip judder).
// The D3D11 path uses a flip-model swap chain without ALLOW_TEARING, so true
// scan-out tearing is only expected on the GDI BitBlt fallback.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace presentdiag {

struct Config {
    bool vsync{};
    int targetFps{};          // 0 = unlimited
    bool composition{};       // GPU composition setting enabled
    bool directComposition{};
    bool hybrid{};
    int refreshHz{};          // 0 = unknown
    int width{}, height{};
    bool coversMonitor{};     // borderless/fullscreen candidate for independent flip
    bool vblankAligned{};     // frame starts aligned to vblanks (one wake per refresh)
    bool alignedStallClassifier{true}; // with vblankAligned: loop-stall needs > 1.4x the interval
    std::string variant{"default"}; // A/B label (SEVENMANIA_DIAG_VARIANT and switches)
};

// CPU-side work observed since the previous accepted GPU submission.
struct FrameWork {
    double paintMs{};      // WM_PAINT start -> Present call
    double presentMs{};    // Present call (upload, draw, copy, submit)
    double loopGapMs{};    // longest gap between WM_PAINT entries (message loop not painting)
    uint64_t uploadBytes{}, textMisses{};
    bool bgaRebuilt{};
};

struct Hitch {
    double atSeconds{}, intervalMs{}, maxPaintMs{}, maxPresentMs{}, maxLoopGapMs{};
    uint64_t cpuFrames{}, uploadBytes{}, textMisses{}, bgaRebuilds{};
    const char* cause{"unknown"};
};

// Most likely reason a submission gap exceeded 1.5x the expected interval.
inline const char* hitchCause(const Hitch& h, double expectedMs, uint64_t frameBytes, double loopStallMs = 0) {
    if (h.maxPaintMs + h.maxPresentMs >= expectedMs * 0.75) {
        if (h.bgaRebuilds) return "cpu-bga-rebuild";
        if (h.textMisses) return "cpu-text-miss";
        if (frameBytes && h.uploadBytes >= frameBytes / 2) return "cpu-upload";
        return h.maxPresentMs > h.maxPaintMs ? "cpu-present" : "cpu-paint";
    }
    if (h.maxLoopGapMs >= (loopStallMs > 0 ? loopStallMs : expectedMs)) return "loop-stall";
    return "gpu-or-compositor";
}

struct Summary {
    Config config;
    long long runId{}; // wall-clock start (Unix seconds), shared by summary and hitch rows
    double seconds{};
    uint64_t gpuSubmits{}, gdiFrames{}, deferred{}, fallbacks{};
    std::string firstFallbackStage;
    long firstFallbackHr{};
    double firstFallbackSeconds{-1};
    double p50Ms{}, p99Ms{}, maxMs{};
    uint64_t missedIntervals{}; // submit gap > 1.5 refresh periods
    std::string classification;
    std::vector<Hitch> hitches;
    uint64_t videoNv12Offered{}, videoNv12Uploads{}; std::string videoNv12Fail;
    uint64_t hitchCount(const char* cause) const {
        return uint64_t(std::count_if(hitches.begin(), hitches.end(), [&](const Hitch& h) { return std::string_view(h.cause) == cause; }));
    }
};

inline const char* classify(const Summary& s) {
    if (s.gdiFrames > 0) return "gdi-fallback";           // real tearing possible
    if (s.gpuSubmits < 2) return "insufficient-data";
    const double missRatio = double(s.missedIntervals) / double(s.gpuSubmits - 1);
    if (s.config.vsync) return missRatio > 0.01 ? "vsync-frame-miss" : "gpu-vsync-clean";
    return missRatio > 0.01 ? "unsynced-flip-frame-miss" : "unsynced-flip";
}

class Session {
    using Clock = std::chrono::steady_clock;
    bool active_{};
    Config config_{};
    Clock::time_point start_{}, last_{};
    std::vector<float> intervals_;
    uint64_t gpu_{}, gdi_{}, fallbacks_{}, deferredBase_{};
    std::string stage_; long hr_{}; double fallbackAt_{-1};
    double expectedMs_{};
    long long runId_{};
    Hitch pending_{};
    std::vector<Hitch> hitches_;
    double seconds(Clock::time_point t) const { return std::chrono::duration<double>(t - start_).count(); }
public:
    bool active() const { return active_; }
    void begin(const Config& config, uint64_t deferredCounter, Clock::time_point now = Clock::now()) {
        *this = Session{}; active_ = true;
        runId_ = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); config_ = config; start_ = now; deferredBase_ = deferredCounter;
        intervals_.reserve(1 << 16);
        const double period = config.refreshHz > 0 ? 1000.0 / config.refreshHz : 0;
        // Without VSync the expected gap is the FPS cap, not the refresh period.
        expectedMs_ = !config.vsync && config.targetFps > 0 ? std::max(period, 1000.0 / config.targetFps) : period;
    }
    double expectedMs() const { return expectedMs_; }
    void work(const FrameWork& w) {
        if (!active_) return;
        ++pending_.cpuFrames;
        pending_.maxPaintMs = std::max(pending_.maxPaintMs, w.paintMs);
        pending_.maxPresentMs = std::max(pending_.maxPresentMs, w.presentMs);
        pending_.maxLoopGapMs = std::max(pending_.maxLoopGapMs, w.loopGapMs);
        pending_.uploadBytes += w.uploadBytes; pending_.textMisses += w.textMisses; pending_.bgaRebuilds += w.bgaRebuilt;
    }
    // One accepted output frame. GPU = Present S_OK, GDI = BitBlt fallback.
    void submitted(bool gpu, Clock::time_point now = Clock::now()) {
        if (!active_) return;
        (gpu ? gpu_ : gdi_)++;
        if (gpu) {
            if (last_ != Clock::time_point{}) {
                const float interval = std::chrono::duration<float, std::milli>(now - last_).count();
                if (intervals_.size() < intervals_.capacity()) intervals_.push_back(interval);
                if (expectedMs_ > 0 && interval > expectedMs_ * 1.5 && hitches_.size() < 4096) {
                    Hitch h = pending_; h.atSeconds = seconds(now); h.intervalMs = interval;
                    const double stallMs = config_.vblankAligned && config_.alignedStallClassifier ? expectedMs_ * 1.4 : expectedMs_;
                    h.cause = hitchCause(h, expectedMs_, uint64_t(config_.width) * config_.height * 4, stallMs);
                    hitches_.push_back(h);
                }
            }
            last_ = now;
        } else last_ = {};
        pending_ = {};
    }
    // Pause or another non-gameplay stretch: do not count it as a frame gap.
    void gap() { last_ = {}; pending_ = {}; }
    void fallback(const char* stage, long hr, Clock::time_point now = Clock::now()) {
        if (!active_) return;
        if (!fallbacks_++) { stage_ = stage ? stage : "unknown"; hr_ = hr; fallbackAt_ = seconds(now); }
    }
    Summary finish(uint64_t deferredCounter, Clock::time_point now = Clock::now()) {
        Summary s; s.config = config_; s.runId = runId_;
        if (!active_) return s;
        active_ = false;
        s.seconds = seconds(now); s.gpuSubmits = gpu_; s.gdiFrames = gdi_; s.fallbacks = fallbacks_;
        s.deferred = deferredCounter >= deferredBase_ ? deferredCounter - deferredBase_ : 0;
        s.firstFallbackStage = stage_; s.firstFallbackHr = hr_; s.firstFallbackSeconds = fallbackAt_;
        if (!intervals_.empty()) {
            auto v = intervals_; std::sort(v.begin(), v.end());
            const auto at = [&](double p) { return double(v[std::min(v.size() - 1, size_t(p * (v.size() - 1) + 0.5))]); };
            s.p50Ms = at(.5); s.p99Ms = at(.99); s.maxMs = v.back();
            const double expected = expectedMs_;
            if (expected > 0) s.missedIntervals = uint64_t(std::count_if(intervals_.begin(), intervals_.end(),
                [&](float ms) { return ms > expected * 1.5; }));
        }
        s.classification = classify(s);
        s.hitches = std::move(hitches_);
        return s;
    }
};

inline std::string csvHeader() {
    return "chart,seconds,classification,vsync,target_fps,composition,direct_composition,hybrid,refresh_hz,width,height,"
           "covers_monitor,gpu_submits,gdi_frames,deferred,fallbacks,first_fallback_stage,first_fallback_hr,"
           "first_fallback_seconds,interval_p50_ms,interval_p99_ms,interval_max_ms,missed_intervals,"
           "hitch_cpu_paint,hitch_cpu_present,hitch_cpu_bga_rebuild,hitch_cpu_text_miss,hitch_cpu_upload,hitch_loop_stall,hitch_gpu_or_compositor,"
           "variant,run_id,video_nv12_offered,video_nv12_uploads,video_nv12_fail\n";
}

inline std::string quoted(const std::string& text) {
    std::string out = "\"";
    for (char c : text) { if (c == '"') out += '"'; out += c; }
    return out + '"';
}

inline std::string csvRow(const std::string& chartUtf8, const Summary& s) {
    const std::string name = quoted(chartUtf8);
    char buffer[768];
    const auto& c = s.config;
    std::snprintf(buffer, sizeof(buffer),
        ",%.2f,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%llu,%llu,%llu,%llu,%s,0x%08lX,%.2f,%.3f,%.3f,%.3f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
        s.seconds, s.classification.c_str(), c.vsync, c.targetFps, c.composition, c.directComposition, c.hybrid,
        c.refreshHz, c.width, c.height, c.coversMonitor,
        (unsigned long long)s.gpuSubmits, (unsigned long long)s.gdiFrames, (unsigned long long)s.deferred,
        (unsigned long long)s.fallbacks, s.firstFallbackStage.empty() ? "-" : s.firstFallbackStage.c_str(),
        (unsigned long)s.firstFallbackHr, s.firstFallbackSeconds, s.p50Ms, s.p99Ms, s.maxMs,
        (unsigned long long)s.missedIntervals,
        (unsigned long long)s.hitchCount("cpu-paint"), (unsigned long long)s.hitchCount("cpu-present"),
        (unsigned long long)s.hitchCount("cpu-bga-rebuild"), (unsigned long long)s.hitchCount("cpu-text-miss"),
        (unsigned long long)s.hitchCount("cpu-upload"), (unsigned long long)s.hitchCount("loop-stall"),
        (unsigned long long)s.hitchCount("gpu-or-compositor"));
    std::string row = name + buffer;
    row.pop_back(); // newline
    return row + ',' + quoted(c.variant) + ',' + std::to_string(s.runId) + ',' + std::to_string(s.videoNv12Offered) + ',' +
        std::to_string(s.videoNv12Uploads) + ',' + (s.videoNv12Fail.empty() ? std::string("-") : s.videoNv12Fail) + '\n';
}

inline std::string hitchHeader() {
    return "chart,at_seconds,interval_ms,cause,cpu_frames,max_paint_ms,max_present_ms,max_loop_gap_ms,upload_bytes,text_misses,bga_rebuilds,run_id\n";
}

inline std::string hitchRow(const std::string& chartUtf8, const Hitch& h, long long runId = 0) {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), ",%.3f,%.3f,%s,%llu,%.3f,%.3f,%.3f,%llu,%llu,%llu\n",
        h.atSeconds, h.intervalMs, h.cause, (unsigned long long)h.cpuFrames, h.maxPaintMs, h.maxPresentMs, h.maxLoopGapMs,
        (unsigned long long)h.uploadBytes, (unsigned long long)h.textMisses, (unsigned long long)h.bgaRebuilds);
    std::string row = quoted(chartUtf8) + buffer;
    row.pop_back(); // newline
    return row + ',' + std::to_string(runId) + '\n';
}

// Appends rows; a file written with a different header is moved to *.old.csv first.
inline bool appendCsv(const std::filesystem::path& file, const std::string& header, const std::string& rows) {
    std::error_code error;
    if (std::filesystem::exists(file, error)) {
        std::string first;
        { std::ifstream in(file, std::ios::binary); std::getline(in, first); }
        if (first + '\n' != header) {
            auto old = file; old.replace_extension(L".old.csv");
            std::filesystem::remove(old, error); std::filesystem::rename(file, old, error);
        }
    }
    const bool writeHeader = !std::filesystem::exists(file, error);
    std::ofstream out(file, std::ios::app | std::ios::binary);
    if (!out) return false;
    if (writeHeader) out << header;
    out << rows;
    return bool(out);
}

// Summary goes to `file`; per-hitch details to <stem>-hitches.csv beside it.
inline bool append(const std::filesystem::path& file, const std::string& chartUtf8, const Summary& s) {
    if (s.gpuSubmits + s.gdiFrames == 0) return true; // nothing was shown (tests, aborted starts)
    bool ok = appendCsv(file, csvHeader(), csvRow(chartUtf8, s));
    if (!s.hitches.empty()) {
        std::string rows;
        for (const auto& h : s.hitches) rows += hitchRow(chartUtf8, h, s.runId);
        auto hitchFile = file; hitchFile.replace_filename(file.stem().wstring() + L"-hitches.csv");
        ok = appendCsv(hitchFile, hitchHeader(), rows) && ok;
    }
    return ok;
}

} // namespace presentdiag
