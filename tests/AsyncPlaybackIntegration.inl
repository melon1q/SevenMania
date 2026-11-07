// Integration through the real worker/publication/readiness/start/rendering functions.
// check is supplied by the host test; all files and settings belong to its temporary root.
template<class Check> void asyncPlaybackIntegration(const std::filesystem::path& root,Check check,bool extended){
    MFStartup(MF_VERSION);const bool ownsGdi=!gGdiPlusToken;Gdiplus::GdiplusStartupInput input;if(ownsGdi)Gdiplus::GdiplusStartup(&gGdiPlusToken,&input,nullptr);
    const auto tone=root/L"async-tone.wav";
    {std::ofstream out(tone,std::ios::binary);const auto put=[&](auto x){out.write(reinterpret_cast<const char*>(&x),sizeof(x));};
    out.write("RIFF",4);put(uint32_t(36+44100*4));out.write("WAVEfmt ",8);put(uint32_t(16));put(uint16_t(1));put(uint16_t(2));put(uint32_t(44100));put(uint32_t(44100*4));put(uint16_t(4));put(uint16_t(16));out.write("data",4);put(uint32_t(44100*4));for(int i=0;i<44100;++i){put(int16_t(i%80<40?4000:-4000));put(int16_t(i%80<40?4000:-4000));}}
    const std::string prefix="#PLAYER 1\n#BPM 120\n#WAV01 async-tone.wav\n#00011:01\n#00118:01\n";
    const auto write=[&](const wchar_t* name,const std::string& text){const auto file=root/name;std::ofstream out(file,std::ios::binary);out<<text;return file;};
    const auto normal=write(L"async-normal.bms",prefix);
    std::vector<std::filesystem::path> cases{normal};
    if(extended){
        cases.push_back(write(L"async-image.bms",prefix+"#BMP01 base.bmp\n#00004:01\n"));
        cases.push_back(write(L"async-video.bms",prefix+"#BMP01 movie.avi\n#00004:01\n"));
        std::string many=prefix;const char* digits="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        for(int i=2;i<=96;++i){const auto file="async-key-"+std::to_string(i)+".wav";std::filesystem::copy_file(tone,root/file,std::filesystem::copy_options::overwrite_existing);many+="#WAV"+std::string{digits[i/36],digits[i%36]}+" "+file+"\n";}
        many+="#00211:";for(int i=1;i<=96;++i)many+=std::string{digits[i/36],digits[i%36]};many+='\n';
        cases.push_back(write(L"async-many.bms",many));
        std::string dense="#PLAYER 1\n#BPM 120\n#WAV01 async-tone.wav\n#00118:01\n";
        for(int m=0;m<1000;++m){char line[16];std::snprintf(line,sizeof(line),"#%03d11:",m);dense+=line;for(int n=0;n<60;++n)dense+="01";dense+='\n';}
        cases.push_back(write(L"async-dense.bms",dense));
    }
    const auto oldWindow=gWindow;gWindow=CreateWindowW(L"STATIC",L"Async preparation test",WS_OVERLAPPEDWINDOW,0,0,1280,720,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    RECT area{};GetClientRect(gWindow,&area);gWindowWidth=area.right;gWindowHeight=area.bottom;const auto dc=GetDC(gWindow);ensureBackBuffer(dc,gWindowWidth,gWindowHeight);
    const auto log=root/L"async-load-measurements.csv";DeleteFileW(log.c_str());SetEnvironmentVariableW(L"SEVENMANIA_LOAD_TRACE",log.c_str());
    const auto run=[&](const std::filesystem::path& path,bool serial,int cancelStage){
        if(serial)SetEnvironmentVariableW(L"SEVENMANIA_LOAD_SERIAL",L"1");else SetEnvironmentVariableW(L"SEVENMANIA_LOAD_SERIAL",nullptr);
        gAudio.releaseSong();gBgaRaster.reset();gBgaVideos.reset();gVideoBga.reset();gPlaybackJob.active=false;
        gCharts={};bms::Chart catalog;catalog.path=path;catalog.mode=bms::ChartMode::SevenKey;catalog.playable=true;gCharts.push_back(catalog);
        gLoadResult={};gLoadResult.index=0;gLoadResult.playback=true;gLoadCancelled=false;gPlaying=gPaused=gShowingResult=false;gLoading=true;gGpuUnavailable=false;gCompositionEnabled=true;gPractice.enabled=false;gStartupSplash=false;
        gPlaybackLoad.begin();gPlaybackLoadStarted=PlaybackLoadJob::Clock::now();const auto stale=gPlaybackJob.generation;beginPlaybackLoadWorker(path,gWindowWidth,gWindowHeight,gPractice);
        beginSongEntry(gCharts[0]);check(songEntryActive(),"selection intro not immediate");check(!gPlaying&&!gAudio.isRunning(),"intro started audio");
        acceptPlaybackChart(stale);completePlaybackWorker(stale);check(!gPlaybackJob.applied&&!gPlaybackJob.consumed,"stale generation accepted");
        if(cancelStage==1)windowProc(gWindow,WM_KEYDOWN,VK_ESCAPE,0);
        bool observedOverlap=false,observedPreparation=false,captured=false;
        const auto deadline=PlaybackLoadJob::Clock::now()+std::chrono::seconds(25);
        while(gLoading&&PlaybackLoadJob::Clock::now()<deadline){
            const bool introBefore=songEntryActive();pollPreparingPlayback(false,false);
            if(introBefore){check(!gPlaying&&!startPreparedChart(false),"intro allowed premature start");drawPreparingPlayback(gBackBufferDc);}
            if(gPlaybackLoad.preparing()){
                observedPreparation=true;observedOverlap|=!gPlaybackJob.complete();
                const auto frozen=gPlayhead;const auto score=gResultScore.value();drawPreparingPlayback(gBackBufferDc);
                if(!serial&&!cancelStage&&!captured&&gPlaybackJob.renders==1){captured=true;
                    GdiFlush();const auto file=root/L"async-preparing.bmp";std::ofstream out(file,std::ios::binary);BITMAPFILEHEADER f{};f.bfType=0x4d42;f.bfOffBits=sizeof(f)+sizeof(BITMAPINFOHEADER);f.bfSize=f.bfOffBits+gBackBufferWidth*gBackBufferHeight*4;BITMAPINFOHEADER h{};h.biSize=sizeof(h);h.biWidth=gBackBufferWidth;h.biHeight=-gBackBufferHeight;h.biPlanes=1;h.biBitCount=32;out.write(reinterpret_cast<const char*>(&f),sizeof(f));out.write(reinterpret_cast<const char*>(&h),sizeof(h));out.write(reinterpret_cast<const char*>(gBackBufferPixels),gBackBufferWidth*gBackBufferHeight*4);
                }
                updatePlaybackFrame();pressLane(1);
                check(!gPlaying&&!gAudio.isRunning()&&gPlayhead==frozen&&gResultScore.value()==score,"async preparation advanced game state");
                if(cancelStage==2)windowProc(gWindow,WM_KEYDOWN,VK_ESCAPE,0);
            }
            if(gPlaybackLoad.canStart()&&!cancelStage){if(gPreviewStartMode==2&&PlaybackLoadJob::elapsed(gPlaybackJob.ready)>50)gPlayPreview.requested=true;pollPreparingPlayback(true,false);}
            if(gPlaying)break;Sleep(2);
        }
        check(!gLoading,"async load timed out");
        if(cancelStage<0)check(!gPlaying&&gPlaybackLoad.phase()==bms::PlaybackLoadPhase::LoadFailed,"failed worker remained pending");
        else if(cancelStage)check(!gPlaying&&gPlaybackLoad.phase()==bms::PlaybackLoadPhase::Cancelled,"async cancellation failed");
        else{
            if(path.filename()==L"async-many.bms"){check(gPlaybackJob.pcmBytes>10*1024*1024,"many-key fixture did not decode unique PCM");if(!serial)check(observedOverlap,"many-key loading did not overlap preparation");}
            check(observedPreparation&&gPlaying,"async auto start failed");
            check(gPlaybackJob.consumed&&gPlaybackJob.gpuReady&&gBgaRaster.initialPrepared(),"started before barrier");
            if(extended&&path==normal&&!serial){const auto generation=gPlaybackJob.generation;restartPlayback();check(gPlaying&&gAudio.isRunning()&&gMissScanIndex==0&&gPlaybackJob.generation==generation,"same-song restart failed");}
            gAudio.stop();gPlaying=false;gPlaybackLoad.reset();
        }
        std::cout<<"AsyncLoad "<<path.filename().string()<<" serial="<<serial<<" cancel="<<cancelStage<<" overlap="<<observedOverlap<<" preparing_ms="<<(gPlaybackJob.preparing==PlaybackLoadJob::Clock::time_point{}?-1:PlaybackLoadJob::elapsed(gPlaybackJob.selected,gPlaybackJob.preparing))<<"\n";
    };
    for(const auto& path:cases){run(path,false,0);if(extended)run(path,true,0);}
    if(extended){gPreviewStartMode=1;run(normal,false,0);gPreviewStartMode=2;run(normal,false,0);gPreviewStartMode=0;for(int repeat=0;repeat<2;++repeat){run(cases[3],true,0);run(cases[3],false,0);}
        const auto invalid=write(L"async-invalid.bms","#PLAYER 2\n#BPM 120\n#00121:01\n");run(invalid,false,-1);run(cases[3],false,1);run(normal,false,0);run(cases[3],false,2);run(normal,false,0);run(normal,false,0);
        // Same ownership order as WM_DESTROY: cancel, join, then free media.
        gLoadResult.index=0;gCharts[0].path=cases[3];gLoadCancelled=false;gLoading=true;gPlaybackLoad.begin();gPlaybackLoadStarted=PlaybackLoadJob::Clock::now();beginPlaybackLoadWorker(cases[3],1280,720,gPractice);gLoadCancelled=true;if(gLoadWorker.joinable())gLoadWorker.join();check(gPlaybackJob.complete(),"shutdown worker not retired");}
    gLoadCancelled=false;gLoading=false;gPlaying=false;gPlaybackLoad.reset();gPlaybackJob.active=false;gPlaybackJob.releaseChart();gAudio.releaseSong();gBgaRaster.reset();gBgaVideos.reset();gVideoBga.reset();gCharts.clear();gActiveChartIndex=-1;gCompositionPresenter.reset();
    ReleaseDC(gWindow,dc);DestroyWindow(gWindow);gWindow=oldWindow;SetEnvironmentVariableW(L"SEVENMANIA_LOAD_TRACE",nullptr);SetEnvironmentVariableW(L"SEVENMANIA_LOAD_SERIAL",nullptr);
    if(ownsGdi){Gdiplus::GdiplusShutdown(gGdiPlusToken);gGdiPlusToken=0;}MFShutdown();
}
