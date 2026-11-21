#include "../src/Main.cpp"
#include "SkinRenderTestSupport.h"
#include "../src/skin/SkinAnchors.h"
using namespace skin_test;
using namespace sevenmania::skin;

struct GpuTestAccess {
    static bool draw(CompositionPresenter& presenter,const std::vector<std::uint32_t>& base,int width) {
        return presenter.drawFrame(base.data(),width,nullptr,0,0,0);
    }
};
namespace {
void registerType(const wchar_t* name,WNDPROC proc) {
    WNDCLASSW type{};type.lpfnWndProc=proc;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=name;
    type.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));require(RegisterClassW(&type)!=0,"Register class failed");
}
void verifyLr2Play(const std::filesystem::path& output) {
    const auto dir=output/L"lr2-play";
    std::filesystem::create_directories(dir);
    gLr2PlayFile=dir/L"play.lr2skin";
    Bytes pixels;
    std::array<std::uint32_t,8> colors{};
    for(unsigned lane=0;lane<8;++lane) {
        const BYTE red=static_cast<BYTE>(100+lane*19);
        colors[lane]=std::uint32_t(red)<<16;
        pixels.insert(pixels.end(),{0,0,red,255});
    }
    save(dir/L"notes.png",pixels,8,1);
    Bytes digits;
    for(BYTE i=0;i<10;++i)digits.insert(digits.end(),{0,BYTE(50+20*i),0,255});
    save(dir/L"digits.png",digits,10,1);
    save(dir/L"bga.png",Bytes{255,0,0,255},1,1);
    const auto writeSkin=[&](bool missing) {
        std::ofstream skin(gLr2PlayFile);
        skin<<"#INFORMATION,0,LR2 play test,SevenMania\n#CLOSE,400\n#IMAGE,notes.png\n#IMAGE,missing.png\n#IMAGE,digits.png\n"
            "#SRC_IMAGE,0,111,0,0,1,1\n#DST_IMAGE,0,0,0,0,640,480,0,255,0,0,60\n"
            "#SRC_BGA,0,0\n#DST_BGA,0,0,460,10,100,100,0,255,255,255,255,1,0,0,0,0,0,41\n"
            "#SRC_LINE,0,111,0,0,1,1,1,1\n#DST_LINE,0,0,20,350,350,1,0,255,255,255,255\n"
            "#SRC_IMAGE,0,0,4,0,1,1\n#DST_IMAGE,0,0,20,250,25,100,0,255,255,255,255,1,0,0,0,0,100\n"
            "#SRC_IMAGE,0,0,5,0,1,1\n#DST_IMAGE,0,0,60,250,5,100,0,255,255,255,255,1,0,0,0,-1,120\n"
            "#DST_IMAGE,0,100,60,250,5,100,0,0,255,255,255\n";
        for(int lane=0;lane<8;++lane) {
            skin<<"#SRC_NOTE,"<<lane<<','<<(missing&&lane==7?1:0)<<','<<lane<<",0,1,1\n";
            skin<<"#DST_NOTE,"<<lane<<",0,"<<20+lane*45<<",330,"<<25+lane<<",20\n";
        }
        skin<<"#SRC_LN_START,1,0,2,0,1,1\n#SRC_LN_END,1,0,3,0,1,1\n"
            "#SRC_LN_BODY,1,0,4,0,2,1,2,1,300,71\n#SRC_MINE,2,0,6,0,1,1\n"
            "#SRC_NOWJUDGE_1P,5,0,2,0,1,1,1,1\n"
            "#DST_NOWJUDGE_1P,5,0,25,335,8,8,0,255,255,255,255,1,0,0,0,-1,46\n"
            "#DST_NOWJUDGE_1P,5,250,25,335,8,8,0,255,255,255,255\n"
            "#SRC_NOWCOMBO_1P,5,2,0,0,10,1,10,1,0,0,0,0,1\n"
            "#DST_NOWCOMBO_1P,5,0,20,0,8,8,0,255,255,255,255,1,0,0,0,-1,46\n"
            "#DST_NOWCOMBO_1P,5,250,20,0,8,8,0,255,255,255,255\n"
            "#SRC_NOWJUDGE_1P,4,0,2,0,1,1,1,1\n"
            "#DST_NOWJUDGE_1P,4,0,180,190,100,12,0,255,255,255,255,1,0,0,0,-1,46\n"
            "#DST_NOWJUDGE_1P,4,250,180,190,100,12,0,255,255,255,255\n"
            "#SRC_NOWCOMBO_1P,4,2,0,0,10,1,10,1,0,0,0,1,5\n"
            "#DST_NOWCOMBO_1P,4,0,100,0,10,12,0,255,255,255,255,1,0,0,0,-1,46\n"
            "#DST_NOWCOMBO_1P,4,250,100,0,10,12,0,255,255,255,255\n"
            "#SRC_NUMBER,0,2,0,0,10,1,10,1,0,0,101,1,2\n"
            "#DST_NUMBER,0,0,470,130,8,8,0,255,255,255,255\n"
            "#SRC_GROOVEGAUGE,0,0,0,0,4,1,4,1,0,0,4\n"
            "#DST_GROOVEGAUGE,0,0,420,160,4,8,0,255,255,255,255\n";
    };
    writeSkin(false);
    const int savedWidth=gWindowWidth,savedHeight=gWindowHeight;
    const bool savedComposition=gCompositionEnabled;
    const auto savedChart=gCharts[0];
    auto chart=savedChart;
    chart.notes.clear();
    for(int lane=0;lane<8;++lane)chart.notes.push_back({lane,1.0});
    chart.measureTimes={2};
    gCharts[0]=chart;
    gWindowWidth=640;gWindowHeight=480;refreshUiScale();
    ensureBackBuffer(nullptr,640,480);
    gCompositionEnabled=true;
    loadLr2PlaySkin(chart);
    require(gLr2PlayScene!=nullptr,"7KEY LR2 play skin failed to load");
    const auto lanes=gLr2PlayScene->playLanes(640,480);
    require(lanes&&(*lanes)[0].left==20&&(*lanes)[7].right==367&&(*lanes)[0].noteBottom==350,"authored lane geometry");
    const auto wide=gLr2PlayScene->playLanes(800,480);
    require(wide&&(*wide)[0].left==100,"letterboxed lane geometry");
    gHit.assign(8,false);
    gNoteSpeed=1;gLaneCoverPercent=0;gTimingOffsets={};gChartOptions={};
    gPlaying=true;gGpuFrame=true;
    const auto render=[&](double time) {
        gPlayhead=time;
        gCompositionPresenter.setModernDrawing(true);
        require(gCompositionPresenter.prepare(gWindow,640,480),"LR2 gameplay target");
        require(gLr2PlayScene->prepare(static_cast<ID2D1DeviceContext*>(gCompositionPresenter.renderTarget())),
            "LR2 gameplay textures");
        prepareAppliedSkinGpu();
        drawGameScreen(gBackBufferDc);
        GdiFlush();
        const auto* base=static_cast<const std::uint32_t*>(gBackBufferPixels);
        require(GpuTestAccess::draw(gCompositionPresenter,{base,base+640*480},640),"draw LR2 gameplay");
        std::vector<std::uint32_t> captured;
        require(gCompositionPresenter.captureLastFrame(captured,640,480),"capture LR2 gameplay");
        return captured;
    };
    auto capture=render(1.0);
    for(int lane=0;lane<8;++lane) {
        require((capture[340*640+(*lanes)[lane].left+5]&0xffffff)==colors[lane],"LR2 note sprite did not reach its authored lane");
        require(gLr2PlayScene->noteSprite(lane,1000).bitmap!=nullptr,"LR2 note atlas missing");
    }
    require((capture[350*640+25]&0xffffff)==60,"note bottom did not meet the LR2 judgement baseline");
    require((capture[230*640+25]&0xffffff)==0xffffff,"LR2 measure line does not use note scroll position");
    capture=render(1.05);
    require((capture[345*640+25]&0xffffff)==colors[0]&&(capture[352*640+25]&0xffffff)==60,
        "late tap should be clipped at the LR2 judgement baseline");
    gLr2PlayScene->releaseDevice();
    gCompositionPresenter.reset();
    capture=render(.75);
    require((capture[340*640+25]&0xffffff)==60,"scroll did not move away from judgement baseline");
    require(std::count_if(capture.begin(),capture.end(),[&](auto pixel){return (pixel&0xffffff)==colors[0];})>0,
        "GPU recreation lost the moving note");
    gCharts[0].notes={{1,1.0,.75}};
    gCharts[0].mines={{2,1.0,1}};
    gHit.assign(1,false);gMineHit.assign(1,false);
    const auto at=[&](int lane,int y){return capture[y*640+(*lanes)[lane].left+5]&0xffffff;};
    capture=render(1.);
    require(at(1,340)==colors[2]&&at(1,280)==colors[4]&&at(1,250)==colors[3],
        "LR2 long-note head, idle body or tail missing");
    require(at(2,340)==colors[6],"LR2 mine sprite missing");
    gMineHit[0]=true;gHit[0]=true;gHolds[1].start(0,1.75,.8);
    capture=render(1.);
    require(at(1,280)==colors[5]&&at(2,340)==60,"held-body timer or consumed mine mismatch");
    gHolds[1]={};gMineHit.clear();
    gCharts[0]=chart;gHit.assign(8,false);
    gLr2Play.keyOn[0]=.9;gPressed[0]=true;
    gLr2Play.judge=5;gLr2Play.judgeAt=.9;gLr2Play.counts={2,2,0,0,0};
    // EX 3 (1 PG + 1 GR) to match counts: since e7484132 the LR2 EX number comes from gResultScore.
    gResultScore.reset(8);gResultScore.apply(TimingJudgement::PGreat);gResultScore.apply(TimingJudgement::Great);
    gFeedback.reset();gFeedback.addCombo(1);
    const auto savedGaugeMode=gGrooveGaugeMode;
    gGrooveGaugeMode=GrooveGaugeMode::Normal;
    gGrooveGauge.configure(8,GrooveGaugeMode::Normal);
    const int savedBgaBrightness=gBgaBrightness,savedBgaDarkness=gBgaDarkness;
    gBgaBrightness=100;gBgaDarkness=0;
    auto& playChart=gCharts[0];
    playChart.bitmaps[L"01"]=dir/L"bga.png";
    playChart.bga={{0,bms::BgaLayer::Base,L"01"}};
    gBgaActiveImages={L"01",L"",L"",L""};
    gBgaRaster.start(playChart,480,480,1);
    gBgaBackplateDirty=true;
    const auto pixel=[&](int x,int y){return capture[y*640+x]&0xffffff;};
    capture=render(1.0);
    require(pixel(25,270)==colors[4]&&pixel(25,338)==colors[2],"key beam or judgement layer order");
    if(pixel(47,338)!=0x004600||pixel(472,132)!=0x006e00)
        std::cerr<<"combo="<<pixel(47,338)<<", EX="<<pixel(472,132)<<"\n";
    require(pixel(47,338)==0x004600,"combo atlas digit");
    require(pixel(472,132)==0x006e00,"EX score atlas digit");
    if(pixel(422,162)!=colors[1]||pixel(462,162)!=colors[3])
        std::cerr<<"gauge="<<gLr2Play.frame.gauge<<", pixels="<<pixel(422,162)<<","<<pixel(462,162)<<"\n";
    require(pixel(422,162)==colors[1]&&pixel(462,162)==colors[3],"filled and empty groove gauge segments");
    require(pixel(510,50)==0x0000ff&&pixel(459,50)==60,"chart BGA must be clipped to its authored destination");
    gLr2Play.judge=4;
    gFeedback.reset();gFeedback.addCombo(150);
    capture=render(1.0);
    require(pixel(164,194)==60&&pixel(165,194)==colors[2]&&pixel(264,194)==colors[2],
        "judgement must shift left by half the visible combo width");
    require(pixel(265,194)==0x004600&&pixel(275,194)==0x009600&&pixel(285,194)==0x003200,
        "three-digit combo must meet the judgement at its right edge");
    gFeedback.reset();
    capture=render(1.0);
    require(pixel(165,194)==60&&pixel(180,194)==colors[2]&&pixel(280,194)==60,
        "a hidden combo must leave the judgement at its authored position");
    gPressed[0]=false;gLr2Play.keyOff[0]=1;
    gBgaBrightness=0;
    capture=render(1.05);
    require(pixel(25,270)==60&&pixel(62,270)!=60&&pixel(510,50)==60,"release timer and BGA off option");
    capture=render(1.3);
    require(pixel(62,270)==60&&pixel(25,338)!=colors[2],"release and judgement animation did not expire");
    gBgaBrightness=savedBgaBrightness;gBgaDarkness=savedBgaDarkness;
    gGrooveGaugeMode=savedGaugeMode;
    gBgaRaster.reset();gBgaActiveImages={};gBgaBackplateDirty=true;gFeedback.reset();
    gCharts[0]=chart;
    writeSkin(true);
    loadLr2PlaySkin(chart);
    require(gLr2PlayScene!=nullptr,"missing image prevented LR2 layout");
    capture=render(1.0);
    require((capture[340*640+(*lanes)[7].left+5]&0xffffff)!=60&&
        !gLr2PlayScene->noteSprite(7,1000).bitmap,"missing note image did not use native fallback");
    gPaused=true;
    quitPausedPlayback();
    require(gPlaying&&!gPaused&&gLr2Play.endingClockPending&&gLr2Play.endingFailed,"pause QUIT starts LR2 fail animation");
    render(1.0);
    require(!gLr2Play.endingClockPending&&lr2HoldFinish(),"first fail frame starts the close timer");
    gLr2Play.endingAt-=std::chrono::milliseconds(401);
    finishPlayback();
    require(!gPlaying&&gShowingResult&&gResultGaugeFailed,"pause QUIT reaches failed result");
    gShowingResult=false;
    resetResultFade();
    auto other=chart;other.mode=bms::ChartMode::FiveKey;
    loadLr2PlaySkin(other);
    require(!gLr2PlayScene,"LR2 7KEY skin leaked into 5KEY");
    std::ofstream(gLr2PlayFile)<<"#INFORMATION,0,Invalid,SevenMania\n#DST_NOTE,0,0,20,330,25,20\n";
    loadLr2PlaySkin(chart);
    require(!gLr2PlayScene,"incomplete LR2 layout should use native gameplay");

    gPlaying=false;gGpuFrame=false;gLr2PlayFile.clear();
    gCompositionPresenter.setModernDrawing(false);
    gCharts[0]=savedChart;gHit.clear();gPlayhead=0;
    gCompositionEnabled=savedComposition;gWindowWidth=savedWidth;gWindowHeight=savedHeight;refreshUiScale();
}
void verifyLr2Decide(const std::filesystem::path& output) {
    const auto dir=output/L"lr2-play";
    gLr2DecideFile=dir/L"decide.lr2skin";
    std::ofstream(gLr2DecideFile)<<"#INFORMATION,6,Decide test,SevenMania\n#SCENETIME,3000\n"
        "#SRC_IMAGE,0,100,-1,-1,-1,-1,1,1\n#DST_IMAGE,0,0,20,20,80,80,0,255,255,255,255,0,0,0,0,0,0,191\n"
        "#SRC_TEXT,0,0,10,0\n#DST_TEXT,0,0,120,30,200,30,0,255,255,255,255\n";
    const bool composition=gCompositionEnabled,unavailable=gGpuUnavailable;
    const int intro=gIntroEnabled;
    gCompositionEnabled=true;gGpuUnavailable=false;gIntroEnabled=1;
    auto chart=gCharts[0];
    chart.title=L"LR2 ENTRY";chart.titleImage=dir/L"bga.png";
    beginSongEntry(chart);
    require(songEntryActive()&&gLr2DecideScene&&gLr2DecideScene->sceneMilliseconds()==3000,
        "type 6 LR2 scene and SCENETIME must load for song entry");
    ensureBackBuffer(nullptr,640,480);
    require(gLr2DecideClockPending,"Decide clock waits for the first presented frame");
    require(presentLr2Decide(640,480),"LR2 Decide presenter");
    require(!gLr2DecideClockPending&&songEntryActive(),"Decide clock starts on presentation");
    std::vector<std::uint32_t> pixels;
    require(gCompositionPresenter.captureLastFrame(pixels,640,480)&&
        (pixels[40*640+40]&0xffffff)==0xff&&(pixels[110*640+110]&0xffffff)==0,
        "LR2 Decide image did not reach the GPU frame at its authored coordinates");
    require(std::any_of(pixels.begin()+30*640,pixels.begin()+75*640,[](auto pixel) {
        return (pixel&0xffffff)==0xffffff;
    }),"LR2 Decide did not render the song title");
    gLr2DecideFile=dir/L"play.lr2skin";
    beginSongEntry(gCharts[0]);
    require(songEntryActive()&&!gLr2DecideScene,"wrong screen type must fall back to the normal song intro");
    gSongIntro.reset();gCompositionPresenter.reset();gLr2DecideFile.clear();
    gCompositionEnabled=composition;gGpuUnavailable=unavailable;gIntroEnabled=intro;
}
void verifyLr2Result(const std::filesystem::path& output) {
    const auto dir=output/L"lr2-play";
    gLr2ResultFile=dir/L"result.lr2skin";
    gLr2SelectActive=false;
    captureBrowseBackdrop();
    require(gResultBackdropValid,"native backdrop is available after capture");
    gLr2SelectActive=true;
    captureBrowseBackdrop();
    require(!gResultBackdropValid,"LR2 transition discards the previous selection's backdrop");
    gLr2SelectActive=false;
    std::ofstream(gLr2ResultFile)<<"#INFORMATION,7,Result test,SevenMania\n#IMAGE,digits.png\n"
        "#SRC_NUMBER,0,0,0,0,10,1,10,1,0,0,110,1,3\n"
        "#DST_NUMBER,0,0,20,20,20,20,0,255,255,255,255\n"
        "#SRC_IMAGE,0,111,0,0,1,1\n#DST_IMAGE,0,0,80,20,20,20,0,255,255,255,255,0,0,0,0,0,0,90,300\n"
        "#SRC_IMAGE,0,111,0,0,1,1\n#DST_IMAGE,0,0,120,20,20,20,0,255,255,255,255,0,0,0,0,0,0,91\n";
    const auto savedChart=gCharts[0];
    const int savedIndex=gActiveChartIndex;
    const bool composition=gCompositionEnabled,unavailable=gGpuUnavailable,cleared=gResultGaugeCleared,directComposition=gDirectCompositionEnabled;
    gActiveChartIndex=0;gCharts[0].notes={{1,1.0},{2,2.0}};
    gLr2Play.counts={4,0,0,0,0};
    // Rank comes from gResultScore's EX score since e7484132: 2 PG on 2 notes is AAA.
    gResultScore.reset(2);gResultScore.apply(TimingJudgement::PGreat);gResultScore.apply(TimingJudgement::PGreat);
    gCompositionEnabled=true;gDirectCompositionEnabled=false;gGpuUnavailable=false;gResultGaugeCleared=true;
    ensureBackBuffer(nullptr,640,480);
    for(bool clear:{true,false}) {
        gResultGaugeCleared=clear;
        gCompositionPresenter.reset();
        loadLr2Result();
        require(gLr2ResultScene&&presentLr2Screen(*gLr2ResultScene,1000,640,480)
            &&!gCompositionPresenter.usesDirectComposition(),"LR2 Result presenter must follow the DirectComposition setting");
        std::vector<std::uint32_t> pixels;
        require(gCompositionPresenter.captureLastFrame(pixels,640,480),"LR2 Result readback");
        require((pixels[30*640+30]&0xffffff)==0x005a00,"LR2 Result judgement count");
        require((pixels[30*640+90]&0xffffff)==(clear?0xffffffu:0u)&&
            (pixels[30*640+130]&0xffffff)==(clear?0u:0xffffffu),"LR2 Result rank and clear/fail options");
    }
    gLr2ResultScene.reset();KillTimer(gWindow,ID_LR2_RESULT_ANIMATION);
    gLr2ResultFile.clear();gCompositionPresenter.reset();gLr2Play.resetTimers(0);
    gActiveChartIndex=savedIndex;gCharts[0]=savedChart;
    gCompositionEnabled=composition;gGpuUnavailable=unavailable;gResultGaugeCleared=cleared;gDirectCompositionEnabled=directComposition;
}
void pump() {
    MSG message{};unsigned count=0;
    while(count++<400&&PeekMessageW(&message,nullptr,0,0,PM_REMOVE))if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}
    require(count<400,"UI command feedback loop");
}
void paintPage(const std::filesystem::path& path={}) {
    const auto window=gRmlSettingsPage.window();RECT r{};GetClientRect(window,&r);
    HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* pixels{};auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);require(bitmap&&pixels,"Capture bitmap failed");
    const auto old=SelectObject(dc,bitmap);SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),0);GdiFlush();
    if(!path.empty()) {
        Bytes copy(static_cast<BYTE*>(pixels),static_cast<BYTE*>(pixels)+std::size_t(r.right)*r.bottom*4);save(path,copy,r.right,r.bottom);
    }
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
}
void click(const std::string& id) {
    paintPage();require(gRmlSettingsPage.focus(id),"Control missing");paintPage();
    const auto r=gRmlSettingsPage.bounds(id);require(r.right>r.left&&r.bottom>r.top,"Control clipped");
    const auto point=MAKELPARAM((r.left+r.right)/2,(r.top+r.bottom)/2);const auto window=gRmlSettingsPage.window();
    SendMessageW(window,WM_MOUSEMOVE,0,point);SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(window,WM_LBUTTONUP,0,point);pump();
}
std::wstring savedPath() {
    const auto path=readSkinSelection();require(path.has_value(),"Saved path decoding failed");return *path;
}
void verifyDcCache() {
    ensureBackBuffer(nullptr,gWindowWidth,gWindowHeight);const auto before=gSkinApplication.readbacks();
    drawBrowseScreen(gBackBufferDc);const auto first=gSkinApplication.readbacks();require(first>before,"Select overlay not drawn");
    for(int i=0;i<20;++i)drawBrowseScreen(gBackBufferDc);
    require(gSkinApplication.readbacks()==first,"Unchanged menu reread GPU surface");
    gCharts[0].title=L"Changed skin title";drawBrowseScreen(gBackBufferDc);require(gSkinApplication.readbacks()>first,"Changed title did not redraw");
}
void verifyImagePipeline(const std::filesystem::path& fixture,const std::filesystem::path& output){
    gPlaying=true;gPaused=false;gShowingResult=false;gGpuFrame=true;gActiveChartIndex=0;gResultScore.reset(10);gGrooveGauge.configure(10,GrooveGaugeMode::Normal);gFeedback.reset();
    require(applySkinFile(fixture,false),"Image HUD fixture load failed");require(!skinNeedsJudgementText(),"Image HUD still requests combined text");
    require(gCompositionPresenter.prepare(gWindow,gWindowWidth,gWindowHeight),"Image pipeline prepare failed");
    const bool compositionBefore=gCompositionEnabled;gCompositionEnabled=true;prepareSkinForPlayback();gCompositionEnabled=compositionBefore;
    const auto warmed=gSkinApplication.gpuStats();require(warmed.hudAtlases==1&&warmed.uploadedBitmaps==1,"Image GPU resources not prepared before playback");
    std::vector<std::uint32_t> background(std::size_t(gWindowWidth)*gWindowHeight,0xff000000u),captured,previous;
    const auto readbacks=gSkinApplication.readbacks();
    for(const auto grade:{TimingJudgement::PGreat,TimingJudgement::Great,TimingJudgement::Good,TimingJudgement::Bad,TimingJudgement::Poor}){
        showTimingJudgement(grade,0);gFeedback.addCombo(1);prepareAppliedSkinGpu();
        require(std::holds_alternative<std::monostate>(gSkinData.snapshot().value(BindingId::PlayJudgementCombo)),"Image pipeline generated a combined string");
        require(std::get<std::int64_t>(gSkinData.snapshot().value(BindingId::PlayJudgementGrade))==int(grade),"Image pipeline used wrong grade");
        require(GpuTestAccess::draw(gCompositionPresenter,background,gWindowWidth)&&gCompositionPresenter.captureLastFrame(captured,gWindowWidth,gWindowHeight),"Applied HUD image draw failed");
        require(previous.empty()||previous!=captured,"Applied judgement image did not change");previous=captured;
        require(gCompositionPresenter.prepare(gWindow,gWindowWidth,gWindowHeight),"Reset draw commands failed");
    }
    require(gSkinApplication.readbacks()==readbacks,"Image gameplay performed CPU readback");
    require(gSkinApplication.gpuStats().uploadedBitmaps==warmed.uploadedBitmaps&&gSkinApplication.gpuStats().decodedImages==warmed.decodedImages,"Actual judgement uploaded/decoded image resources");
    save(output/L"applied-image-hud.png",Bytes(reinterpret_cast<BYTE*>(captured.data()),reinterpret_cast<BYTE*>(captured.data())+captured.size()*4),gWindowWidth,gWindowHeight);
    gPlaying=false;gGpuFrame=false;gCompositionPresenter.reset();clearSkinSelection(false);
}
void verifyNoteImages(const std::filesystem::path& fixture, const std::filesystem::path& output) {
    const auto root = output / L"note-images";
    const auto notes = root / L"Notes";
    const auto skin = root / L"play.semaskin";
    std::filesystem::create_directories(notes);
    // A previous interrupted run may have left one of the generated sheets in this fixture.
    for (const auto* name : {L"000-NoteGroup", L"001-NoteGroup", L"Default-NoteGroup", L"NoteGroup"}) {
        for (const auto* ext : {L".png", L".ini"})
            std::filesystem::remove(notes / (std::wstring(name) + ext));
    }
    std::filesystem::copy_file(fixture, skin, std::filesystem::copy_options::overwrite_existing);
    save(notes / L"normal-white.png", Bytes{0, 0, 255, 255, 0, 0, 0, 0}, 2, 1);
    save(notes / L"scratch-red.png", Bytes{255, 0, 0, 255}, 1, 1);
    save(notes / L"long-start.png", Bytes{0, 255, 0, 255}, 1, 1);
    save(notes / L"long-body.png", Bytes{255, 255, 0, 255}, 1, 1);
    save(notes / L"long-end.png", Bytes{0, 255, 255, 255}, 1, 1);
    save(notes / L"mine.png", Bytes{255, 0, 255, 255}, 1, 1);
    { std::ofstream invalid(notes / L"scratch-long-start.png"); invalid << "invalid PNG"; }
    require(applySkinFile(skin, false), "Note images rejected the skin");
    require(gNoteImages.find(NotePart::LongStart, true) == gNoteImages.find(NotePart::LongStart, false),
        "Invalid scratch image did not fall back to the shared image");
    require(std::filesystem::remove(notes / L"normal-white.png"), "Cached PNG still open");

    // Pixel offsets below are FHD values: render at the design size (scale 1).
    const int savedWidth = gWindowWidth, savedHeight = gWindowHeight;
    gWindowWidth = 1920; gWindowHeight = 1080; refreshUiScale();
    require(gUiScale == 1.0, "FHD window did not keep the design scale");
    auto& chart = gCharts[0];
    chart.notes = {{0, 0.25}, {1, 0.25}, {2, 0.25, 0.5}};
    chart.mines = {{3, 0.5, 1}};
    gHit.assign(chart.notes.size(), 0);
    gMineHit.assign(chart.mines.size(), 0);
    gPlaying = true;
    gPlayhead = 0;
    gNoteSpeed = 1;
    gNoteHeight = 12;
    gTimingOffsets = {};
    gChartOptions = {};
    gLaneCoverPercent = 0;
    gGearPosition = 1;
    const auto widths = bms::chartDeckWidths(chart);
    auto layout = bms::chartLayout(chart, 0, widths.key, widths.scratch);
    layout.fieldX = std::max(0, gWindowWidth - layout.totalWidth()) / 2;
    const int judge = judgementLineY(gWindowHeight);
    const auto at = [&](int lane, int x, int y) {
        return static_cast<const uint32_t*>(gBackBufferPixels)[size_t(y) * gBackBufferWidth + layout.left(lane) + x] & 0xffffffu;
    };
    ensureBackBuffer(nullptr, gWindowWidth, gWindowHeight);
    drawGameScreen(gBackBufferDc);
    GdiFlush();
    require(at(0, 8, judge - 62) == 0x0000ff && at(1, 8, judge - 62) == 0xff0000, "Tap or scratch sprite missing");
    require(at(1, layout.width(1) - 8, judge - 62) != 0xff0000, "Transparent PNG pixel painted opaque");
    require(at(2, 8, judge - 62) == 0x00ff00 && at(2, 8, judge - 135) == 0x00ffff &&
        at(2, 8, judge - 197) == 0xffff00, "Long-note start, body, or end sprite missing");
    require(at(3, 8, judge - 130) == 0xff00ff, "Mine sprite missing");

    gHit[2] = 1;
    gHolds[2].start(2, 0.75);
    drawGameScreen(gBackBufferDc);
    GdiFlush();
    require(at(2, 8, judge + 2) == 0x00ff00 && at(2, 8, judge - 135) == 0x00ffff &&
        at(2, 8, judge - 197) == 0xffff00, "Held long-note sprites missing");
    gHolds[2] = {};

    // A long note nobody pressed keeps its remaining body on screen long after its head scrolled past the line.
    gLongNoteIndex = {};
    chart.notes[2].lengthSeconds = 6.0;
    gHit[2] = 0;
    for (const double playhead : {0.4, 1.0, 3.0}) {
        gPlayhead = playhead;
        drawGameScreen(gBackBufferDc);
        GdiFlush();
        require(at(2, 8, judge - 50) != 0x010203, "An unpressed long note vanished while its body was still above the line");
    }
    chart.notes[2].lengthSeconds = 0.5;
    gLongNoteIndex = {};
    gHit[2] = 1;
    gPlayhead = 0;

    // Releasing a held long note early leaves it on screen (inactive); it must not vanish for any frame.
    gPlayhead = 0.4;
    gHolds[2].start(2, 0.75, 0.25);
    drawGameScreen(gBackBufferDc);
    GdiFlush();
    const auto heldBody = at(2, 8, judge - 50), heldBodyHigh = at(2, 8, judge - 20);
    require(heldBody != 0x010203 && heldBodyHigh != 0x010203, "Held long-note body missing before the release");
    resolveHold(2, gHolds[2].release(0.4, 0.1));
    require(!gHolds[2].active() && !gLingeringLongNotes.empty(), "Early release did not keep the note on screen");
    drawGameScreen(gBackBufferDc);
    GdiFlush();
    require(at(2, 8, judge - 50) == heldBody && at(2, 8, judge - 20) == heldBodyHigh, "Released long-note body vanished");
    gLingeringLongNotes.clear();
    // A clean release a moment before the tail keeps the remaining body until the tail reaches the line.
    gHolds[2].start(2, 0.75, 0.25);
    resolveHold(2, gHolds[2].release(0.7, 0.1));
    require(gLingeringLongNotes.empty() && !gReleasedLongNotes.empty(), "Clean early release did not keep the remaining body");
    drawGameScreen(gBackBufferDc);
    GdiFlush();
    require(at(2, 8, judge - 50) == heldBody && at(2, 8, judge - 20) == heldBodyHigh, "Cleanly released long-note body vanished");
    gPlayhead = 0.76;
    drawGameScreen(gBackBufferDc);
    GdiFlush();
    require(gReleasedLongNotes.empty(), "Released remnant stayed after the tail reached the line");
    gReleasedLongNotes.clear();
    gPlayhead = 0;

    require(applySkinFile(skin, false), "Reloading note images failed");
    require(gNoteImages.find(NotePart::Tap, false, 7) == nullptr, "Removed tap image survived reload");
    drawGameScreen(gBackBufferDc);
    GdiFlush();
    const auto color = gPalette[gLaneColors[1]].color;
    const uint32_t expected = (uint32_t(GetRValue(color)) << 16) | (uint32_t(GetGValue(color)) << 8) | GetBValue(color);
    require(at(1, 8, judge - 62) == expected, "Missing tap image did not restore legacy rendering");

    require(gCompositionPresenter.prepare(gWindow, gWindowWidth, gWindowHeight), "Note GPU prepare failed");
    gNoteImages.prepareGpu(gCompositionPresenter.renderTarget());
    const RECT clip{12, 12, 24, 24};
    gGpuNotePass = true;
    const bool queued = drawNoteImage(gBackBufferDc, gNoteImages.find(NotePart::Tap, true, 4), 8, 8, 28, 28, clip);
    gGpuNotePass = false;
    require(queued, "Note sprite submission failed");
    std::vector<uint32_t> black(size_t(gWindowWidth) * gWindowHeight, 0xff000000u), captured;
    require(GpuTestAccess::draw(gCompositionPresenter, black, gWindowWidth) &&
        gCompositionPresenter.captureLastFrame(captured, gWindowWidth, gWindowHeight), "Note GPU capture failed");
    require((captured[16 * size_t(gWindowWidth) + 16] & 0xffffffu) == 0x0000ff &&
        (captured[16 * size_t(gWindowWidth) + 10] & 0xffffffu) == 0, "GPU sprite or clip incorrect");
    gCompositionPresenter.reset();
    Bytes atlas(324 * 360 * 4);
    for (int y = 0; y < 360; ++y) {
        for (int x = 0; x < 324; ++x) {
            const size_t i = (y * 324 + x) * 4;
            atlas[i] = static_cast<BYTE>(x / 2);
            atlas[i + 1] = static_cast<BYTE>(y / 2);
            atlas[i + 3] = 255;
        }
    }
    save(notes / L"NoteGroup.png", atlas, 324, 360);
    require(applySkinFile(skin, false), "NoteGroup skin load failed");
    const auto* green = gNoteImages.find(NotePart::Tap, true, 3);
    const auto* white = gNoteImages.find(NotePart::Tap, false, 7);
    const auto* blueBody = gNoteImages.find(NotePart::LongBody, false, 2);
    const auto* red = gNoteImages.find(NotePart::Tap, true, 4);
    require(green && green->width == 108 && white && white->pixels[0] == 0xff00006c &&
        blueBody && blueBody->pixels[0] == 0xff00368a && red && red->width == 1,
        "NoteGroup crop or individual image priority incorrect");
    // Idle body: the empty y108 cell, never animated. Held body: the glowing y144 and y180 cells alternate.
    require(gNoteImages.find(NotePart::LongBody, false, 2, 1) == blueBody && gNoteImages.find(NotePart::LongBody, false, 2, 7) == blueBody,
        "The default sheet's idle long-note body must not animate");
    const auto heldPixel = [&](NotePart part, long long tick) {
        const auto* image = gNoteImages.find(part, false, 2, tick);
        return image ? image->pixels[0] : 0u;
    };
    require(heldPixel(NotePart::HeldBody, 0) == 0xff00488au && heldPixel(NotePart::HeldBody, 1) == 0xff005a8au &&
        heldPixel(NotePart::HeldBody, 2) == 0xff00488au, "Default sheet held body must alternate the y144 and y180 cells");
    require(gNoteImages.find(NotePart::HeldStart, false, 2) == gNoteImages.find(NotePart::LongStart, false, 2) &&
        gNoteImages.find(NotePart::HeldEnd, false, 2) == gNoteImages.find(NotePart::LongEnd, false, 2),
        "The default sheet's held head and tail must use the normal ones");
    std::filesystem::remove(notes / L"NoteGroup.png");

    // A NoteGroup.ini declares a different sheet layout; cells with Width=0 are disabled.
    Bytes sheet(100 * 72 * 4);
    for (int y = 0; y < 72; ++y) {
        for (int x = 0; x < 100; ++x) {
            const size_t i = (y * 100 + x) * 4;
            sheet[i] = static_cast<BYTE>(x);
            sheet[i + 1] = static_cast<BYTE>(y);
            sheet[i + 3] = 255;
        }
    }
    save(notes / L"NoteGroup.png", sheet, 100, 72);
    {
        std::ofstream ini(notes / L"NoteGroup.ini");
        ini << "[Sheet]\nWidth=100\nHeight=72\nCellHeight=36\n[Parts]\nTap=0\nLongStart=36\nLongBody=0\nLongEnd=36\n"
               "[normal-white]\nX=10\nWidth=20\n[normal-blue]\nWidth=0\n[scratch-green]\nX=0\nWidth=10\n";
    }
    auto warnings = gNoteImages.load(skin);
    const auto* declaredWhite = gNoteImages.find(NotePart::Tap, false, 7);
    const auto* declaredGreen = gNoteImages.find(NotePart::Tap, true, 3);
    require(warnings.empty(), "NoteGroup.ini layout produced warnings");
    require(declaredWhite && declaredWhite->width == 20 && declaredWhite->height == 36,
        "NoteGroup.ini white cell size not honoured");
    require(declaredWhite->pixels[0] == 0xff00000au, "NoteGroup.ini white cell offset not honoured");
    require(declaredGreen && declaredGreen->width == 10, "NoteGroup.ini scratch cell not honoured");
    require(!gNoteImages.find(NotePart::Tap, false, 2), "NoteGroup.ini Width=0 did not disable the cell");
    // Without a manifest a sheet of another size is ignored, and the host is told why.
    std::filesystem::remove(notes / L"NoteGroup.ini");
    warnings = gNoteImages.load(skin);
    require(warnings.size() == 1 && !gNoteImages.find(NotePart::Tap, false, 7), "Mismatched NoteGroup size was not reported");
    // Long-note body animation: LongBodyFrames cells follow the body cell downwards.
    Bytes tall(100 * 108 * 4);
    for (int y = 0; y < 108; ++y) {
        for (int x = 0; x < 100; ++x) {
            const size_t i = (y * 100 + x) * 4;
            tall[i] = static_cast<BYTE>(x);
            tall[i + 1] = static_cast<BYTE>(y);
            tall[i + 3] = 255;
        }
    }
    save(notes / L"NoteGroup.png", tall, 100, 108);
    const auto writeBodyIni = [&](int frames, int fps) {
        std::ofstream ini(notes / L"NoteGroup.ini");
        ini << "[Sheet]\nWidth=100\nHeight=108\nCellHeight=36\n[Parts]\nLongBody=0\nLongBodyFrames=" << frames
            << "\nLongBodyFps=" << fps << "\n[normal-white]\nX=10\nWidth=20\n";
    };
    writeBodyIni(3, 24);
    warnings = gNoteImages.load(skin);
    const auto heldPixelAt = [&](long long tick) {
        const auto* body = gNoteImages.find(NotePart::HeldBody, false, 7, tick);
        return body ? body->pixels[0] : 0u;
    };
    const auto bodyAt = [&](long long tick) {
        const auto* body = gNoteImages.find(NotePart::LongBody, false, 7, tick);
        return body ? body->pixels[0] : 0u;
    };
    require(warnings.empty() && gNoteImages.bodyFps() == 24, "Body animation manifest produced warnings or ignored the fps");
    require(bodyAt(0) == 0xff00000au && bodyAt(1) == 0xff00240au && bodyAt(2) == 0xff00480au,
        "Body animation frames were not read from the sheet");
    require(bodyAt(3) == bodyAt(0) && bodyAt(-1) == bodyAt(2), "Body animation frame index does not wrap");
    writeBodyIni(4, 12);
    warnings = gNoteImages.load(skin);
    require(warnings.size() == 1 && bodyAt(3) == bodyAt(0), "Body frame outside the sheet was not reported or truncated");
    writeBodyIni(1, 12);
    gNoteImages.load(skin);
    require(bodyAt(1) == bodyAt(0), "A single-frame body must stay static");
    // A manifest declares no held cells by default: the held parts then fall back to the normal ones.
    require(gNoteImages.find(NotePart::HeldStart, false, 7) == gNoteImages.find(NotePart::LongStart, false, 7) &&
        gNoteImages.find(NotePart::HeldBody, false, 7, 3) == gNoteImages.find(NotePart::LongBody, false, 7, 3),
        "Undeclared held cells must fall back to the normal long-note images");
    {
        std::ofstream ini(notes / L"NoteGroup.ini");
        ini << "[Sheet]\nWidth=100\nHeight=108\nCellHeight=36\n[Parts]\nLongBody=0\nLongBodyFrames=1\nHeldLongBody=36\n"
               "HeldLongBodyFrames=2\nHeldLongBodyFps=30\n[normal-white]\nX=10\nWidth=20\n";
    }
    warnings = gNoteImages.load(skin);
    require(warnings.empty() && gNoteImages.bodyFps(true) == 30 && gNoteImages.bodyFps(false) == 12, "Held body manifest not read");
    require(heldPixelAt(0) == 0xff00240au && heldPixelAt(1) == 0xff00480au && heldPixelAt(2) == 0xff00240au,
        "Held body frames were not read from the declared cells");
    require(bodyAt(0) == 0xff00000au && bodyAt(1) == 0xff00000au, "The normal body changed when a held body was declared");
    std::filesystem::remove(notes / L"NoteGroup.ini");
    std::filesystem::remove(notes / L"NoteGroup.png");

    // Named sheets: <name>-NoteGroup.png with its own <name>-NoteGroup.ini.
    save(notes / L"000-NoteGroup.png", sheet, 100, 72);
    save(notes / L"001-NoteGroup.png", sheet, 100, 72);
    for (const auto* name : {"000", "001"}) {
        std::ofstream ini(notes / (std::wstring(name, name + 3) + L"-NoteGroup.ini"));
        ini << "[Sheet]\nWidth=100\nHeight=72\nCellHeight=36\n[Parts]\nLongStart=36\nLongBody=0\nLongEnd=36\n[normal-white]\nX=" << (name[2] == '0' ? 10 : 30) << "\nWidth=20\n";
    }
    const auto names = skinNoteGroupFiles(skin.parent_path());
    require(names.size() == 2 && names[0].first == L"000" && names[1].first == L"001", "Named sheets were not listed by name");
    warnings = gNoteImages.load(skin);
    const auto* automatic = gNoteImages.find(NotePart::Tap, false, 7);
    require(warnings.empty() && automatic && automatic->pixels[0] == 0xff00000au, "Automatic selection did not use the first named sheet");
    warnings = gNoteImages.load(skin, L"001");
    require(warnings.empty() && gNoteImages.find(NotePart::Tap, false, 7)->pixels[0] == 0xff00001eu,
        "Selected named sheet or its own ini was not used");
    warnings = gNoteImages.load(skin, L"nope");
    require(warnings.size() == 1 && gNoteImages.find(NotePart::Tap, false, 7), "Missing selected sheet was not reported");
    require(skinSettingDecode(skinSettingEncode(L"한글-1")) == L"한글-1" && skinSettingEncode(L"").empty(),
        "Sheet name setting round trip failed");
    // The upgrade keeps the old sheet beside the bundled replacement. Custom skins keep legacy precedence.
    for (const auto* ext : {L".png", L".ini"}) {
        std::filesystem::rename(notes / (std::wstring(L"000-NoteGroup") + ext), notes / (std::wstring(L"NoteGroup") + ext));
        std::filesystem::rename(notes / (std::wstring(L"001-NoteGroup") + ext), notes / (std::wstring(L"Default-NoteGroup") + ext));
    }
    const auto checkAutoSheet = [&](bool bundled, std::uint32_t expected) {
        const auto messages = gNoteImages.load(skin, {}, bundled);
        const auto* tap = gNoteImages.find(NotePart::Tap, false, 7);
        require(messages.empty() && tap && tap->pixels[0] == expected, "Incorrect automatic sheet after upgrade");
    };
    checkAutoSheet(true, 0xff00001eu);
    checkAutoSheet(false, 0xff00000au);
    std::filesystem::remove(notes / L"NoteGroup.png");
    checkAutoSheet(true, 0xff00001eu);
    warnings = gNoteImages.load(defaultSkinPath());
    require(warnings.empty(), "The default skin's note sheet was rejected");
    require(gNoteImages.find(NotePart::Tap, false, 2) && gNoteImages.find(NotePart::Tap, false, 7) &&
        gNoteImages.find(NotePart::Tap, true, 3) && gNoteImages.find(NotePart::Tap, true, 4) &&
        gNoteImages.find(NotePart::LongStart, false, 7) && gNoteImages.find(NotePart::LongEnd, false, 7),
        "The default skin's note sheet lacks tap or long-note cells");
    require(gNoteImages.find(NotePart::HeldBody, false, 2, 0) != gNoteImages.find(NotePart::HeldBody, false, 2, 1) &&
        gNoteImages.find(NotePart::LongBody, false, 2, 0) == gNoteImages.find(NotePart::LongBody, false, 2, 1),
        "The default skin's held body must alternate while the idle body stays still");
    gPlaying = false;
    chart.notes.clear();
    {   // 1280x720: lanes, note height and scroll distance scale by 2/3, so notes keep their travel time.
        // The game keeps its back buffer at the client size; match the test window to it.
        RECT savedWindow{}; GetWindowRect(gWindow, &savedWindow);
        RECT frame{0, 0, 1280, 720}; AdjustWindowRect(&frame, static_cast<DWORD>(GetWindowLongPtrW(gWindow, GWL_STYLE)), FALSE);
        SetWindowPos(gWindow, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        RECT client{}; GetClientRect(gWindow, &client);
        require(client.right == 1280 && client.bottom == 720, "Test window did not take the 720p client size");
        gWindowWidth = 1280; gWindowHeight = 720; refreshUiScale();
        require(std::abs(gUiScale - 2.0 / 3.0) < 1e-9 && gNoteHeight == 24, "720p scale or note height mismatch");
        const auto scaledWidths = bms::chartDeckWidths(chart, gUiScale);
        require(scaledWidths.key == 40 && scaledWidths.scratch == 72, "Lane widths did not scale");
        chart.notes = {{0, 0.25}, {1, 0.25}};chart.mines.clear();
        gHit.assign(chart.notes.size(), 0);gMineHit.clear();for (auto& hold : gHolds) hold = {};gLingeringLongNotes.clear();gReleasedLongNotes.clear();gLongNoteIndex = {};
        gPlayhead = 0; gNoteHeight = 12;
        auto scaledLayout = bms::chartLayout(chart, 0, scaledWidths.key, scaledWidths.scratch);
        scaledLayout.fieldX = std::max(0, gWindowWidth - scaledLayout.totalWidth()) / 2;
        const int scaledJudge = judgementLineY(gWindowHeight);
        ensureBackBuffer(nullptr, gWindowWidth, gWindowHeight);
        drawGameScreen(gBackBufferDc);
        GdiFlush();
        const auto scaledAt = [&](int lane, int x, int y) {
            return static_cast<const uint32_t*>(gBackBufferPixels)[size_t(y) * gBackBufferWidth + scaledLayout.left(lane) + x] & 0xffffffu;
        };
        // 270 * 2/3 px/s * 0.25 s = 45px above the line (FHD: 67.5px). Something must be drawn in each lane
        // within the scaled band and nothing at the unscaled distance.
        const auto drawnBetween = [&](int lane, int from, int to) {
            for (int y = from; y <= to; ++y) if (scaledAt(lane, 6, scaledJudge - y) != 0x010203) return true;
            return false;
        };
        require(drawnBetween(0, 33, 46) && drawnBetween(1, 33, 46), "Scaled tap note missing at the scaled distance");
        require(!drawnBetween(0, 55, 70) && !drawnBetween(1, 55, 70), "Scaled note drawn at the FHD distance");
        SetWindowPos(gWindow, nullptr, 0, 0, savedWindow.right - savedWindow.left, savedWindow.bottom - savedWindow.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    gWindowWidth = savedWidth; gWindowHeight = savedHeight; refreshUiScale();
    chart.mines.clear();
    gHit.clear();
    gMineHit.clear();
    clearSkinSelection(false);
}
void verifyExternalSkinAssets(const std::filesystem::path& output) {
    const auto root = output / L"external-assets";
    const auto gear = root / L"Gear";
    const auto effects = root / L"HitEffects";
    const auto skin = root / L"test.semaskin";
    for (const auto* name : {L"Buttons-Black", L"Buttons-White", L"Scratch"})
        std::filesystem::create_directories(gear / name);
    std::filesystem::create_directories(effects);
    std::filesystem::copy_file(defaultSkinPath(), skin, std::filesystem::copy_options::overwrite_existing);
    const Bytes blue{255,0,0,255};
    const auto arbitrary = gear / L"Buttons-Black" / L"arbitrary.texture";
    const auto second = gear / L"Buttons-Black" / L"duplicate.png";
    const auto encoded = root / L"source.png";
    save(encoded, blue, 1, 1);
    std::filesystem::copy_file(encoded, arbitrary, std::filesystem::copy_options::overwrite_existing);
    { std::ofstream invalid(gear / L"Buttons-White" / L"broken.png"); invalid << "bad image"; }
    save(gear / L"Scratch" / L"custom.png", blue, 1, 1);
    for (int number : {10,2,1}) {
        Bytes pixels(24, static_cast<BYTE>(number));
        for (size_t i=3; i<pixels.size(); i+=4) pixels[i] = 255;
        save(effects / (std::to_wstring(number) + L".png"), pixels, 6, 1);
    }
    save(effects / L"3.png", blue, 1, 1);
    // Frame count comes from an optional <number>.ini: 4.png has 4 frames, 5.png asks for an invalid 7 (6 px wide).
    save(effects / L"4.png", Bytes(16, 255), 4, 1);
    { std::ofstream ini(effects / L"4.ini"); ini << "[HitEffect]\nFrames=4\n"; }
    save(effects / L"5.png", Bytes(24, 255), 6, 1);
    { std::ofstream ini(effects / L"5.ini"); ini << "[HitEffect]\nFrames=7\n"; }
    save(effects / L"01.png", Bytes(24, 255), 6, 1);
    require(applySkinFile(skin,false), "External asset skin load failed");
    const auto resource = [](std::string_view id) {
        const auto* screen = gSkinApplication.screenLayout(ScreenKind::Play);
        for (const auto& element : screen->elements) if (element.id == id) return element.resource;
        return std::string{};
    };
    require(resource("gear-key-2") == "external-black-key" && resource("gear-key-1") == "white-key" &&
        resource("gear-scratch-disc") == "external-disc", "Gear override or per-part fallback incorrect");
    std::vector<int> ids;
    for (const auto& [number,image] : gSkinHitEffects) ids.push_back(number);
    require(ids == std::vector<int>{1,2,4,10} && gSkinHitEffect == 1, "Hit effect ordering/default incorrect");
    require(gSkinHitEffectFrames.at(1) == 6 && gSkinHitEffectFrames.at(4) == 4, "Hit effect frame count not read");
    WritePrivateProfileStringW(L"Skin",L"HitEffect",L"10",gSettingsFile.c_str());
    require(applySkinFile(skin,false) && gSkinHitEffect == 10, "Hit effect selection was not persisted");
    std::filesystem::copy_file(encoded, second, std::filesystem::copy_options::overwrite_existing);
    require(std::filesystem::remove(effects / L"10.png"), "Cannot remove cached hit effect");
    require(applySkinFile(skin,false) && resource("gear-key-2") == "external-black-key" && gSkinHitEffect == 1,
        "Extra Gear candidate disabled the part, or absent saved number did not fall back");
    auto packaged = std::make_shared<Bytes>();
    {
        std::ifstream png(effects / L"2.png", std::ios::binary);
        packaged->assign(std::istreambuf_iterator<char>(png), {});
    }
    for (const auto* name : {L"1.png", L"2.png", L"4.png"}) std::filesystem::remove(effects / name);
    std::filesystem::remove(second);
    loadSkinHitEffects(root, packaged);
    uint32_t spritePixel{};
    gFeedback.hit(1, 0);
    gFeedback.drawSprites(0, 68, 300, 0.1, 104,
        [&](const uint32_t* pixels, int, int, int, int, int, int, uint64_t, float) { spritePixel = pixels[0]; });
    require(gSkinHitEffects.empty() && gSkinHitEffect == 0 && spritePixel == 0xff020202, "Packaged hit effect fallback failed");
    gFeedback.reset();
    clearSkinSelection(false);
}
void verifySelectShortcuts(const std::filesystem::path& output) {
    require(applySkinFile(defaultSkinPath(),false),"Reference skin failed to load");
    gPlaying=false;gShowingResult=false;gLoading=false;gStartupSplash=false;
    gCharts[0].title=L"Song"; // Keep marquee animation out of the unchanged-value cache check.
    const auto* activeScreen=gSkinApplication.screenLayout(ScreenKind::Select);
    require(activeScreen,"Reference data missing");
    const auto& screen=*activeScreen;
    const auto oldWidth=gWindowWidth,oldHeight=gWindowHeight;
    for(const auto [width,height]:{std::pair{1920,1080},std::pair{1280,720},std::pair{1024,768},std::pair{2560,1440},std::pair{2560,1080}}){
        gWindowWidth=width;gWindowHeight=height;
        const auto view=bms::ui::viewport(width,height,screen.resolution.width,screen.resolution.height);
        const auto anchors=sevenmania::skin::computeAnchors(screen.elements,screen.resolution);
        for(const int slot:{0,2}){ // slot-1 (judgement profile) was removed: the profile is pinned to Normal
            const auto found=std::find_if(screen.elements.begin(),screen.elements.end(),[&](const auto& e){return e.id=="slot-"+std::to_string(slot);});
            require(found!=screen.elements.end(),"Reference slot missing");
            const auto mapped=bms::ui::mapRect(view,{found->x,found->y,found->width,found->height},anchors[std::size_t(found-screen.elements.begin())]);
            const int x=int(mapped.x+mapped.width/2),y=int(mapped.y+mapped.height/2);
            require(selectSkinShortcutAt(x,y)==slot,"Scaled skin hit area mismatch");
            gNoteSpeed=1;gJudgementProfile=JudgementProfile::Normal;gGrooveGaugeMode=GrooveGaugeMode::Normal;
            SendMessageW(gWindow,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
            require(slot==0?gNoteSpeed==1.25:slot==1?gJudgementProfile==JudgementProfile::Hard:gGrooveGaugeMode==GrooveGaugeMode::Hard,"Left click did not change setting");
            SkinDataProvider provider;require(captureSevenManiaSkinData(provider),"Shortcut snapshot failed");
            const auto id=slot==0?BindingId::SelectSpeedText:slot==1?BindingId::SelectJudgementText:BindingId::SelectGaugeMode;
            require(std::get<std::string>(provider.snapshot().value(id))==(slot==0?"1.25x":"HARD"),"Shortcut display value stale");
            require(slot==0?GetPrivateProfileIntW(L"Display",L"SpeedSteps",99,gSettingsFile.c_str())==5:
                GetPrivateProfileIntW(L"Gameplay",L"GrooveGaugeMode",99,gSettingsFile.c_str())!=99,"Shortcut settings not saved");
            SendMessageW(gWindow,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(x,y));
            require(gNoteSpeed==1&&gJudgementProfile==JudgementProfile::Normal&&gGrooveGaugeMode==GrooveGaugeMode::Normal,"Right click did not decrease setting");
        }
        require(selectSkinShortcutAt(width-1,0)==-1,"Outside click selected shortcut");
    }
    gWindowWidth=1920;gWindowHeight=1080;ensureBackBuffer(nullptr,1920,1080);
    const auto activate=[&](std::string_view id) {
        const auto found=std::find_if(screen.elements.begin(),screen.elements.end(),[&](const auto& e){return e.id==id;});
        return found!=screen.elements.end()&&adjustBrowseShortcut(int(found->x+found->width/2),int(found->y+found->height/2),true);
    };
    drawBrowseScreen(gBackBufferDc);const auto before=gSkinApplication.readbacks();
    require(activate("slot-0"),"Reference speed click missed");drawBrowseScreen(gBackBufferDc);
    require(gSkinApplication.readbacks()>before,"Changed speed did not redraw cached overlay");
    const auto speedRead=gSkinApplication.readbacks();
    require(activate("slot-2"),"Reference gauge click missed");drawBrowseScreen(gBackBufferDc);
    require(gSkinApplication.readbacks()>speedRead,"Changed gauge did not redraw cached overlay");
    GdiFlush();save(output/L"select-shortcuts.png",Bytes(static_cast<BYTE*>(gBackBufferPixels),static_cast<BYTE*>(gBackBufferPixels)+1920*1080*4),1920,1080);
    const auto stable=gSkinApplication.readbacks();drawBrowseScreen(gBackBufferDc);require(gSkinApplication.readbacks()==stable,"Unchanged shortcut rerendered skin");
    gWindowWidth=oldWidth;gWindowHeight=oldHeight;
}
void verifyGpu(const std::filesystem::path& output) {
    gPlaying=true;gPaused=false;gGpuFrame=true;gActiveChartIndex=0;gResultScore.reset(10);gFeedback.reset();
    require(gCompositionPresenter.prepare(gWindow,gWindowWidth,gWindowHeight),"GPU presenter failed");prepareAppliedSkinGpu();
    const auto readbacks=gSkinApplication.readbacks();
    std::vector<std::uint32_t> base(std::size_t(gWindowWidth)*gWindowHeight,0xff203040u);
    gCompositionPresenter.rectangle(0,0,20,20,RGB(0,0,255));
    require(GpuTestAccess::draw(gCompositionPresenter,base,gWindowWidth),"GPU skin draw failed");
    std::vector<std::uint32_t> captured;require(gCompositionPresenter.captureLastFrame(captured,gWindowWidth,gWindowHeight),"GPU capture failed");
    require((captured[0]&0xffffff)==0x0000ff,"Skin covered native foreground");
    require(captured!=base,"GPU overlay missing");
    Bytes pixels(reinterpret_cast<BYTE*>(captured.data()),reinterpret_cast<BYTE*>(captured.data())+captured.size()*4);
    save(output/L"applied-gpu.png",pixels,gWindowWidth,gWindowHeight);
    for(int i=0;i<10;++i){gResultScore.apply(TimingJudgement::PGreat);prepareAppliedSkinGpu();require(GpuTestAccess::draw(gCompositionPresenter,base,gWindowWidth),"Live GPU update failed");}
    require(gSkinApplication.readbacks()==readbacks,"Gameplay used CPU overlay readback");
    // Exercise the real gameplay backdrop together with the applied skin.
    drawGameScreen(gBackBufferDc);GdiFlush();prepareAppliedSkinGpu();
    require(GpuTestAccess::draw(gCompositionPresenter,base,gWindowWidth),"Retained gameplay with skin failed");
    require(gCompositionPresenter.captureLastFrame(captured,gWindowWidth,gWindowHeight),"Retained skin capture failed");
    require(gSkinApplication.readbacks()==readbacks,"Retained gameplay introduced skin readback");
    save(output/L"retained-gameplay.png",Bytes(reinterpret_cast<BYTE*>(captured.data()),reinterpret_cast<BYTE*>(captured.data())+captured.size()*4),gWindowWidth,gWindowHeight);
    require(gCompositionPresenter.prepare(gWindow,gWindowWidth,gWindowHeight),"Reset gameplay draw commands failed");prepareAppliedSkinGpu();
    showTimingJudgement(TimingJudgement::PGreat);prepareAppliedSkinGpu();
    gCompositionPresenter.rectangle(0,0,float(gWindowWidth),float(gWindowHeight),RGB(0,0,0));
    gCompositionPresenter.setFrameForeground(nullptr);
    require(GpuTestAccess::draw(gCompositionPresenter,base,gWindowWidth),"Occluded judgement reproduction failed");
    require(gCompositionPresenter.captureLastFrame(captured,gWindowWidth,gWindowHeight),"Occluded capture failed");
    require(std::all_of(captured.begin(),captured.end(),[](auto pixel){return (pixel&0xffffff)==0;}),"Occlusion test did not cover old overlay stage");
    prepareAppliedSkinGpu();
    require(GpuTestAccess::draw(gCompositionPresenter,base,gWindowWidth),"Foreground judgement render failed");
    require(gCompositionPresenter.captureLastFrame(captured,gWindowWidth,gWindowHeight),"Foreground capture failed");
    require(std::any_of(captured.begin(),captured.end(),[](auto pixel){return (pixel&0xffffff)!=0;}),"Native draw commands covered skin judgement");
    save(output/L"judgement-foreground.png",Bytes(reinterpret_cast<BYTE*>(captured.data()),reinterpret_cast<BYTE*>(captured.data())+captured.size()*4),gWindowWidth,gWindowHeight);
    gCompositionPresenter.reset();require(gCompositionPresenter.prepare(gWindow,gWindowWidth,gWindowHeight),"GPU recreation failed");
    prepareAppliedSkinGpu();require(GpuTestAccess::draw(gCompositionPresenter,base,gWindowWidth),"Recreated GPU skin draw failed");
    gPaused=true;showTimingJudgement(TimingJudgement::PGreat);prepareAppliedSkinGpu();
    // An opaque pause/menu surface must cover judgement, unlike live gameplay notes.
    gCompositionPresenter.rectangle(0,0,float(gWindowWidth),float(gWindowHeight),RGB(0,0,0));
    require(GpuTestAccess::draw(gCompositionPresenter,base,gWindowWidth),"Paused skin draw failed");
    require(gCompositionPresenter.captureLastFrame(captured,gWindowWidth,gWindowHeight),"Paused skin capture failed");
    require(std::all_of(captured.begin(),captured.end(),[](auto pixel){return (pixel&0xffffff)==0;}),"Judgement rendered over pause/menu surface");
    save(output/L"pause-judgement-layer.png",Bytes(reinterpret_cast<BYTE*>(captured.data()),reinterpret_cast<BYTE*>(captured.data())+captured.size()*4),gWindowWidth,gWindowHeight);
    gPaused=false;prepareAppliedSkinGpu();require(GpuTestAccess::draw(gCompositionPresenter,base,gWindowWidth),"Resume skin draw failed");
    require(gCompositionPresenter.captureLastFrame(captured,gWindowWidth,gWindowHeight),"Resume skin capture failed");
    require(std::any_of(captured.begin(),captured.end(),[](auto pixel){return (pixel&0xffffff)!=0;}),"Resume did not restore judgement foreground");
    gPaused=false;gPlaying=false;gGpuFrame=false;gCompositionPresenter.reset();
    drawBrowseScreen(gBackBufferDc); // drops the obsolete GPU resource domain
}
void verifyTransitions() {
    gPlaying=true;gGpuFrame=false;const auto before=gSkinApplication.readbacks();
    drawGameScreen(gBackBufferDc);require(gSkinApplication.readbacks()>before,"CPU gameplay overlay missing");
    gPlaying=false;gShowingResult=true;drawResultScreen(gBackBufferDc);
    const auto resultReads=gSkinApplication.readbacks();require(resultReads>before+1,"Result overlay missing");
    drawResultScreen(gBackBufferDc);require(gSkinApplication.readbacks()==resultReads,"Unchanged result reread GPU surface");
    SkinDataProvider data;data.beginFrame(ScreenKind::Result);data.set(BindingId::ResultScore,std::int64_t(100));data.endFrame();
    ensureBackBuffer(nullptr,640,360);
    require(gSkinApplication.drawDc(gBackBufferDc,640,360,data.snapshot()),"Overlay resize failed");
    data.beginFrame(ScreenKind::Result);data.endFrame();const auto reads=gSkinApplication.readbacks();
    require(gSkinApplication.drawDc(gBackBufferDc,640,360,data.snapshot())&&gSkinApplication.readbacks()>reads,"Empty text retained old cached pixels");
    require(!gSkinApplication.drawDc(gBackBufferDc,100000,360,data.snapshot()),"Oversized viewport accepted");
    require(gSkinApplication.drawDc(gBackBufferDc,640,360,data.snapshot()),"Valid viewport did not recover");
    ensureBackBuffer(nullptr,gWindowWidth,gWindowHeight);gShowingResult=false;
}
void verifyResourceFps(const std::filesystem::path& output) {
    const bool oldUsage=gShowResourceUsage,oldPlaying=gPlaying,oldResult=gShowingResult,oldComposition=gCompositionEnabled;
    gPlaying=true;gShowingResult=false;gPaused=false;gLoading=false;gStartupSplash=false;
    gShowResourceUsage=false;recordResourceFrame(1000);require(gFrameStatistics.average()==0,"FPS collected while resource overlay disabled");
    gShowResourceUsage=true;recordResourceFrame(1000);
    for(int i=1;i<=600;++i)recordResourceFrame(1000+i*.001);
    require(std::abs(gFrameStatistics.current()-1000)<.01&&std::abs(gFrameStatistics.average()-1000)<.01&&std::abs(gFrameStatistics.lowOnePercent()-1000)<.01,"Normal build FPS statistics not collected");
    gPaused=true;recordResourceFrame(1100);gPaused=false;recordResourceFrame(1100.001);recordResourceFrame(1100.002);
    require(std::abs(gFrameStatistics.average()-1000)<.01,"Pause duration affected average FPS");
    gLoading=true;recordResourceFrame(1200);gLoading=false;recordResourceFrame(1200.001);recordResourceFrame(1200.002);
    require(std::abs(gFrameStatistics.average()-1000)<.01,"Loading duration affected average FPS");
    gCompositionEnabled=false;SystemResourceSnapshot values{};values.cpuPercent=12;
    const auto lines=resourceUsageLines(values);
    require(lines[0]==L"CPU = 12%"&&lines[1].starts_with(L"Memory = ")&&lines[3]==L"FPS NOW = 1000"&&lines[4]==L"FPS AVG = 1000"&&lines[5]==L"FPS 1% LOW = 1000","Resource FPS labels incorrect");
    ensureBackBuffer(nullptr,gWindowWidth,gWindowHeight);
    GdiFlush();auto* pixels=static_cast<std::uint32_t*>(gBackBufferPixels);
    std::fill_n(pixels,std::size_t(gWindowWidth)*gWindowHeight,0xff000000u);
    drawResourceUsageOverlay(gBackBufferDc);GdiFlush();
    bool fpsPixels=false;
    for(int y=58;y<124;++y)for(int x=gWindowWidth-260;x<gWindowWidth-18;++x)fpsPixels=fpsPixels||(pixels[std::size_t(y)*gWindowWidth+x]&0xffffff)!=0;
    require(fpsPixels,"FPS text missing below CPU and memory");
    save(output/L"resource-fps.png",Bytes(reinterpret_cast<BYTE*>(pixels),reinterpret_cast<BYTE*>(pixels)+std::size_t(gWindowWidth)*gWindowHeight*4),gWindowWidth,gWindowHeight);
    const bool oldPractice=gPractice.enabled;
    gPractice.enabled=true;gCompositionEnabled=true;gGpuFrame=true;
    require(gCompositionPresenter.prepare(gWindow,gWindowWidth,gWindowHeight),"Resource GPU prepare failed");
    drawResourceUsageOverlay(gBackBufferDc);
    std::vector<std::uint32_t> black(std::size_t(gWindowWidth)*gWindowHeight,0xff000000u),gpuPixels;
    require(GpuTestAccess::draw(gCompositionPresenter,black,gWindowWidth)&&gCompositionPresenter.captureLastFrame(gpuPixels,gWindowWidth,gWindowHeight),"Resource GPU capture failed");
    bool lowVisible=false;
    for(int y=201;y<213;++y)for(int x=gWindowWidth-260;x<gWindowWidth-18;++x)lowVisible=lowVisible||(gpuPixels[std::size_t(y)*gWindowWidth+x]&0xffffff)!=0;
    require(lowVisible,"Practice 1% Low clipped outside old CPU upload area");
    save(output/L"resource-fps-gpu-practice.png",Bytes(reinterpret_cast<BYTE*>(gpuPixels.data()),reinterpret_cast<BYTE*>(gpuPixels.data())+gpuPixels.size()*4),gWindowWidth,gWindowHeight);
    gCompositionPresenter.reset();gGpuFrame=false;gPractice.enabled=oldPractice;gCompositionEnabled=false;
    gPlaying=false;gShowingResult=true;recordResourceFrame(1300);require(gFrameStatistics.average()==0,"FPS retained across screen change");
    recordResourceFrame(1300+1./60);require(std::abs(gFrameStatistics.average()-60)<.01,"Result screen FPS incorrect");
    gShowResourceUsage=false;recordResourceFrame(1400);
    require(gFrameStatistics.average()==0,"FPS reset failed when disabled");
    GdiFlush();std::fill_n(pixels,std::size_t(gWindowWidth)*gWindowHeight,0xff000000u);drawResourceUsageOverlay(gBackBufferDc);
    require(std::all_of(pixels,pixels+std::size_t(gWindowWidth)*gWindowHeight,[](auto p){return (p&0xffffff)==0;}),"Disabled resource overlay still visible");
    gShowResourceUsage=oldUsage;gPlaying=oldPlaying;gShowingResult=oldResult;gCompositionEnabled=oldComposition;
    gFrameStatistics.reset();gFrameStatisticsView=-1;
}
}
int wmain(int argc,wchar_t** argv) {
    const bool noteImagesOnly = argc == 4 && std::wstring_view(argv[3]) == L"--note-images";
    if(argc!=3 && !noteImagesOnly)return 2;
    const auto fixtures=std::filesystem::absolute(argv[1]);const auto output=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(output);
    const auto config=output/(L"skin-"+std::to_wstring(GetCurrentProcessId())+L".ini");
    Gdiplus::GdiplusStartupInput startup;Gdiplus::GdiplusStartup(&gGdiPlusToken,&startup,nullptr);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int result=0;
    try {
        gSettingsFile=config;WritePrivateProfileStringW(L"Display",L"ScreenMode",L"2",config.c_str());
        INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&common);
        registerType(L"SkinApplicationOwner",windowProc);registerType(L"BmsPlayerSettingsWindow",settingsProc);registerType(L"BmsPlayerSettingsBody",settingsBodyProc);
        registerType(L"BmsPlayerSettingsPreviewOverlay",settingsPreviewOverlayProc);
        gWindow=CreateWindowW(L"SkinApplicationOwner",L"Skin test",WS_OVERLAPPEDWINDOW,0,0,1280,860,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);require(gWindow!=nullptr,"Create owner failed");
        bms::Chart chart;chart.path=output/L"sample.bms";chart.title=L"Actual song";chart.artist=L"Artist";chart.playLevel=L"9";chart.initialBpm=120;chart.playable=true;chart.mode=bms::ChartMode::SevenKey;
        gCharts={chart};gActiveChartIndex=0;gSongSelection.rebuild(gCharts);gSongSelection.selectGroup(0);gStartupSplash=false;gLoading=false;
        if (noteImagesOnly) {
            verifyNoteImages(fixtures/L"loader-result.semaskin", output);
            verifyExternalSkinAssets(output);
            std::cout << "Note image checks: " << checks << "\n";
            DestroyWindow(gWindow);
            gWindow=nullptr;
            Gdiplus::GdiplusShutdown(gGdiPlusToken);
            CoUninitialize();
            return 0;
        }
        auto settings=CreateWindowExW(WS_EX_CONTROLPARENT,L"BmsPlayerSettingsWindow",L"Skin settings",WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_VSCROLL|WS_CLIPCHILDREN,
            0,0,780,820,gWindow,nullptr,GetModuleHandleW(nullptr),nullptr);require(settings!=nullptr,"Create settings failed");
        selectSettingsCategory(5);
        const bool rml=bms::ui::RmlSettingsHost::available();
        if(rml){paintPage();require(gRmlSettingsPageActive,"RmlUi settings failed");}
        else require(!gRmlSettingsPageActive&&IsWindow(gSkinNativeStatus),"Native-only settings failed");
        const auto external=output/L"한글 & 외부 🎵 스킨.semaskin";std::filesystem::copy_file(fixtures/L"data-binding.semaskin",external,std::filesystem::copy_options::overwrite_existing);
        WritePrivateProfileStringW(L"Skin",L"JudgementMode",L"0",config.c_str());
        WritePrivateProfileStringW(L"Skin",L"ComboMode",L"-1",config.c_str());
        WritePrivateProfileStringW(L"Skin",L"MirrorGearUi",L"1",config.c_str());
        loadDisplaySettings();
        require(gSkinJudgementMode==0&&gSkinComboMode==-1&&gSkinMirrorUi,"Launcher skin modes were ignored at startup");
        WritePrivateProfileStringW(L"Skin",L"JudgementMode",L"1",config.c_str());
        WritePrivateProfileStringW(L"Skin",L"ComboMode",L"1",config.c_str());
        WritePrivateProfileStringW(L"Skin",L"MirrorGearUi",L"0",config.c_str());
        loadDisplaySettings();
        require(applySkinFile(external)&&gSkinApplication.active()&&gSkinSource==external,"Skin application failed");
        require(skinOwnsJudgement(),"Skin judgement ownership missing");
        require(savedPath()==external.wstring(),"Skin path not saved");
        {std::ifstream file(config,std::ios::binary);const std::string ini{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};require(ini.find("utf8:")!=std::string::npos,"Unicode path was not stored as ASCII-safe bytes");}
        if(rml)paintPage(output/L"settings-skin.png");verifyDcCache();verifyGpu(output);verifyTransitions();verifyResourceFps(output);
        const auto bad=output/L"bad.semaskin";{std::ofstream file(bad);file<<"not zip";}
        require(!applySkinFile(bad,false),"Invalid package loaded");
        require(!gSkinApplication.active()&&gSkinRetryPending&&gSkinWanted==bad&&savedPath()==external.wstring(),"Failed load did not enter blank/retry state");
        gSkinApplication.clear();restoreSkinSelection();require(gSkinApplication.active()&&gSkinSource==external,"Startup restoration failed");
        WritePrivateProfileStringW(L"Skin",L"PackagePath",L"",config.c_str());restoreSkinSelection();
        require(gSkinApplication.active()&&gSkinSource==defaultSkinPath()&&savedPath().empty(),"Reset did not load default package");
        const auto partial=fixtures/L"loader-result.semaskin";require(applySkinFile(partial),"Partial skin rejected");
        {SkinGearState gear;gear.active=true;gear.centerX=960;gear.lineY=720;gSkinApplication.setGearState(gear);
        gSkinApplication.setJudgementAnchor(960,720,1920,1080);
        require(!gSkinApplication.coversLegacySlots(845,877,1074,929),"Partial skin suppressed legacy icons");}
        require(!skinOwnsJudgement(),"Partial skin hid native judgement");
        require(!gSkinApplication.supports(ScreenKind::Select)&&!gSkinApplication.supports(ScreenKind::Play)&&gSkinApplication.supports(ScreenKind::Result),"Partial screens incorrect");
        const auto reads=gSkinApplication.readbacks();drawBrowseScreen(gBackBufferDc);require(gSkinApplication.readbacks()==reads,"Unsupported screen attempted rendering");
        WritePrivateProfileStringW(L"Skin",L"PackagePath",(output/(L"missing-"+std::to_wstring(GetCurrentProcessId())+L".semaskin")).c_str(),config.c_str());restoreSkinSelection();
        require(!gSkinApplication.active()&&gSkinRetryPending,"Missing startup skin did not stay blank");
        drawSkinOnly(gBackBufferDc);GdiFlush();require((static_cast<const std::uint32_t*>(gBackBufferPixels)[0]&0xffffff)==0,"Missing skin showed native background");
        std::filesystem::copy_file(external,output/(L"missing-"+std::to_wstring(GetCurrentProcessId())+L".semaskin"),std::filesystem::copy_options::overwrite_existing);
        SendMessageW(gWindow,WM_TIMER,ID_SKIN_RETRY,0);require(gSkinApplication.active()&&!gSkinRetryPending,"Retry did not recover restored package");
        WritePrivateProfileStringW(L"Skin",L"PackagePath",L"",config.c_str());restoreSkinSelection();
        SendMessageW(settings,WM_RML_SETTINGS_NAVIGATE,0,1);require(!gRmlSettingsPageActive&&IsWindow(gSkinNativeStatus),"Native skin fallback controls missing");
        require(!gSkinJudgementChoice&&!gSkinComboChoice,"Game exposed launcher-owned HUD controls");
        const auto handles=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        for(int i=0;i<5;++i){require(applySkinFile(external,false),"Repeated apply failed");drawBrowseScreen(gBackBufferDc);clearSkinSelection(false);}
        require(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=handles+2,"Repeated apply leaked GDI objects");
        SendMessageW(settings,WM_CLOSE,0,0);verifyImagePipeline(fixtures/L"hud-images.semaskin",output);verifySelectShortcuts(output);
        verifyLr2Play(output);verifyLr2Decide(output);verifyLr2Result(output);DestroyWindow(gWindow);gWindow=nullptr;
        std::cout<<"Skin application checks: "<<checks<<"; RmlUi/native controls, persistence, CPU cache, live GPU overlay and recovery\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";result=1;}
    if(IsWindow(gSettingsWindow))DestroyWindow(gSettingsWindow);if(IsWindow(gWindow))DestroyWindow(gWindow);gWindow=nullptr;
    Gdiplus::GdiplusShutdown(gGdiPlusToken);CoUninitialize();return result;
}
