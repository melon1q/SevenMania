#include "CompositionPresenter.h"
#include "FramePacer.h"
#include <mmsystem.h>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
struct ProbeSession {
    HWND window{};
    bool com{}, timer{};
    ~ProbeSession() {
        if (window) DestroyWindow(window);
        if (timer) timeEndPeriod(1);
        if (com) CoUninitialize();
    }
};
}

// Measures accepted submissions with synthetic CPU work, not physical scanout
// or input latency. The inactive window never changes focus or saved settings.
int main(int argc, char** argv) {
    int fps{};
    try {
        if (argc != 3) throw std::invalid_argument("arguments");
        size_t used{};
        fps = std::stoi(argv[1], &used);
        if (used != std::string(argv[1]).size() || fps < -1 || fps > unlimitedFpsLimit ||
            (std::string(argv[2]) != "native" && std::string(argv[2]) != "dc"))
            throw std::invalid_argument("arguments");
    } catch (const std::exception&) {
        std::cerr << "Usage: BmsOutputCadenceProfile fps native|dc (0=unlimited, -1=VSync)\n";
        return 2;
    }
    ProbeSession session;
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    session.com = true;
    session.timer = timeBeginPeriod(1) == TIMERR_NOERROR;
    session.window = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"Output cadence check",
        WS_OVERLAPPEDWINDOW, 20, 20, 656, 399, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!session.window) return 1;
    ShowWindow(session.window, SW_SHOWNOACTIVATE); UpdateWindow(session.window);
    const bool composition = std::string(argv[2]) == "dc";
    CompositionPresenter renderer;
    renderer.setDirectComposition(composition); renderer.setVsync(fps == vsyncFps);
    if (!renderer.prepare(session.window, 640, 360)) return 1;
    std::vector<uint32_t> background(640 * 360, 0xff182430), cpu(background);
    const std::vector<RECT> regions{{0, 0, 224, 360}};
    FramePacer pacer;
    pacer.reset(fps);
    size_t rendered = 0;
    uint64_t previous = renderer.submittedFrames();
    const auto start = std::chrono::steady_clock::now();
    auto last = start;
    std::vector<double> gaps;
    const auto sample = [&] {
        const auto submitted = renderer.submittedFrames();
        if (submitted == previous) return;
        const auto now = std::chrono::steady_clock::now();
        if (previous) gaps.push_back(std::chrono::duration<double, std::milli>(now - last).count());
        last = now; previous = submitted;
    };
    bool wake = false;
    MSG message{};
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(4)) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (wake) {
            wake = false;
            if (!pacer.due() && renderer.hasPendingFrame()) {
                if (!renderer.present(session.window, cpu.data(), 640, 360)) return 1;
                sample();
            }
        }
        if (pacer.due()) {
            if (!renderer.prepare(session.window, 640, 360)) return 1;
            renderer.hybridBackground(background.data(), 1, 0, {0, 0, 224, 360}, 0, regions);
            renderer.rectangle(10, static_cast<float>(rendered % 330), 200,
                static_cast<float>(rendered % 330 + 12), RGB(0, 160, 255));
            const auto paintEnd = std::chrono::steady_clock::now() + std::chrono::microseconds(500);
            while (std::chrono::steady_clock::now() < paintEnd) YieldProcessor();
            if (!renderer.present(session.window, cpu.data(), 640, 360)) return 1;
            ++rendered; sample(); pacer.advance();
            continue;
        }
        const HANDLE output = renderer.presentationWaitHandle();
        if (pacer.pollBeforeDeadline()) {
            if (output && WaitForSingleObject(output, 0) == WAIT_OBJECT_0) wake = renderer.acceptPresentationWake(output);
            else YieldProcessor();
            continue;
        }
        const HANDLE timer = pacer.arm();
        HANDLE handles[2]{};
        DWORD count = 0;
        if (timer) handles[count++] = timer;
        const DWORD outputIndex = count;
        if (output) handles[count++] = output;
        const DWORD result = MsgWaitForMultipleObjectsEx(count, handles, timer ? 1000 : 1, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (output && result == WAIT_OBJECT_0 + outputIndex) wake = renderer.acceptPresentationWake(output);
    }
    std::sort(gaps.begin(), gaps.end());
    const auto percentile = [&](double p) {
        return gaps.empty() ? 0 : gaps[std::min(gaps.size() - 1, static_cast<size_t>(p * gaps.size()))];
    };
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "Submission intervals only; 0.5 ms synthetic CPU painting, four seconds\n"
        << "setting=" << fps << " backend=" << (composition ? "dc" : "native")
        << " render_fps=" << rendered / elapsed << " accepted_fps=" << previous / elapsed
        << " gap_p50=" << percentile(.5) << " gap_p95=" << percentile(.95)
        << " gap_p99=" << percentile(.99) << " max=" << percentile(1) << " ms\n";
    return 0;
}
