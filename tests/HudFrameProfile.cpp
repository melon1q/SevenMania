// Opt-in diagnostic runner for the real application, audio transport and paint loop.
#include "../src/Main.cpp"
#include <iostream>
#include <fstream>
#include <psapi.h>

struct AudioEngineTestAccess {
    static uint64_t cpuTicks(AudioEngine& audio) {
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!audio.worker_.joinable() || !GetThreadTimes(audio.worker_.native_handle(), &created, &exited, &kernel, &user)) return 0;
        const auto ticks = [](FILETIME time) { return (uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
        return ticks(kernel) + ticks(user);
    }
};

struct GpuTestAccess {
    static bool pixels(std::vector<uint32_t>& pixels) {
        if(!gCompositionPresenter.prepare(gWindow,gWindowWidth,gWindowHeight))return false;
        gGpuFrame=true;prepareAppliedSkinGpu();drawGameScreen(gBackBufferDc);GdiFlush();
        return gCompositionPresenter.drawFrame(gBackBufferPixels,gBackBufferWidth,nullptr,0,0,0)&&gCompositionPresenter.captureLastFrame(pixels,gWindowWidth,gWindowHeight);
    }
    static void latencyReport(const std::filesystem::path& path){
#ifndef BMS_DISABLE_NVIDIA_REFLEX
        std::ofstream file(path.wstring()+L".latency.csv");
        using Query=void*(__cdecl*)(unsigned);auto module=GetModuleHandleW(L"nvapi64.dll");auto query=module?reinterpret_cast<Query>(GetProcAddress(module,"nvapi_QueryInterface")):nullptr;
        decltype(&NvAPI_D3D_GetLatency) get{};if(query)for(const auto& entry:nvapi_interface_table)if(std::strcmp(entry.func,"NvAPI_D3D_GetLatency")==0)get=reinterpret_cast<decltype(get)>(query(entry.id));
        NV_LATENCY_RESULT_PARAMS data{};data.version=NV_LATENCY_RESULT_PARAMS_VER;
        const int status=get?get(gCompositionPresenter.device_.Get(),&data):NVAPI_NO_IMPLEMENTATION;
        file<<"status,"<<status<<"\nframe,input_to_gpu_end_ms,render_submit_ms\n";
        if(status==NVAPI_OK)for(const auto& r:data.frameReport)if(r.inputSampleTime&&r.gpuRenderEndTime>=r.inputSampleTime)file<<r.frameID<<','<<(r.gpuRenderEndTime-r.inputSampleTime)/1000.0<<','<<(r.renderSubmitEndTime-r.renderSubmitStartTime)/1000.0<<'\n';
#endif
    }
    static void adapter(const std::filesystem::path& path){
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC d{};
        if(SUCCEEDED(gCompositionPresenter.device_.As(&dxgi))&&SUCCEEDED(dxgi->GetAdapter(&adapter))&&SUCCEEDED(adapter->GetDesc(&d))){std::ofstream file(path.wstring()+L".adapter.txt");file<<"vendor_id="<<d.VendorId<<"\ndevice_id="<<d.DeviceId<<"\ndescription="<<settingsUtf8(d.Description)<<"\n";}
    }
};
namespace {
std::filesystem::path profileChart, profileCsv;
double profileStart = 30, profileDuration = 10;
int profileFps{};
std::filesystem::path profileSkin;int judgementMode{},comboMode{};bool probeDetailed=true;bool verifyPixels=false;bool focusProbe=false,focusPosted=false;
bool profileGpu{}, profileStarted{}, profileLoading{}, profileReflex{}, profileQuitCheck{};
size_t profileNextNote{};
struct ReplayEvent {double time{};int lane{};bool down{};};
std::vector<ReplayEvent> replay;size_t replayCursor{};bool replayEnabled=true;
std::chrono::steady_clock::time_point profileBegan = std::chrono::steady_clock::now();
int profileResult = 1;
uint64_t audioCpuStart{},processCpuStart{};
uint64_t processCpu(){FILETIME a{},b{},k{},u{};GetProcessTimes(GetCurrentProcess(),&a,&b,&k,&u);return (uint64_t(k.dwHighDateTime)<<32|k.dwLowDateTime)+(uint64_t(u.dwHighDateTime)<<32|u.dwLowDateTime);}
uint64_t gpuUploadStart{}, gpuSubmitStart{};
uint64_t gpuSamplesStart{};
double gpuBusyStart{};
uint64_t audioEmptyStart{}, audioFailuresStart{};
uint64_t framesCreatedStart{},framesReusedStart{},framesOverflowStart{},pixelAllocationsStart{},framesDestroyedStart{};
struct MemorySample {double wall{};SIZE_T privateBytes{},workingSet{};uint64_t frameBytes{},allocations{};};
std::array<MemorySample,128> memorySamples;size_t memoryCount{};
std::chrono::steady_clock::time_point nextMemorySample{};
void CALLBACK profileTick(HWND, UINT, UINT_PTR timer, DWORD) {
    if (!gWindow) return;
    if (std::chrono::steady_clock::now() - profileBegan > std::chrono::seconds(90)) {
        std::cerr << "Gameplay profile timed out\n"; KillTimer(nullptr, timer); DestroyWindow(gWindow); return;
    }
    if (!profileLoading && !gLoading && !gPlaying && !gStartupSplash) {
        stopBrowseMedia();
        gCharts = {bms::BmsParser(false,0x20261024).parse(profileChart).chart};
        if (!gCharts[0].playable) { std::wcerr << gCharts[0].blockReason; KillTimer(nullptr, timer); DestroyWindow(gWindow); return; }
        buildSongGroups(); rebuildSongRows(0);
        gCompositionEnabled = profileGpu; gTargetFps = profileFps; gReflexRequested = profileReflex;
        gDirectCompositionEnabled=true;gShowResourceUsage=false;gAccuracyLimit=AccuracyLimit::None;gVolumes={0,0,0,0};gAudio.setVolumes(0,0,0,0);
        gGrooveGaugeMode = GrooveGaugeMode::Normal;
        if(!applySkinFile(profileSkin,false)){std::cerr<<"Skin apply failed\n";KillTimer(nullptr,timer);DestroyWindow(gWindow);return;}
        gSkinJudgementMode=judgementMode;gSkinComboMode=comboMode;if(gSkinApplication.setHudModes(judgementMode,comboMode)){std::cerr<<"HUD mode failed\n";KillTimer(nullptr,timer);DestroyWindow(gWindow);return;}
        beginSelectedChart(); profileLoading = true; return;
    }
    if (!gPlaying) return;
    if (gPaused) {HUD_COUNT(AutoResumes);resumePlayback();}
    if (!profileStarted) {
        wchar_t bgaCase[32]{};GetEnvironmentVariableW(L"BMS_PROBE_BGA_CASE",bgaCase,32);
        if(std::wstring_view(bgaCase)==L"off")gBgaBrightness=0;
        if(std::wstring_view(bgaCase)==L"still"||std::wstring_view(bgaCase)==L"switch"){
            auto& chart=gCharts[0];auto movie=chart.fallbackVideo;
            if(movie.empty())for(const auto& [id,path]:chart.bitmaps)if(BgaVideoLayers::isVideo(path)){movie=path;break;}
            gVideoBga.reset();gBgaVideos.reset();gBgaRaster.reset();
            chart.fallbackVideo.clear();chart.bitmaps.clear();chart.bitmaps[L"zz"]=std::filesystem::absolute(L"Assets/startup-splash.png");
            chart.bga={{0,bms::BgaLayer::Base,L"zz"}};
            if(std::wstring_view(bgaCase)==L"switch"){
                if(movie.empty()){std::cerr<<"No movie for transition probe\n";DestroyWindow(gWindow);return;}
                chart.bitmaps[L"zy"]=movie;
                chart.bga.insert(chart.bga.end(),{{profileStart+4,bms::BgaLayer::Base,L"zy"},{profileStart+8,bms::BgaLayer::Base,L"zz"},{profileStart+12,bms::BgaLayer::Base,L"zy"}});
            }
            if(!std::filesystem::is_regular_file(chart.bitmaps.at(L"zz"))||!chart.bitmaps.contains(bms::assetId(chart,chart.bga.front().imageId))){std::cerr<<"Invalid still fixture resource\n";DestroyWindow(gWindow);return;}
            gBgaVideos.prepare(chart,gWindowWidth,gWindowHeight);gBgaBackplateDirty=true;
            if(std::wstring_view(bgaCase)==L"switch"&&!gBgaVideos.declared()){std::cerr<<"Transition fixture did not register video\n";DestroyWindow(gWindow);return;}
            std::cout<<"bga_fixture_valid=1\n";
        }
        std::cout<<"bga_case="<<settingsUtf8(bgaCase)<<" brightness="<<gBgaBrightness<<" declared_movies="<<gBgaVideos.declared()<<"\n";
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
        gJudgementText.clear();gJudgementUntil={};gResultScore.reset(int(bms::judgementNoteCount(gCharts[0])));gFeedback.reset();
        prepareSkinForPlayback();
        if(verifyPixels){
            gAudio.pause();bool ok=true;std::ofstream report(profileCsv.wstring()+L".pixels.txt");
            for(int position=0;position<3;++position){gGearPosition=position;
                for(int j=0;j<2;++j)for(int c=0;c<2;++c){
                    ok=ok&&!gSkinApplication.setHudModes(j,c);prepareSkinForPlayback();
                    std::vector<uint32_t> before,after;
                    hudprobe::recorder.forceLegacyIcons=true;
                    for(int warm=0;warm<3;++warm)ok=GpuTestAccess::pixels(before)&&ok;
                    hudprobe::recorder.forceLegacyIcons=false;
                    ok=GpuTestAccess::pixels(after)&&ok;
                    const double center=position==0?342:position==2?gWindowWidth-342:gWindowWidth/2.0;
                    const bool covered=gSkinApplication.coversLegacySlots(center-115,judgementLineY(gWindowHeight)+157,center+114,judgementLineY(gWindowHeight)+209);
                    size_t different=before.size()==after.size()?0:1;
                    if(before.size()==after.size())for(size_t i=0;i<before.size();++i)different+=before[i]!=after[i];
                    report<<"position="<<position<<" modes="<<j<<c<<" covered="<<covered<<" changed_pixels="<<different<<"\n";
                    ok=ok&&covered&&different==0;
                }
            }
            report<<"passed="<<ok<<"\n";profileResult=ok?0:1;KillTimer(nullptr,timer);DestroyWindow(gWindow);return;
        }
        GpuTestAccess::adapter(profileCsv);
        SetForegroundWindow(gWindow);SetFocus(gWindow);
        hudprobe::recorder.start(probeDetailed);profileStarted = true;
        framesCreatedStart=VideoFrameMetrics::created.load();framesReusedStart=VideoFrameMetrics::reused.load();framesOverflowStart=VideoFrameMetrics::overflow.load();pixelAllocationsStart=VideoFrameMetrics::pixelAllocations.load();framesDestroyedStart=VideoFrameMetrics::destroyed.load();
        nextMemorySample=std::chrono::steady_clock::now();
        processCpuStart=processCpu();
        audioCpuStart = AudioEngineTestAccess::cpuTicks(gAudio);
        gpuUploadStart = gCompositionPresenter.uploadedBytes(); gpuSubmitStart = gCompositionPresenter.submittedFrames();
        gpuBusyStart = gCompositionPresenter.gpuMilliseconds();
        gpuSamplesStart = gCompositionPresenter.gpuSamples();
        std::wcout << L"Chart=" << gCharts[0].title << L" notes=" << notes.size() << L" mode=" << (profileGpu ? L"GPU" : L"CPU")
            << L" size=" << gWindowWidth << L"x" << gWindowHeight << L" buffer=" << gSoundBufferFrames << L" fps_setting=" << profileFps << L'\n';
    }
    const double position = gAudio.positionSeconds();
    const auto sampleNow=std::chrono::steady_clock::now();
    if(sampleNow>=nextMemorySample&&memoryCount<memorySamples.size()){
        PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);
        if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m)))memorySamples[memoryCount++]={std::chrono::duration<double>(sampleNow-hudprobe::recorder.origin).count(),m.PrivateUsage,m.WorkingSetSize,VideoFrameMetrics::liveBytes.load(),VideoFrameMetrics::pixelAllocations.load()-pixelAllocationsStart};
        nextMemorySample=sampleNow+std::chrono::seconds(1);
    }
    if(focusProbe&&!focusPosted&&position>=profileStart+4){focusPosted=true;PostMessageW(gWindow,WM_KILLFOCUS,0,0);}
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
        std::cout << "gpu_upload_MiB=" << (gCompositionPresenter.uploadedBytes() - gpuUploadStart) / (1024.0 * 1024)
            << " submitted_frames=" << gCompositionPresenter.submittedFrames() - gpuSubmitStart
            << " gpu_timestamp_bracket_ms=" << gCompositionPresenter.gpuMilliseconds() - gpuBusyStart
            << " gpu_samples=" << gCompositionPresenter.gpuSamples() - gpuSamplesStart << '\n';
        std::cout<<"process_cpu_ms="<<(processCpu()-processCpuStart)/10000.0<<" wall_duration_s="<<profileDuration<<"\n";
        PROCESS_MEMORY_COUNTERS memory{};memory.cb=sizeof(memory);GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory));
        std::cout<<"peak_working_set_MiB="<<memory.PeakWorkingSetSize/(1024.0*1024)<<" trace_rows="<<hudprobe::recorder.size<<" trace_dropped="<<hudprobe::recorder.dropped<<" replay_events="<<replayCursor<<" score="<<gResultScore.value()<<'\n';
        hudprobe::recorder.enabled=false;gAudio.pause();
        hudprobe::recorder.save(profileCsv);GpuTestAccess::latencyReport(profileCsv);
        std::ofstream frameMemory(profileCsv.wstring()+L".memory.csv");frameMemory<<"wall_s,private_bytes,working_set_bytes,video_frame_capacity_bytes,pixel_allocations\n";
        for(size_t i=0;i<memoryCount;++i){const auto& m=memorySamples[i];frameMemory<<m.wall<<','<<m.privateBytes<<','<<m.workingSet<<','<<m.frameBytes<<','<<m.allocations<<'\n';}
        std::cout<<"video_frames_created="<<VideoFrameMetrics::created.load()-framesCreatedStart<<" reused="<<VideoFrameMetrics::reused.load()-framesReusedStart<<" overflow="<<VideoFrameMetrics::overflow.load()-framesOverflowStart<<" pixel_allocations="<<VideoFrameMetrics::pixelAllocations.load()-pixelAllocationsStart<<" destroyed="<<VideoFrameMetrics::destroyed.load()-framesDestroyedStart<<" live_frame_bytes="<<VideoFrameMetrics::liveBytes.load()<<" peak_frame_bytes="<<VideoFrameMetrics::peakBytes.load()<<'\n';
        const auto hud=gSkinApplication.gpuStats();std::ofstream metadata(profileCsv.wstring()+L".meta.json");
        metadata<<"{\"width\":"<<gWindowWidth<<",\"height\":"<<gWindowHeight<<",\"gpu_fallback\":"<<gGpuUnavailable<<",\"rows\":"<<hudprobe::recorder.size<<",\"dropped\":"<<hudprobe::recorder.dropped<<",\"atlas_count\":"<<hud.hudAtlases<<",\"font_glyphs\":"<<hud.hudFontGlyphs<<",\"judgement_mode\":"<<judgementMode<<",\"combo_mode\":"<<comboMode<<",\"score\":"<<gResultScore.value()<<",\"replay_events\":"<<replayCursor<<",\"submits\":"<<hudprobe::recorder.submitSize<<",\"gpu_samples\":"<<hudprobe::recorder.gpuSize<<"}";

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
    if(argc!=9&&argc!=10){std::cerr<<"Usage: HudFrameProfile chart skin output judgement-mode combo-mode duration detail fps\n";return 2;}
    profileChart=std::filesystem::absolute(argv[1]);profileSkin=std::filesystem::absolute(argv[2]);profileCsv=std::filesystem::absolute(argv[3]);
    judgementMode=std::stoi(argv[4]);comboMode=std::stoi(argv[5]);profileDuration=std::stod(argv[6]);probeDetailed=std::stoi(argv[7])!=0;profileFps=std::stoi(argv[8]);profileGpu=true;profileStart=30;
    hudprobe::recorder.forceLegacyIcons=argc==10&&std::wstring(argv[9])==L"legacy";
    verifyPixels=argc==10&&std::wstring(argv[9])==L"verify";
    profileReflex=argc==10&&std::wstring(argv[9]).starts_with(L"reflex");
    hudprobe::recorder.legacyReflex=argc==10&&std::wstring(argv[9]).find(L"before")!=std::wstring::npos;
    focusProbe=argc==10&&std::wstring(argv[9])==L"reflex-focus";
    VideoFrameMetrics::uiThread=GetCurrentThreadId();
    hudprobe::recorder.allocate();
    const auto timer = SetTimer(nullptr, 0, 10, profileTick);
    wWinMain(GetModuleHandleW(nullptr), nullptr, nullptr, SW_SHOWNORMAL);
    KillTimer(nullptr, timer);
    return profileResult;
}
