#include "SkinRenderer.h"
#include "HudFrameProbe.h"
#include "SkinInternal.h"
#include "SkinAnimation.h"
#include "SkinEntrance.h"
#include "TextRasterCache.h"
#include "TextMarquee.h"
#include "HudTextStyle.h"
#include "FontPolicy.h"
#include "SkinHudImages.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <tuple>
#include <limits>
#include <zlib.h>

namespace sevenmania::skin {
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr,const std::string& path,const char* message) {
    if(FAILED(hr))detail::fail(ErrorCode::InvalidImage,path,message);
}
D2D1_RECT_F rect(const SkinRect& r) noexcept {
    return D2D1::RectF(float(r.x),float(r.y),float(r.x+r.width),float(r.y+r.height));
}
Bytes decode(IWICImagingFactory* factory,const SkinResource& r) {
    if(std::uint64_t(r.width)*r.height*4>std::numeric_limits<UINT>::max())
        detail::fail(ErrorCode::LimitExceeded,r.path,"Pixel buffer exceeds WIC addressable size");
    if(r.kind==ResourceKind::Png) {
        // WIC can tolerate damaged PNG chunks; reject their CRC/bounds first.
        const auto& b=*r.bytes;
        const auto u32=[&](std::size_t i){return std::uint32_t(b[i])<<24|std::uint32_t(b[i+1])<<16|std::uint32_t(b[i+2])<<8|b[i+3];};
        std::size_t at=8;bool end=false,data=false;
        while(at<b.size()) {
            if(b.size()-at<12)detail::fail(ErrorCode::InvalidImage,r.path,"Truncated PNG chunk");
            const auto count=u32(at);
            if(count>b.size()-at-12)detail::fail(ErrorCode::InvalidImage,r.path,"PNG chunk exceeds resource");
            if(crc32(0,b.data()+at+4,count+4)!=u32(at+8+count))detail::fail(ErrorCode::InvalidImage,r.path,"PNG chunk CRC mismatch");
            const auto type=u32(at+4);at+=std::size_t(count)+12;
            if(type==0x49444154)data=true;
            if(type==0x49454e44) {end=count==0&&at==b.size();break;}
        }
        if(!end||!data)detail::fail(ErrorCode::InvalidImage,r.path,"Missing PNG image data/end");
    }
    ComPtr<IWICStream> stream;ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> converter;
    check(factory->CreateStream(&stream),r.path,"Cannot create image stream");
    check(stream->InitializeFromMemory(const_cast<BYTE*>(r.bytes->data()),static_cast<DWORD>(r.bytes->size())),r.path,"Cannot open image bytes");
    check(factory->CreateDecoder(r.kind==ResourceKind::Png?GUID_ContainerFormatPng:GUID_ContainerFormatJpeg,nullptr,&decoder),r.path,"Cannot create image decoder");
    check(decoder->Initialize(stream.Get(),WICDecodeMetadataCacheOnDemand),r.path,"Cannot decode image");
    check(decoder->GetFrame(0,&frame),r.path,"Cannot read image frame");
    UINT w{},h{};check(frame->GetSize(&w,&h),r.path,"Cannot read image dimensions");
    if(w!=r.width||h!=r.height)detail::fail(ErrorCode::InvalidImage,r.path,"Image dimensions changed");
    check(factory->CreateFormatConverter(&converter),r.path,"Cannot create pixel converter");
    check(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),r.path,"Cannot convert image pixels");
    Bytes pixels(std::size_t(w)*h*4);
    check(converter->CopyPixels(nullptr,w*4,static_cast<UINT>(pixels.size()),pixels.data()),r.path,"Invalid image pixel data");
    return pixels;
}
}
struct SkinRenderer::Impl {
    EmbeddedHudFont bundledFonts;
    struct Font {HFONT handle{};~Font(){if(handle)DeleteObject(handle);}};
    struct MemoryFont {HANDLE handle{};std::shared_ptr<const Bytes> bytes;~MemoryFont(){if(handle)RemoveFontMemResourceEx(handle);}};
    struct Layer {
        Entrance entrance;double entranceStart{};mutable bool entrancePending{};
        ComPtr<ID2D1RoundedRectangleGeometry> rounded;ComPtr<ID2D1Layer> roundedLayer;double roundedWidth{},roundedHeight{};
        std::unique_ptr<HudImageAtlas> hud;
        SkinElement element;ComPtr<ID2D1Bitmap> bitmap,fillMask;D2D1_RECT_F source{};SkinRect destination;
        CompiledBinding binding;std::string display;std::wstring wide;std::shared_ptr<Font> font;
        std::vector<AnimationTrack> animations;bool text{},initialized{};
        COLORREF displayColor{};int rainbowPhase=-1;double overflow{};std::chrono::steady_clock::time_point marqueeStart{};std::uint64_t bitmapBytes{};
    };
    ComPtr<ID2D1RenderTarget> target;
    Resolution resolution;
    SkinGearState gear;
    std::array<bool,3> legacySlotsCovered{};
    std::array<std::array<double,4>,3> legacyPanelBounds{};
    std::chrono::steady_clock::time_point animationStart=std::chrono::steady_clock::now();
    std::vector<std::unique_ptr<MemoryFont>> memoryFonts;
    std::map<std::tuple<std::string,std::string,unsigned>,std::shared_ptr<Font>> fonts;
    std::vector<Layer> layers;
    SkinRenderStats stats;
    ScreenKind screen{};
    bool judgementRainbow{};
    double judgementOffset{}, anchorX{}, anchorY{}, anchorWidth{}, anchorHeight{};
    std::vector<BindingDiagnostic> diagnostics;
    TextRasterCache textCache,maskCache;
    ComPtr<ID2D1SolidColorBrush> textBrush;
    std::string scratch;
    HDC measure{};
    ~Impl(){if(measure)DeleteDC(measure);}
    void update(const SkinDataSnapshot* snapshot,const Limits& limits) {
        if(snapshot&&(!snapshot->active()||snapshot->screen()!=screen))snapshot=nullptr;
        for(auto& layer:layers) {
            HUD_SCOPE_IF(HudUpdate,layer.element.binding=="play.judgementCombo");
            if(layer.hud){if(layer.hud->update(snapshot,layer.element.hud)){++stats.hudUpdates;++stats.textChanges;}continue;}
            if(!layer.text)continue;
            HUD_SCOPE_IF(ScoreUpdate,layer.element.binding=="play.score");
            HUD_SCOPE_IF(GaugeUpdate,layer.element.binding=="play.gauge");
            const auto& e=layer.element;
            SkinBindingResolver::resolve(e,layer.binding,snapshot,scratch);
            if(gear.active&&e.id=="gear-speed-reference"){char value[64];std::snprintf(value,sizeof(value),"%.2fx",gear.speed);scratch=value;}
            const bool rainbow=e.binding=="play.judgementCombo"&&judgementRainbow&&!scratch.empty();
            const int phase=rainbow?0:-1;
            COLORREF displayColor=RGB(e.tint.r,e.tint.g,e.tint.b);
            if(screen==ScreenKind::Select&&e.binding=="chart.levelText"&&snapshot){if(const auto* color=std::get_if<std::int64_t>(&snapshot->value(BindingId::ChartDifficultyColor));color&&*color>=0&&*color<=0xffffff)displayColor=static_cast<COLORREF>(*color);}
            if(layer.initialized&&layer.display==scratch&&(layer.rainbowPhase==phase&&(e.binding=="play.judgementCombo"||layer.displayColor==displayColor)))continue;
            layer.displayColor=displayColor;
            layer.rainbowPhase=phase;
            const int count=scratch.empty()?0:MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,scratch.data(),static_cast<int>(scratch.size()),nullptr,0);
            if(!scratch.empty()&&!count)detail::fail(ErrorCode::InvalidModel,e.id,"Invalid UTF-8 snapshot value");
            if(count>4096)detail::fail(ErrorCode::LimitExceeded,e.id,"Snapshot text too long");
            layer.wide.resize(static_cast<std::size_t>(count));
            if(count)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,scratch.data(),static_cast<int>(scratch.size()),layer.wide.data(),count);
            if(!measure)detail::fail(ErrorCode::AllocationFailed,e.id,"Cannot create text measure DC");
            const auto old=SelectObject(measure,layer.font->handle);SIZE size{};int fit{};
            const bool measured=GetTextExtentExPointW(measure,layer.wide.data(),count,4080,&fit,nullptr,&size)!=0;
            if(old)SelectObject(measure,old);
            if(!measured&&!layer.wide.empty())detail::fail(ErrorCode::InvalidModel,e.id,"Cannot measure text");
            if(fit<count) {
                if(fit>0&&layer.wide[static_cast<std::size_t>(fit)-1]>=0xd800&&layer.wide[static_cast<std::size_t>(fit)-1]<=0xdbff)--fit;
                layer.wide.resize(static_cast<std::size_t>(fit));
            }
            const auto raster=[&](HDC dc,const std::wstring& text,RECT bounds,HFONT font,COLORREF color) {
                if(e.binding=="play.judgementCombo"){drawHudTextBody(dc,text,bounds,font,RGB(0,0,0),phase);return;}
                const auto oldFont=SelectObject(dc,font);const int mode=SetBkMode(dc,TRANSPARENT);const auto oldColor=SetTextColor(dc,color);
                DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&bounds,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_NOPREFIX);
                SetTextColor(dc,oldColor);SetBkMode(dc,mode);if(oldFont)SelectObject(dc,oldFont);
            };
            if(layer.wide.empty()) {
                stats.bitmapBytes-=layer.bitmapBytes;layer.bitmapBytes=0;layer.bitmap.Reset();layer.fillMask.Reset();layer.overflow=0;
            } else {
                const TextRasterCache::GpuDraw upload=[&](const std::uint32_t* pixels,int w,int h,int,int,int,int,std::uint64_t) {
                    const auto bytes=std::uint64_t(w)*h*4*(e.binding=="play.judgementCombo"?2:1);
                    if(stats.bitmapBytes-layer.bitmapBytes+bytes>limits.totalDecodedImageBytes)detail::fail(ErrorCode::LimitExceeded,e.id,"Text bitmap budget exceeded");
                    const auto existing=layer.bitmap?layer.bitmap->GetPixelSize():D2D1::SizeU(0,0);
                    if(existing.width==static_cast<UINT>(w)&&existing.height==static_cast<UINT>(h)) {
                        check(layer.bitmap->CopyFromMemory(nullptr,pixels,static_cast<UINT>(w)*4),e.id,"Cannot update text bitmap");
                    } else {
                        ComPtr<ID2D1Bitmap> next;
                        check(target->CreateBitmap(D2D1::SizeU(w,h),pixels,static_cast<UINT>(w)*4,
                            D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&next),e.id,"Cannot upload text bitmap");
                        layer.bitmap=std::move(next);
                    }
                    stats.bitmapBytes=stats.bitmapBytes-layer.bitmapBytes+bytes;layer.bitmapBytes=bytes;++stats.textUploads;HUD_COUNT(TextUploads);if(e.binding=="play.judgementCombo")HUD_COUNT(HudTextUploads);
                    layer.overflow=screen==ScreenKind::Select&&e.binding=="song.title"&&e.textStyle.clip?std::max(0.0,double(w-16)-e.width):0;
                    layer.marqueeStart=std::chrono::steady_clock::now();
                    const double dx=layer.overflow>0?0:e.textStyle.align==TextAlign::Left?0:e.textStyle.align==TextAlign::Center?(e.width-(w-16))/2:e.width-(w-16);
                    const double dy=e.textStyle.verticalAlign==TextVerticalAlign::Top?0:e.textStyle.verticalAlign==TextVerticalAlign::Center?(e.height-(h-16))/2:e.height-(h-16);
                    layer.destination={e.x+dx,e.y+dy,double(w),double(h)};
                    layer.source=D2D1::RectF(0,0,float(w),float(h));return true;
                };
                const auto color=e.binding=="play.judgementCombo"?(rainbow?RGB(0,0,1):RGB(0,0,0)):displayColor;
                if(!textCache.draw(nullptr,4096,512,layer.wide,{0,0,4096,512},layer.font->handle,color,1,raster,false,upload,true))
                    detail::fail(ErrorCode::InvalidModel,e.id,"Cannot rasterize bounded text");
                if(rainbow)layer.fillMask.Reset();
                if(e.binding=="play.judgementCombo"&&!rainbow) {
                    const TextRasterCache::GpuDraw maskUpload=[&](const std::uint32_t* pixels,int w,int h,int,int,int,int,std::uint64_t) {
                        const auto size=layer.fillMask?layer.fillMask->GetPixelSize():D2D1::SizeU(0,0);
                        if(size.width==UINT(w)&&size.height==UINT(h))check(layer.fillMask->CopyFromMemory(nullptr,pixels,w*4),e.id,"Cannot update glyph mask");
                        else {layer.fillMask.Reset();check(target->CreateBitmap(D2D1::SizeU(w,h),pixels,w*4,D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),&layer.fillMask),e.id,"Cannot upload glyph mask");}
                        ++stats.textUploads;HUD_COUNT(TextUploads);if(e.binding=="play.judgementCombo")HUD_COUNT(HudTextUploads);return true;
                    };
                    if(!maskCache.draw(nullptr,4096,512,layer.wide,{0,0,4096,512},layer.font->handle,RGB(255,255,255),1,drawHudGlyphMask,false,maskUpload,true))
                        detail::fail(ErrorCode::InvalidModel,e.id,"Cannot rasterize glyph mask");
                }
                ++stats.textLayouts;
            }
            layer.display.assign(scratch);layer.initialized=true;++stats.textChanges;
        }
    }
};
SkinRenderer::SkinRenderer(Limits limits):limits_(limits){}
SkinRenderer::~SkinRenderer()=default;
void SkinRenderer::unload() noexcept {impl_.reset();}
SkinRenderStats SkinRenderer::stats() const noexcept {return impl_?impl_->stats:SkinRenderStats{};}
std::optional<SkinError> SkinRenderer::prepare(std::shared_ptr<const SkinPackage> package,ScreenKind screen,ID2D1RenderTarget* target) noexcept {
    HUD_SCOPE(ResourcePrepare);HUD_COUNT(SkinPrepares);
    try {
        if(!package||!target)detail::fail(ErrorCode::InvalidModel,"renderer","Package and target are required");
        // Also validates models constructed directly by future converters/callers.
        auto valid=SkinLoader(limits_).prepare(*package);
        if(!valid)return valid.error;
        package=std::move(valid.package);
        if(!package->screens.contains(screen))detail::fail(ErrorCode::NotFound,"renderer","Skin does not support this screen");
        auto next=std::make_unique<Impl>();next->target=target;
        const auto& layout=package->screens.at(screen);next->resolution=layout.resolution;next->screen=screen;next->scratch.reserve(16384);
        const HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        check(initialized==RPC_E_CHANGED_MODE?S_OK:initialized,"renderer","Cannot initialize image apartment");
        struct Apartment {bool own;~Apartment(){if(own)CoUninitialize();}} apartment{SUCCEEDED(initialized)};
        ComPtr<IWICImagingFactory> factory;
        check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"renderer","Cannot create WIC factory");
        struct Decoded {std::shared_ptr<const Bytes> encoded;Bytes pixels;};
        std::vector<Decoded> decoded;decoded.reserve(package->resources.size()); // Keep atlas pixel references stable during preparation.
        using Key=std::tuple<std::size_t,unsigned,unsigned,unsigned,unsigned>;
        std::map<Key,ComPtr<ID2D1Bitmap>> bitmaps;
        std::uint64_t decodedBytes=0;
        const auto getPixels=[&](const SkinResource& r)->const Bytes& {
            auto found=std::find_if(decoded.begin(),decoded.end(),[&](const auto& d){return d.encoded==r.bytes||*d.encoded==*r.bytes;});
            if(found!=decoded.end())return found->pixels;
            const auto bytes=std::uint64_t(r.width)*r.height*4;if(bytes>limits_.totalDecodedImageBytes-decodedBytes)detail::fail(ErrorCode::LimitExceeded,r.path,"Decoded image budget exceeded");
            auto pixels=decode(factory.Get(),r);decodedBytes+=bytes;decoded.push_back({r.bytes,std::move(pixels)});++next->stats.decodedImages;return decoded.back().pixels;
        };
        for(auto e:layout.elements) {
            if(screen==ScreenKind::Play&&e.id=="play-bga-viewport")continue;
            if(screen==ScreenKind::Play&&e.binding!="play.judgementCombo"&&(e.kind==ElementKind::Text||e.kind==ElementKind::Number)){
                const auto& center=e.extensions[L"gearLayouts"][L"center"];
                const double x=center[L"x"].num(e.x)+e.width/2,y=center[L"y"].num(e.y);
                if(x>=682&&x<=1238&&y>=0&&y<1080){e.extensions.type=bms::JsonValue::Type::Object;auto& follow=e.extensions.object[L"followGear"];follow.type=bms::JsonValue::Type::Boolean;follow.boolean=true;}
            }
            if(e.binding=="play.judgementCombo"&&!e.hud.images()){e.width=600;e.height=100;e.x=660;e.y=520;e.rotation=0;e.opacity=1;e.tint={255,255,255,255};e.textStyle.fontSize=80;e.textStyle.align=TextAlign::Center;e.textStyle.verticalAlign=TextVerticalAlign::Center;e.textStyle.clip=true;}
            const bool isText=e.kind==ElementKind::Text||e.kind==ElementKind::Number;
            const auto binding=isText?SkinBindingResolver::compile(e,screen,next->diagnostics):CompiledBinding{};
            if(!e.visible||e.opacity==0||e.tint.a==0) {++next->stats.skippedLayers;continue;}
            if(e.binding=="play.judgementCombo"&&e.hud.images()){
                Impl::Layer layer;layer.element=e;layer.destination={e.x,e.y,e.width,e.height};layer.hud=std::make_unique<HudImageAtlas>();
                layer.hud->prepare(*package,e,target,getPixels,limits_,next->stats,next->diagnostics);next->layers.push_back(std::move(layer));++next->stats.imageLayers;continue;
            }
            if(isText) {
                if(!next->bundledFonts.load())detail::fail(ErrorCode::UnsupportedResource,"fonts","Cannot register bundled fonts");
                if(!next->measure)next->measure=CreateCompatibleDC(nullptr);
                if(e.binding=="play.judgementCombo"&&!next->textBrush)check(target->CreateSolidColorBrush(D2D1::ColorF(1,1,1),&next->textBrush),e.id,"Cannot create glyph brush");
                if(!e.resource.empty()) {
                    const auto& resource=package->resources.at(e.resource);
                    const bool registered=std::any_of(next->memoryFonts.begin(),next->memoryFonts.end(),[&](const auto& f){return *f->bytes==*resource.bytes;});
                    if(!registered) {
                        auto font=std::make_unique<Impl::MemoryFont>();font->bytes=resource.bytes;DWORD count{};
                        font->handle=AddFontMemResourceEx(const_cast<BYTE*>(resource.bytes->data()),static_cast<DWORD>(resource.bytes->size()),nullptr,&count);
                        if(!font->handle||!count)detail::fail(ErrorCode::UnsupportedResource,resource.path,"Cannot register private package font");
                        next->memoryFonts.push_back(std::move(font));
                    }
                }
                const auto key=std::tuple{e.textStyle.font,e.resource,e.textStyle.fontSize};
                if(!next->fonts.contains(key)) {
                    auto font=std::make_shared<Impl::Font>();
                    const bool oxaniumBold=e.textStyle.font=="Oxanium Bold";
                    const auto family=detail::wide(oxaniumBold?"Oxanium":e.textStyle.font,e.id);
                    font->handle=CreateFontW(-static_cast<int>(e.textStyle.fontSize),0,0,0,oxaniumBold?FW_BOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH|FF_DONTCARE,family.c_str());
                    if(!font->handle)detail::fail(ErrorCode::AllocationFailed,e.id,"Cannot create GDI font");
                    next->fonts.emplace(key,std::move(font));
                }
                Impl::Layer layer;layer.element=e;layer.binding=binding;layer.text=true;layer.font=next->fonts.at(key);
                layer.display.reserve(512);layer.wide.reserve(512);
                next->layers.push_back(std::move(layer));++next->stats.textLayers;continue;
            }
            if(e.kind==ElementKind::Box) {
                const std::uint32_t pixel=0xff000000u|(unsigned(e.tint.r)<<16)|(unsigned(e.tint.g)<<8)|e.tint.b;
                Impl::Layer layer;layer.element=e;layer.destination={e.x,e.y,e.width,e.height};layer.source=D2D1::RectF(0,0,1,1);
                check(target->CreateBitmap(D2D1::SizeU(1,1),&pixel,4,D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&layer.bitmap),e.id,"Cannot create box");
                next->layers.push_back(std::move(layer));continue;
            }
            const auto& r=package->resources.at(e.resource);
            (void)getPixels(r);
            const auto found=std::find_if(decoded.begin(),decoded.end(),[&](const auto& d){return d.encoded==r.bytes||*d.encoded==*r.bytes;});
            const auto index=std::size_t(found-decoded.begin());
            // A uniform opaque native gear panel is composited after the CPU regions.
            // Prove coverage once; do not parse layout JSON or inspect pixels in the hot path.
            if(e.id=="gear-panel"&&e.extensions[L"nativeGearPart"].boolean&&e.opacity==1&&e.tint.a==255&&e.rotation==0&&e.cornerRadius==0&&!e.clip&&
               e.kind==ElementKind::Image&&!e.source&&r.width==1&&r.height==1&&decoded[index].pixels[3]==255&&e.extensions[L"animations"].type==bms::JsonValue::Type::Null){
                const double centers[]={342,960,1578};const wchar_t* names[]={L"left",L"center",L"right"};
                for(int position=0;position<3;++position){const auto& positionLayout=e.extensions[L"gearLayouts"][names[position]];const bool custom=positionLayout.type==bms::JsonValue::Type::Object;
                    const double x=custom?positionLayout[L"x"].num(e.x):e.x,y=custom?positionLayout[L"y"].num(e.y):e.y;
                    const double left=x-(custom?centers[position]:960),top=y-720;
                    next->legacyPanelBounds[position]={left,top,left+e.width,top+e.height};
                    next->legacySlotsCovered[position]=std::isfinite(left)&&std::isfinite(top)&&left<=-115&&left+e.width>=114&&top<=157&&top+e.height>=209;
                }
            }
            const bool icon=e.id.starts_with("gear-icon-")||(e.id.starts_with("slot-")&&e.kind==ElementKind::Image);
            const unsigned rasterScale=icon&&r.width<=target->GetMaximumBitmapSize()/2&&r.height<=target->GetMaximumBitmapSize()/2?2:1;
            const Key key{index,e.tint.r,e.tint.g,e.tint.b,rasterScale};
            if(!bitmaps.contains(key)) {
                next->stats.bitmapBytes+=std::uint64_t(r.width)*r.height*4*rasterScale*rasterScale;
                if(next->stats.bitmapBytes>limits_.totalDecodedImageBytes)detail::fail(ErrorCode::LimitExceeded,r.path,"Tint bitmap budget exceeded");
                // Tint RGB is baked once; alpha remains a draw-time multiplier.
                auto pixels=decoded[index].pixels;
                for(std::size_t i=0;i<pixels.size();i+=4) {
                    pixels[i]=std::uint8_t((unsigned(pixels[i])*e.tint.b+127)/255);
                    pixels[i+1]=std::uint8_t((unsigned(pixels[i+1])*e.tint.g+127)/255);
                    pixels[i+2]=std::uint8_t((unsigned(pixels[i+2])*e.tint.r+127)/255);
                }
                const unsigned pixelWidth=r.width*rasterScale,pixelHeight=r.height*rasterScale;
                if(rasterScale==2){
                    ComPtr<IWICBitmap> sourceBitmap;ComPtr<IWICBitmapScaler> scaler;
                    check(factory->CreateBitmapFromMemory(r.width,r.height,GUID_WICPixelFormat32bppPBGRA,r.width*4,UINT(pixels.size()),pixels.data(),&sourceBitmap),r.path,"Cannot prepare icon raster");
                    check(factory->CreateBitmapScaler(&scaler),r.path,"Cannot create icon scaler");
                    check(scaler->Initialize(sourceBitmap.Get(),pixelWidth,pixelHeight,WICBitmapInterpolationModeCubic),r.path,"Cannot scale icon raster");
                    Bytes enlarged(std::size_t(pixelWidth)*pixelHeight*4);
                    check(scaler->CopyPixels(nullptr,pixelWidth*4,UINT(enlarged.size()),enlarged.data()),r.path,"Cannot read scaled icon raster");pixels=std::move(enlarged);
                }
                ComPtr<ID2D1Bitmap> bitmap;
                check(target->CreateBitmap(D2D1::SizeU(pixelWidth,pixelHeight),pixels.data(),pixelWidth*4,
                    D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96.f*rasterScale,96.f*rasterScale),&bitmap),r.path,"Cannot upload skin bitmap");
                bitmaps.emplace(key,std::move(bitmap));++next->stats.uploadedBitmaps;
            }
            SkinRect source{0,0,double(r.width),double(r.height)};
            if(e.kind==ElementKind::Sprite)source={0,0,double(e.sprite->frameWidth),double(e.sprite->frameHeight)};
            if(e.source)source=*e.source;
            Impl::Layer layer;layer.element=e;layer.bitmap=bitmaps.at(key);layer.source=rect(source);layer.destination={e.x,e.y,e.width,e.height};
            next->layers.push_back(std::move(layer));++next->stats.imageLayers;
        }
        for(auto& layer:next->layers){
            layer.animations=parseAnimations(layer.element.extensions);
            layer.entrance=Entrance::parse(layer.element.extensions);layer.entrancePending=layer.entrance.preset!=0;
            const auto& e=layer.element;
            if(e.cornerRadius>0&&layer.bitmap&&(e.kind==ElementKind::Image||e.kind==ElementKind::Box)){
                ComPtr<ID2D1Factory> geometryFactory;target->GetFactory(&geometryFactory);
                const float radius=float(std::min({e.cornerRadius,e.width/2,e.height/2}));
                check(geometryFactory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(0,0,float(e.width),float(e.height)),radius,radius),&layer.rounded),e.id,"Cannot create rounded corners");
                check(target->CreateLayer(nullptr,&layer.roundedLayer),e.id,"Cannot create rounded clipping layer");layer.roundedWidth=e.width;layer.roundedHeight=e.height;
            }
        }
        next->update(nullptr,limits_);impl_=std::move(next);return {};
    } catch(const detail::Failure& e){return e.error;}
      catch(const std::bad_alloc&){return SkinError{ErrorCode::AllocationFailed,"renderer","Skin render allocation failed"};}
      catch(const std::exception& e){return SkinError{ErrorCode::InvalidModel,"renderer",e.what()};}
      catch(...){return SkinError{ErrorCode::InvalidModel,"renderer","Unknown skin render failure"};}
}
std::optional<SkinError> SkinRenderer::update(const SkinDataSnapshot* snapshot) noexcept {
    if(!impl_)return SkinError{ErrorCode::InvalidModel,"renderer","Renderer is not prepared"};
    try {impl_->update(snapshot,limits_);return {};}
    catch(const detail::Failure& e){return e.error;}
    catch(const std::bad_alloc&){return SkinError{ErrorCode::AllocationFailed,"renderer","Text update allocation failed"};}
    catch(...){return SkinError{ErrorCode::InvalidModel,"renderer","Text update failed"};}
}
std::span<const BindingDiagnostic> SkinRenderer::diagnostics() const noexcept {
    return impl_?std::span<const BindingDiagnostic>(impl_->diagnostics):std::span<const BindingDiagnostic>{};
}
std::string_view SkinRenderer::displayText(std::string_view id) const noexcept {
    if(impl_)for(const auto& layer:impl_->layers)if(layer.text&&layer.element.id==id)return layer.display;
    return {};
}
bool SkinRenderer::draw(float width,float height,bool foregroundOnly,bool skipForeground) const noexcept {
    HUD_SCOPE(SkinDraw);
    if(!impl_||!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0)return false;
    const auto size=impl_->target->GetSize();
    if(width>size.width||height>size.height)return false;
    auto* t=impl_->target.Get();D2D1_MATRIX_3X2_F old{};t->GetTransform(&old);
    const float vw=float(impl_->resolution.width),vh=float(impl_->resolution.height);
    const float scale=std::min(width/vw,height/vh),ox=(width-vw*scale)*0.5f,oy=(height-vh*scale)*0.5f;
    const auto base=D2D1::Matrix3x2F::Scale(scale,scale)*D2D1::Matrix3x2F::Translation(ox,oy);
    t->SetTransform(D2D1::Matrix3x2F::Identity());
    t->PushAxisAlignedClip(D2D1::RectF(ox,oy,ox+vw*scale,oy+vh*scale),D2D1_ANTIALIAS_MODE_ALIASED);
    impl_->stats.hudDrawCalls=0;
    const double entranceNow=std::chrono::duration<double>(std::chrono::steady_clock::now()-impl_->animationStart).count();
    for(const auto& layer:impl_->layers) {
        const auto& e=layer.element;
        const double entryAge=entranceNow-layer.entranceStart;
        const auto entry=layer.entrance.sample(entryAge,e.height);
        if(entryAge>=layer.entrance.delay+layer.entrance.duration)layer.entrancePending=false;
        const auto drawEntrance=[&](const SkinRect& d,float opacity,auto paint){
            if(!layer.entrance.preset||entryAge>=layer.entrance.delay+layer.entrance.duration){paint(opacity);return;}
            D2D1_MATRIX_3X2_F previous;t->GetTransform(&previous);
            const auto center=D2D1::Point2F(float(d.x+d.width/2),float(d.y+d.height/2));
            const auto transform=D2D1::Matrix3x2F::Scale(float(entry.scale),float(entry.scale),center)*D2D1::Matrix3x2F::Rotation(float(entry.rotation),center)*D2D1::Matrix3x2F::Translation(0,float(entry.y))*previous;
            t->SetTransform(transform);
            if(entry.wipe<1)t->PushAxisAlignedClip(D2D1::RectF(float(d.x),float(d.y),float(d.x+d.width*entry.wipe),float(d.y+d.height)),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            if(entry.blur>.1){for(int iy=-1;iy<=1;++iy)for(int ix=-1;ix<=1;++ix){t->SetTransform(D2D1::Matrix3x2F::Translation(float(ix*entry.blur),float(iy*entry.blur))*transform);paint(float(opacity*entry.opacity/9));}}
            else paint(float(opacity*entry.opacity));
            if(entry.wipe<1)t->PopAxisAlignedClip();t->SetTransform(previous);
        };
        const auto drawBitmap=[&](const SkinRect& destination,float opacity,D2D1_BITMAP_INTERPOLATION_MODE interpolation){
            drawEntrance(destination,opacity,[&](float alpha){
                if(layer.rounded){auto params=D2D1::LayerParameters();params.geometricMask=layer.rounded.Get();params.maskTransform=D2D1::Matrix3x2F::Scale(float(destination.width/layer.roundedWidth),float(destination.height/layer.roundedHeight))*D2D1::Matrix3x2F::Translation(float(destination.x),float(destination.y));t->PushLayer(params,layer.roundedLayer.Get());}
                t->DrawBitmap(layer.bitmap.Get(),rect(destination),alpha,interpolation,&layer.source);
                if(layer.rounded)t->PopLayer();
            });
        };
        const bool foreground=impl_->screen==ScreenKind::Play&&(e.binding=="play.judgement"||e.binding=="play.judgementCombo"||e.binding=="play.combo");
        if((foregroundOnly&&!foreground)||(skipForeground&&foreground)||(!layer.bitmap&&!layer.hud))continue;t->SetTransform(base);
        HUD_SCOPE_IF(HudLayer,foreground);
        if(impl_->screen==ScreenKind::Play&&!layer.hud&&!layer.fillMask&&(e.extensions[L"nativeGearPart"].boolean||e.extensions[L"followGear"].boolean)&&impl_->gear.active){
            const auto& gear=impl_->gear;
            auto d=layer.destination;const auto& layout=e.extensions[L"gearLayouts"][gear.position==0?L"left":gear.position==2?L"right":L"center"];
            if(layout.type==bms::JsonValue::Type::Object){d.x+=layout[L"x"].num(e.x)-e.x;d.y+=layout[L"y"].num(e.y)-e.y;}
            if(!e.extensions[L"nativeGearPart"].boolean&&e.extensions[L"followGear"].boolean){const auto& center=e.extensions[L"gearLayouts"][L"center"];d.x=layer.destination.x+center[L"x"].num(e.x)-e.x;d.y=layer.destination.y+center[L"y"].num(e.y)-e.y;d.x+=gear.centerX-960;d.y+=gear.lineY-720;t->SetTransform(D2D1::Matrix3x2F::Identity());drawBitmap(d,float(e.opacity*e.tint.a/255.0),D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);continue;}
            const double centers[]={342,960,1578};d.x+=gear.centerX-(layout.type==bms::JsonValue::Type::Object?centers[gear.position]:960);d.y+=gear.lineY-720;
            float opacity=float(e.opacity*e.tint.a/255.0);
            if(e.id=="gear-gauge-fill-reference")d.width=(556-8)*std::clamp(gear.gauge,0.0,100.0)/100;
            if(e.id.starts_with("gear-key-")&&!e.id.starts_with("gear-key-guide-")){const auto lane=e.id.back()-'0';if(lane>=0&&lane<8&&gear.pressed[lane])opacity*=.6f;}
            const auto local=e.id=="gear-scratch-disc"?D2D1::Matrix3x2F::Rotation(float(gear.rotation),D2D1::Point2F(float(d.x+d.width/2),float(d.y+d.height/2))):D2D1::Matrix3x2F::Identity();
            t->SetTransform(local);drawBitmap(d,opacity,D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);continue;
        }
        double ax=0,ay=0,as=1,ar=e.rotation,ao=e.opacity;
        if(impl_->screen==ScreenKind::Play&&impl_->gear.active&&!layer.hud){const auto& layout=e.extensions[L"gearLayouts"][(impl_->gear.mirrorUi?2-impl_->gear.position:impl_->gear.position)==0?L"left":(impl_->gear.mirrorUi?2-impl_->gear.position:impl_->gear.position)==2?L"right":L"center"];if(layout.type==bms::JsonValue::Type::Object){ax=layout[L"x"].num(e.x)-e.x;ay=layout[L"y"].num(e.y)-e.y;}if(impl_->gear.mirrorUi)ax=impl_->resolution.width-(e.x+ax)-e.width-e.x;}

        if(impl_->screen==ScreenKind::Play&&e.binding=="play.judgementCombo"){if(impl_->anchorWidth>0&&impl_->anchorHeight>0){const double scale=std::min(impl_->anchorWidth/impl_->resolution.width,impl_->anchorHeight/impl_->resolution.height);const double ox=(impl_->anchorWidth-impl_->resolution.width*scale)/2,oy=(impl_->anchorHeight-impl_->resolution.height*scale)/2;ax=layer.hud?(impl_->anchorX-ox)/scale-impl_->resolution.width/2:(impl_->anchorX-ox)/scale-e.width/2-e.x;ay=(impl_->anchorY-oy)/scale-impl_->resolution.height*2.0/3+impl_->judgementOffset;}else {ax=layer.hud?0:(impl_->resolution.width-e.width)/2-e.x;ay=impl_->judgementOffset;}}const double now=std::chrono::duration<double>(std::chrono::steady_clock::now()-impl_->animationStart).count();
        { HUD_SCOPE_IF(Animations,foreground);
        for(const auto& track:layer.animations)if((e.binding!="play.judgementCombo"||layer.hud)&&track.running(now)){double value=track.sample(now);if(track.property=="x")ax=value-e.x;else if(track.property=="y")ay=value-e.y;else if(track.property=="scale")as=value;else if(track.property=="rotation")ar=value;else if(track.property=="opacity")ao=value;}

        }
        if(e.binding=="play.judgementCombo"&&!layer.hud)as=std::min(impl_->gear.judgementScale,e.width/std::max(1.0,layer.destination.width));
        if(e.clip)t->PushAxisAlignedClip(rect(*e.clip),D2D1_ANTIALIAS_MODE_ALIASED);
        if((layer.text||layer.hud)&&e.textStyle.clip){const double clipScale=e.binding=="play.judgementCombo"?1:as;t->PushAxisAlignedClip(rect({e.x+ax+e.width*(1-clipScale)/2,e.y+ay+e.height*(1-clipScale)/2,e.width*clipScale,e.height*clipScale}),D2D1_ANTIALIAS_MODE_ALIASED);}
        t->SetTransform(D2D1::Matrix3x2F::Scale(float(as),float(as),D2D1::Point2F(float(e.x+e.width/2),float(e.y+e.height/2)))*D2D1::Matrix3x2F::Rotation(float(std::remainder(ar,360.0)),D2D1::Point2F(float(e.x+e.width/2),float(e.y+e.height/2)))*D2D1::Matrix3x2F::Translation(float(ax),float(ay))*base);
        if(layer.hud){drawEntrance({e.x,e.y,e.width,e.height},float(ao*e.tint.a/255.0),[&](float alpha){impl_->stats.hudDrawCalls+=layer.hud->draw(t,e,alpha);});if(e.textStyle.clip)t->PopAxisAlignedClip();if(e.clip)t->PopAxisAlignedClip();continue;}
        auto destination=layer.destination;
        if(layer.overflow>0)destination.x-=bms::ui::textMarqueeOffset(layer.overflow,std::chrono::duration<double>(std::chrono::steady_clock::now()-layer.marqueeStart).count());
        { HUD_SCOPE_IF(HudBitmapGroup,foreground);if(foreground)HUD_BITMAP(layer.bitmap.Get(),1);
        drawBitmap(destination,float(ao*e.tint.a/255.0),(e.source||e.kind==ElementKind::Sprite)?D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR:D2D1_BITMAP_INTERPOLATION_MODE_LINEAR); }
        if(layer.fillMask) {
            HUD_SCOPE_IF(HudMask,foreground);if(foreground)HUD_COUNT(HudMasks);
            const auto color=impl_->gear.judgementColor;
            impl_->textBrush->SetColor(D2D1::ColorF(GetRValue(color)/255.f,GetGValue(color)/255.f,GetBValue(color)/255.f));
            impl_->textBrush->SetOpacity(float(ao*e.tint.a/255.0));
            const auto antialias=t->GetAntialiasMode();t->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
            const auto bounds=rect(destination);t->FillOpacityMask(layer.fillMask.Get(),impl_->textBrush.Get(),D2D1_OPACITY_MASK_CONTENT_GRAPHICS,&bounds,&layer.source);
            t->SetAntialiasMode(antialias);
        }
        if((layer.text||layer.hud)&&e.textStyle.clip)t->PopAxisAlignedClip();
        if(e.clip)t->PopAxisAlignedClip();
    }
    t->PopAxisAlignedClip();t->SetTransform(old);return true;
}
bool SkinRenderer::coversLegacySlots(int position,double left,double top,double right,double bottom) const noexcept {
    if(!impl_||position<0||position>2||!impl_->legacySlotsCovered[position])return false;
    const auto& bounds=impl_->legacyPanelBounds[position];
    return bounds[0]<=left&&bounds[1]<=top&&bounds[2]>=right&&bounds[3]>=bottom;
}
void SkinRenderer::setGearState(const SkinGearState& state) noexcept {if(impl_)impl_->gear=state;}
void SkinRenderer::forceEntranceDuration(double seconds) noexcept {
    if(!impl_||impl_->screen!=ScreenKind::Play||!std::isfinite(seconds)||seconds<=0)return;
    for(auto& layer:impl_->layers)if(layer.entrance.preset){layer.entrance.duration=seconds;layer.entrance.delay=0;}
}
void SkinRenderer::emit(std::string_view event) noexcept {if(!impl_)return;double now=std::chrono::duration<double>(std::chrono::steady_clock::now()-impl_->animationStart).count();for(auto& layer:impl_->layers)if(event=="screen.enter"){layer.entranceStart=now;layer.entrancePending=layer.entrance.preset!=0;}for(auto& layer:impl_->layers)for(auto& t:layer.animations)if(t.trigger==event&&!(t.policy=="ignore"&&t.active&&(t.loop||now-t.start<t.duration))){t.start=now;t.active=true;}}
void SkinRenderer::setJudgementRainbow(bool enabled) noexcept {if(impl_)impl_->judgementRainbow=enabled;}
void SkinRenderer::setJudgementAnchor(double x,double y,double w,double h) noexcept {if(impl_){impl_->anchorX=x;impl_->anchorY=y;impl_->anchorWidth=w;impl_->anchorHeight=h;}}
void SkinRenderer::setJudgementOffset(double offset) noexcept {if(impl_)impl_->judgementOffset=offset;}
bool SkinRenderer::scrolling() const noexcept {if(impl_)for(const auto& layer:impl_->layers)if(layer.overflow>0)return true;return false;}
bool SkinRenderer::hasBox(std::string_view mode) const noexcept {if(impl_)for(const auto& layer:impl_->layers)if(layer.element.kind==ElementKind::Box&&layer.element.boxMode==mode)return true;return false;}
bool SkinRenderer::updateBox(std::string_view mode,const std::uint32_t* pixels,unsigned width,unsigned height) noexcept {
 if(!impl_||!pixels||!width||!height||width>8192||height>8192||std::uint64_t(width)*height>limits_.imagePixels)return false;
 bool updated=false;for(auto& layer:impl_->layers)if(layer.element.kind==ElementKind::Box&&layer.element.boxMode==mode){
 auto size=layer.bitmap->GetPixelSize();if(size.width!=width||size.height!=height){ComPtr<ID2D1Bitmap> bitmap;
 if(FAILED(impl_->target->CreateBitmap(D2D1::SizeU(width,height),pixels,width*4,D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&bitmap)))return false;
 layer.bitmap=std::move(bitmap);layer.source=D2D1::RectF(0,0,float(width),float(height));}else if(FAILED(layer.bitmap->CopyFromMemory(nullptr,pixels,width*4)))return false;updated=true;}return updated;
}
bool SkinRenderer::animating() const noexcept {if(!impl_)return false;double now=std::chrono::duration<double>(std::chrono::steady_clock::now()-impl_->animationStart).count();for(auto& l:impl_->layers){if(l.entrancePending)return true;for(auto& t:l.animations)if(t.active)return true;}return false;}
bool SkinRenderer::editGeometry(std::string_view id,double x,double y,double width,double height) noexcept {
    if(!impl_||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(width)||!std::isfinite(height)||std::abs(x)>1000000||std::abs(y)>1000000||width<=0||height<=0||width>1000000||height>1000000)return false;
    for(auto& layer:impl_->layers)if(layer.element.id==id){auto& e=layer.element;
        if(layer.text){layer.destination.x+=x-e.x+(width-e.width)*(e.textStyle.align==TextAlign::Center?.5:e.textStyle.align==TextAlign::Right?1:0);layer.destination.y+=y-e.y+(height-e.height)*(e.textStyle.verticalAlign==TextVerticalAlign::Center?.5:e.textStyle.verticalAlign==TextVerticalAlign::Bottom?1:0);}
        else layer.destination={x,y,width,height};e.x=x;e.y=y;e.width=width;e.height=height;return true;}
    return false;
}
}
