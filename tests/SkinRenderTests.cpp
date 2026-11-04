#include "SkinRenderTestSupport.h"
using namespace skin_test;
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <iostream>
#include <cmath>
#include <limits>
#include <fstream>
#include <algorithm>

using namespace sevenmania::skin;
using Microsoft::WRL::ComPtr;
namespace {
void verify(Surface& surface,const Bytes& pixels) {
    const double scale=std::min(surface.width/320.0,surface.height/180.0);
    const double ox=(surface.width-320*scale)/2,oy=(surface.height-180*scale)/2;
    const auto color=[&](double x,double y,int r,int g,int b) {
        const auto px=unsigned(ox+x*scale),py=unsigned(oy+y*scale);const auto i=(std::size_t(py)*surface.width+px)*4;
        const bool ok=std::abs(int(pixels[i])-b)<=2&&std::abs(int(pixels[i+1])-g)<=2&&std::abs(int(pixels[i+2])-r)<=2;
        if(!ok)std::cerr<<surface.width<<"x"<<surface.height<<" at "<<x<<","<<y<<": RGB "<<int(pixels[i+2])<<","<<int(pixels[i+1])<<","<<int(pixels[i])<<" expected "<<r<<","<<g<<","<<b<<"\n";
        require(ok,"rendered pixel mismatch");
    };
    color(50,50,255,128,128);color(30,30,255,0,0);color(70,70,0,255,0);color(90,90,0,0,255);
    color(190,30,128,128,128);color(230,90,0,0,255);color(205,110,0,0,0);
    color(30,150,0,0,255);color(115,150,255,255,0);color(105,150,0,0,0);
    color(270,30,0,0,255);color(270,70,128,128,128);color(5,5,0,0,0);
    require(pixels[0]==0&&pixels[1]==0&&pixels[2]==0,"contain margin not preserved");
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc<3)throw std::runtime_error("Usage: SevenManiaSkinRenderTests package.semaskin output-directory [--hardware]");
        hr(CoInitializeEx(nullptr,COINIT_MULTITHREADED));
        struct Apartment {~Apartment(){CoUninitialize();}} apartment;
        const auto output=std::filesystem::path(argv[2]);std::filesystem::create_directories(output);
        auto loaded=SkinLoader{}.load(argv[1]);require(bool(loaded),"render fixture load failed");
        bool hardware=false,preview=false;
        for(int i=3;i<argc;++i) {
            const std::wstring option=argv[i];
            if(option==L"--hardware")hardware=true;
            else if(option==L"--preview")preview=true;
            else throw std::runtime_error("Unknown option");
        }
        const auto driver=hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP;
        if(preview) {
            for(const auto& [screen,layout]:loaded.package->screens) {
                (void)layout;Surface surface(1920,1080,driver);SkinRenderer renderer;
                auto error=renderer.prepare(loaded.package,screen,surface.target.Get());
                if(error)throw std::runtime_error(error->path+": "+error->message);
                save(output/(std::string(screenName(screen))+".png"),surface.render(renderer),1920,1080);
                if(screen==ScreenKind::Play){
                    const auto panel=std::find_if(layout.elements.begin(),layout.elements.end(),[](const auto& e){return e.id=="gear-panel";});
                    if(panel!=layout.elements.end()) {
                        for(int position=0;position<3;++position)
                            require(renderer.coversLegacySlots(position,-115,157,114,209),"Opaque default panel did not cover legacy slots");
                        require(!renderer.coversLegacySlots(1,-10000,157,114,209),"Uncovered rectangle was suppressed");
                        for(int variant=0;variant<6;++variant) {
                            auto model=*loaded.package;
                            auto& elements=model.screens.at(ScreenKind::Play).elements;
                            auto& changed=*std::find_if(elements.begin(),elements.end(),[](const auto& e){return e.id=="gear-panel";});
                            if(variant==0)changed.opacity=.5;
                            if(variant==1)changed.visible=false;
                            if(variant==2)changed.clip=SkinRect{0,0,1,1};
                            if(variant==3)changed.extensions.object[L"nativeGearPart"].boolean=false;
                            if(variant==4)changed.source=SkinRect{0,0,1,1};
                            if(variant==5){auto& a=changed.extensions.object[L"animations"];a.type=bms::JsonValue::Type::Array;}
                            SkinRenderer partial;require(!partial.prepare(std::make_shared<const SkinPackage>(model),screen,surface.target.Get()),"Partial panel prepare failed");
                            require(!partial.coversLegacySlots(1,-115,157,114,209),"Unproven panel suppressed legacy UI");
                        }
                    }
                    SkinGearState state;state.active=true;state.centerX=960;state.lineY=720;state.speed=6;renderer.setGearState(state);require(!renderer.update(nullptr),"Gear update failed");require(renderer.displayText("gear-speed-reference")=="6.00x","Live gear speed stale");
                    const auto idle=surface.render(renderer);save(output/L"gear-center.png",idle,1920,1080);
                    state.pressed[1]=true;state.gauge=80;state.rotation=90;renderer.setGearState(state);require(!renderer.update(nullptr),"Gear input update failed");
                    const auto changed=surface.render(renderer);require(changed!=idle,"Gear state did not change pixels");save(output/L"gear-active.png",changed,1920,1080);
                    SkinDataProvider judgement;judgement.beginFrame(ScreenKind::Play);judgement.set(BindingId::PlayJudgementCombo,std::string("GREAT 123"));judgement.set(BindingId::PlayJudgementGrade,std::int64_t(1));judgement.set(BindingId::PlayJudgementAge,0.0);judgement.set(BindingId::PlayCombo,std::int64_t(123));judgement.endFrame();renderer.setJudgementAnchor(960,720,1920,1080);renderer.setJudgementOffset(0);require(!renderer.update(&judgement.snapshot()),"Judgement update failed");const auto judged=surface.render(renderer);save(output/L"judgement-visible.png",judged,1920,1080);require(judged!=changed,"Judgement text invisible");renderer.setJudgementRainbow(true);judgement.beginFrame(ScreenKind::Play);judgement.set(BindingId::PlayJudgementCombo,std::string("GREAT 123"));judgement.set(BindingId::PlayJudgementGrade,std::int64_t(0));judgement.set(BindingId::PlayJudgementAge,0.0);judgement.set(BindingId::PlayCombo,std::int64_t(123));judgement.endFrame();require(!renderer.update(&judgement.snapshot()),"PGREAT update failed");const auto rainbow=surface.render(renderer);require(rainbow!=judged,"Default PGREAT colors disabled");save(output/L"judgement-pgreat.png",rainbow,1920,1080);
                    state.position=0;state.centerX=342;renderer.setGearState(state);renderer.setJudgementAnchor(342,720,1920,1080);const auto moved=surface.render(renderer);require(moved!=changed,"Gear did not follow horizontal position");save(output/L"gear-left.png",moved,1920,1080);
                }
                const auto stats=renderer.stats();std::cout<<screenName(screen)<<": "<<stats.imageLayers<<" image layers, "<<stats.decodedImages<<" decodes, "<<stats.uploadedBitmaps<<" bitmaps\n";
            }
            return 0;
        }
        for(const auto size:{Resolution{320,180},Resolution{640,480},Resolution{1920,1080},Resolution{2560,1440},Resolution{800,180}}) {
            Surface surface(size.width,size.height,driver);SkinRenderer renderer;
            auto error=renderer.prepare(loaded.package,ScreenKind::Result,surface.target.Get());
            if(error)throw std::runtime_error(error->path+": "+error->message);
            const auto stats=renderer.stats();
            require(stats.decodedImages==3,"identical images decoded more than once");
            require(stats.uploadedBitmaps==7&&stats.imageLayers==10,"tinted texture/alias cache not reused");
            auto pixels=surface.render(renderer);verify(surface,pixels);
            save(output/(std::to_wstring(size.width)+L"x"+std::to_wstring(size.height)+L".png"),pixels,size.width,size.height);
            for(int i=0;i<5;++i)surface.render(renderer);
            require(renderer.stats().decodedImages==stats.decodedImages&&renderer.stats().uploadedBitmaps==stats.uploadedBitmaps,"frame reloaded resources");
            require(!renderer.draw(0,100)&&!renderer.draw(std::numeric_limits<float>::quiet_NaN(),100),"invalid viewport accepted");
            require(bool(renderer.prepare({},ScreenKind::Result,surface.target.Get())),"null package accepted");
            require(bool(renderer.prepare(loaded.package,ScreenKind::Play,surface.target.Get())),"missing screen accepted");
            require(bool(renderer.prepare(loaded.package,ScreenKind::Result,nullptr)),"null target accepted");
            verify(surface,surface.render(renderer)); // failed switch retained old target/resources
            renderer.unload();require(!renderer.draw(320,180)&&renderer.stats().uploadedBitmaps==0,"unload failed");
        }
        {
            auto model=*loaded.package;
            SkinScreen select;select.kind=ScreenKind::Select;select.resolution={320,180};
            SkinElement box;box.id="box";box.kind=ElementKind::Box;box.boxMode="title";box.width=100;box.height=100;box.tint={20,30,40,255};select.elements.push_back(box);
            SkinElement title;title.id="title";title.kind=ElementKind::Text;title.binding="song.title";title.width=40;title.height=40;title.textStyle.fontSize=24;select.elements.push_back(title);
            SkinElement level=title;level.id="level-color";level.binding="chart.levelText";level.x=120;level.width=100;select.elements.push_back(level);
            model.screens[ScreenKind::Select]=select;model.manifest.supportedScreens.push_back(ScreenKind::Select);
            Surface testSurface(320,180,driver);SkinRenderer test;
            require(!test.prepare(std::make_shared<const SkinPackage>(model),ScreenKind::Select,testSurface.target.Get()),"box prepare failed");
            require(test.hasBox("title")&&!test.hasBox("bga"),"box mode lookup failed");
            const std::uint32_t red=0xffff0000;require(test.updateBox("title",&red,1,1),"box image update failed");
            auto pixels=testSurface.render(test);auto index=(std::size_t(80)*320+80)*4;require(pixels[index+2]>240,"box image not rendered");
            SkinDataProvider provider;provider.beginFrame(ScreenKind::Select);provider.set(BindingId::SongTitle,std::string("A very long selected song title"));provider.endFrame();
            require(!test.update(&provider.snapshot())&&test.scrolling(),"overflowing title does not scroll");
            provider.beginFrame(ScreenKind::Select);provider.set(BindingId::SongTitle,std::string("A"));provider.endFrame();
            require(!test.update(&provider.snapshot())&&!test.scrolling(),"short title scrolls");
            provider.beginFrame(ScreenKind::Select);provider.set(BindingId::ChartLevelText,std::string("12"));provider.set(BindingId::ChartDifficultyColor,std::int64_t(RGB(96,165,250)));provider.endFrame();require(!test.update(&provider.snapshot()),"Level blue update failed");const auto blue=testSurface.render(test);
            provider.beginFrame(ScreenKind::Select);provider.set(BindingId::ChartLevelText,std::string("12"));provider.set(BindingId::ChartDifficultyColor,std::int64_t(RGB(251,146,60)));provider.endFrame();require(!test.update(&provider.snapshot()),"Level orange update failed");require(testSurface.render(test)!=blue,"Same level did not change difficulty color");
        }
        Surface surface(320,180,driver);SkinRenderer renderer;
        for(int i=0;i<100;++i) {
            require(!renderer.prepare(loaded.package,ScreenKind::Result,surface.target.Get()),"repeat prepare failed");
            surface.render(renderer);renderer.unload();
        }
        {
            Surface replacement(640,480,driver);
            require(!renderer.prepare(loaded.package,ScreenKind::Result,surface.target.Get()),"old target prepare failed");
            require(!renderer.prepare(loaded.package,ScreenKind::Result,replacement.target.Get()),"target recreation prepare failed");
            verify(replacement,replacement.render(renderer));renderer.unload();
        }
        {
            auto jpeg=*loaded.package;
            const auto file=std::filesystem::path(argv[1]).parent_path()/L"pixel.jpg";
            std::ifstream input(file,std::ios::binary);
            require(bool(input),"JPEG fixture missing");
            const Bytes data{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
            for(const auto* id:{"white","alias"}) {
                jpeg.manifest.resourcePaths.at(id).path="assets/images/pixel.jpg";
                jpeg.resources.at(id).path="assets/images/pixel.jpg";
                jpeg.resources.at(id).bytes=std::make_shared<const Bytes>(data);
            }
            require(!renderer.prepare(std::make_shared<const SkinPackage>(jpeg),ScreenKind::Result,surface.target.Get()),"JPEG render prepare failed");
            surface.render(renderer);renderer.unload();
        }
        {
            auto model=*loaded.package;SkinElement text;text.id="bundled-font";text.kind=ElementKind::Text;text.text="HUD 123";text.x=10;text.y=10;text.width=200;text.height=60;
            for(const char* family:{"Michroma","Oxanium","Oxanium Bold","BankGothic-Bold"}){text.textStyle.font=family;model.screens.at(ScreenKind::Result).elements={text};require(!renderer.prepare(std::make_shared<const SkinPackage>(model),ScreenKind::Result,surface.target.Get()),"Bundled/legacy font renderer failed");surface.render(renderer);renderer.unload();}
        }
        {
            auto model=*loaded.package;SkinScreen play;play.kind=ScreenKind::Play;play.resolution={1920,1080};
            SkinElement text;text.id="judgement-mask";text.kind=ElementKind::Text;text.binding="play.judgementCombo";text.width=600;text.height=100;text.textStyle.font="Michroma";
            play.elements={text};model.screens[ScreenKind::Play]=play;model.manifest.supportedScreens.push_back(ScreenKind::Play);
            Surface masks(960,540,driver);SkinRenderer glyphs;SkinDataProvider provider;
            if(const auto error=glyphs.prepare(std::make_shared<const SkinPackage>(model),ScreenKind::Play,masks.target.Get()))throw std::runtime_error("Mask fixture: "+error->path+": "+error->message);
            provider.beginFrame(ScreenKind::Play);provider.set(BindingId::PlayJudgementCombo,std::string("PGREAT 123"));provider.endFrame();
            SkinGearState state;state.active=true;state.centerX=960;state.lineY=720;state.judgementColor=RGB(0,0,0);glyphs.setGearState(state);require(!glyphs.update(&provider.snapshot()),"Mask update failed");
            const auto black=masks.render(glyphs);const auto stats=glyphs.stats();
            state.judgementColor=RGB(255,255,255);glyphs.setGearState(state);glyphs.update(&provider.snapshot());const auto white=masks.render(glyphs);
            require(white!=black,"Glyph mask did not color text");
            for(const auto color:{RGB(255,32,32),RGB(32,255,32),RGB(32,32,255),RGB(255,255,32),RGB(255,32,255),RGB(32,255,255)}){
                state.judgementColor=color;glyphs.setGearState(state);require(!glyphs.update(&provider.snapshot()),"Mask color update failed");
                const auto colored=masks.render(glyphs);require(colored!=black&&colored!=white,"Glyph tint invisible");
                for(size_t pixel=0;pixel<black.size();pixel+=4)if(std::equal(black.begin()+pixel,black.begin()+pixel+4,white.begin()+pixel))
                    require(std::equal(black.begin()+pixel,black.begin()+pixel+4,colored.begin()+pixel),"Tint changed outline or background");
            }
            require(glyphs.stats().textUploads==stats.textUploads&&glyphs.stats().textLayouts==stats.textLayouts,"Color-only update rerasterized or uploaded text");
            glyphs.setJudgementRainbow(true);require(!glyphs.update(&provider.snapshot()),"Rainbow style update failed");
            const auto rainbow=masks.render(glyphs);save(output/L"judgement-mask-color.png",rainbow,960,540);
            const auto baked=glyphs.stats();Sleep(60);require(!glyphs.update(&provider.snapshot()),"Static rainbow update failed");
            require(masks.render(glyphs)==rainbow,"PGREAT still flashes over time");
            require(glyphs.stats().textUploads==baked.textUploads&&glyphs.stats().textLayouts==baked.textLayouts,"Static rainbow rerasterized or uploaded text");
            glyphs.setJudgementRainbow(false);glyphs.update(&provider.snapshot());
            require(masks.render(glyphs)!=rainbow,"Ordinary judgement retained rainbow");
            provider.beginFrame(ScreenKind::Play);provider.set(BindingId::PlayJudgementCombo,std::string("PGREAT 124"));provider.endFrame();glyphs.update(&provider.snapshot());
            require(glyphs.stats().textUploads>stats.textUploads,"Changed combo did not refresh glyph shape");
        }
        auto invalid=*loaded.package;
        invalid.screens.at(ScreenKind::Result).elements.front().rotation=std::numeric_limits<double>::infinity();
        require(!SkinLoader{}.prepare(invalid),"nonfinite rotation accepted");
        invalid=*loaded.package;invalid.screens.at(ScreenKind::Result).elements.front().source=SkinRect{0,0,999,2};
        require(!SkinLoader{}.prepare(invalid),"out-of-bounds source accepted");
        invalid=*loaded.package;invalid.screens.at(ScreenKind::Result).elements.front().clip=SkinRect{0,0,-1,2};
        require(!SkinLoader{}.prepare(invalid),"invalid clip accepted");
        require(!renderer.prepare(loaded.package,ScreenKind::Result,surface.target.Get()),"baseline prepare failed");
        invalid=*loaded.package;invalid.resources.erase("white");
        require(bool(renderer.prepare(std::make_shared<const SkinPackage>(invalid),ScreenKind::Result,surface.target.Get())),"missing image accepted");
        verify(surface,surface.render(renderer));
        invalid=*loaded.package;
        auto corrupt=std::make_shared<Bytes>(*invalid.resources.at("white").bytes);
        for(std::size_t i=42;i<corrupt->size()-12;++i)(*corrupt)[i]=0;
        invalid.resources.at("white").bytes=corrupt;invalid.resources.at("alias").bytes=corrupt;
        require(bool(renderer.prepare(std::make_shared<const SkinPackage>(invalid),ScreenKind::Result,surface.target.Get())),"corrupt pixels accepted");
        verify(surface,surface.render(renderer));
        Limits limited;limited.totalDecodedImageBytes=160;SkinRenderer bounded(limited);
        require(bool(bounded.prepare(loaded.package,ScreenKind::Result,surface.target.Get())),"tint bitmap budget ignored");
        std::cout<<"Skin render checks: "<<checks<<"; 100 load/draw/unload cycles; "<<(driver==D3D_DRIVER_TYPE_WARP?"D3D11 WARP":"D3D11 hardware")<<"\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
