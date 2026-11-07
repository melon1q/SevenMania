#include "../src/Main.cpp"
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct SongPreviewTestAccess {
    static const AudioEngine* audio(const SongPreview& preview){return preview.active_?preview.active_->audio.get():nullptr;}
};
struct AudioEngineTestAccess {
    static bool has(const AudioEngine& audio,const std::wstring& id){return audio.samples_.contains(id);}
    static void beep() {
        AudioEngine audio;audio.loadDiagnosticBeep();audio.setVolumes(100,100,100,100);audio.running_=true;
        check(audio.triggerEffect(L"fps-drop"),"diagnostic beep was not queued");
        const auto count=audio.sampleRate()/10;audio.mixBuffer_.resize(count*2);std::vector<int16_t> pcm(count*2);
        audio.fillBuffer(pcm.data(),count);audio.running_=false;
        check(std::any_of(pcm.begin(),pcm.begin()+count,[](auto v){return v!=0;}),"beep did not reach mixer");
        check(std::all_of(pcm.begin()+count,pcm.end(),[](auto v){return v==0;}),"beep did not stop within 50ms");
    }
};
static void screenshot(const std::filesystem::path& path) {
    GdiFlush();BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+gWindowWidth*gWindowHeight*4;
    BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=gWindowWidth;info.biHeight=-gWindowHeight;info.biPlanes=1;info.biBitCount=32;
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<char*>(&file),sizeof(file));out.write(reinterpret_cast<char*>(&info),sizeof(info));out.write(static_cast<char*>(gBackBufferPixels),gWindowWidth*gWindowHeight*4);
}
int wmain(int argc,wchar_t** argv) {
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);MFStartup(MF_VERSION);
    Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&gGdiPlusToken,&input,nullptr);int status=0;
    try {
        const auto output=argc>1?std::filesystem::path(argv[1]):std::filesystem::path(L"Validation/Diagnostics");std::filesystem::create_directories(output);
        AudioEngineTestAccess::beep();
        if(argc>2) {
            auto parsed=bms::BmsParser(false,123,false).parse(argv[2]);const auto& chart=parsed.chart;
            check(chart.playable && chart.title==L"ha＠ppy s＠mmer v＠c＠tion!![Normal]","actual song title decoded incorrectly");
            check(chart.missingSampleFiles.empty() && chart.silentSampleIds.empty(),"actual song lost keysound references");
            SongPreview preview;preview.select(chart);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
            while(!preview.cached(chart.path) && std::chrono::steady_clock::now()<deadline)Sleep(10);
            check(preview.cached(chart.path),"actual preview did not prepare");preview.tick(0,0,256);check(preview.playing(),"actual preview failed to start");
            const auto* audio=SongPreviewTestAccess::audio(preview);std::set<std::wstring> used;
            const double end=chart.previewStartSeconds+chart.previewDurationSeconds;
            for(const auto& e:chart.bgm)if(e.timeSeconds<end)used.insert(e.sampleId);
            for(const auto& n:chart.notes){if(n.timeSeconds<end)used.insert(n.sampleId);if(n.longNote() && n.timeSeconds+n.lengthSeconds<end)used.insert(n.endSampleId);}
            used.erase(L"");for(const auto& id:used)check(audio && AudioEngineTestAccess::has(*audio,id),"actual preview silently omitted a keysound");
            std::cout<<"Actual song: "<<chart.samples.size()<<" resolved samples, "<<used.size()<<" preview samples decoded; missing=0, silent=0\n";
            preview.shutdown();
        }
        gWindow=CreateWindowW(L"STATIC",L"Diagnostic HUD fixture",WS_OVERLAPPEDWINDOW,0,0,1296,759,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        RECT rect{};GetClientRect(gWindow,&rect);gWindowWidth=rect.right;gWindowHeight=rect.bottom;
        HDC dc=GetDC(gWindow);ensureBackBuffer(dc,gWindowWidth,gWindowHeight);ReleaseDC(gWindow,dc);
        gSmallFont=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        bms::Chart chart;chart.mode=bms::ChartMode::SevenKey;chart.initialBpm=120;chart.notes={{1,10,0,L""}};
        gCharts={chart};gActiveChartIndex=0;gHit={false};gGpuFrame=false;gPlaying=false;
        gFrameStatistics.reset();for(int i=0;i<5001;++i)gFrameStatistics.record(i*.001);
        drawGameScreen(gBackBufferDc);screenshot(output/L"fps-hud.bmp");
        check(gFrameStatistics.average()>999 && gFrameStatistics.lowOnePercent()>999,"HUD reset collected statistics");
        DeleteObject(gSmallFont);gSmallFont=nullptr;DestroyWindow(gWindow);gWindow=nullptr;
        std::cout<<"Diagnostic beep PCM and FPS HUD rendering passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';status=1;}
    gAudio.stop();Gdiplus::GdiplusShutdown(gGdiPlusToken);MFShutdown();CoUninitialize();return status;
}
