#include "../src/Main.cpp"
#include "SkinRenderTestSupport.h"
#include "skin/SevenManiaSkinData.h"

using namespace sevenmania::skin;
using namespace skin_test;
namespace {
std::string resolve(SkinElement element,const SkinDataSnapshot* snapshot,ScreenKind screen=ScreenKind::Play) {
    std::vector<BindingDiagnostic> diagnostic;
    auto binding=SkinBindingResolver::compile(element,screen,diagnostic);
    std::string output;SkinBindingResolver::resolve(element,binding,snapshot,output);return output;
}
void units() {
    require(SkinBindingRegistry::definitions().size()==static_cast<size_t>(BindingId::Count),"Registry count changed");
    require(!SkinBindingRegistry::find("unknown")&&!SkinBindingRegistry::find(BindingId::Invalid),"Unknown key accepted");
    SkinDataProvider provider;provider.beginFrame(ScreenKind::Play);
    require(provider.setWide(BindingId::SongTitle,L"한글 日本語 ABC"),"UTF-16 conversion failed");
    require(provider.set(BindingId::PlayScore,std::int64_t(882460)),"Integer rejected");
    require(!provider.set(BindingId::PlayScore,1.0),"Wrong numeric type accepted");
    require(!provider.set(BindingId::PlayGauge,std::numeric_limits<double>::infinity()),"Infinity accepted");
    require(!provider.set(BindingId::ResultScore,std::int64_t(1)),"Wrong scope accepted");
    require(!provider.set(BindingId::SelectSpeedText,std::string("1.00x")),"Select setting leaked into gameplay scope");
    require(!provider.set(BindingId::SongArtist,std::string("\xff")),"Malformed UTF-8 accepted");
    require(provider.setWide(BindingId::PlayGaugeMode,L"HARD"),"Gauge mode string rejected");
    require(!provider.set(BindingId::PlayGaugeMode,1.0),"Gauge mode accepted wrong type");
    provider.endFrame();const auto frozen=provider.snapshot();
    const auto revision=provider.snapshot().revision();
    provider.beginFrame(ScreenKind::Play);provider.setWide(BindingId::SongTitle,L"한글 日本語 ABC");provider.set(BindingId::PlayScore,std::int64_t(882460));provider.setWide(BindingId::PlayGaugeMode,L"HARD");provider.endFrame();
    require(provider.snapshot().revision()==revision,"Unchanged values bumped revision");
    SkinElement e;e.kind=ElementKind::Number;e.binding="play.score";e.numberFormat.grouping=true;
    require(resolve(e,&provider.snapshot())=="882,460","Grouping failed");
    e.numberFormat.minDigits=9;e.numberFormat.decimals=2;
    require(resolve(e,&provider.snapshot())=="000,882,460.00","Padding/decimals failed");
    provider.beginFrame(ScreenKind::Play);provider.set(BindingId::PlayScore,std::numeric_limits<std::int64_t>::min());provider.endFrame();
    e.numberFormat={0,0,true};require(resolve(e,&provider.snapshot())=="-9,223,372,036,854,775,808","INT64_MIN precision lost");
    e.binding="song.bpm";e.numberFormat={2,0,false};
    provider.beginFrame(ScreenKind::Play);provider.set(BindingId::SongBpm,123.456);provider.endFrame();
    require(resolve(e,&provider.snapshot())=="123.46","Real formatting failed");
    e.defaultValue=std::string("--");require(resolve(e,nullptr)=="--","Missing default failed");
    provider.beginFrame(ScreenKind::Result);provider.set(BindingId::SongBpm,300.0);provider.endFrame();
    require(resolve(e,&provider.snapshot())=="--","Wrong screen leaked common key");
    require(std::holds_alternative<std::monostate>(provider.snapshot().value(BindingId::PlayScore)),"Transition retained play value");
    require(std::get<std::int64_t>(frozen.value(BindingId::PlayScore))==882460,"Owned snapshot changed");
    e.binding="unknown.key";std::vector<BindingDiagnostic> diagnostics;
    const auto binding=SkinBindingResolver::compile(e,ScreenKind::Result,diagnostics);
    require(diagnostics.size()==1,"Unknown binding not diagnosed");
    std::string output;for(int i=0;i<100;++i)SkinBindingResolver::resolve(e,binding,&provider.snapshot(),output);
    require(output=="--"&&diagnostics.size()==1,"Unknown binding spammed/failed fallback");
    e.kind=ElementKind::Text;e.binding.clear();e.text="한글 日本語 ABC";
    require(resolve(e,nullptr)==e.text,"Fixed UTF-8 text failed");
    e.binding="play.paused";provider.beginFrame(ScreenKind::Play);provider.set(BindingId::PlayPaused,true);provider.endFrame();
    require(resolve(e,&provider.snapshot())=="true","Boolean formatting failed");
    provider.beginFrame(ScreenKind::Play);provider.endFrame();e.defaultValue=std::string("absent");
    require(resolve(e,&provider.snapshot())=="absent","Missing frame retained old data");
    provider.beginFrame(ScreenKind::Select);provider.setWide(BindingId::SongTitle,L"first");provider.set(BindingId::SongTitle,std::string("second"));provider.setWide(BindingId::SongTitle,L"first");provider.endFrame();
    require(std::get<std::string>(provider.snapshot().value(BindingId::SongTitle))=="first","Mixed string setters stale");
    provider.beginFrame(ScreenKind::Select);provider.set(BindingId::SongTitle,std::string("second"));provider.setWide(BindingId::SongTitle,L"");provider.endFrame();
    require(std::get<std::string>(provider.snapshot().value(BindingId::SongTitle)).empty(),"Empty wide text retained old string");
    provider.clear();require(!provider.snapshot().active(),"Clear did not deactivate snapshot");
}
void styles(const SkinPackage& source,Surface& surface,const std::filesystem::path& output) {
    SkinPackage model=source;auto& elements=model.screens.at(ScreenKind::Result).elements;
    SkinElement e;e.id="style";e.kind=ElementKind::Text;e.x=100;e.y=30;e.width=200;e.height=70;
    e.text="ABC";e.textStyle.font="Segoe UI";e.textStyle.fontSize=28;
    SkinRenderer renderer;
    const auto draw=[&]() {
        elements={e};auto error=renderer.prepare(std::make_shared<const SkinPackage>(model),ScreenKind::Result,surface.target.Get());
        require(!error,"Style prepare failed");return surface.render(renderer);
    };
    const auto bounds=[&](const Bytes& pixels) {
        RECT r{640,360,-1,-1};
        for(int y=0;y<360;++y)for(int x=0;x<640;++x) {
            const auto i=(std::size_t(y)*640+x)*4;
            if(pixels[i]>2||pixels[i+1]>2||pixels[i+2]>2){r.left=std::min(r.left,LONG(x));r.top=std::min(r.top,LONG(y));r.right=std::max(r.right,LONG(x));r.bottom=std::max(r.bottom,LONG(y));}
        }
        require(r.right>=r.left,"Text had no visible pixels");return r;
    };
    auto left=bounds(draw());require(left.left<110&&left.top<50,"Left/top alignment failed");
    e.textStyle.align=TextAlign::Right;e.textStyle.verticalAlign=TextVerticalAlign::Bottom;
    auto right=bounds(draw());require(right.left>220&&right.top>60,"Right/bottom alignment failed");
    e.textStyle.align=TextAlign::Center;e.textStyle.verticalAlign=TextVerticalAlign::Center;
    auto centered=draw();auto center=bounds(centered);require(center.left>160&&center.left<190&&center.top>40&&center.top<70,"Center alignment failed");
    save(output/L"alignment.png",centered,640,360);
    e.textStyle.align=TextAlign::Left;e.textStyle.verticalAlign=TextVerticalAlign::Top;e.y=80;e.width=25;e.height=25;e.text.assign(4096,'W');
    auto clipped=draw();auto clippedBounds=bounds(clipped);
    require(clippedBounds.left>=100&&clippedBounds.right<125&&clippedBounds.top>=80&&clippedBounds.bottom<105,"Text escaped clip box");
    e.textStyle.clip=false;require(bounds(draw()).right>125,"clipText false ignored");
    e.text="ABC";e.width=200;e.height=70;e.tint={255,0,0,128};e.opacity=.5;
    auto tinted=draw();unsigned red=0;bool channels=true;
    for(std::size_t i=0;i<tinted.size();i+=4){red=std::max(red,unsigned(tinted[i+2]));channels&=tinted[i]==0&&tinted[i+1]==0;}
    require(channels,"Text tint channels failed");
    require(red>0&&red<=65,"Text opacity/tint alpha failed");
    Limits limit;limit.totalDecodedImageBytes=1024;SkinRenderer bounded(limit);
    require(bool(bounded.prepare(std::make_shared<const SkinPackage>(model),ScreenKind::Result,surface.target.Get())),"Text GPU budget ignored");
    e.textStyle.fontSize=0;elements={e};
    require(bool(renderer.prepare(std::make_shared<const SkinPackage>(model),ScreenKind::Result,surface.target.Get())),"Invalid common text style accepted");
    require(surface.render(renderer)==tinted,"Failed style switch lost old pixels");
    renderer.unload();e.textStyle.fontSize=28;e.tint={};e.opacity=1;
    const auto fontPath=std::filesystem::path(L"C:/Windows/Fonts/segoeui.ttf");
    std::ifstream font(fontPath,std::ios::binary);require(bool(font),"System private-font test source missing");
    auto data=std::make_shared<const Bytes>(Bytes{std::istreambuf_iterator<char>(font),std::istreambuf_iterator<char>()});
    model.manifest.resourcePaths["font"]={"assets/fonts/test.ttf",true};model.resources["font"]={"font","assets/fonts/test.ttf",ResourceKind::Font,data,0,0};
    e.resource="font";auto privateFont=draw();save(output/L"private-font.png",privateFont,640,360);
    auto damaged=std::make_shared<Bytes>(std::size_t(28),std::uint8_t(0));(*damaged)[1]=1;(*damaged)[5]=1;
    model.resources.at("font").bytes=damaged;
    require(bool(renderer.prepare(std::make_shared<const SkinPackage>(model),ScreenKind::Result,surface.target.Get())),"Broken font registration accepted");
    require(surface.render(renderer)==privateFont,"Bad font replaced working renderer");
}
void integration(const std::filesystem::path& packagePath,const std::filesystem::path& output,D3D_DRIVER_TYPE driver) {
    auto loaded=SkinLoader{}.load(packagePath);require(bool(loaded),"Binding fixture load failed");
    Surface surface(640,360,driver);SkinRenderer renderer;SkinDataProvider provider;
    gCharts.clear();bms::Chart chart;chart.title=L"SevenMania 한글 日本語";chart.artist=L"작곡가 / Artist";
    chart.path=output/L"sample.bms";chart.playLevel=L"13.5";chart.initialBpm=150.25;chart.mode=bms::ChartMode::SevenKey;
    chart.tempos={{0,150.25},{10,180.5}};gCharts.push_back(chart);gSongSelection.rebuild(gCharts);gSongSelection.selectGroup(0);
    gActiveChartIndex=0;gPlaying=false;gShowingResult=false;gLoading=false;gStartupSplash=false;
    gNoteSpeed=6.25;gGrooveGaugeMode=GrooveGaugeMode::Hard;gJudgementProfile=JudgementProfile::Easy;
    captureSevenManiaSkinData(provider);
    require(std::get<std::string>(provider.snapshot().value(BindingId::SelectSpeedText))=="6.25x"&&
        std::get<std::string>(provider.snapshot().value(BindingId::SelectGaugeMode))=="HARD"&&
        std::get<std::string>(provider.snapshot().value(BindingId::SelectJudgementText))=="EASY","Actual select settings capture stale");
    require(!renderer.prepare(loaded.package,ScreenKind::Select,surface.target.Get()),"Select prepare failed");
    require(!renderer.update(&provider.snapshot()),"Select update failed");
    require(renderer.displayText("title")=="SevenMania 한글 日本語"&&renderer.displayText("level")=="13.5","Actual chart binding failed");
    require(renderer.displayText("bpm")=="150.2"&&renderer.diagnostics().size()==1,"BPM/default diagnostics failed");
    auto selectPixels=surface.render(renderer);require(std::any_of(selectPixels.begin(),selectPixels.end(),[](auto p){return p!=0&&p!=255;}),"Text pixels missing");
    save(output/L"select.png",selectPixels,640,360);
    const auto stats=renderer.stats();
    for(int i=0;i<100;++i){captureSevenManiaSkinData(provider);require(!renderer.update(&provider.snapshot()),"Repeated update failed");}
    require(renderer.stats().textUploads==stats.textUploads&&renderer.stats().textLayouts==stats.textLayouts,"Unchanged text recreated resources");
    require(surface.render(renderer)==selectPixels,"Unchanged text pixels changed");
    gCharts[0].genre=L"Test Genre";captureSevenManiaSkinData(provider);require(std::get<std::string>(provider.snapshot().value(BindingId::SongGenre))=="Test Genre","Genre capture failed");
    gCharts[0].playLevel=L"unranked";captureSevenManiaSkinData(provider);renderer.update(&provider.snapshot());
    require(renderer.displayText("level")=="--"&&renderer.displayText("level-raw")=="unranked","Nonnumeric level synthesized");
    gCharts[0].playLevel=L"13.5";
    gPlaying=true;gPaused=false;gPlayhead=12;gResultScore.reset(10);gFeedback.reset();gGrooveGauge.configure(10,GrooveGaugeMode::Normal);
    showTimingJudgement(TimingJudgement::PGreat);gFeedback.addCombo(1);
    const auto score=gResultScore.value();const auto gauge=gGrooveGauge.value();const auto combo=gFeedback.combo();
    captureSevenManiaSkinData(provider);
    require(gResultScore.value()==score&&gGrooveGauge.value()==gauge&&gFeedback.combo()==combo,"Capture mutated gameplay");
    for(const auto mode:{GrooveGaugeMode::Normal,GrooveGaugeMode::Easy,GrooveGaugeMode::Hard}){
        gGrooveGaugeMode=mode;captureSevenManiaSkinData(provider);
        const std::string name=mode==GrooveGaugeMode::Hard?"HARD":mode==GrooveGaugeMode::Easy?"EASY":"NORMAL";
        require(std::get<std::string>(provider.snapshot().value(BindingId::PlayGaugeMode))==name,"Actual gauge mode capture incorrect");
        require(gGrooveGauge.value()==gauge,"Gauge label capture changed gauge amount");
    }
    gGrooveGaugeMode=GrooveGaugeMode::Normal;
    require(!renderer.prepare(loaded.package,ScreenKind::Play,surface.target.Get())&&!renderer.update(&provider.snapshot()),"Play prepare failed");
    require(renderer.displayText("score")=="0,100,000"&&renderer.displayText("combo")=="1"&&renderer.displayText("judgement")=="GREAT"&&renderer.displayText("bpm")=="180.5","Actual play state mismatch");
    auto playPixels=surface.render(renderer);save(output/L"play-before.png",playPixels,640,360);
    showTimingJudgement(TimingJudgement::PGreat,0);captureSevenManiaSkinData(provider);
    require(std::get<std::int64_t>(provider.snapshot().value(BindingId::PlayJudgementGrade))==0&&std::get<double>(provider.snapshot().value(BindingId::PlayJudgementAge))>=0,"Raw PGREAT/age capture failed");
    showTimingJudgement(TimingJudgement::Great,0);captureSevenManiaSkinData(provider);
    require(std::get<std::int64_t>(provider.snapshot().value(BindingId::PlayJudgementGrade))==1,"PGREAT/GREAT images cannot distinguish actual grade");
    showTimingJudgement(TimingJudgement::Good);gFeedback.addCombo(1);gPaused=true;captureSevenManiaSkinData(provider);
    require(!renderer.update(&provider.snapshot()),"Changed play update failed");
    require(renderer.displayText("score")=="0,150,000"&&renderer.displayText("paused")=="true"&&renderer.displayText("judgement")=="GOOD","Score update not reflected");
    auto updated=surface.render(renderer);require(updated!=playPixels,"Changed score did not change pixels");save(output/L"play-after.png",updated,640,360);
    gJudgementUntil={};captureSevenManiaSkinData(provider);renderer.update(&provider.snapshot());require(renderer.displayText("judgement")=="--","Expired HUD judgement persisted");
    gPlaying=false;gShowingResult=true;gResultGaugeCleared=true;gResultGaugeFailed=false;gPractice.enabled=false;
    captureSevenManiaSkinData(provider);
    require(!renderer.update(&provider.snapshot())&&renderer.displayText("score").empty(),"Old play score leaked into result snapshot");
    require(!renderer.prepare(loaded.package,ScreenKind::Result,surface.target.Get())&&!renderer.update(&provider.snapshot()),"Result prepare failed");
    require(renderer.displayText("score")=="150,000"&&renderer.displayText("rank")=="F"&&renderer.displayText("max-combo")=="2"&&renderer.displayText("clear")=="CLEAR","Actual result values failed");
    require(renderer.diagnostics().size()==2&&renderer.displayText("wrong-screen")=="--"&&renderer.displayText("wrong-type")=="--","Invalid bindings not isolated");
    std::ofstream log(output/L"binding-diagnostics.txt");
    for(const auto& d:renderer.diagnostics())log<<d.element<<": "<<d.binding<<": "<<d.message<<"\n";
    save(output/L"result.png",surface.render(renderer),640,360);
    require(!renderer.update(nullptr)&&renderer.displayText("score").empty(),"Snapshot clear retained result score");
    renderer.unload();const auto handlesBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(int i=0;i<50;++i) {
        require(!renderer.prepare(loaded.package,ScreenKind::Result,surface.target.Get())&&!renderer.update(&provider.snapshot()),"Repeated binding load failed");
        surface.render(renderer);renderer.unload();
    }
    require(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=handlesBefore+2,"Repeated text load leaked GDI handles");
    gCharts.clear();gSongSelection.clear();gShowingResult=false;captureSevenManiaSkinData(provider);
    require(std::holds_alternative<std::monostate>(provider.snapshot().value(BindingId::SongTitle)),"Deleted chart left a borrowed reference");
    gWindow=CreateWindowExW(0,L"STATIC",L"Skin capture owner test",WS_POPUP,0,0,10,10,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(gWindow!=nullptr,"Thread ownership test window failed");
    bool captured=true;std::thread worker([&]{captured=captureSevenManiaSkinData(provider);});worker.join();
    require(!captured,"Worker thread read live game data");DestroyWindow(gWindow);gWindow=nullptr;
    styles(*loaded.package,surface,output);
    gFeedback.reset();gPaused=false;
}
}
void gearIcons(const std::filesystem::path& output,D3D_DRIVER_TYPE driver) {
    auto loaded=SkinLoader{}.load(defaultSkinPath());require(bool(loaded),"Updated gear skin failed validation");
    const auto& elements=loaded.package->screens.at(ScreenKind::Play).elements;
    auto element=[&](const std::string& id)->const SkinElement&{
        const auto found=std::find_if(elements.begin(),elements.end(),[&](const auto& e){return e.id==id;});
        require(found!=elements.end(),"Gear element missing");return *found;
    };
    require(element("gear-icon-0").resource=="hi-speed-slot"&&element("gear-icon-1").resource=="gauge-mode-slot","Gear icons share a generic image");
    require(loaded.package->resources.at("hi-speed-slot").width==64&&loaded.package->resources.at("gauge-mode-slot").width==64,"Supplied icon images missing");
    require(element("gear-gauge-mode").binding=="play.gaugeMode","Gauge label not bound");
    Surface surface(1920,1080,driver);SkinRenderer renderer;SkinDataProvider provider;
    require(!renderer.prepare(loaded.package,ScreenKind::Play,surface.target.Get()),"Gear renderer preparation failed");
    SkinGearState gear;gear.active=true;gear.lineY=720;gear.speed=6.5;
    const double centers[]={342,960,1578};
    for(int position=0;position<3;++position){
        gear.position=position;gear.centerX=centers[position];renderer.setGearState(gear);
        Bytes previous;
        for(const auto* mode:{"NORMAL","EASY","HARD"}){
            provider.beginFrame(ScreenKind::Play);provider.set(BindingId::PlayGaugeMode,std::string(mode));provider.endFrame();
            require(!renderer.update(&provider.snapshot()),"Gauge icon update failed");
            require(renderer.displayText("gear-gauge-mode")==mode&&renderer.displayText("gear-speed-reference")=="6.50x","Gauge/speed icon text incorrect");
            const auto pixels=surface.render(renderer);require(previous.empty()||pixels!=previous,"Gauge mode did not change icon pixels");previous=pixels;
            if(position==1)save(output/(std::string("gear-icons-")+mode+".png"),pixels,1920,1080);
        }
    }
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc<3)return 2;hr(CoInitializeEx(nullptr,COINIT_MULTITHREADED));
        struct Apartment{~Apartment(){CoUninitialize();}} apartment;
        const auto output=std::filesystem::path(argv[2]);std::filesystem::create_directories(output);
        const auto driver=argc>3?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP;
        gPlayRecordsFile=output/L"isolated-records.ini";DeleteFileW(gPlayRecordsFile.c_str());
        gCharts.resize(2);gCharts[0].path=output/L"a.bms";gCharts[1].path=output/L"b.bms";rebuildChartRecordKeys();gPlayRecords.clear();
        require(browseRecordSummary(playRecordForChart(0))==L"NO RECORD","Missing record was displayed as zero");
        savePlayRecord(gCharts[0].path,true,123,900000);
        require(playRecordForChart(0)&&playRecordForChart(0)->bestAccuracyScore==900000,"Saved key differs from lookup key");
        require(!playRecordForChart(1),"Records leaked to another chart");
        savePlayRecord(gCharts[0].path,false,90,950000);
        require(playRecordForChart(0)->cleared&&playRecordForChart(0)->maxCombo==123,"Independent record fields regressed");
        require(browseRecordSummary(playRecordForChart(0)).find(L"950000   RANK  S   CLEAR")!=std::wstring::npos,"Best score/rank/clear summary incorrect");
        gPlayRecords.clear();loadPlayRecords();rebuildChartRecordKeys();
        require(playRecordForChart(0)&&playRecordForChart(0)->bestAccuracyScore==950000&&!playRecordForChart(1),"Restart or chart switch lost records");
        gPlayRecordsFile.clear();gPlayRecords.clear();gCharts.clear();
        units();integration(argv[1],output,driver);gearIcons(output,driver);
        std::cout<<"Skin binding checks: "<<checks<<"; actual engine capture, Unicode raster, 50 prepare/update/draw/unload cycles\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
