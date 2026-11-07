#pragma once
#include "skin/SkinRenderer.h"
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <stdexcept>
namespace skin_test {
using namespace sevenmania::skin;
using Microsoft::WRL::ComPtr;
unsigned checks{};
void require(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
void hr(HRESULT result){if(FAILED(result))throw std::runtime_error("DirectX/WIC operation failed: "+std::to_string(static_cast<unsigned>(result)));}
struct Surface {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> texture,staging;ComPtr<ID2D1Factory> factory;ComPtr<ID2D1RenderTarget> target;
    unsigned width,height;
    Surface(unsigned w,unsigned h,D3D_DRIVER_TYPE driver):width(w),height(h) {
        hr(D3D11CreateDevice(nullptr,driver,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
        D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=1;d.ArraySize=1;
        d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        hr(device->CreateTexture2D(&d,nullptr,&texture));
        d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        hr(device->CreateTexture2D(&d,nullptr,&staging));
        ComPtr<IDXGISurface> surface;hr(texture.As(&surface));
        hr(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory.GetAddressOf()));
        hr(factory->CreateDxgiSurfaceRenderTarget(surface.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(d.Format,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96),&target));
    }
    Bytes render(const SkinRenderer& renderer) {
        target->BeginDraw();target->Clear(D2D1::ColorF(0,0,0,1));
        const auto previous=D2D1::Matrix3x2F::Translation(7,9);target->SetTransform(previous);
        require(renderer.draw(float(width),float(height)),"draw failed");
        D2D1_MATRIX_3X2_F after;target->GetTransform(&after);
        require(after._31==7&&after._32==9,"caller transform not restored");
        hr(target->EndDraw());
        context->CopyResource(staging.Get(),texture.Get());D3D11_MAPPED_SUBRESOURCE m{};
        hr(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m));Bytes pixels(std::size_t(width)*height*4);
        for(unsigned y=0;y<height;++y)std::copy_n(static_cast<const BYTE*>(m.pData)+std::size_t(y)*m.RowPitch,width*4,pixels.data()+std::size_t(y)*width*4);
        context->Unmap(staging.Get(),0);return pixels;
    }
};
void save(const std::filesystem::path& path,const Bytes& pixels,unsigned w,unsigned h) {
    ComPtr<IWICImagingFactory> f;ComPtr<IWICStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;
    hr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f)));
    hr(f->CreateStream(&stream));hr(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE));
    hr(f->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));hr(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
    hr(encoder->CreateNewFrame(&frame,nullptr));hr(frame->Initialize(nullptr));hr(frame->SetSize(w,h));
    auto format=GUID_WICPixelFormat32bppBGRA;hr(frame->SetPixelFormat(&format));
    hr(frame->WritePixels(h,w*4,static_cast<UINT>(pixels.size()),const_cast<BYTE*>(pixels.data())));hr(frame->Commit());hr(encoder->Commit());
}
}
