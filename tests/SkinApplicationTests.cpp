#include "../src/Main.cpp"
#include "SkinRenderTestSupport.h"
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
void activateSkinControl(const std::string& id) {
    if(gRmlSettingsPageActive)click(id);
    else {SendMessageW(gSettingsWindow,WM_COMMAND,id=="skin-load"?ID_SKIN_LOAD:ID_SKIN_CLEAR,0);pump();}
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
void verifySelectShortcuts(const std::filesystem::path& output) {
    require(applySkinFile(defaultSkinPath(),false),"Reference skin failed to load");
    gPlaying=false;gShowingResult=false;gLoading=false;gStartupSplash=false;
    gCharts[0].title=L"Song"; // Keep marquee animation out of the unchanged-value cache check.
    auto loaded=SkinLoader{}.load(defaultSkinPath());require(bool(loaded),"Reference data missing");
    const auto& screen=loaded.package->screens.at(ScreenKind::Select);
    const auto oldWidth=gWindowWidth,oldHeight=gWindowHeight;
    for(const auto [width,height]:{std::pair{1920,1080},std::pair{1280,720},std::pair{1024,768},std::pair{2560,1440}}){
        gWindowWidth=width;gWindowHeight=height;
        const double scale=std::min(double(width)/screen.resolution.width,double(height)/screen.resolution.height);
        const double ox=(width-screen.resolution.width*scale)/2,oy=(height-screen.resolution.height*scale)/2;
        for(int slot=0;slot<3;++slot){
            const auto found=std::find_if(screen.elements.begin(),screen.elements.end(),[&](const auto& e){return e.id=="slot-"+std::to_string(slot);});
            require(found!=screen.elements.end(),"Reference slot missing");
            const int x=int(ox+(found->x+found->width/2)*scale),y=int(oy+(found->y+found->height/2)*scale);
            require(selectSkinShortcutAt(x,y)==slot,"Scaled skin hit area mismatch");
            gNoteSpeed=1;gJudgementProfile=JudgementProfile::Normal;gGrooveGaugeMode=GrooveGaugeMode::Normal;
            SendMessageW(gWindow,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));
            require(slot==0?gNoteSpeed==1.25:slot==1?gJudgementProfile==JudgementProfile::Hard:gGrooveGaugeMode==GrooveGaugeMode::Hard,"Left click did not change setting");
            SkinDataProvider provider;require(captureSevenManiaSkinData(provider),"Shortcut snapshot failed");
            const auto id=slot==0?BindingId::SelectSpeedText:slot==1?BindingId::SelectJudgementText:BindingId::SelectGaugeMode;
            require(std::get<std::string>(provider.snapshot().value(id))==(slot==0?"1.25x":"HARD"),"Shortcut display value stale");
            require(GetPrivateProfileIntW(L"Gameplay",slot==0?L"JudgementProfile":L"GrooveGaugeMode",99,gSettingsFile.c_str())!=99,"Shortcut settings not saved");
            SendMessageW(gWindow,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(x,y));
            require(gNoteSpeed==1&&gJudgementProfile==JudgementProfile::Normal&&gGrooveGaugeMode==GrooveGaugeMode::Normal,"Right click did not decrease setting");
        }
        require(selectSkinShortcutAt(width-1,0)==-1,"Outside click selected shortcut");
    }
    gWindowWidth=1920;gWindowHeight=1080;ensureBackBuffer(nullptr,1920,1080);
    drawBrowseScreen(gBackBufferDc);const auto before=gSkinApplication.readbacks();
    require(adjustBrowseShortcut(74,980,true),"Reference speed click missed");drawBrowseScreen(gBackBufferDc);
    require(gSkinApplication.readbacks()>before,"Changed speed did not redraw cached overlay");
    const auto speedRead=gSkinApplication.readbacks();
    require(adjustBrowseShortcut(298,980,true),"Reference gauge click missed");drawBrowseScreen(gBackBufferDc);
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
    if(argc!=3)return 2;
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
        auto settings=CreateWindowExW(WS_EX_CONTROLPARENT,L"BmsPlayerSettingsWindow",L"Skin settings",WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_VSCROLL|WS_CLIPCHILDREN,
            0,0,780,820,gWindow,nullptr,GetModuleHandleW(nullptr),nullptr);require(settings!=nullptr,"Create settings failed");
        selectSettingsCategory(5);
        const bool rml=bms::ui::RmlSettingsHost::available();
        if(rml){paintPage();require(gRmlSettingsPageActive,"RmlUi settings failed");}
        else require(!gRmlSettingsPageActive&&IsWindow(gSkinNativeStatus),"Native-only settings failed");
        const auto external=output/L"한글 & 외부 🎵 스킨.semaskin";std::filesystem::copy_file(fixtures/L"data-binding.semaskin",external,std::filesystem::copy_options::overwrite_existing);
        unsigned picks=0;gSkinPicker=[&](HWND owner)->std::optional<std::filesystem::path>{require(owner==settings,"Picker owner incorrect");++picks;return external;};
        activateSkinControl("skin-load");require(picks==1&&gSkinApplication.active()&&gSkinSource==external,"Load button did not apply skin");
        require(skinOwnsJudgement(),"Skin judgement ownership missing");
        require(savedPath()==external.wstring(),"Skin path not saved");if(rml)require(gRmlSettingsPage.focused()=="skin-load","Picker did not restore focus");
        {std::ifstream file(config,std::ios::binary);const std::string ini{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};require(ini.find("utf8:")!=std::string::npos,"Unicode path was not stored as ASCII-safe bytes");}
        if(rml)paintPage(output/L"settings-skin.png");verifyDcCache();verifyGpu(output);verifyTransitions();verifyResourceFps(output);
        const auto bad=output/L"bad.semaskin";{std::ofstream file(bad);file<<"not zip";}
        gSkinPicker=[&](HWND)->std::optional<std::filesystem::path>{return bad;};activateSkinControl("skin-load");
        require(!gSkinApplication.active()&&gSkinRetryPending&&gSkinWanted==bad&&savedPath()==external.wstring(),"Failed load did not enter blank/retry state");
        gSkinPicker=[](HWND)->std::optional<std::filesystem::path>{return {};};activateSkinControl("skin-load");require(gSkinRetryPending&&gSkinWanted==bad,"Cancel changed retry target");
        gSkinApplication.clear();restoreSkinSelection();require(gSkinApplication.active()&&gSkinSource==external,"Startup restoration failed");
        activateSkinControl("skin-clear");require(gSkinApplication.active()&&gSkinSource==defaultSkinPath()&&savedPath().empty(),"Reset did not load default package");
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
        SendMessageW(gWindow,WM_TIMER,ID_SKIN_RETRY,0);require(gSkinApplication.active()&&!gSkinRetryPending,"Retry did not recover restored package");activateSkinControl("skin-clear");require(savedPath().empty(),"Missing saved skin could not be reset");
        SendMessageW(settings,WM_RML_SETTINGS_NAVIGATE,0,1);require(!gRmlSettingsPageActive&&IsWindow(gSkinNativeStatus),"Native skin fallback controls missing");
        gSkinPicker=[&](HWND)->std::optional<std::filesystem::path>{return external;};SendMessageW(settings,WM_COMMAND,ID_SKIN_LOAD,0);require(gSkinApplication.active(),"Native load failed");
        SendMessageW(settings,WM_COMMAND,ID_SKIN_CLEAR,0);require(gSkinApplication.active()&&gSkinSource==defaultSkinPath(),"Native default restore failed");
        selectSettingsCategory(5);
        require(gSettingsControlRects.at(gSkinJudgementChoice).top>gSettingsControlRects.at(gJudgementTextYSlider).bottom,"HUD mode selector overlaps offset slider");
        for(int mode:{1,0,-1}){
            require(postSetting(ID_SKIN_JUDGEMENT_MODE,mode)&&postSetting(ID_SKIN_COMBO_MODE,mode),"HUD settings enqueue failed");SendMessageW(gWindow,WM_APPLY_SETTINGS,0,0);syncRmlSettingsState();
            require(gSkinJudgementMode==mode&&gSkinComboMode==mode,"HUD settings did not apply");
            require(int(GetPrivateProfileIntW(L"Skin",L"JudgementMode",99,config.c_str()))==mode&&int(GetPrivateProfileIntW(L"Skin",L"ComboMode",99,config.c_str()))==mode,"HUD modes not persisted");
            if(rml)require(gRmlSettingsPage.value("skin-judgement-mode")==std::to_string(mode)&&gRmlSettingsPage.value("skin-combo-mode")==std::to_string(mode),"HUD RML settings stale");
        }
        SendMessageW(settings,WM_RML_SETTINGS_NAVIGATE,0,1);SendMessageW(gSkinJudgementChoice,CB_SETCURSEL,1,0);
        SendMessageW(settings,WM_COMMAND,MAKEWPARAM(ID_SKIN_JUDGEMENT_MODE,CBN_SELCHANGE),reinterpret_cast<LPARAM>(gSkinJudgementChoice));
        require(gSkinJudgementMode==0,"Native HUD mode selection failed");require(applyNativeSetting(ID_SKIN_JUDGEMENT_MODE,-1),"Restore package mode failed");
        const auto handles=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        for(int i=0;i<5;++i){require(applySkinFile(external,false),"Repeated apply failed");drawBrowseScreen(gBackBufferDc);clearSkinSelection(false);}
        require(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=handles+2,"Repeated apply leaked GDI objects");
        gSkinPicker=pickSkinFile;SendMessageW(settings,WM_CLOSE,0,0);verifyImagePipeline(fixtures/L"hud-images.semaskin",output);verifySelectShortcuts(output);DestroyWindow(gWindow);gWindow=nullptr;
        std::cout<<"Skin application checks: "<<checks<<"; RmlUi/native controls, persistence, CPU cache, live GPU overlay and recovery\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";result=1;}
    if(IsWindow(gSettingsWindow))DestroyWindow(gSettingsWindow);if(IsWindow(gWindow))DestroyWindow(gWindow);gWindow=nullptr;
    Gdiplus::GdiplusShutdown(gGdiPlusToken);CoUninitialize();return result;
}
