#pragma once
#include "HudFrameProbe.h"
// Prepared once per render target. Runtime state is integers and a fixed digit buffer.
#include <wrl/client.h>
#include "SkinInternal.h"
#include "SkinRenderer.h"
#include "HudTextStyle.h"
#include "TextRasterCache.h"
#include <functional>
#include <array>
#include <limits>

namespace sevenmania::skin {
struct HudImageAtlas {
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    std::array<D2D1_RECT_F,16> sources{};
    std::array<unsigned,20> digits{};
    unsigned digitCount{};
    std::uint64_t combo=std::numeric_limits<std::uint64_t>::max();
    int grade=-1;
    bool update(const SkinDataSnapshot* snapshot,const SkinHudStyle& style) noexcept {
        int next=-1;std::uint64_t value=0;
        if(snapshot){const auto* g=std::get_if<std::int64_t>(&snapshot->value(BindingId::PlayJudgementGrade));
            const auto* age=std::get_if<double>(&snapshot->value(BindingId::PlayJudgementAge));
            const auto* c=std::get_if<std::int64_t>(&snapshot->value(BindingId::PlayCombo));
            if(g&&age&&*g>=0&&*g<6&&*age>=0&&*age*1000<style.displayMs){next=int(*g);if(c&&*c>=0)value=static_cast<std::uint64_t>(*c);}}
        if(next==grade&&value==combo)return false;
        grade=next;
        if(combo!=value){combo=value;digitCount=0;do{digits[digitCount++]=unsigned(value%10);value/=10;}while(value&&digitCount<digits.size());
            std::reverse(digits.begin(),digits.begin()+digitCount);}
        return true;
    }
    template<class Pixels> void prepare(const SkinPackage& package,const SkinElement& e,ID2D1RenderTarget* target,
        Pixels getPixels,const Limits& limits,SkinRenderStats& stats,std::vector<BindingDiagnostic>& diagnostics) {
        HUD_COUNT(AtlasBuilds);
        struct Image{unsigned width{},height{};Bytes fallback;const Bytes* pixels{};};std::array<Image,16> images;
        EmbeddedHudFont embedded;HFONT font{};HANDLE privateFont{};
        struct Cleanup{HFONT& font;HANDLE& memory;~Cleanup(){if(font)DeleteObject(font);if(memory)RemoveFontMemResourceEx(memory);}} cleanup{font,privateFont};
        const auto warn=[&](const std::string& id,const char* message){diagnostics.push_back({e.id,id,message});
            const auto line="SevenMania HUD "+e.id+" / "+id+": "+message+"\n";OutputDebugStringA(line.c_str());};
        const wchar_t* labels[]={L"GREAT",L"GREAT",L"GOOD",L"BAD",L"POOR",L"MINE"};
        const COLORREF colors[]={RGB(255,214,20),RGB(74,222,128),RGB(56,189,248),RGB(251,146,60),RGB(248,113,113),RGB(255,64,64)};
        for(unsigned i=0;i<images.size();++i){auto& image=images[i];const bool judgement=i<6;
            const auto& id=judgement?e.hud.judgements[i]:e.hud.digits[i-6];
            const bool requested=(judgement?e.hud.judgementMode:e.hud.comboMode)=="image";
            if(requested&&!id.empty()){
                auto found=package.resources.find(id);
                if(found!=package.resources.end()&&(found->second.kind==ResourceKind::Png||found->second.kind==ResourceKind::Jpeg)){
                    try{const auto& resource=found->second;if(resource.width>8190||resource.height>8190)detail::fail(ErrorCode::LimitExceeded,id,"HUD atlas image exceeds 8190px");
                        image.pixels=&getPixels(resource);image.width=resource.width;image.height=resource.height;
                    }catch(const detail::Failure& error){if(error.error.code!=ErrorCode::InvalidImage&&error.error.code!=ErrorCode::LimitExceeded)throw;warn(id,error.error.message.c_str());}
                }else warn(id,"Missing image resource; using prepared font glyph");
            }else if(requested)warn(judgement?"judgements":"digits","Image not assigned; using prepared font glyph");
            if(!image.pixels){
                if(!font){
                    if(!embedded.load())detail::fail(ErrorCode::UnsupportedResource,e.id,"Cannot load fallback font");
                    if(!e.resource.empty()){const auto& resource=package.resources.at(e.resource);DWORD count{};
                        privateFont=AddFontMemResourceEx(const_cast<BYTE*>(resource.bytes->data()),DWORD(resource.bytes->size()),nullptr,&count);
                        if(!privateFont||!count)detail::fail(ErrorCode::UnsupportedResource,e.id,"Cannot load HUD font");}
                    const auto family=detail::wide(e.textStyle.font=="Oxanium Bold"?"Oxanium":e.textStyle.font,e.id);
                    font=CreateFontW(-int(e.textStyle.fontSize),0,0,0,e.textStyle.font=="Oxanium Bold"?FW_BOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,family.c_str());
                    if(!font)detail::fail(ErrorCode::AllocationFailed,e.id,"Cannot prepare HUD font");
                }
                const std::wstring text=judgement?std::wstring(labels[i]):std::wstring(1,wchar_t(L'0'+i-6));
                TextRasterCache raster;
                const TextRasterCache::GpuDraw capture=[&](const std::uint32_t* pixels,int w,int h,int,int,int,int,std::uint64_t){image.width=w;image.height=h;const auto* bytes=reinterpret_cast<const std::uint8_t*>(pixels);image.fallback.assign(bytes,bytes+std::size_t(w)*h*4);image.pixels=&image.fallback;return true;};
                const auto draw=[&](HDC dc,const std::wstring& t,RECT r,HFONT f,COLORREF){drawHudTextBody(dc,t,r,f,judgement?colors[i]:RGB(245,248,252),judgement&&i==0?0:-1);};
                if(!raster.draw(nullptr,4096,512,text,{0,0,4096,512},font,0,1,draw,false,capture,true))detail::fail(ErrorCode::AllocationFailed,e.id,"Cannot rasterize fallback HUD glyph");
                ++stats.hudFontGlyphs;
            }
        }
        // Shelf atlas, with a transparent pixel around each glyph to avoid filtering bleed.
        unsigned x=1,y=1,row=0,usedWidth=1;
        for(unsigned i=0;i<images.size();++i){const auto& image=images[i];if(x+image.width+1>8192){x=1;y+=row+2;row=0;}
            if(y+image.height+1>8192)detail::fail(ErrorCode::LimitExceeded,e.id,"HUD atlas exceeds 8192px");
            sources[i]=D2D1::RectF(float(x),float(y),float(x+image.width),float(y+image.height));x+=image.width+2;row=std::max(row,image.height);usedWidth=std::max(usedWidth,x);}
        const unsigned width=std::min(8192u,usedWidth),height=y+row+1;const auto bytes=std::uint64_t(width)*height*4;
        if(bytes>limits.totalDecodedImageBytes||stats.bitmapBytes+bytes>limits.totalDecodedImageBytes)detail::fail(ErrorCode::LimitExceeded,e.id,"HUD atlas memory budget exceeded");
        Bytes pixels(static_cast<std::size_t>(bytes));
        for(unsigned i=0;i<images.size();++i){const auto& image=images[i];const auto left=unsigned(sources[i].left),top=unsigned(sources[i].top);
            for(unsigned yy=0;yy<image.height;++yy)std::copy_n(image.pixels->data()+std::size_t(yy)*image.width*4,std::size_t(image.width)*4,pixels.data()+(std::size_t(top+yy)*width+left)*4);}
        for(std::size_t i=0;i<pixels.size();i+=4){pixels[i]=std::uint8_t(unsigned(pixels[i])*e.tint.b/255);pixels[i+1]=std::uint8_t(unsigned(pixels[i+1])*e.tint.g/255);pixels[i+2]=std::uint8_t(unsigned(pixels[i+2])*e.tint.r/255);}
        const auto hr=target->CreateBitmap(D2D1::SizeU(width,height),pixels.data(),width*4,D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&bitmap);
        if(FAILED(hr))detail::fail(ErrorCode::AllocationFailed,e.id,"Cannot upload HUD atlas");
        stats.bitmapBytes+=bytes;++stats.uploadedBitmaps;++stats.hudAtlases;
    }
    unsigned draw(ID2D1RenderTarget* target,const SkinElement& e,float opacity) const noexcept {
        if(grade<0||!bitmap)return 0;
        HUD_SCOPE(HudBitmapGroup);HUD_BITMAP(bitmap.Get(),1+digitCount);
        const auto& h=e.hud;const double width=h.judgementWidth+h.gap+digitCount*h.digitWidth+(digitCount-1)*h.spacing;
        const double fit=std::min(1.0,e.width/std::max(1.0,width));
        double x=e.x+(e.textStyle.align==TextAlign::Left?0:e.textStyle.align==TextAlign::Center?(e.width-width*fit)/2:e.width-width*fit);
        const double height=std::max(h.judgementHeight,h.digitHeight)*fit;
        const double y=e.y+(e.textStyle.verticalAlign==TextVerticalAlign::Top?0:e.textStyle.verticalAlign==TextVerticalAlign::Center?(e.height-height)/2:e.height-height);
        auto paint=[&](unsigned index,double w,double hh){const auto dest=D2D1::RectF(float(x),float(y+(height-hh*fit)/2),float(x+w*fit),float(y+(height+hh*fit)/2));target->DrawBitmap(bitmap.Get(),dest,opacity,D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,&sources[index]);x+=w*fit;};
        paint(unsigned(grade),h.judgementWidth,h.judgementHeight);x+=h.gap*fit;
        for(unsigned i=0;i<digitCount;++i){paint(6+digits[i],h.digitWidth,h.digitHeight);x+=h.spacing*fit;}
        return 1+digitCount;
    }
};
}
