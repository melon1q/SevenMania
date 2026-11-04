#include "SkinInternal.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>

namespace sevenmania::skin::detail {
void imageInfo(SkinResource& resource,const Limits& limits) {
    const auto& data=*resource.bytes;
    const auto ext=resource.path.substr(resource.path.find_last_of('.'));
    resource.width=resource.height=0;
    if(ext==".json") { resource.kind=ResourceKind::AnimationData;(void)json(data,resource.path,limits);return; }
    if(ext==".ttf"||ext==".otf") {
        resource.kind=ResourceKind::Font;
        const auto be16=[&](std::size_t p){return std::uint32_t(data[p])<<8|data[p+1];};
        const auto be32=[&](std::size_t p){return be16(p)<<16|be16(p+2);};
        if(data.size()<12 || (be32(0)!=0x00010000&&be32(0)!=0x4f54544f)) fail(ErrorCode::UnsupportedResource,resource.path,"Expected sfnt TTF/OTF header (font registration is deferred)");
        const auto tables=be16(4);
        if(!tables||tables>256||12+tables*16>data.size())fail(ErrorCode::UnsupportedResource,resource.path,"Invalid font table directory");
        for(std::size_t i=0;i<tables;++i) {
            const auto offset=be32(12+i*16+8),length=be32(12+i*16+12);
            if(std::uint64_t(offset)+length>data.size())fail(ErrorCode::UnsupportedResource,resource.path,"Font table exceeds resource");
        }
        return;
    }
    if(ext!=".png"&&ext!=".jpg"&&ext!=".jpeg")fail(ErrorCode::UnsupportedResource,resource.path,"Unsupported resource format");
    const auto checkSize=[&](std::uint32_t w,std::uint32_t h){
        if(!w||!h||w>limits.imageDimension||h>limits.imageDimension||std::uint64_t(w)*h>limits.imagePixels)
            fail(ErrorCode::LimitExceeded,resource.path,"Image dimensions/pixel count exceed limit");
    };
    if(ext==".png") {
        const Bytes signature{137,80,78,71,13,10,26,10};
        if(data.size()<33 || !std::equal(signature.begin(),signature.end(),data.begin()) ||
            std::string(data.begin()+12,data.begin()+16)!="IHDR")fail(ErrorCode::InvalidImage,resource.path,"Invalid PNG signature/IHDR");
        const auto be32=[&](std::size_t p){return std::uint32_t(data[p])<<24|std::uint32_t(data[p+1])<<16|std::uint32_t(data[p+2])<<8|data[p+3];};
        if(be32(8)!=13)fail(ErrorCode::InvalidImage,resource.path,"Invalid PNG IHDR size");
        checkSize(be32(16),be32(20));
    } else {
        if(data.size()<4||data[0]!=255||data[1]!=216)fail(ErrorCode::InvalidImage,resource.path,"Invalid JPEG signature");
        std::size_t p=2;bool found=false;
        while(p<data.size()) {
            if(data[p++]!=255)break;
            while(p<data.size()&&data[p]==255)++p;
            if(p==data.size())break;const auto marker=data[p++];
            if(marker==0xda||marker==0xd9)break;
            if(marker==1||(marker>=0xd0&&marker<=0xd7))continue;
            if(data.size()-p<2)break;
            const std::size_t length=std::size_t(data[p])<<8|data[p+1];
            if(length<2||length>data.size()-p)break;
            if(marker>=0xc0&&marker<=0xcf&&marker!=0xc4&&marker!=0xc8&&marker!=0xcc) {
                if(length<8)break;
                checkSize(std::uint32_t(data[p+5])<<8|data[p+6],std::uint32_t(data[p+3])<<8|data[p+4]);found=true;break;
            }
            p+=length;
        }
        if(!found)fail(ErrorCode::InvalidImage,resource.path,"Missing valid JPEG frame dimensions");
    }
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(initialized)&&initialized!=RPC_E_CHANGED_MODE)fail(ErrorCode::InvalidImage,resource.path,"Cannot initialize WIC COM apartment");
    struct Apartment {bool own;~Apartment(){if(own)CoUninitialize();}} apartment{SUCCEEDED(initialized)};
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;ComPtr<IWICStream> stream;ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;
    const auto check=[&](HRESULT result){if(FAILED(result))fail(ErrorCode::InvalidImage,resource.path,"Invalid/unsupported image metadata");};
    check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    check(factory->CreateStream(&stream));
    check(stream->InitializeFromMemory(const_cast<BYTE*>(data.data()),static_cast<DWORD>(data.size())));
    const GUID expected=ext==".png"?GUID_ContainerFormatPng:GUID_ContainerFormatJpeg;
    check(factory->CreateDecoder(expected,nullptr,&decoder));
    check(decoder->Initialize(stream.Get(),WICDecodeMetadataCacheOnDemand));
    GUID format{};check(decoder->GetContainerFormat(&format));
    if(ext==".png"?format!=GUID_ContainerFormatPng:format!=GUID_ContainerFormatJpeg)
        fail(ErrorCode::InvalidImage,resource.path,"Image extension does not match encoded format");
    UINT count{};check(decoder->GetFrameCount(&count));
    if(count!=1)fail(ErrorCode::InvalidImage,resource.path,"Only a single image frame is supported");
    check(decoder->GetFrame(0,&frame));check(frame->GetSize(&resource.width,&resource.height));
    checkSize(resource.width,resource.height);
    resource.kind=ext==".png"?ResourceKind::Png:ResourceKind::Jpeg;
}
}
