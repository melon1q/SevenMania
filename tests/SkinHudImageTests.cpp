#include "SkinRenderTestSupport.h"
#include "editor/HudImageEditor.h"
#include "skin/SkinApplication.h"
#include <psapi.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <new>

static std::atomic<bool> trackAllocations=false;
static std::atomic<std::size_t> allocations=0;
void* operator new(std::size_t n){if(trackAllocations.load(std::memory_order_relaxed))++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}void operator delete[](void* p)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
using namespace skin_test;
using namespace sevenmania::editor;
namespace {
std::size_t privateBytes(){PROCESS_MEMORY_COUNTERS_EX info{};GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&info),sizeof(info));return info.PrivateUsage;}
void frame(SkinDataProvider& data,int grade,std::int64_t combo,double age=0){data.beginFrame(ScreenKind::Play);data.set(BindingId::PlayJudgementGrade,std::int64_t(grade));data.set(BindingId::PlayJudgementAge,age);data.set(BindingId::PlayCombo,combo);data.endFrame();}
void run(const std::filesystem::path& input,const std::filesystem::path& output,D3D_DRIVER_TYPE driver){
    auto loaded=SkinLoader{}.load(input);require(bool(loaded),"Image HUD package failed");auto model=*loaded.package;auto& element=model.screens.at(ScreenKind::Play).elements[0];
    Surface surface(960,540,driver);SkinRenderer renderer;require(!renderer.prepare(loaded.package,ScreenKind::Play,surface.target.Get()),"HUD preparation failed");
    auto prepared=renderer.stats();require(prepared.decodedImages==16&&prepared.uploadedBitmaps==1&&prepared.hudAtlases==1&&prepared.hudFontGlyphs==0,"Image HUD did not prepare exactly one image-only atlas");
    require(prepared.textLayouts==0&&prepared.textUploads==0,"Image HUD used text rasterization");SkinDataProvider data;
    Bytes previous;
    for(int grade=0;grade<6;++grade){frame(data,grade,123);require(!renderer.update(&data.snapshot()),"Judgement update failed");auto pixels=surface.render(renderer);require(previous.empty()||pixels!=previous,"Judgement images did not change");previous=pixels;save(output/("judgement-"+std::to_string(grade)+".png"),pixels,960,540);}
    for(const auto combo:{0ll,9ll,10ll,99ll,100ll,999ll,1000ll,999999999999999999ll}){
        frame(data,0,combo);require(!renderer.update(&data.snapshot()),"Combo update failed");auto pixels=surface.render(renderer);
        auto digits=std::to_string(combo).size();require(renderer.stats().hudDrawCalls==1+digits,"HUD draw count/number of digits incorrect");save(output/("combo-"+std::to_string(combo)+".png"),pixels,960,540);
    }
    frame(data,0,77);renderer.update(&data.snapshot());auto steady=surface.render(renderer);Sleep(60);renderer.update(&data.snapshot());require(surface.render(renderer)==steady,"Image PGREAT flickered");
    frame(data,0,77,0.9);renderer.update(&data.snapshot());auto hidden=surface.render(renderer);require(hidden!=steady&&renderer.stats().hudDrawCalls==0,"Display time was ignored");
    data.beginFrame(ScreenKind::Select);data.endFrame();renderer.update(&data.snapshot());require(surface.render(renderer)==hidden,"Screen change retained judgement");
    // Warm the renderer and measure only C++ update work, not WARP readback or presentation.
    for(int i=0;i<100;++i){frame(data,0,i);renderer.update(&data.snapshot());}
    const auto memoryBefore=privateBytes();allocations=0;trackAllocations=true;const auto begin=std::chrono::steady_clock::now();bool failed=false;
    for(int i=0;i<100000;++i){frame(data,i%6,i);failed|=bool(renderer.update(&data.snapshot()));}
    const auto elapsed=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();trackAllocations=false;
    require(!failed&&allocations==0,"Repeated HUD updates allocated C++ memory");const auto memoryAfter=privateBytes();
    const auto after=renderer.stats();require(after.decodedImages==prepared.decodedImages&&after.uploadedBitmaps==prepared.uploadedBitmaps&&after.textUploads==0&&after.textLayouts==0&&after.bitmapBytes==prepared.bitmapBytes,"Judgement events decoded/uploaded/rasterized resources");
    require(memoryAfter<=memoryBefore+(16u<<20),"Repeated HUD updates grew process private memory abnormally");
    // Repeated simultaneous judgements produce one current HUD, not stacked copies.
    for(int i=0;i<100;++i)frame(data,0,100);renderer.update(&data.snapshot());surface.render(renderer);require(renderer.stats().hudDrawCalls==4,"Simultaneous inputs duplicated HUD layers");
    // Real draw transactions use the same atlas over hundreds of successive frames.
    for(int i=0;i<500;++i){frame(data,0,i);renderer.update(&data.snapshot());surface.target->BeginDraw();surface.target->Clear(D2D1::ColorF(0,0,0,1));require(renderer.draw(960,540),"Repeated draw failed");hr(surface.target->EndDraw());}
    require(renderer.stats().uploadedBitmaps==prepared.uploadedBitmaps&&renderer.stats().textUploads==0,"Rendering uploaded HUD resources");
    auto reloadMemory=privateBytes();for(int i=0;i<20;++i){renderer.unload();require(!renderer.prepare(loaded.package,ScreenKind::Play,surface.target.Get()),"Reload failed");frame(data,0,123);renderer.update(&data.snapshot());surface.render(renderer);}require(privateBytes()<=reloadMemory+(24u<<20),"Reload memory growth exceeded tolerance");
    // Missing IDs and damaged PNG pixel data use glyphs baked during preparation.
    auto missing=model;missing.screens.at(ScreenKind::Play).elements[0].hud.judgements[0]="missing";
    require(!renderer.prepare(std::make_shared<const SkinPackage>(missing),ScreenKind::Play,surface.target.Get()),"Missing HUD resource did not fall back");require(renderer.stats().hudFontGlyphs==1&&!renderer.diagnostics().empty(),"Missing image fallback/diagnostic absent");frame(data,0,12);renderer.update(&data.snapshot());save(output/L"missing-fallback.png",surface.render(renderer),960,540);
    auto damaged=model;auto& resource=damaged.resources.at(element.hud.digits[9]);auto damagedBytes=std::make_shared<Bytes>(*resource.bytes);damagedBytes->back()^=0xff;resource.bytes=damagedBytes;
    require(!renderer.prepare(std::make_shared<const SkinPackage>(damaged),ScreenKind::Play,surface.target.Get()),"Corrupt PNG did not fall back");require(renderer.stats().hudFontGlyphs==1&&!renderer.diagnostics().empty(),"Corrupt image fallback absent");
    auto shared=model;auto& sharedStyle=shared.screens.at(ScreenKind::Play).elements[0].hud;sharedStyle.judgements.fill("hud-pgreat");sharedStyle.digits.fill("hud-pgreat");
    require(!renderer.prepare(std::make_shared<const SkinPackage>(shared),ScreenKind::Play,surface.target.Get())&&renderer.stats().decodedImages==1&&renderer.stats().uploadedBitmaps==1,"Shared HUD images decoded more than once");
    for(int mode=0;mode<4;++mode){auto mixed=model;auto& e=mixed.screens.at(ScreenKind::Play).elements[0];e.hud.judgementMode=mode&1?"image":"font";e.hud.comboMode=mode&2?"image":"font";
        require(!renderer.prepare(std::make_shared<const SkinPackage>(mixed),ScreenKind::Play,surface.target.Get()),"Independent modes failed");renderer.setJudgementAnchor(480,180,960,540);frame(data,0,123);data.set(BindingId::PlayJudgementCombo,std::string("GREAT 123"));renderer.update(&data.snapshot());save(output/("modes-"+std::to_string(mode)+".png"),surface.render(renderer),960,540);
        require(mode==0?renderer.stats().textUploads>0:renderer.stats().hudAtlases==1,"Font/image modes chose wrong renderer");}
    require(!renderer.prepare(loaded.package,ScreenKind::Play,surface.target.Get()),"Geometry fixture prepare failed");frame(data,0,123);renderer.update(&data.snapshot());const auto original=surface.render(renderer);
    require(renderer.editGeometry(element.id,element.x-100,element.y-80,element.width/2,element.height),"HUD geometry edit failed");require(surface.render(renderer)!=original,"HUD geometry did not affect pixels");
    auto rightAligned=model;rightAligned.screens.at(ScreenKind::Play).elements[0].textStyle.align=TextAlign::Right;
    require(!renderer.prepare(std::make_shared<const SkinPackage>(rightAligned),ScreenKind::Play,surface.target.Get()),"Alignment prepare failed");renderer.update(&data.snapshot());require(surface.render(renderer)!=original,"HUD alignment did not affect pixels");
    // Existing track evaluator handles optional opacity/position/scale, no pixel updates.
    auto animated=model;auto& ae=animated.screens.at(ScreenKind::Play).elements[0];ae.extensions=detail::json(bytes(R"({"animations":[{"id":"fade","property":"opacity","trigger":"judgement","duration":1,"keys":[{"time":0,"value":0},{"time":1,"value":0}]}]})"),"animation",{});
    require(!renderer.prepare(std::make_shared<const SkinPackage>(animated),ScreenKind::Play,surface.target.Get()),"Animation prepare failed");renderer.emit("judgement");frame(data,0,12);renderer.update(&data.snapshot());const auto faded=surface.render(renderer);bool colored=false;for(size_t i=0;i<faded.size();i+=4)colored|=faded[i]||faded[i+1]||faded[i+2];require(!colored,"HUD opacity track did not hide pixels");require(renderer.stats().uploadedBitmaps==1,"Animation uploaded texture");
    // Matched synthetic combo changes: measure update only, including actual raster/upload work for font mode.
    auto fontModel=model;fontModel.screens.at(ScreenKind::Play).elements[0].hud.judgementMode="font";fontModel.screens.at(ScreenKind::Play).elements[0].hud.comboMode="font";
    SkinRenderer fontRenderer,imageRenderer;require(!fontRenderer.prepare(std::make_shared<const SkinPackage>(fontModel),ScreenKind::Play,surface.target.Get()),"Font benchmark prepare failed");
    require(!imageRenderer.prepare(loaded.package,ScreenKind::Play,surface.target.Get()),"Image benchmark prepare failed");
    fontRenderer.setJudgementRainbow(true);
    double fontUs=0,imageUs=0;
    for(int i=0;i<256;++i){frame(data,0,i);data.set(BindingId::PlayJudgementCombo,std::string("GREAT ")+std::to_string(i));
        auto start=std::chrono::steady_clock::now();require(!fontRenderer.update(&data.snapshot()),"Font comparison update failed");fontUs+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
        start=std::chrono::steady_clock::now();require(!imageRenderer.update(&data.snapshot()),"Image comparison update failed");imageUs+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();}
    // Model validation rejects invalid modes, nonfinite sizes and path-like resource IDs.
    auto invalid=model;invalid.screens.at(ScreenKind::Play).elements[0].hud.digits[0]="../outside.png";require(!SkinLoader{}.prepare(invalid),"External HUD path accepted");
    invalid=model;invalid.screens.at(ScreenKind::Play).elements[0].hud.comboMode="script";require(!SkinLoader{}.prepare(invalid),"Unknown rendering mode accepted");
    invalid=model;invalid.screens.at(ScreenKind::Play).elements[0].hud.spacing=std::numeric_limits<double>::infinity();require(!SkinLoader{}.prepare(invalid),"Nonfinite spacing accepted");
    // Use the editor's actual import/save/open path; preserve unrelated properties.
    Document doc;doc.open(input);doc.screen="play";const auto originalX=doc.elements()[0][L"x"].num();
    auto archive=detail::readZip(input,{});const auto png=output/L"import.PNG";write(png,*archive.at("assets/images/pgreat.png"));importHudImage(doc,0,0,png);importHudImage(doc,0,6,png);
    const auto saved=output/L"editor-roundtrip.semaskin";doc.save(saved);Document reopened;reopened.open(saved);reopened.screen="play";
    require(reopened.elements()[0][L"x"].num()==originalX&&reopened.elements()[0][L"hud"][L"judgementMode"].str()==L"image","Editor mode/geometry lost");
    auto roundtrip=SkinLoader{}.load(saved);require(bool(roundtrip),"Editor package failed");require(roundtrip.package->resources.contains(detail::utf8(reopened.elements()[0][L"hud"][L"judgements"][L"pgreat"].str())),"Imported image not packaged");
    Document optional;optional.open(input);optional.screen="play";
    auto& entry=optional.manifest.object[L"resourcePaths"].object[L"hud-pgreat"];entry.type=T::Object;entry.object[L"path"]=str(L"assets/images/pgreat.png");entry.object[L"required"]=jsonBool(false);optional.files.erase("assets/images/pgreat.png");
    optional.save(output/L"optional-missing.semaskin");auto optionalLoaded=SkinLoader{}.load(output/L"optional-missing.semaskin");require(bool(optionalLoaded),"Missing optional image rejected whole package");
    require(!renderer.prepare(optionalLoaded.package,ScreenKind::Play,surface.target.Get())&&renderer.stats().hudFontGlyphs==1,"Optional image did not fall back");
    SkinApplication app;require(!app.load(input,960,540)&&!app.needsJudgementText(),"Image application needs text unexpectedly");require(!app.setHudModes(0,0)&&app.needsJudgementText(),"Font override failed");require(!app.setHudModes(-1,-1)&&!app.needsJudgementText(),"Skin mode restore failed");require(bool(app.setHudModes(7,0)),"Invalid override accepted");
    Document invisible;invisible.open(input);invisible.screen="play";invisible.elements()[0].object[L"opacity"]=num(0);invisible.save(output/L"invisible-image-hud.semaskin");
    require(!app.load(output/L"invisible-image-hud.semaskin",960,540)&&app.ownsJudgement(),"Transparent image HUD exposed hardcoded judgement fallback");
    std::ofstream report(output/L"performance.txt");report<<"driver="<<(driver==D3D_DRIVER_TYPE_WARP?"WARP":"hardware")<<"\nupdates=100000\nupdate_loop_total_us="<<elapsed<<"\nupdate_loop_mean_us="<<elapsed/100000<<"\nmatched_changes=256\nfont_update_total_us="<<fontUs<<"\nimage_update_total_us="<<imageUs<<"\nfont_text_uploads="<<fontRenderer.stats().textUploads<<"\nimage_text_uploads="<<imageRenderer.stats().textUploads<<"\ncpp_allocations=0\nprivate_bytes_before="<<memoryBefore<<"\nprivate_bytes_after="<<memoryAfter<<"\nprepared_atlases="<<prepared.hudAtlases<<"\nprepared_decodes="<<prepared.decodedImages<<"\njudgement_plus_3_digit_draw_calls=4\nNote: update loop includes snapshot setters; this is not game FPS or end-to-end frame time.\n";
}
}
int wmain(int argc,wchar_t** argv){try{if(argc<3)return 2;hr(CoInitializeEx(nullptr,COINIT_MULTITHREADED));std::filesystem::create_directories(argv[2]);run(argv[1],argv[2],argc>3?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP);CoUninitialize();std::cout<<checks<<" image HUD checks passed\n";return 0;}catch(const detail::Failure& e){std::cerr<<e.error.path<<": "<<e.error.message<<'\n';return 1;}catch(const std::exception& e){trackAllocations=false;std::cerr<<e.what()<<'\n';return 1;}}
