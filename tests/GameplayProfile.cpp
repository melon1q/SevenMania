// Opt-in diagnostic runner for the real application, audio transport and paint loop.
#include "../src/Main.cpp"
#include <iostream>
#include <fstream>
#include <psapi.h>

struct AudioEngineTestAccess {
    static uint64_t emptyPadding(AudioEngine& audio) { return audio.emptyPaddingEvents_.load(); }
    static uint64_t failures(AudioEngine& audio) { return audio.renderFailures_.load(); }
    static uint64_t cpuTicks(AudioEngine& audio) {
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!audio.worker_.joinable() || !GetThreadTimes(audio.worker_.native_handle(), &created, &exited, &kernel, &user)) return 0;
        const auto ticks = [](FILETIME time) { return (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
        return ticks(kernel) + ticks(user);
    }
};

namespace {
std::filesystem::path profileChart, profileCsv;
double profileStart = 30, profileDuration = 10;
int profileFps{};
bool profileGpu{}, profileStarted{}, profileLoading{}, profileReflex{}, profileQuitCheck{};
size_t profileNextNote{};
struct ReplayEvent {double time{};int lane{};bool down{};};
std::vector<ReplayEvent> replay;size_t replayCursor{};bool replayEnabled=true;
std::chrono::steady_clock::time_point profileBegan = std::chrono::steady_clock::now();
int profileResult = 1;
uint64_t audioCpuStart{};
uint64_t gpuUploadStart{}, gpuSubmitStart{};
uint64_t gpuSamplesStart{};
double gpuBusyStart{};
uint64_t audioEmptyStart{}, audioFailuresStart{};
void CALLBACK profileTick(HWND, UINT, UINT_PTR timer, DWORD) {
    if (!gWindow) return;
    if (std::chrono::steady_clock::now() - profileBegan > std::chrono::seconds(90)) {
        std::cerr << "Gameplay profile timed out\n"; KillTimer(nullptr, timer); DestroyWindow(gWindow); return;
    }
    if (!profileLoading && !gLoading && !gPlaying) {
        stopBrowseMedia();
        gCharts = {bms::BmsParser(false,0x20261024).parse(profileChart).chart};
        if (!gCharts[0].playable) { std::wcerr << gCharts[0].blockReason; KillTimer(nullptr, timer); DestroyWindow(gWindow); return; }
        buildSongGroups(); rebuildSongRows(0);
        gCompositionEnabled = profileGpu; gTargetFps = profileFps; gReflexRequested = profileReflex;
        if (profileQuitCheck) gDirectCompositionEnabled = false;
        gGrooveGaugeMode = GrooveGaugeMode::Normal;
        beginSelectedChart(); profileLoading = true; return;
    }
    if (!gPlaying) return;
    if (gPaused) resumePlayback();
    if (!profileStarted) {
        std::wstring error;
        if (!gAudio.startAt(profileStart, error)) { std::wcerr << error; KillTimer(nullptr, timer); DestroyWindow(gWindow); return; }
        gPlayhead = profileStart; gBgaEventIndex = 0; gBgaActiveImages.fill(L"");
        updateBgaState(gCharts[0], gPlayhead);
        const auto& notes = gCharts[0].notes;
        profileNextNote = static_cast<size_t>(std::lower_bound(notes.begin(), notes.end(), profileStart,
            [](const auto& note, double time) { return note.timeSeconds < time; }) - notes.begin());
        replay.clear();replayCursor=0;
        for(const auto& note:notes)if(note.timeSeconds>=profileStart && note.timeSeconds<profileStart+profileDuration) {
            replay.push_back({note.timeSeconds,note.lane,true});
            replay.push_back({note.timeSeconds+(note.longNote()?note.lengthSeconds:.025),note.lane,false});
        }
        std::stable_sort(replay.begin(),replay.end(),[](const auto&a,const auto&b){return a.time<b.time || (a.time==b.time && a.down<b.down);});
        gMissScanIndex = profileNextNote;
        std::fill_n(gHit.begin(), profileNextNote, uint8_t{1});
        BMS_PERF_SESSION(profileCsv); profileStarted = true;
        audioCpuStart = AudioEngineTestAccess::cpuTicks(gAudio);
        audioEmptyStart = AudioEngineTestAccess::emptyPadding(gAudio);
        audioFailuresStart = AudioEngineTestAccess::failures(gAudio);
        gpuUploadStart = gCompositionPresenter.uploadedBytes(); gpuSubmitStart = gCompositionPresenter.submittedFrames();
        gpuBusyStart = gCompositionPresenter.gpuMilliseconds();
        gpuSamplesStart = gCompositionPresenter.gpuSamples();
        std::wcout << L"Chart=" << gCharts[0].title << L" notes=" << notes.size() << L" mode=" << (profileGpu ? L"GPU" : L"CPU")
            << L" size=" << gWindowWidth << L"x" << gWindowHeight << L" buffer=" << gSoundBufferFrames << L" fps_setting=" << profileFps << L'\n';
    }
    const double position = gAudio.positionSeconds();
    // Post the exact same authored schedule through the application's message
    // pump and KEYDOWN/KEYUP handlers. Dispatch jitter is recorded, not hidden.
    while(replayEnabled && replayCursor<replay.size() && replay[replayCursor].time<=position) {
        const auto event=replay[replayCursor++];
        BMS_PERF_EVENT(event.down?10:11,event.lane,event.time);
        PostMessageW(gWindow,event.down?WM_KEYDOWN:WM_KEYUP,laneKey(event.lane),event.down?0:static_cast<LPARAM>(0xc0000000));
    }
    if (position >= profileStart + profileDuration) {
        std::cout << "reflex_active=" << (gCompositionPresenter.lowLatency().active() == latency::Mode::Reflex)
            << " driver_low_latency=" << gCompositionPresenter.lowLatency().enabledInDriver() << " marker_frame=" << gCompositionPresenter.lowLatency().frameId() << '\n';
        std::cout << "GPU fallback=" << gGpuUnavailable << " measured_fps=" << gMeasuredFps << '\n';
        std::cout << "audio_thread_cpu_ms=" << (AudioEngineTestAccess::cpuTicks(gAudio) - audioCpuStart) / 10000.0 << '\n';
        std::cout << "audio_empty_padding_events=" << AudioEngineTestAccess::emptyPadding(gAudio) - audioEmptyStart
            << " audio_render_failures=" << AudioEngineTestAccess::failures(gAudio) - audioFailuresStart << '\n';
        std::cout << "gpu_upload_MiB=" << (gCompositionPresenter.uploadedBytes() - gpuUploadStart) / (1024.0 * 1024)
            << " submitted_frames=" << gCompositionPresenter.submittedFrames() - gpuSubmitStart
            << " gpu_busy_ms=" << gCompositionPresenter.gpuMilliseconds() - gpuBusyStart
            << " gpu_samples=" << gCompositionPresenter.gpuSamples() - gpuSamplesStart << '\n';
        PROCESS_MEMORY_COUNTERS memory{};memory.cb=sizeof(memory);GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory));
        std::cout<<"peak_working_set_MiB="<<memory.PeakWorkingSetSize/(1024.0*1024)<<" trace_rows="<<perf::trace.rows.size()<<" trace_dropped="<<perf::trace.dropped<<" replay_events="<<replayCursor<<" score="<<gResultScore.value()<<'\n';
        perf::trace.save();
        profileResult = profileGpu && gGpuUnavailable ? 1 : 0;
        KillTimer(nullptr, timer);
        if (profileQuitCheck) {
            pausePlayback(); UpdateWindow(gWindow);
            const bool hadSurface = FindWindowExW(gWindow,nullptr,L"BmsGpuOutputSurface",nullptr) != nullptr;
            SendMessageW(gWindow,WM_COMMAND,ID_PAUSE_QUIT,0);
            DwmFlush();
            const bool gone = FindWindowExW(gWindow,nullptr,L"BmsGpuOutputSurface",nullptr) == nullptr;
            GdiFlush();
            const int width=gBackBufferWidth,height=gBackBufferHeight;
            bool browserVisible=false;
            // Inspect only the application's own rendered buffer; no desktop capture.
            for(int y=18;y<44 && !browserVisible;++y) for(int x=26;x<220;++x) {
                const uint32_t p=static_cast<uint32_t*>(gBackBufferPixels)[y*width+x];
                if ((p&0xffffffu)==0x7dd3fcu) { browserVisible=true; break; }
            }
            std::cout << "quit_had_gpu_surface=" << hadSurface << " quit_surface_removed=" << gone
                << " quit_browser_rendered=" << browserVisible << " quit_reflex_off=" << (gCompositionPresenter.lowLatency().active()==latency::Mode::Off) << '\n';
            if (!hadSurface || !gone || !browserVisible || gPlaying || gPaused) profileResult=1;
            BITMAPFILEHEADER header{}; header.bfType=0x4d42; header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER); header.bfSize=header.bfOffBits+width*height*4;
            BITMAPINFOHEADER bitmap{}; bitmap.biSize=sizeof(bitmap); bitmap.biWidth=width; bitmap.biHeight=-height; bitmap.biPlanes=1; bitmap.biBitCount=32;
            std::ofstream picture(profileCsv.parent_path()/L"quit-browser-preview.bmp",std::ios::binary);
            picture.write(reinterpret_cast<const char*>(&header),sizeof(header)); picture.write(reinterpret_cast<const char*>(&bitmap),sizeof(bitmap));
            picture.write(static_cast<const char*>(gBackBufferPixels),static_cast<std::streamsize>(width)*height*4);
        }
        DestroyWindow(gWindow);
    }
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) { std::wcerr << L"Usage: BmsGameplayProfile chart.bms trace.csv cpu|gpu [start_seconds] [duration_seconds] [fps: 0=unlimited,-1=vsync] [off|reflex] [quit-check]\n"; return 2; }
    profileChart = std::filesystem::absolute(argv[1]); profileCsv = std::filesystem::absolute(argv[2]);
    profileGpu = std::wstring(argv[3]) == L"gpu";
    if (argc > 4) profileStart = std::stod(argv[4]);
    if (argc > 5) profileDuration = std::stod(argv[5]);
    if (argc > 6) profileFps = std::stoi(argv[6]);
    if (argc > 7) profileReflex = std::wstring(argv[7]) == L"reflex";
    if (argc > 8) profileQuitCheck = std::wstring(argv[8]) == L"quit-check";
    if (std::find(frameRateValues.begin(), frameRateValues.end(), profileFps) == frameRateValues.end()) return 2;
    replayEnabled=argc<=9 || std::wstring(argv[9])!=L"idle";
    wchar_t boundary[8]{};perf::detailed=GetEnvironmentVariableW(L"BMS_STUDY_BOUNDARY_ONLY",boundary,8)==0;
    const auto timer = SetTimer(nullptr, 0, 10, profileTick);
    wWinMain(GetModuleHandleW(nullptr), nullptr, nullptr, SW_SHOWNORMAL);
    KillTimer(nullptr, timer);
    return profileResult;
}
