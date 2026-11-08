#include <algorithm>
#include "PresentationDiagnostics.h"
#include <iostream>
#include <stdexcept>
static void check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
using Clock = std::chrono::steady_clock;
using std::chrono::microseconds;
static presentdiag::Summary run(presentdiag::Config config, int frames, int periodUs, int missEvery, bool gdiAt = false) {
    presentdiag::Session session; auto t = Clock::time_point{} + std::chrono::seconds(1);
    session.begin(config, 10, t);
    for (int i = 0; i < frames; ++i) {
        t += microseconds(missEvery && i && i % missEvery == 0 ? periodUs * 2 : periodUs);
        session.submitted(true, t);
    }
    if (gdiAt) { session.fallback("present", 0x887A0005L, t); session.submitted(false, t); }
    return session.finish(25, t);
}
int main() { try {
    presentdiag::Config vsync{}; vsync.vsync = true; vsync.refreshHz = 144;
    auto clean = run(vsync, 2000, 6944, 0);
    check(clean.classification == "gpu-vsync-clean", "steady vsync must be clean");
    check(clean.missedIntervals == 0 && clean.deferred == 15, "deferred delta or misses wrong");
    check(clean.p50Ms > 6.9 && clean.p50Ms < 7.0, "p50 interval wrong");
    auto missed = run(vsync, 2000, 6944, 20);
    check(missed.classification == "vsync-frame-miss" && missed.missedIntervals == 99, "vsync misses not detected");
    auto gdi = run(vsync, 100, 6944, 0, true);
    check(gdi.classification == "gdi-fallback" && gdi.firstFallbackStage == "present" && gdi.fallbacks == 1, "gdi fallback not classified");
    presentdiag::Config capped{}; capped.targetFps = 500; capped.refreshHz = 144;
    auto unsynced = run(capped, 2000, 2000, 0);
    check(unsynced.classification == "unsynced-flip", "fps cap faster than refresh must not count as misses");
    presentdiag::Session paused; auto t = Clock::time_point{} + std::chrono::seconds(1); paused.begin(vsync, 0, t);
    for (int i = 0; i < 100; ++i) { t += microseconds(6944); paused.submitted(true, t); }
    paused.gap(); t += std::chrono::seconds(5);
    for (int i = 0; i < 100; ++i) { t += microseconds(6944); paused.submitted(true, t); }
    check(paused.finish(0, t).maxMs < 8, "pause gap counted as a frame interval");
    check(missed.hitches.size() == 99 && missed.hitchCount("gpu-or-compositor") == 99, "hitches without CPU work must be gpu-or-compositor");
    {   // Attribute hitches to the CPU work recorded since the previous submission.
        presentdiag::Config c = vsync; c.width = 100; c.height = 100;
        presentdiag::Session s; auto now = Clock::time_point{} + std::chrono::seconds(1); s.begin(c, 0, now);
        const auto step = [&](int us, presentdiag::FrameWork w) { s.work(w); now += microseconds(us); s.submitted(true, now); };
        step(6944, {});
        step(6944, {});
        presentdiag::FrameWork bga; bga.paintMs = 12; bga.bgaRebuilt = true; step(13888, bga);
        presentdiag::FrameWork text; text.paintMs = 9; text.textMisses = 3; step(13888, text);
        presentdiag::FrameWork upload; upload.presentMs = 8; upload.uploadBytes = 40000; step(13888, upload);
        presentdiag::FrameWork present; present.presentMs = 8; step(13888, present);
        presentdiag::FrameWork stall; stall.paintMs = 1; stall.loopGapMs = 12; step(13888, stall);
        presentdiag::FrameWork cheap; cheap.paintMs = 1; step(6944, cheap);
        const auto r = s.finish(0, now);
        check(r.hitches.size() == 5, "hitch count wrong");
        check(r.hitchCount("cpu-bga-rebuild") == 1 && r.hitchCount("cpu-text-miss") == 1 && r.hitchCount("cpu-upload") == 1 &&
            r.hitchCount("cpu-present") == 1 && r.hitchCount("loop-stall") == 1, "hitch causes misattributed");
        check(r.hitches[0].bgaRebuilds == 1 && r.hitches[0].cpuFrames == 1, "pending work not reset per submission");
        const auto line = presentdiag::hitchRow("x.bms", r.hitches[0]);
        check(line.find(",cpu-bga-rebuild,") != std::string::npos, "hitch row malformed");
    }
    {   // A play that never showed a frame (tests, aborted starts) writes nothing.
        const auto file = std::filesystem::temp_directory_path() / L"sevenmania-presentdiag-empty-test.csv";
        std::error_code error; std::filesystem::remove(file, error);
        check(presentdiag::append(file, "empty.bms", presentdiag::Summary{}) && !std::filesystem::exists(file), "empty play was recorded");
    }
    {   // Aligned pacing wakes once per refresh: a 4.17 ms loop gap is not a stall.
        presentdiag::Hitch h; h.maxPaintMs = 0.3; h.maxLoopGapMs = 4.17;
        check(std::string_view(presentdiag::hitchCause(h, 4.1667, 0)) == "loop-stall", "raw classifier changed");
        check(std::string_view(presentdiag::hitchCause(h, 4.1667, 0, 4.1667 * 1.4)) == "gpu-or-compositor", "aligned gap misread as stall");
        h.maxLoopGapMs = 7.0;
        check(std::string_view(presentdiag::hitchCause(h, 4.1667, 0, 4.1667 * 1.4)) == "loop-stall", "real aligned stall missed");
        presentdiag::Config c; c.vsync = true; c.refreshHz = 240; c.vblankAligned = true;
        presentdiag::Session s; auto t = Clock::time_point{} + std::chrono::seconds(1); s.begin(c, 0, t);
        presentdiag::FrameWork w; w.paintMs = 0.3; w.loopGapMs = 4.17;
        s.work({}); t += microseconds(4167); s.submitted(true, t);
        s.work(w); t += microseconds(8334); s.submitted(true, t);
        check(s.finish(0, t).hitchCount("gpu-or-compositor") == 1, "session ignored aligned classifier");
    }
    const auto row = presentdiag::csvRow("a\"b.bms", gdi);
    {   presentdiag::Summary labeled = gdi; labeled.config.variant = "B"; labeled.runId = 42;
        const auto r = presentdiag::csvRow("x.bms", labeled);
        check(r.find(",\"B\",42,0,0,-\n") != std::string::npos, "variant/run_id/video columns missing");
        presentdiag::Hitch h; h.cause = "loop-stall";
        const auto hr = presentdiag::hitchRow("x.bms", h, 42);
        check(hr.size() > 4 && hr.compare(hr.size() - 4, 4, ",42\n") == 0, "hitch run_id missing");
        check(std::count(hr.begin(), hr.end(), ',') == 11, "hitch column count wrong");
        const auto header = presentdiag::csvHeader();
        check(std::count(header.begin(), header.end(), ',') == std::count(r.begin(), r.end(), ','), "column count mismatch");
    }
    check(row.rfind("\"a\"\"b.bms\",", 0) == 0 && row.find("gdi-fallback") != std::string::npos && row.back() == '\n', "csv row malformed");
    std::cout << "presentation diagnostics tests passed\n"; return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
