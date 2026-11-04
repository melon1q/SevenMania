#include "../src/Main.cpp"
#include "VideoFixtures.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

static void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
static void writeChart(const std::filesystem::path& path,const std::string& text){std::ofstream out(path,std::ios::binary);out<<text;}
struct AudioEngineTestAccess {
    static void clock(AudioEngine& audio,double time){audio.paused_=true;audio.pausedPositionSeconds_=time;audio.running_=true;}
    static std::wstring lastTrigger(AudioEngine& audio){return audio.pendingTriggers_.empty()?L"":audio.pendingTriggers_.back();}
    static size_t frames(const AudioEngine& audio,const std::wstring& id){const auto it=audio.samples_.find(id);return it==audio.samples_.end()?0:it->second->pcm.size()/2;}
    static std::vector<int16_t> mix(AudioEngine& audio,double time,unsigned frames){
        audio.nextFrame_=static_cast<uint64_t>(time*audio.sampleRate_);audio.mixBuffer_.resize(frames*2);
        std::vector<int16_t> out(frames*2);audio.fillBuffer(out.data(),frames);return out;
    }
};
struct GpuTestAccess {
    static std::vector<uint32_t> read(CompositionPresenter& r,const void* pixels,int width,int height) {
        check(r.drawFrame(pixels,width,nullptr,0,0,0),"GPU frame composition failed");
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        D3D11_TEXTURE2D_DESC desc{};r.frame_->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
        check(SUCCEEDED(r.device_->CreateTexture2D(&desc,nullptr,&staging)),"GPU readback allocation failed");
        r.context_->CopyResource(staging.Get(),r.frame_.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        check(SUCCEEDED(r.context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"GPU readback map failed");
        std::vector<uint32_t> out(static_cast<size_t>(width)*height);
        for(int y=0;y<height;++y)std::copy_n(reinterpret_cast<const uint32_t*>(static_cast<const BYTE*>(mapped.pData)+y*mapped.RowPitch),width,out.data()+y*width);
        r.context_->Unmap(staging.Get(),0);return out;
    }
};
static void bmp(const std::filesystem::path& path,uint32_t color,bool hole=false){
    Gdiplus::Bitmap image(256,256,PixelFormat32bppARGB);
    Gdiplus::Graphics g(&image);Gdiplus::SolidBrush brush{Gdiplus::Color(color)};g.FillRectangle(&brush,0,0,256,256);
    if(hole){Gdiplus::SolidBrush black{Gdiplus::Color(255,0,0,0)};g.FillRectangle(&black,0,0,128,256);}
    const CLSID encoder{0x557cf400,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    check(image.Save(path.c_str(),&encoder,nullptr)==Gdiplus::Ok,"sample BMP creation failed");
}
static void snapshot(const std::filesystem::path& path){
    GdiFlush();BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);file.bfSize=file.bfOffBits+gWindowWidth*gWindowHeight*4;
    BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=gWindowWidth;info.biHeight=-gWindowHeight;info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<char*>(&file),sizeof(file));out.write(reinterpret_cast<char*>(&info),sizeof(info));out.write(static_cast<char*>(gBackBufferPixels),gWindowWidth*gWindowHeight*4);
}
int wmain(int argc,wchar_t** argv){
    if(argc<2)return 2;CoInitializeEx(nullptr,COINIT_MULTITHREADED);MFStartup(MF_VERSION);AVIFileInit();
    Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&gGdiPlusToken,&input,nullptr);
    int status=0;
    try{
        const std::filesystem::path root=argv[1];std::filesystem::create_directories(root);
        check(std::filesystem::is_regular_file(root/L"tone.wav"),"run BmsCompatibilityTests first");
        bmp(root/L"base.bmp",0xff0000ff);bmp(root/L"layer.bmp",0xffff0000,true);bmp(root/L"poor.bmp",0xff00ff00);
        fixtures::writeVideo(root/L"movie.avi");fixtures::writeVideo(root/L"offset.avi",10);
        const std::string prefix="#PLAYER 1\n#BPM 120\n#WAV01 tone.wav\n#WAV02 tone.wav\n#WAV00 tone.wav\n";
        writeChart(root/L"10-bga-layers.bme",prefix+"#TITLE BGA layer and key animation\n#BMP01 base.bmp\n#BMP02 layer.bmp\n#BMP00 poor.bmp\n#BMP0A movie.avi\n#BMP0B offset.avi\n#@BGA03 02 0 0 128 256 128 0\n#EXBMP04 128,0,0,0 layer.bmp\n#ARGB01 128,0,0,0\n#SWBGA01 100:400:11:1:255,0,0,0 01020102\n#00004:01\n#00007:02\n#000A5:01\n#001A2:01\n#00204:0A\n#00207:0B\n#00404:0A\n#00819:01\n");
        auto bga=bms::BmsParser(false,17,false).parse(root/L"10-bga-layers.bme");check(bga.chart.playable && bga.errors.empty(),"BGA command sample failed to parse");
        BgaRaster raster;raster.canvas(256,256);std::vector<uint32_t> pixels(256*256,0xff000000);
        check(raster.draw(bga.chart,L"01",pixels.data()),"BASE image unavailable");check(raster.draw(bga.chart,L"02",pixels.data(),255,true),"LAYER unavailable");
        check(pixels[64]==0xff0000ff && pixels[192]==0xffff0000,"BMP black did not reveal BASE");
        pixels.assign(256*256,0xff0000ff);check(raster.draw(bga.chart,L"04",pixels.data()),"EXBMP failed");
        const auto half=pixels[192];check(((half>>16)&255)>=127 && ((half>>16)&255)<=129 && (half&255)>=126 && (half&255)<=128,"EXBMP alpha was not blended");
        check(bga.chart.bgaColors[2].size()==1 && bga.chart.bgaColors[2][0].color.alpha==128,"ARGB layer timeline missing");
        pixels.assign(256*256,0xff0000ff);const auto layerColor=bga.chart.bgaColors[2][0].color;
        check(raster.draw(bga.chart,L"02",pixels.data(),255,true,&layerColor) && ((pixels[192]>>16)&255)>=127 && (pixels[64]&255)==255,"ARGB keyed alpha draw failed");
        AsyncVideoBga authoredFps;authoredFps.open(root/L"movie.avi",16,16,false,20);check(authoredFps.waitUntilPrepared(),"VIDEO F/S setup failed");
        authoredFps.request(.55,16,16);check(authoredFps.waitUntilPrepared() && authoredFps.frame()->pixels.front()==0xff00ff00,"VIDEO F/S did not change frame clock");authoredFps.reset();
        auto nativeChart=bga.chart;nativeChart.options={L"charatbeatHDX:bgamode 1"};BgaVideoLayers nativeVideo;nativeVideo.prepare(nativeChart,32,32);
        check(nativeVideo.frame(nativeChart,0,4,32,32,true)->width==16,"native BGA size was ignored");nativeVideo.reset();
        wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);
        const auto bitmapMaterial=std::filesystem::path(module).parent_path()/L"Materials"/L"bms-compatibility-fixture";std::filesystem::create_directories(bitmapMaterial);
        std::filesystem::copy_file(root/L"base.bmp",bitmapMaterial/L"base.bmp",std::filesystem::copy_options::overwrite_existing);
        writeChart(root/L"29-material-bitmap.bme",prefix+"#TITLE Shared bitmap material\n#MATERIALSBMP bms-compatibility-fixture\n#BMP01 base.bmp\n#00004:01\n#00119:01\n");
        const auto materialChart=bms::BmsParser(false,17,false).parse(root/L"29-material-bitmap.bme");
        pixels.assign(256*256,0);raster.reset();raster.canvas(256,256);
        check(materialChart.chart.playable && raster.draw(materialChart.chart,L"01",pixels.data()) && pixels[64]==0xff0000ff,"MATERIALSBMP did not reach decoded image");
        BgaSwitchState animation;std::array<bool,16> keys{};keys[1]=true;animation.press(bga.chart,1,0);
        check(animation.frame(bga.chart,.11,keys).id==L"02" && animation.frame(bga.chart,.41,keys).id.empty(),"SWBGA key animation timing wrong");
        BgaVideoLayers videos;videos.prepare(bga.chart,32,32);
        auto first=videos.frame(bga.chart,0,4,32,32,true);check(first && first->pixels.front()==0xffff0000,"alphabetic BMP video did not start on event");
        auto layer=videos.frame(bga.chart,2,5.1,32,32,true);check(layer && layer->pixels.front()==0xff00ff00,"independent layer video clock wrong");
        auto advanced=videos.frame(bga.chart,0,5.1,32,32,true);check(advanced && advanced->pixels.front()==0xff00ff00,"video did not advance");
        auto repeat=videos.frame(bga.chart,0,8,32,32,true);check(repeat && repeat->pixels.front()==0xffff0000,"repeated BMP did not restart movie");
        videos.reset();
        const auto bmson=root/L"11-stem-slicing.bmson";
        writeChart(bmson,R"({"version":"1.0.0","info":{"title":"Stem slice compatibility","artist":"Generated fixture","mode_hint":"beat-7k","init_bpm":120,"resolution":240,"judge_rank":100,"total":100},"bpm_events":[{"y":240,"bpm":240}],"stop_events":[{"y":240,"duration":240}],"sound_channels":[{"name":"tone.wav","notes":[{"x":1,"y":0,"l":0,"c":false},{"x":2,"y":24,"l":0,"c":true},{"x":0,"y":240,"l":0,"c":false},{"x":7,"y":480,"l":240,"c":false}]},{"name":"tone.wav","notes":[{"x":1,"y":0,"l":0,"c":false}]}],"bga":{"bga_header":[{"id":1,"name":"base.bmp"}],"bga_events":[{"y":0,"id":1}]}})");
        auto sliced=bms::BmsParser().parse(bmson);check(sliced.chart.playable && sliced.chart.notes.size()==3 && !sliced.chart.sampleGroups.empty(),"bmson merged notes missing");
        check(std::abs(sliced.chart.notes.back().timeSeconds-1)<1e-7 && std::abs(sliced.chart.notes.back().lengthSeconds-.25)<1e-7,"bmson pulse/STOP/LN conversion wrong");
        AudioEngine stem;std::wstring error;check(stem.prepare(sliced.chart,error),"bmson stem preparation failed");
        check(AudioEngineTestAccess::frames(stem,L"source-0-slice-0")==static_cast<size_t>(stem.sampleRate()*.05),"bmson stem not cut at next pulse");
        check(AudioEngineTestAccess::frames(stem,sliced.chart.notes.front().sampleId)>0,"stacked bmson keysounds not mixed");
        auto wave=AudioEngineTestAccess::mix(stem,.5,256);check(std::any_of(wave.begin(),wave.end(),[](auto x){return x!=0;}),"bmson BGM did not reach mixer");stem.stop();
        writeChart(root/L"12-gameplay-events.bme",prefix+"#TITLE Input and judgement integration\n#TOTAL 400\n#RANK 2\n#00031:02\n#001D1:0A\n#00219:01\n#00351:0102\n");
        auto gameplay=bms::BmsParser(false,17,false).parse(root/L"12-gameplay-events.bme");check(gameplay.chart.playable,"gameplay fixture blocked");
        gWindow=CreateWindowW(L"STATIC",L"Compatibility renderer",WS_OVERLAPPEDWINDOW,0,0,1280,720,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        RECT bounds{};GetClientRect(gWindow,&bounds);gWindowWidth=bounds.right;gWindowHeight=bounds.bottom;
        HDC dc=GetDC(gWindow);ensureBackBuffer(dc,gWindowWidth,gWindowHeight);ReleaseDC(gWindow,dc);
        gCharts={gameplay.chart};gActiveChartIndex=0;gPlaying=true;gShowingResult=gPaused=false;gPractice.enabled=false;gChartOptions={};
        gHit.assign(gameplay.chart.notes.size(),false);gMineHit.assign(gameplay.chart.mines.size(),false);gMissScanIndex=0;gPressed.fill(false);gHolds.fill(LongNoteHold{});
        for(auto& indices:gLaneNoteIndices)indices.clear();for(size_t i=0;i<gameplay.chart.notes.size();++i)gLaneNoteIndices[gameplay.chart.notes[i].lane].push_back(i);
        gGrooveGauge.configure(gameplay.chart.notes.size(),GrooveGaugeMode::Normal,400);gResultScore.reset(static_cast<int>(gameplay.chart.notes.size()));
        check(gAudio.prepare(gameplay.chart,error),"input fixture audio prepare failed");AudioEngineTestAccess::clock(gAudio,1);pressLane(1);
        check(AudioEngineTestAccess::lastTrigger(gAudio)==L"02","Invisible note did not change real input keysound");
        AudioEngineTestAccess::clock(gAudio,2);updatePlaybackFrame();check(std::abs(gGrooveGauge.value()-15)<1e-7,"Mine input did not damage real game gauge");
        gPressed.fill(false);AudioEngineTestAccess::clock(gAudio,4);pressLane(7);check(gHit[0] && gResultScore.value()>0,"note input did not update judgement/score");
        AudioEngineTestAccess::clock(gAudio,6);pressLane(1);check(gHolds[1].active(),"LN input did not start hold");
        AudioEngineTestAccess::clock(gAudio,7);updatePlaybackFrame();check(!gHolds[1].active(),"ordinary LN did not auto-complete in game");
        check(AudioEngineTestAccess::lastTrigger(gAudio)==L"02","LN tail audio did not reach trigger queue");
        gAudio.stop();
        auto fxChart=bms::BmsParser(false,17,false).parse(root/L"09-path-wavcmd-metadata.bme");AudioEngine fxAudio;
        check(fxAudio.prepare(fxChart.chart,error),"WAVCMD decode failed");check(std::abs(static_cast<double>(AudioEngineTestAccess::frames(fxAudio,L"01"))-fxAudio.sampleRate()*.05)<=1,"WAVCMD pitch/duration not applied to decoded audio");fxAudio.releaseSong();
        // Exercise the same input path for charge/hell-charge release and recovery.
        for(auto mode:{bms::LongNoteMode::CN,bms::LongNoteMode::HCN}) {
            auto typed=gameplay.chart;typed.notes={{1,1,1,L"01",L"02",mode}};typed.mines.clear();typed.invisible.clear();
            gCharts={typed};gActiveChartIndex=0;gHit.assign(1,false);gHolds.fill(LongNoteHold{});gPressed.fill(false);gMineHit.clear();gMissScanIndex=0;
            for(auto& indices:gLaneNoteIndices)indices.clear();gLaneNoteIndices[1]={0};gGrooveGauge.configure(2,GrooveGaugeMode::Normal,200);gResultScore.reset(2);
            check(gAudio.prepare(typed,error),"CN/HCN prepare failed");AudioEngineTestAccess::clock(gAudio,1);pressLane(1);
            if(mode==bms::LongNoteMode::HCN){AudioEngineTestAccess::clock(gAudio,1.3);windowProc(gWindow,WM_KEYUP,laneKey(1),0);check(gHolds[1].active(),"HCN release aborted recoverable hold");AudioEngineTestAccess::clock(gAudio,1.5);pressLane(1);}
            AudioEngineTestAccess::clock(gAudio,mode==bms::LongNoteMode::CN?2.05:2);if(mode==bms::LongNoteMode::CN)windowProc(gWindow,WM_KEYUP,laneKey(1),0);else updatePlaybackFrame();
            check(!gHolds[1].active() && gResultScore.value()>0,"CN/HCN endpoint not resolved in gameplay");
            if(mode==bms::LongNoteMode::CN)check(gResultScore.value()==750000,"CN late GOOD release was incorrectly promoted to PGREAT");gAudio.stop();
        }
        gPlaying=false;

        for(const auto& [extension,mode,channels]:{std::tuple{L"13-five.bms",bms::ChartMode::FiveKey,"#00011:01\n#00115:01\n"},std::tuple{L"14-ten.bms",bms::ChartMode::TenKey,"#00011:01\n#00125:01\n"},std::tuple{L"15-double.bme",bms::ChartMode::FourteenKey,"#00019:01\n#00129:01\n"},std::tuple{L"16-popn.pms",bms::ChartMode::PopnNine,"#00011:01\n#00125:01\n#001D1:0A\n#001E5:0A\n"}}){
            writeChart(root/extension,prefix+"#TITLE Additional lane mode\n"+channels);auto parsed=bms::BmsParser(false,17,false).parse(root/extension);
            check(parsed.chart.playable && parsed.chart.mode==mode,"additional key mode blocked");
            if(mode==bms::ChartMode::PopnNine)check(parsed.chart.mines.size()==2 && parsed.chart.mines[0].lane==0 && parsed.chart.mines[1].lane==8,"PMS mines mapped to wrong key layout");gCharts={parsed.chart};gActiveChartIndex=0;gPlayhead=0;gHit.assign(parsed.chart.notes.size(),false);gHolds.fill(LongNoteHold{});gPressed.fill(false);gMineHit.clear();gJudgementText.clear();gFeedback.reset();gBgaActiveImages.fill(L"");gBgaActiveImages[1]=L"00";gBgaBackplateDirty=true;gBgaRaster.reset();
            const auto layout=bms::chartLayout(parsed.chart,0,68,104);gKeyBeams.configure(layout);gFeedback.configure(layout);gGpuFrame=false;drawGameScreen(gBackBufferDc);snapshot(root/(std::wstring(extension)+L"-cpu.bmp"));
            const bool dual=mode==bms::ChartMode::TenKey || mode==bms::ChartMode::FourteenKey;
            const int keyWidth=dual?45:68,scratch=dual?68:104;
            auto geometry=bms::chartLayout(parsed.chart,0,keyWidth,scratch);geometry.fieldX=(gWindowWidth-geometry.totalWidth())/2;
            const int firstLane=parsed.chart.notes.front().lane;
            const int px=(geometry.left(firstLane)+geometry.right(firstLane))/2,py=judgementLineY(gWindowHeight)+5;
            const uint32_t expected=static_cast<uint32_t*>(gBackBufferPixels)[py*gWindowWidth+px]&0xffffffu;
            check(expected!=0,"CPU note was not painted in requested lane");
            check(keyLane(laneKey(parsed.chart.notes.back().lane),0)==parsed.chart.notes.back().lane,"2P/PMS key input mapping failed");
            check(gCompositionPresenter.prepare(gWindow,gWindowWidth,gWindowHeight),"GPU compatibility render initialization failed");gGpuFrame=true;drawGameScreen(gBackBufferDc);
            const auto gpu=GpuTestAccess::read(gCompositionPresenter,gBackBufferPixels,gWindowWidth,gWindowHeight);
            check((gpu[py*gWindowWidth+px]&0xffffffu)==expected,"CPU/GPU note coordinates or colors differ");
            std::copy(gpu.begin(),gpu.end(),static_cast<uint32_t*>(gBackBufferPixels));snapshot(root/(std::wstring(extension)+L"-gpu.bmp"));
            check(gCompositionPresenter.present(gWindow,gBackBufferPixels,gWindowWidth,gWindowHeight),"GPU compatibility presentation failed");gGpuFrame=false;gCompositionPresenter.reset();
        }
        gCharts={bga.chart};gActiveChartIndex=0;gBgaVideos.prepare(bga.chart,gWindowWidth,gWindowHeight);gHit.assign(bga.chart.notes.size(),false);gPlayhead=4;gBgaEventIndex=0;gBgaActiveImages.fill(L"");gBgaActiveImages[1]=L"00";updateBgaState(bga.chart,4);gBgaBackplateDirty=true;drawGameScreen(gBackBufferDc);snapshot(root/L"bga-gameplay-cpu.bmp");
        {
            // Feed actual reusable decoder frames through uploadHybrid and D2D,
            // then read the GPU result. Hold the previous upload owner exactly
            // as the game does, including repeated rewind and pool reuse.
            AsyncVideoBga video;video.open(root/L"movie.avi",64,64);
            check(video.waitUntilPrepared(),"GPU pool fixture could not prepare");
            CompositionPresenter presenter;check(presenter.prepare(gWindow,64,64),"GPU pool target failed");
            std::vector<uint32_t> empty(64*64,0);
            for(int i=0;i<12;++i){
                video.request((i%3)*1.1,64,64);check(video.waitUntilPrepared(),"GPU pool frame missing");
                const auto frame=video.frame();
                presenter.hybridBackground(frame->pixels.data(),frame->generation,0,RECT{},0,{},
                    std::shared_ptr<const uint32_t>(frame,frame->pixels.data()));
                const auto output=GpuTestAccess::read(presenter,empty.data(),64,64);
                const uint32_t color=i%3==0?0xffff0000u:i%3==1?0xff00ff00u:0xff0000ffu;
                // The render target uses D2D1_ALPHA_MODE_IGNORE; only RGB is defined.
                check((output[32*64+32]&0xffffffu)==(color&0xffffffu),"GPU uploadHybrid showed stale or damaged reused pixels");
            }
        }
        gBgaVideos.reset();gCharts.clear();gAudio.releaseSong();gCompositionPresenter.reset();DestroyWindow(gWindow);gWindow=nullptr;
        if(argc>2) {
            size_t verified=0;
            for(const auto& entry:std::filesystem::recursive_directory_iterator(argv[2])) {
                const auto ext=bms::toLower(entry.path().extension().wstring());
                if(ext!=L".bms" && ext!=L".bme" && ext!=L".bml" && ext!=L".pms" && ext!=L".bmson")continue;
                const auto parsed=bms::BmsParser(false,17,false).parse(entry.path());
                const auto name=entry.path().filename().wstring();
                const bool diagnostic=name.starts_with(L"18-") || name.starts_with(L"19-") || name.starts_with(L"25-");
                if(!parsed.chart.playable || (!diagnostic && parsed.chart.partialLoad)) {
                    std::wcerr<<L"Exported sample failed: "<<entry.path()<<L" : "<<parsed.chart.partialLoadReason<<L"\n";
                    throw std::runtime_error("exported sample parse/assets failed");
                }
                AudioEngine audio;check(audio.prepare(parsed.chart,error),"exported sample did not decode all audio");++verified;
            }
            check(verified>=35,"exported sample collection incomplete");std::cout<<"Exported sample charts decoded successfully: "<<verified<<"\n";
        }
        std::cout<<"Compatibility playback: actual input/judgement/gauge/mixer, bmson slices, BMP layers, video restart/layers, CPU and GPU key modes passed.\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';status=1;gAudio.stop();gBgaVideos.reset();gCompositionPresenter.reset();}
    AVIFileExit();MFShutdown();Gdiplus::GdiplusShutdown(gGdiPlusToken);gGdiPlusToken=0;CoUninitialize();return status;
}
