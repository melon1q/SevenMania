// UI-only publication, readiness and diagnostics. No decoder runs in these functions.
void drawGameScreen(HDC);void drawPlaybackLoadingScreen(HDC);
void samplePlaybackLoadMemory(){
    PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);
    if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))){gPlaybackJob.peakWorkingSet=std::max(gPlaybackJob.peakWorkingSet,size_t(memory.WorkingSetSize));gPlaybackJob.peakPrivate=std::max(gPlaybackJob.peakPrivate,size_t(memory.PrivateUsage));}
}
void reportPlaybackLoad(const char* outcome){
    auto& j=gPlaybackJob;if(j.reported)return;j.reported=true;
    if(!GetEnvironmentVariableW(L"SEVENMANIA_LOAD_TRACE",nullptr,0))return;
    auto values=j.frames;std::sort(values.begin(),values.begin()+j.frameCount);
    const auto percentile=[&](double p){return j.frameCount?values[std::min(j.frameCount-1,size_t(std::ceil(j.frameCount*p)-1))]:0;};
    const auto time=[&](auto t){return t==PlaybackLoadJob::Clock::time_point{}?-1.0:PlaybackLoadJob::elapsed(j.selected,t);};
    wchar_t tracePath[32768]{};GetEnvironmentVariableW(L"SEVENMANIA_LOAD_TRACE",tracePath,32768);
    const auto file=std::wstring_view(tracePath)==L"1"?executableFolder()/L"SevenMania-loading.csv":std::filesystem::path(tracePath);const bool header=!std::filesystem::exists(file);std::ofstream out(file,std::ios::app);
    if(header)out<<"chart,generation,serial,outcome,preparing_ms,ready_ms,playing_ms,parse_ms,audio_ms,rate_ms,video_ms,image_ready_elapsed_ms,gpu_ms,frame_count,p95_ms,p99_ms,max_ms,peak_ws_bytes,peak_private_bytes,pcm_bytes,gpu_fallback,render_count,render_max_ms\n";
    auto name=j.path.filename().u8string();std::string escaped;for(auto c:name){if(c=='"')escaped+='"';escaped+=char(c);}out<<'"'<<escaped<<"\",";
    out<<j.generation<<','<<j.serial<<','<<outcome<<','<<time(j.preparing)<<','<<time(j.ready)<<','<<time(j.playing)<<','<<j.parseMs<<','<<j.audioMs<<','<<j.rateMs<<','<<j.videoMs<<','<<j.imageMs<<','<<j.gpuMs<<','<<j.frameCount<<','<<percentile(.95)<<','<<percentile(.99)<<','<<percentile(1)<<','<<j.peakWorkingSet<<','<<j.peakPrivate<<','<<j.pcmBytes<<','<<gGpuUnavailable<<','<<j.renders<<','<<j.renderMaxMs<<'\n';
}
void acceptPlaybackChart(std::uintptr_t generation){
    auto& j=gPlaybackJob;
    if(!j.active||generation!=j.generation||j.applied||gLoadCancelled||gPlaybackLoad.phase()!=bms::PlaybackLoadPhase::LoadingEssential)return;
    if(j.serial&&!j.complete())return;
    const auto chart=j.snapshot();if(!chart)return;
    const int index=gLoadResult.index;
    if(index<0||index>=int(gCharts.size())||chart->path!=j.path||gCharts[index].path!=j.path){gLoadCancelled=true;return;}
    try{
        gLoadResult.chart=*chart;gPlaybackLoad.resourcesLoaded();
        j.imageStarted=PlaybackLoadJob::Clock::now();
        if(!gPlaybackLoad.beginPreparation()||!prepareSelectedChartData(true)){gLoadCancelled=true;return;}
        j.assetWidth=gWindowWidth;j.assetHeight=gWindowHeight;gPlayPreview.reset();gPlayPreview.extract(gCharts[gActiveChartIndex]);j.applied=true;j.preparing=PlaybackLoadJob::Clock::now();
        samplePlaybackLoadMemory();InvalidateRect(gWindow,nullptr,FALSE);
    }catch(...){gLoadCancelled=true;gPlaybackLoad.fail();}
}
void completePlaybackWorker(std::uintptr_t generation){
    auto& j=gPlaybackJob;if(!j.active||generation!=j.generation||!j.complete())return;
    if(j.consumed&&!gLoadCancelled)return;
    // All expensive decoder work is over. join only retires the completed thread.
    if(!j.consumed&&gLoadWorker.joinable())gLoadWorker.join();j.consumed=true;
    if(!gLoadCancelled&&j.error.empty())acceptPlaybackChart(generation);
    if(!gLoadCancelled&&j.error.empty()&&j.applied){
        if(!j.partial.empty()){auto& chart=gCharts[gActiveChartIndex];chart.partialLoad=true;appendPartialLoadReason(chart,j.partial);rebuildPartialLoadSummary();}
        j.releaseChart();return;
    }
    const auto error=j.error;const bool cancelled=gLoadCancelled&&gPlaybackLoad.phase()!=bms::PlaybackLoadPhase::LoadFailed;
    if(cancelled&&gPlaybackLoad.phase()!=bms::PlaybackLoadPhase::LoadFailed)gPlaybackLoad.cancel();else gPlaybackLoad.fail();
    samplePlaybackLoadMemory();reportPlaybackLoad(cancelled?"cancelled":"failed");j.active=false;gPlayPreview.group.fill(false);std::vector<uint32_t>().swap(gPlayPreview.background);j.releaseChart();
    gLoading=false;showBrowseView();setStatus(cancelled?L"불러오기를 취소했습니다.":error.empty()?L"플레이 준비에 실패했습니다.":error);
}
void pollPreparingPlayback(bool automaticStart=true,bool notifyFailure=true){
    auto& j=gPlaybackJob;if(!j.active)return;
    if(!gLoadCancelled)acceptPlaybackChart(j.generation);
    if(j.complete())completePlaybackWorker(j.generation);
    if(!j.active||gLoadCancelled||!j.applied)return;
    if(j.assetWidth!=gWindowWidth||j.assetHeight!=gWindowHeight){
        j.gpuReady=false;
        if(!gBgaRaster.initialPrepared())return; // Retire old-size CPU jobs without joining.
        gBgaRaster.canvas(gWindowWidth,gWindowHeight);gBgaRaster.update(gPlayhead);
        j.assetWidth=gWindowWidth;j.assetHeight=gWindowHeight;j.imageStarted=PlaybackLoadJob::Clock::now();j.imageMs=0;
    }
    if(!j.gpuReady&&j.painted){
        const auto begin=PlaybackLoadJob::Clock::now();
#ifdef BMS_ENABLE_SKIN_BINDINGS
        prepareSkinForPlayback();
#endif
        j.gpuMs=PlaybackLoadJob::elapsed(begin);j.gpuReady=true;
    }
    const bool images=gBgaRaster.initialPrepared();
    if(images&&!j.imageMs)j.imageMs=PlaybackLoadJob::elapsed(j.imageStarted);
    if(j.consumed&&j.gpuReady&&j.painted&&images){
        if(gPlaybackLoad.phase()==bms::PlaybackLoadPhase::PreparingPlay&&gPlaybackLoad.ready()){j.ready=PlaybackLoadJob::Clock::now();samplePlaybackLoadMemory();}
        if(automaticStart&&!songEntryActive()&&gPlaybackLoad.canStart()&&(gPlayPreview.requested||PlaybackLoadJob::elapsed(j.ready)>=3000)){
            const bool started=startPreparedChart(notifyFailure);if(started)j.playing=PlaybackLoadJob::Clock::now();
            samplePlaybackLoadMemory();reportPlaybackLoad(started?"playing":"failed");j.active=false;gPlayPreview.group.fill(false);std::vector<uint32_t>().swap(gPlayPreview.background);
        }
    }
}
void drawPreparingPlayback(HDC dc){
    if(songEntryActive()){drawSongEntry(dc);return;}
    auto& j=gPlaybackJob;
    if(!gPlaybackLoad.preparing()||!j.applied){drawPlaybackLoadingScreen(dc);return;}
    const auto now=PlaybackLoadJob::Clock::now();
    if(j.lastFrame!=PlaybackLoadJob::Clock::time_point{}&&j.frameCount<j.frames.size())j.frames[j.frameCount++]=PlaybackLoadJob::elapsed(j.lastFrame,now);
    j.lastFrame=now;++j.frameTotal;
    // Reuse the current gear/notes renderer at a frozen playhead. Backdrop rendering
    // and skin browse-video code explicitly bypass worker-owned media while preparing.
    const std::array<double,6> signature{double(gWindowWidth),double(gWindowHeight),gNoteSpeed,double(gLaneCoverPercent),double(gJudgeLineOffset),double(gGearPosition)};
    bool animated=false;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    animated=preparingSkinAnimated();
#endif
    if(j.previewDirty||signature!=j.sceneSignature||(animated&&PlaybackLoadJob::elapsed(j.lastSceneRender)>=33)){
        const auto paint=PlaybackLoadJob::Clock::now();
#ifdef BMS_ENABLE_SKIN_BINDINGS
        if(!drawPreparingSkin(dc))
#endif
        drawGameScreen(dc);
        j.renderMaxMs=std::max(j.renderMaxMs,PlaybackLoadJob::elapsed(paint));++j.renders;
        j.lastSceneRender=now;j.sceneSignature=signature;j.previewDirty=false;
    }
    if(dc==gBackBufferDc&&gBackBufferPixels){
        const size_t count=size_t(gBackBufferWidth)*gBackBufferHeight;GdiFlush();
        if(gPlayPreview.background.size()!=count||gPlayPreview.revision!=j.renders){gPlayPreview.background.assign(static_cast<uint32_t*>(gBackBufferPixels),static_cast<uint32_t*>(gBackBufferPixels)+count);gPlayPreview.revision=j.renders;}
        else std::copy(gPlayPreview.background.begin(),gPlayPreview.background.end(),static_cast<uint32_t*>(gBackBufferPixels));
        drawPreviewNotes(dc);
    }
    j.painted=true;
    if(j.frameTotal%16==0)samplePlaybackLoadMemory();
}
