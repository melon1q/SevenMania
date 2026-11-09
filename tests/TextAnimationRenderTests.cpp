// Text and box elements must follow x/y/opacity keyframes like images do.
#include "SkinRenderTestSupport.h"
#include "skin/SkinAnimation.h"
#include "skin/SkinData.h"
#include <chrono>
#include <thread>
using namespace skin_test;
using namespace sevenmania::skin;
namespace {
struct Extent{int minX=1<<30,maxX=-1;long long lit=0;};
Extent measure(const Bytes& pixels,unsigned w,unsigned h){
    Extent e;for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const auto i=(size_t(y)*w+x)*4;if(pixels[i]+pixels[i+1]+pixels[i+2]>96){e.minX=std::min(e.minX,int(x));e.maxX=std::max(e.maxX,int(x));++e.lit;}}
    return e;
}
}
int main(int argc,char** argv){
    try{
        require(argc>1,"fixture path");
        auto loaded=SkinLoader{}.load(argv[1]);require(bool(loaded),"fixture load");
        Surface surface(640,360,D3D_DRIVER_TYPE_WARP);
        const std::string animation=R"({"animations":[{"id":"slide","property":"x","trigger":"screen.enter","duration":0.2,"finish":"hold","keys":[{"time":0,"value":10},{"time":0.2,"value":170}]}]})";
        for(const auto kind:{ElementKind::Text,ElementKind::Box}){
            auto model=*loaded.package;SkinElement element;element.id="mover";element.kind=kind;element.x=10;element.y=10;element.width=60;element.height=40;
            if(kind==ElementKind::Text){element.text="MMMM";element.textStyle.font="Michroma";element.textStyle.fontSize=30;}
            else {element.boxMode="solid";element.tint={255,255,255,255};}
            element.extensions=sevenmania::skin::detail::json(Bytes(animation.begin(),animation.end()),"anim",{});
            auto& screen=model.screens.at(ScreenKind::Result);screen.elements={element};
            SkinRenderer renderer;
            if(auto error=renderer.prepare(std::make_shared<const SkinPackage>(model),ScreenKind::Result,surface.target.Get()))throw std::runtime_error("prepare: "+error->path+": "+error->message);
            SkinDataProvider provider;provider.beginFrame(ScreenKind::Result);provider.endFrame();
            if(auto error=renderer.update(&provider.snapshot()))throw std::runtime_error("update: "+error->message);
            renderer.emit("screen.enter");
            const auto first=measure(surface.render(renderer),surface.width,surface.height);
            std::this_thread::sleep_for(std::chrono::milliseconds(450));
            if(auto error=renderer.update(&provider.snapshot()))throw std::runtime_error("update: "+error->message);
            const auto last=measure(surface.render(renderer),surface.width,surface.height);
            std::cout<<(kind==ElementKind::Text?"text":"box")<<": first "<<first.minX<<".."<<first.maxX<<" lit "<<first.lit<<", last "<<last.minX<<".."<<last.maxX<<" lit "<<last.lit<<"\n";
            require(first.maxX>=0,"element not drawn at start");
            require(last.minX>first.minX+30,kind==ElementKind::Text?"text did not follow x keyframes":"box did not follow x keyframes");
        }
        std::cout<<"text and box elements follow x keyframes\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAILED: "<<error.what()<<"\n";return 1;}
}
