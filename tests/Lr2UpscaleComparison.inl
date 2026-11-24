// Optional diagnostic command using the player's compositor and the supplied SD skins.
struct GpuTestAccess {
    static double measure(CompositionPresenter& presenter, const void* pixels, int width,
                          CompositionPresenter::OverlayDraw draw) {
        presenter.collectGpuTiming();
        auto* slot=presenter.beginGpuTiming();
        require(slot!=nullptr,"GPU timestamp queries unavailable");
        require(presenter.drawFrame(pixels,width,nullptr,0,0,0,draw),"LR2 comparison render failed");
        presenter.finishGpuTiming(slot);
        auto* context=presenter.context_.Get();
        context->Flush();
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock{};
        HRESULT ready=S_FALSE;
        for(int i=0;i<2000&&ready==S_FALSE;++i) {
            ready=context->GetData(slot->disjoint.Get(),&clock,sizeof(clock),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(ready==S_FALSE)Sleep(1);
        }
        require(ready==S_OK,"GPU timestamps did not become ready");
        UINT64 start{},end{};
        hr(context->GetData(slot->start.Get(),&start,sizeof(start),0));
        hr(context->GetData(slot->end.Get(),&end,sizeof(end),0));
        slot->pending=false;
        return clock.Disjoint||!clock.Frequency||end<start ? -1 : double(end-start)*1000/clock.Frequency;
    }
    static UpscaleFilter filter(const CompositionPresenter& presenter) { return presenter.frameScale_->filter; }
};

namespace {
void loadComparisonScene(PreviewScene& scene,const std::filesystem::path& path,bool play,const ScreenData& song) {
    Options options;
    options.screen=play?ScreenType::Play7Key:ScreenType::Select;
    options.previewIncludes=true;
    auto result=parse(path,options);
    const bool sd=result&&result.document->header.width==640&&result.document->header.height==480;
    if(!sd)for(const auto& diagnostic:result.diagnostics)
        if(diagnostic.error)std::cerr<<diagnostic.line<<": "<<diagnostic.message<<"\n";
    require(sd,"comparison requires a 640x480 LR2 skin of the requested type");
    const bool loaded=scene.load(*result.document,result.diagnostics,play,&song);
    if(!loaded)for(const auto& diagnostic:result.diagnostics)
        if(diagnostic.error)std::cerr<<diagnostic.line<<": "<<diagnostic.message<<"\n";
    require(loaded,"comparison skin did not load");
}

void compareLr2Upscale(const std::filesystem::path& selectPath,const std::filesystem::path& playPath,
                       const std::filesystem::path& chartPath,const std::filesystem::path& output) {
    const auto chart=bms::BmsParser(false,1).parse(chartPath).chart;
    require(chart.mode==bms::ChartMode::SevenKey&&!chart.notes.empty(),"comparison requires a 7-key BMS chart with notes");
    HWND window=CreateWindowW(L"STATIC",L"LR2 upscale comparison",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,
        GetModuleHandleW(nullptr),nullptr);
    require(window!=nullptr,"comparison window creation failed");
    struct WindowGuard { HWND window; ~WindowGuard(){DestroyWindow(window);} } guard{window};
    CompositionPresenter presenter;
    presenter.setModernDrawing(true);
    ScreenData song;
    song.songText[0]=chart.title; song.songText[1]=chart.subtitle; song.songText[2]=chart.title;
    song.songText[3]=chart.genre; song.songText[4]=chart.artist; song.songText[5]=chart.subartist;
    song.numbers.noteCount=static_cast<int>(chart.notes.size());
    PreviewScene selectScene,playScene;
    loadComparisonScene(selectScene,selectPath,false,song);
    loadComparisonScene(playScene,playPath,true,song);
    require(std::filesystem::create_directories(output),"choose a new directory for the comparison outputs");
    SelectFrame select;
    select.song=song;
    select.songSelected=true; select.keys=7; select.difficulty=chart.difficulty;
    select.hasBga=!chart.bga.empty()||!chart.fallbackVideo.empty();
    select.hasLongNotes=std::any_of(chart.notes.begin(),chart.notes.end(),[](const auto& note){return note.longNote();});
    select.banner=chart.banner;
    const int center=std::clamp(selectScene.selectCenter(),0,maxSelectBars-1);
    select.rows[center].row=0; select.rows[center].title=chart.title; select.rows[center].level=chart.playLevel;
    PlayFrame play;
    const double time=std::max(0.,chart.notes[chart.notes.size()/3].timeSeconds-.25);
    const double clock=1000*time;
    play.readyAge=play.playAge=clock; play.elapsed=time; play.remaining=std::max(0.,chart.durationSeconds-time);
    play.gauge=82; play.combo=12; play.judge=5; play.judgementAge=80; play.bpm=chart.initialBpm;
    play.noteCount=song.numbers.noteCount;
    const auto lanes=playScene.playLanes(640,480);
    require(lanes.has_value(),"comparison play skin has no usable lanes");
    std::ofstream report(output/"timings.csv");
    require(bool(report),"comparison CSV could not be opened");
    report<<"screen,width,height,filter,samples,gpu_median_ms,gpu_p95_ms,chart_time_s\n";
    for(const int height:{1080,1440}) {
        const int width=height*16/9;
        const std::vector<uint32_t> black(size_t(width)*height,0xff000000);
        for(const bool playing:{false,true})for(const auto filter:{UpscaleFilter::Bicubic,UpscaleFilter::Hybrid}) {
            auto& scene=playing?playScene:selectScene;
            require(presenter.prepare(window,width,height),"comparison presenter could not prepare");
            auto* target=static_cast<ID2D1DeviceContext*>(presenter.renderTarget());
            require(scene.prepare(target),"comparison skin textures could not upload");
            if(!playing)select.hasBanner=scene.updateSelectBanner(select.banner);
            const auto scale=scene.upscale(filter,width,height);
            require(scale&&presenter.setFrameUpscale(*scale,false)&&GpuTestAccess::filter(presenter)==filter,
                "requested comparison filter unavailable");
            struct Frame {
                PreviewScene& scene; const FrameScale& scale; const SelectFrame& select;
                const PlayFrame& play; const bms::Chart& chart; const std::array<PlayLane,16>& lanes; bool playing; double clock,time;
            } frame{scene,*scale,select,play,chart,*lanes,playing,clock,time};
            const CompositionPresenter::OverlayDraw draw{[](void* data,ID2D1RenderTarget* target,int w,int h) {
                const auto& f=*static_cast<const Frame*>(data);
                auto* dc=static_cast<ID2D1DeviceContext*>(target);
                dc->Clear(D2D1::ColorF(D2D1::ColorF::Black));
                if(!f.playing) {
                    f.scene.draw(dc,f.clock,w,h,nullptr,DrawPass::All,&f.select,{},&f.scale);
                    return;
                }
                f.scene.draw(dc,f.clock,w,h,&f.play,DrawPass::BeforeNotes);
                // Reuse the chart's scroll timeline and the authored LR2 lane and note sprites.
                const auto now=bms::scrollPositionSeconds(f.chart,f.time);
                const float pixelsPerSecond=240;
                for(const auto& note:f.chart.notes) {
                    if(note.lane<0||note.lane>=8||note.timeSeconds<f.time)continue;
                    const auto& lane=f.lanes[note.lane];
                    const float y=lane.noteBottom-lane.noteHeight-
                        float(bms::scrollPositionSeconds(f.chart,note.timeSeconds)-now)*pixelsPerSecond;
                    if(y+lane.noteHeight<=0&&!f.chart.nonMonotonicScroll)break;
                    if(y+lane.noteHeight<=0||y>=h)continue;
                    const auto sprite=f.scene.noteSprite(note.lane,f.clock,
                        note.longNote()?PlayNotePart::Start:PlayNotePart::Tap);
                    if(!sprite.bitmap)continue;
                    const auto source=D2D1::RectF(sprite.x,sprite.y,sprite.x+sprite.width,sprite.y+sprite.height);
                    dc->DrawBitmap(sprite.bitmap,D2D1::RectF(float(lane.left),y,float(lane.right),y+lane.noteHeight),
                        1,D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR,source);
                }
                f.scene.draw(dc,f.clock,w,h,&f.play,DrawPass::AfterNotes);
            },&frame};
            std::vector<double> times;
            for(int i=0;i<36;++i) {
                const double sample=GpuTestAccess::measure(presenter,black.data(),width,draw);
                if(i>=6&&sample>=0)times.push_back(sample);
            }
            require(times.size()>=24,"too few valid GPU timing samples");
            std::sort(times.begin(),times.end());
            const char* screen=playing?"play":"select";
            const char* name=filter==UpscaleFilter::Hybrid?"hybrid":"bicubic";
            report<<screen<<','<<width<<','<<height<<','<<name<<','<<times.size()<<','<<times[times.size()/2]
                  <<','<<times[(95*times.size()+99)/100-1]<<','<<time<<'\n';
            std::vector<uint32_t> pixels;
            require(presenter.captureLastFrame(pixels,width,height),"comparison capture failed");
            Bytes bgra(pixels.size()*4);
            std::memcpy(bgra.data(),pixels.data(),bgra.size());
            for(size_t i=3;i<bgra.size();i+=4)bgra[i]=255;
            save(output/(std::string(screen)+"-"+std::to_string(height)+"p-"+name+".png"),bgra,width,height);
        }
    }
    require(bool(report),"comparison CSV write failed");
}
}
