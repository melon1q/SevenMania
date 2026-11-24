#pragma once
#include "Lr2FrameScale.h"
#include <d3d11.h>
#include <wrl/client.h>
#include "Lr2HybridShaders.h"

class Lr2HybridUpscale {
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11VertexShader> vertex_;
    Ptr<ID3D11PixelShader> pixel_;
    Ptr<ID3D11SamplerState> sampler_;
    Ptr<ID3D11Buffer> constants_;
    Ptr<ID3D11RasterizerState> rasterizer_;
    Ptr<ID3D11ShaderResourceView> input_;
    Ptr<ID3D11RenderTargetView> output_;
    bool failed_{};

    bool prepareShaders(ID3D11Device* device) {
#if BMS_HAS_LR2_HYBRID
        if (FAILED(device->CreateVertexShader(lr2Hybrid_vertex,sizeof(lr2Hybrid_vertex),nullptr,&vertex_)) ||
            FAILED(device->CreatePixelShader(lr2Hybrid_pixel,sizeof(lr2Hybrid_pixel),nullptr,&pixel_))) return false;
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.ComparisonFunc=D3D11_COMPARISON_NEVER;
        sampler.MaxLOD=D3D11_FLOAT32_MAX;
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode=D3D11_FILL_SOLID;
        raster.CullMode=D3D11_CULL_NONE;
        raster.DepthClipEnable=TRUE;
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth=32;
        buffer.Usage=D3D11_USAGE_DEFAULT;
        buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        return SUCCEEDED(device->CreateSamplerState(&sampler,&sampler_)) &&
            SUCCEEDED(device->CreateRasterizerState(&raster,&rasterizer_)) &&
            SUCCEEDED(device->CreateBuffer(&buffer,nullptr,&constants_));
#else
        (void)device;
        return false;
#endif
    }
public:
    void releaseInput() { input_.Reset(); }
    bool enabled() const { return !failed_; }
    void disable() {
        if (!failed_) OutputDebugStringW(L"LR2 hybrid upscale unavailable. Using Bicubic.\n");
        failed_=true;
    }
    bool prepare(ID3D11Device* device,ID3D11Texture2D* input,ID3D11Texture2D* output) {
        if (failed_) return false;
        if ((!constants_ && !prepareShaders(device)) ||
            (!input_ && FAILED(device->CreateShaderResourceView(input,nullptr,&input_))) ||
            (!output_ && FAILED(device->CreateRenderTargetView(output,nullptr,&output_)))) {
            disable();
            return false;
        }
        return true;
    }
    void draw(ID3D11DeviceContext* context,const sevenmania::skin::lr2::FrameScale& scale) {
        const float values[]{float(scale.width),float(scale.height),scale.x,scale.y,scale.outputWidth,scale.outputHeight,0,0};
        const D3D11_VIEWPORT viewport{scale.x,scale.y,scale.outputWidth,scale.outputHeight,0,1};
        const float black[]{0,0,0,1};
        auto* target=output_.Get();
        auto* input=input_.Get();
        auto* sampler=sampler_.Get();
        auto* constants=constants_.Get();
        context->ClearState();
        context->UpdateSubresource(constants,0,nullptr,values,0,0);
        context->ClearRenderTargetView(target,black);
        context->OMSetRenderTargets(1,&target,nullptr);
        context->RSSetState(rasterizer_.Get());
        context->RSSetViewports(1,&viewport);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex_.Get(),nullptr,0);
        context->PSSetShader(pixel_.Get(),nullptr,0);
        context->PSSetShaderResources(0,1,&input);
        context->PSSetSamplers(0,1,&sampler);
        context->PSSetConstantBuffers(0,1,&constants);
        context->Draw(3,0);
        context->ClearState();
    }
};
