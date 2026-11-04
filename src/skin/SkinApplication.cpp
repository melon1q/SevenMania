#include "SkinApplication.h"
#include "SelectionSlotLayout.h"
#include "HudFrameProbe.h"
#include "SkinInternal.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <cmath>

namespace sevenmania::skin {
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT value,const char* message) {
    if(FAILED(value))detail::fail(ErrorCode::InvalidImage,"skin application",message);
}
struct Surface {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> texture,staging;
    ComPtr<ID2D1Factory> factory;ComPtr<ID2D1RenderTarget> target;
    HDC dc{};HBITMAP bitmap{};HGDIOBJ original{};BYTE* pixels{};
    int width{},height{};
    ~Surface(){GdiFlush();if(dc&&original)SelectObject(dc,original);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);if(context){context->ClearState();context->Flush();}}
    void create(int w,int h) {
        if(w<=0||h<=0||w>8192||h>8192||std::uint64_t(w)*h>(16u<<20))
            detail::fail(ErrorCode::LimitExceeded,"skin application","Overlay viewport exceeds supported dimensions");
        auto result=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context);
        if(FAILED(result))check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"Cannot create overlay device");
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;
        d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        check(device->CreateTexture2D(&d,nullptr,&texture),"Cannot create overlay surface");
        d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        check(device->CreateTexture2D(&d,nullptr,&staging),"Cannot create overlay staging surface");
        ComPtr<IDXGISurface> surface;check(texture.As(&surface),"Cannot get overlay DXGI surface");
        check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()),"Cannot create overlay factory");
        check(factory->CreateDxgiSurfaceRenderTarget(surface.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(d.Format,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&target),"Cannot create overlay target");
        dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);
        if(!dc||!bitmap||!pixels)detail::fail(ErrorCode::AllocationFailed,"skin application","Cannot allocate overlay bitmap");
        original=SelectObject(dc,bitmap);width=w;height=h;
    }
    void readback() {
        HUD_SCOPE(Readback);HUD_COUNT(Readbacks);
        GdiFlush();context->CopyResource(staging.Get(),texture.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Cannot read overlay surface");
        for(int y=0;y<height;++y)std::copy_n(static_cast<const BYTE*>(mapped.pData)+std::size_t(y)*mapped.RowPitch,std::size_t(width)*4,pixels+std::size_t(y)*width*4);
        context->Unmap(staging.Get(),0);
    }
};
}
struct SkinApplication::Impl {
    std::shared_ptr<const SkinPackage> package,source;
    SkinGearState gear;
    std::unique_ptr<Surface> surface;
    std::array<std::unique_ptr<SkinRenderer>,3> renderers;
    SkinRenderer gpu;
    ComPtr<ID2D1RenderTarget> gpuTarget;
    bool gpuFailed{},cpuFailed{},cached{},entered{};
    bool judgementRainbow{},needsText=true,imageHud{};
    double judgementOffset{},anchorX{},anchorY{},anchorWidth{},anchorHeight{};
    ScreenKind cachedScreen{};
    std::uint64_t readbacks{};
    int attemptedWidth{},attemptedHeight{};
    void resize(int w,int h) {
        auto next=std::make_unique<Surface>();next->create(w,h);
        std::array<std::unique_ptr<SkinRenderer>,3> prepared;
        for(const auto& [kind,screen]:package->screens) {
            (void)screen;auto renderer=std::make_unique<SkinRenderer>();
            if(auto error=renderer->prepare(package,kind,next->target.Get()))throw detail::Failure{*error};
            prepared[static_cast<std::size_t>(kind)]=std::move(renderer);
        }
        needsText=false;imageHud=false;
        const auto play=package->screens.find(ScreenKind::Play);
        if(play==package->screens.end())needsText=true;
        else for(const auto& e:play->second.elements)if(e.visible){if(e.binding=="play.judgement"||(e.binding=="play.judgementCombo"&&!e.hud.images()))needsText=true;if(e.binding=="play.judgementCombo"&&e.hud.images())imageHud=true;}
        renderers=std::move(prepared);surface=std::move(next);cached=false;cpuFailed=false;
    }
};
SkinApplication::SkinApplication()=default;
SkinApplication::~SkinApplication()=default;
void SkinApplication::clear() noexcept {impl_.reset();error_.reset();}
void SkinApplication::setGearState(const SkinGearState& state) noexcept {if(impl_){impl_->gear=state;if(state.active)impl_->cached=false;}}
void SkinApplication::emit(std::string_view event) noexcept {if(!impl_)return;for(auto& r:impl_->renderers)if(r)r->emit(event);impl_->gpu.emit(event);impl_->cached=false;}
bool SkinApplication::animating() const noexcept {if(impl_)for(auto& r:impl_->renderers)if(r&&r->animating())return true;return false;}
void SkinApplication::setJudgementRainbow(bool enabled) noexcept {if(impl_)impl_->judgementRainbow=enabled;}
void SkinApplication::setJudgementAnchor(double x,double y,double w,double h) noexcept {if(impl_&&(impl_->anchorX!=x||impl_->anchorY!=y||impl_->anchorWidth!=w||impl_->anchorHeight!=h)){impl_->anchorX=x;impl_->anchorY=y;impl_->anchorWidth=w;impl_->anchorHeight=h;impl_->cached=false;}}
void SkinApplication::setJudgementOffset(double offset) noexcept {if(impl_&&impl_->judgementOffset!=offset){impl_->judgementOffset=offset;impl_->cached=false;}}
bool SkinApplication::scrolling() const noexcept {return impl_&&impl_->renderers[0]&&impl_->renderers[0]->scrolling();}
bool SkinApplication::hasBox(std::string_view mode) const noexcept {return impl_&&impl_->renderers[0]&&impl_->renderers[0]->hasBox(mode);}
void SkinApplication::updateBox(std::string_view mode,const std::uint32_t* pixels,unsigned width,unsigned height) noexcept {if(impl_&&impl_->renderers[0]&&impl_->renderers[0]->updateBox(mode,pixels,width,height))impl_->cached=false;}
bool SkinApplication::active() const noexcept {return impl_!=nullptr;}
const SkinManifest* SkinApplication::manifest() const noexcept {return impl_?&impl_->package->manifest:nullptr;}
bool SkinApplication::ownsJudgement() const noexcept {
    if(!impl_||impl_->gpuFailed||impl_->cpuFailed)return false;
    const auto screen=impl_->package->screens.find(ScreenKind::Play);if(screen==impl_->package->screens.end())return false;
    return std::any_of(screen->second.elements.begin(),screen->second.elements.end(),[](const SkinElement& e){return e.kind==ElementKind::Text&&(e.binding=="play.judgement"||e.binding=="play.judgementCombo")&&(e.hud.images()||(e.visible&&e.opacity>0&&e.tint.a>0));});
}
int SkinApplication::selectShortcutAt(double x,double y,int width,int height) const noexcept {
    if(!impl_||width<=0||height<=0)return -1;
    const auto found=impl_->package->screens.find(ScreenKind::Select);if(found==impl_->package->screens.end())return -1;
    const auto& screen=found->second;
    const double scale=std::min(double(width)/screen.resolution.width,double(height)/screen.resolution.height);
    x=(x-(width-screen.resolution.width*scale)/2)/scale;y=(y-(height-screen.resolution.height*scale)/2)/scale;
    for(auto it=screen.elements.rbegin();it!=screen.elements.rend();++it){const auto& e=*it;
        const int slot=e.id=="slot-0"?0:e.id=="slot-1"?1:e.id=="slot-2"?2:-1;
        if(slot<0||!e.visible||e.opacity<=0||e.tint.a==0)continue;
        if(e.clip&&(x<e.clip->x||x>=e.clip->x+e.clip->width||y<e.clip->y||y>=e.clip->y+e.clip->height))continue;
        // Undo the element's centre rotation, just as the renderer applies it.
        const double angle=-e.rotation*3.141592653589793/180,dx=x-e.x-e.width/2,dy=y-e.y-e.height/2;
        const double px=dx*std::cos(angle)-dy*std::sin(angle)+e.width/2,py=dx*std::sin(angle)+dy*std::cos(angle)+e.height/2;
        if(px>=0&&px<e.width&&py>=0&&py<e.height)return slot;
    }
    return -1;
}
bool SkinApplication::needsJudgementText() const noexcept {return !impl_||impl_->needsText;}
bool SkinApplication::hasImageHud() const noexcept {return impl_&&impl_->imageHud;}
bool SkinApplication::coversLegacySlots(double left,double top,double right,double bottom) const noexcept {
    if(!impl_||!impl_->gear.active||impl_->gpuFailed||impl_->cpuFailed)return false;
    if(impl_->anchorWidth<=0||impl_->anchorHeight<=0)return false;
    const auto found=impl_->package->screens.find(ScreenKind::Play);
    if(found==impl_->package->screens.end())return false;
    const auto& screen=found->second;
    const double scale=std::min(impl_->anchorWidth/screen.resolution.width,impl_->anchorHeight/screen.resolution.height);
    const double ox=(impl_->anchorWidth-screen.resolution.width*scale)/2,oy=(impl_->anchorHeight-screen.resolution.height*scale)/2;
    if(left<ox||top<oy||right>impl_->anchorWidth-ox||bottom>impl_->anchorHeight-oy)return false;
    const auto& renderer=impl_->renderers[static_cast<size_t>(ScreenKind::Play)];
    return renderer&&renderer->coversLegacySlots(impl_->gear.position,left-impl_->gear.centerX,top-impl_->gear.lineY,right-impl_->gear.centerX,bottom-impl_->gear.lineY);
}
SkinRenderStats SkinApplication::gpuStats() const noexcept {return impl_?impl_->gpu.stats():SkinRenderStats{};}
std::span<const BindingDiagnostic> SkinApplication::diagnostics() const noexcept {
    if(impl_&&impl_->renderers[static_cast<size_t>(ScreenKind::Play)])return impl_->renderers[static_cast<size_t>(ScreenKind::Play)]->diagnostics();return {};
}
std::optional<SkinError> SkinApplication::setHudModes(int judgement,int combo) noexcept {
    try {
        if(judgement < -1||judgement>1||combo < -1||combo>1)detail::fail(ErrorCode::InvalidModel,"HUD modes","Invalid mode override");
        if(judgement==judgementMode_&&combo==comboMode_)return {};
        if(impl_){
            auto package=std::make_shared<SkinPackage>(*impl_->source);
            for(auto& [kind,screen]:package->screens)if(kind==ScreenKind::Play)for(auto& e:screen.elements)if(e.binding=="play.judgementCombo"){
                if(judgement>=0)e.hud.judgementMode=judgement?"image":"font";if(combo>=0)e.hud.comboMode=combo?"image":"font";}
            auto next=std::make_unique<Impl>();next->source=impl_->source;next->package=std::move(package);next->gear=impl_->gear;
            next->judgementOffset=impl_->judgementOffset;next->judgementRainbow=impl_->judgementRainbow;next->anchorX=impl_->anchorX;next->anchorY=impl_->anchorY;next->anchorWidth=impl_->anchorWidth;next->anchorHeight=impl_->anchorHeight;
            next->resize(impl_->surface->width,impl_->surface->height);impl_=std::move(next);
        }
        judgementMode_=judgement;comboMode_=combo;return {};
    }catch(const detail::Failure& e){return e.error;}catch(...){return SkinError{ErrorCode::AllocationFailed,"HUD modes","Cannot prepare HUD mode"};}
}
bool SkinApplication::supports(ScreenKind screen) const noexcept {return impl_&&impl_->package->screens.contains(screen);}
std::uint64_t SkinApplication::readbacks() const noexcept {return impl_?impl_->readbacks:0;}
std::optional<SkinError> SkinApplication::load(const std::filesystem::path& file,int width,int height) noexcept {
    try {
        auto loaded=SkinLoader{}.load(file);if(!loaded){error_=loaded.error;return error_;}
        // Editing references are displayed by the editor only; the live engine owns notes and key input.
        auto runtime=std::make_shared<SkinPackage>(*loaded.package);
        for(auto& [kind,screen]:runtime->screens){(void)kind;std::erase_if(screen.elements,[](const auto& element){return element.extensions[L"editorOnly"].boolean;});}
        // Upgrade only the original reference controls in memory. Never rewrite a user's skin.
        if(auto select=runtime->screens.find(ScreenKind::Select);select!=runtime->screens.end())for(auto& e:select->second.elements){
            if(e.binding.empty()){
                if(e.id=="slot-value-0")e.binding="select.speedText";
                else if(e.id=="slot-value-1")e.binding="select.judgementText";
                else if(e.id=="slot-value-2")e.binding="select.gaugeMode";
            }
            const auto resource=e.id=="slot-0"?"hi-speed-slot":e.id=="slot-2"?"gauge-mode-slot":"";
            if(*resource&&e.resource=="slot"&&runtime->resources.contains(resource))e.resource=resource;
            if(e.id=="slot-label-0"&&runtime->resources.contains("hi-speed-slot"))e.visible=false;
            if(e.id=="slot-label-2"&&runtime->resources.contains("gauge-mode-slot"))e.visible=false;
        }
        synchronizeSelectionSlots(*runtime);
        auto source=std::make_shared<const SkinPackage>(*runtime);
        for(auto& [kind,screen]:runtime->screens)if(kind==ScreenKind::Play)for(auto& e:screen.elements)if(e.binding=="play.judgementCombo"){
            if(judgementMode_>=0)e.hud.judgementMode=judgementMode_?"image":"font";if(comboMode_>=0)e.hud.comboMode=comboMode_?"image":"font";}
        auto next=std::make_unique<Impl>();next->source=std::move(source);next->package=std::move(runtime);next->resize(width,height);
        impl_=std::move(next);error_.reset();return {};
    }catch(const detail::Failure& e){error_=e.error;}
     catch(const std::bad_alloc&){error_=SkinError{ErrorCode::AllocationFailed,"skin application","Skin allocation failed"};}
     catch(...){error_=SkinError{ErrorCode::InvalidModel,"skin application","Skin setup failed"};}
    return error_;
}
void SkinApplication::releaseGpu() noexcept {if(impl_){impl_->gpu.unload();impl_->gpuTarget.Reset();impl_->gpuFailed=false;}}
bool SkinApplication::drawDc(HDC destination,int width,int height,const SkinDataSnapshot& snapshot) noexcept {
    if(!destination||!snapshot.active()||!supports(snapshot.screen()))return false;
    if(impl_->cpuFailed&&impl_->attemptedWidth==width&&impl_->attemptedHeight==height)return false;
    if(impl_->cpuFailed){impl_->cpuFailed=false;impl_->cached=false;}
    impl_->attemptedWidth=width;impl_->attemptedHeight=height;
    try {
        if(impl_->surface->width!=width||impl_->surface->height!=height)impl_->resize(width,height);
        if(impl_->cpuFailed)return false;
        auto& renderer=*impl_->renderers[static_cast<std::size_t>(snapshot.screen())];
        if(!impl_->entered||impl_->cachedScreen!=snapshot.screen()){renderer.emit("screen.enter");impl_->entered=true;}
        renderer.setGearState(impl_->gear);
        renderer.setJudgementRainbow(impl_->judgementRainbow);
        renderer.setJudgementOffset(impl_->judgementOffset);
        renderer.setJudgementAnchor(impl_->anchorX,impl_->anchorY,impl_->anchorWidth,impl_->anchorHeight);
        const auto before=renderer.stats().textChanges;
        if(auto error=renderer.update(&snapshot))throw detail::Failure{*error};
        // Includes transitions to empty text, independently of snapshot producer/revision.
        const bool changed=renderer.animating()||renderer.scrolling()||!impl_->cached||impl_->cachedScreen!=snapshot.screen()||before!=renderer.stats().textChanges;
        if(changed) {
            auto* target=impl_->surface->target.Get();target->BeginDraw();target->Clear(D2D1::ColorF(0,0,0,0));
            const bool drawn=renderer.draw(float(width),float(height));const auto result=target->EndDraw();
            check(result,"Cannot draw overlay");if(!drawn)detail::fail(ErrorCode::InvalidModel,"skin application","Invalid overlay viewport");
            impl_->surface->readback();++impl_->readbacks;impl_->cached=true;impl_->cachedScreen=snapshot.screen();
        }
        return AlphaBlend(destination,0,0,width,height,impl_->surface->dc,0,0,width,height,{AC_SRC_OVER,0,255,AC_SRC_ALPHA})!=0;
    }catch(const detail::Failure& e){error_=e.error;}
     catch(...){error_=SkinError{ErrorCode::InvalidModel,"skin application","Overlay update failed"};}
    impl_->cpuFailed=true;return false;
}
bool SkinApplication::prepareGpu(ID2D1RenderTarget* target,const SkinDataSnapshot& snapshot) noexcept {
    HUD_SCOPE(PrepareCheck);
    if(!target||!snapshot.active()||snapshot.screen()!=ScreenKind::Play||!supports(ScreenKind::Play))return false;
    if(impl_->gpuTarget.Get()!=target) {
        HUD_COUNT(PrepareTargetChanges);
        impl_->gpu.unload();impl_->gpuTarget=target;impl_->gpuFailed=false;
        if(auto error=impl_->gpu.prepare(impl_->package,ScreenKind::Play,target)){error_=*error;impl_->gpuFailed=true;}
    }
    if(impl_->gpuFailed)return false;
    impl_->gpu.setGearState(impl_->gear);
    impl_->gpu.setJudgementRainbow(impl_->judgementRainbow);
    impl_->gpu.setJudgementOffset(impl_->judgementOffset);
    impl_->gpu.setJudgementAnchor(impl_->anchorX,impl_->anchorY,impl_->anchorWidth,impl_->anchorHeight);
    if(auto error=impl_->gpu.update(&snapshot)){error_=*error;impl_->gpuFailed=true;return false;}
    return true;
}
bool SkinApplication::drawGpuForeground(float width,float height) const noexcept {return impl_&&!impl_->gpuFailed&&impl_->gpu.draw(width,height,true,false);}
bool SkinApplication::drawGpu(float width,float height) const noexcept {return impl_&&!impl_->gpuFailed&&impl_->gpu.draw(width,height,false,true);}
}
