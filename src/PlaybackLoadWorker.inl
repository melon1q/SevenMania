// Existing decoder sequence, now publishing an immutable chart before media preparation.
void beginPlaybackLoadWorker(const std::filesystem::path& path,int width,int height,PracticeMode practiceSettings) {
    gPlaybackJob.begin(path);gPlaybackJob.selected=gPlaybackLoadStarted;const auto generation=gPlaybackJob.generation;
    gLoadWorker=std::thread([path,width,height,practiceSettings,generation]{
        const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        try {
            auto begin=PlaybackLoadJob::Clock::now();
#ifdef BMS_PERF_DIAGNOSTICS
            auto parsed=bms::BmsParser(false,0x20261024).parse(path).chart;
#else
            auto parsed=bms::BmsParser().parse(path).chart;
#endif
            gPlaybackJob.parseMs=PlaybackLoadJob::elapsed(begin);
            if(!bms::gameplayModeAllowed(parsed))gPlaybackJob.error=bms::gameplayModeBlockReason(parsed);
            else if(!parsed.playable)gPlaybackJob.error=parsed.blockReason;
            else if(!gLoadCancelled){
                const auto chart=std::make_shared<const bms::Chart>(std::move(parsed));gPlaybackJob.publish(chart);
                PostMessageW(gWindow,WM_PLAY_CHART_READY,generation,0);
                begin=PlaybackLoadJob::Clock::now();
                gBgaVideos.prepare(*chart,width,height,&gLoadCancelled);
                if(gBgaVideos.failures())gPlaybackJob.partial+=L"디코딩할 수 없는 BGA 영상 "+std::to_wstring(gBgaVideos.failures())+L"개 ";
                if(!gBgaVideos.declared()&&!chart->fallbackVideo.empty())gVideoBga.open(chart->fallbackVideo,width,height,true,chart->videoFps);
                gPlaybackJob.videoMs=PlaybackLoadJob::elapsed(begin);
                size_t failures{};begin=PlaybackLoadJob::Clock::now();
                const bool audio=gAudio.prepare(*chart,gPlaybackJob.error,&gLoadCancelled,true,&failures);
                gPlaybackJob.audioMs=PlaybackLoadJob::elapsed(begin);
                if(!audio&&gPlaybackJob.error.empty()&&!gLoadCancelled)gPlaybackJob.error=L"키음 준비에 실패했습니다.";
                if(failures)gPlaybackJob.partial+=L"디코딩할 수 없는 음원 "+std::to_wstring(failures)+L"개 ";
                begin=PlaybackLoadJob::Clock::now();
                if(audio&&gPlaybackJob.error.empty()&&!gLoadCancelled&&!gAudio.preparePlaybackRate((practiceSettings.enabled?practiceSettings.rate():1.0)*bms::forcedOptions(*chart).rate,gPlaybackJob.error,&gLoadCancelled)&&gPlaybackJob.error.empty()&&!gLoadCancelled)gPlaybackJob.error=L"재생속도 준비에 실패했습니다.";
                gPlaybackJob.rateMs=PlaybackLoadJob::elapsed(begin);
                begin=PlaybackLoadJob::Clock::now();
                if(gPlaybackJob.error.empty()&&!gLoadCancelled&&!chart->fallbackVideo.empty()){
                    if(practiceSettings.enabled)gVideoBga.request(std::max(0.0,practiceSettings.startSeconds-bms::bgaVideoStartSeconds(*chart)),width,height);
                    if(!gBgaVideos.declared()&&!gVideoBga.waitUntilPrepared(&gLoadCancelled))gPlaybackJob.partial+=L"시작 영상 준비 실패 ";
                }
                gPlaybackJob.videoMs+=PlaybackLoadJob::elapsed(begin);
                gPlaybackJob.pcmBytes=gAudio.decodedBytes();
            }
        }catch(...){gPlaybackJob.error=L"채보나 음원 준비 중 오류가 발생했습니다.";}
        if(SUCCEEDED(com))CoUninitialize();
        gPlaybackJob.done.store(true,std::memory_order_release);
        PostMessageW(gWindow,WM_LOAD_FINISHED,generation,1);
    });
}
