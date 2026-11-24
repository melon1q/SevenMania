#include <windows.h>
#include "CompositionPresenter.h"
#include <iostream>
#include <stdexcept>
#include <string_view>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "d2d1.lib")

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct GpuTestAccess {
    static bool videoFailed(const CompositionPresenter& renderer) { return renderer.videoFailed_; }
    static HWND outputHwnd(CompositionPresenter& renderer) {
        HWND result{}; if (renderer.swap_) renderer.swap_->GetHwnd(&result); return result;
    }
    static void verifyHybridDeferral(CompositionPresenter& renderer, const std::vector<uint32_t>& base, int size) {
        const auto real = renderer.presentationReady_;
        renderer.presentationReady_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        const auto bytes = renderer.uploadedBytes();
        renderer.hybridReadyPermit_ = false;
        const bool okay = renderer.present(nullptr, base.data(), size, size) && renderer.presentationDeferred() && renderer.uploadedBytes() == bytes;
        bool readyFramesUploaded = true;
        for (int i = 0; i < 3; ++i) {
            renderer.hybridReadyPermit_ = false;
            readyFramesUploaded &= !renderer.canBeginHybridFrame();
            SetEvent(renderer.presentationReady_);
            const HANDLE handle = renderer.presentationWaitHandle();
            readyFramesUploaded &= handle && MsgWaitForMultipleObjectsEx(1, &handle, 1000, 0, 0) == WAIT_OBJECT_0;
            readyFramesUploaded &= renderer.acceptPresentationWake(handle);
            readyFramesUploaded &= !renderer.acceptPresentationWake(handle); // one reservation per signal
            readyFramesUploaded &= renderer.canBeginHybridFrame() && renderer.canBeginHybridFrame();
            readyFramesUploaded &= WaitForSingleObject(handle, 0) == WAIT_TIMEOUT;
            const auto previousBytes = renderer.uploadedBytes();
            // Change a CPU-region pixel: unchanged pixels are no longer re-uploaded.
            auto frame = base; frame[0] ^= 0x00010101u * static_cast<uint32_t>(i + 1);
            readyFramesUploaded &= renderer.present(nullptr, frame.data(), size, size) && renderer.uploadedBytes() > previousBytes;
            ResetEvent(renderer.presentationReady_);
        }
        CloseHandle(renderer.presentationReady_); renderer.presentationReady_ = real;
        renderer.hybridReadyPermit_ = false;
        require(okay, "busy hybrid queue still uploaded or rendered a frame");
        require(readyFramesUploaded, "ready hybrid frames were delayed by a separate software limiter");
    }
    static void verifyPendingRetry(CompositionPresenter& renderer) {
        const HANDLE real = renderer.presentationReady_;
        renderer.presentationReady_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        renderer.hybridReadyPermit_ = true; renderer.pendingFrame_ = true;
        require(renderer.finishPresentation(DXGI_ERROR_WAS_STILL_DRAWING) && renderer.presentationDeferred() &&
            renderer.hasPendingFrame() && !renderer.presentationWaitHandle(),
            "nonblocking Present lost its acquired permit; no future signal is guaranteed after rejection");
        require(renderer.canBeginHybridFrame(), "rejected Present waits twice for the same output slot");
        const auto frames = renderer.submittedFrames();
        require(renderer.finishPresentation(DXGI_STATUS_OCCLUDED) && renderer.submittedFrames() == frames &&
            !renderer.hasPendingFrame() && !renderer.hybridReadyPermit_, "occlusion was counted as an output frame");
        renderer.hybridReadyPermit_ = true; renderer.pendingFrame_ = true;
        require(renderer.finishPresentation(S_OK) && renderer.submittedFrames() == frames + 1 &&
            !renderer.hasPendingFrame() && !renderer.hybridReadyPermit_, "accepted output did not consume its permit");
        renderer.presentationDeferred_ = false;
        CloseHandle(renderer.presentationReady_); renderer.presentationReady_ = real;
    }
    static std::vector<uint32_t> renderAndRead(CompositionPresenter& renderer,
        const std::vector<uint32_t>& base, const std::vector<uint32_t>& overlay, int size) {
        require(renderer.drawFrame(base.data(), size, overlay.data(), size, size, 0), "GPU drawing failed");
        Microsoft::WRL::ComPtr<ID3D11Texture2D> buffer, staging;
        buffer = renderer.frame_;
        require(buffer != nullptr, "GPU buffer missing");
        D3D11_TEXTURE2D_DESC desc{}; buffer->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
        require(SUCCEEDED(renderer.device_->CreateTexture2D(&desc, nullptr, &staging)), "GPU staging failed");
        renderer.context_->CopyResource(staging.Get(), buffer.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        require(SUCCEEDED(renderer.context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "GPU readback failed");
        std::vector<uint32_t> pixels(static_cast<size_t>(size) * size);
        for (int y = 0; y < size; ++y) std::copy_n(reinterpret_cast<const uint32_t*>(
            static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch), size, pixels.data() + y * size);
        renderer.context_->Unmap(staging.Get(), 0);
        return pixels;
    }

    static void verifyLr2Upscale(HWND window) {
        using namespace sevenmania::skin::lr2;
        require(!frameScale(UpscaleFilter::Original,640,480,1920,1080,false),"original path must stay direct");
        require(!frameScale(UpscaleFilter::Nearest,640,480,1280,480,false),"letterbox at original size must stay direct");
        require(!frameScale(UpscaleFilter::Nearest,1280,720,1024,768,true),"a shrinking axis must stay direct");
        require(!frameScale(UpscaleFilter::Bicubic,1920,1080,3840,2160,true),"FHD skins are excluded");
        const auto hd=frameScale(UpscaleFilter::Bicubic,1280,720,1600,900,true);
        require(hd&&hd->x==0&&hd->outputHeight==900,"HD stretch with fractional scale");
        const auto wide=frameScale(UpscaleFilter::Nearest,640,480,1366,768,false);
        require(wide&&std::abs(wide->x-171.f)<.01f,"fractional SD scale centered");
        float pointerX=683,pointerY=384;
        wide->toCanvas(pointerX,pointerY);
        require(std::abs(pointerX-320)<.01f&&std::abs(pointerY-240)<.01f,"client pointer did not map to native canvas");

        CompositionPresenter renderer;
        renderer.setModernDrawing(true);
        constexpr int size=1280;
        std::vector<uint32_t> base(size_t(size)*size,0xff204060u);
        ID2D1Bitmap1* previousNative{};
        for (const auto filter:{UpscaleFilter::Nearest,UpscaleFilter::Bicubic}) {
            require(renderer.prepare(window,size,size),"native frame renderer unavailable");
            const auto scale=frameScale(filter,640,480,size,size,false);
            require(scale&&renderer.setFrameUpscale(*scale,true),"native target allocation failed");
            if(previousNative)require(renderer.nativeFrame_.Get()==previousNative,"filter switch recreated the native bitmap");
            previousNative=renderer.nativeFrame_.Get();
            renderer.rectangle(320,0,640,480,RGB(255,255,255));
            renderer.setFrameForeground([](ID2D1RenderTarget* target,int w,int h) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
                if(SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Blue),&brush)))
                    target->FillRectangle(D2D1::RectF(float(w-40),float(h-40),float(w),float(h)),brush.Get());
            });
            renderer.markFrameForegroundBoundary();
            renderer.markOutputOverlayBoundary();
            renderer.rectangle(20,20,40,40,RGB(0,255,0));
            require(renderer.drawFrame(base.data(),size,nullptr,0,0,0),"native LR2 frame failed to draw");
            std::vector<uint32_t> pixels;
            require(renderer.captureLastFrame(pixels,size,size),"upscaled frame readback failed");
            const auto rgb=[&](int px,int py) { return pixels[size_t(py)*size+px]&0xffffffu; };
            require(rgb(10,10)==0&&rgb(30,30)==0x00ff00u,"letterbox or client-space overlay changed");
            require(rgb(200,640)==0x204060u&&rgb(900,640)==0xffffffu&&rgb(1240,1080)==0x0000ffu,
                "upscaled CPU frame, notes or foreground missing");
            if(filter==UpscaleFilter::Nearest)require(rgb(639,640)==0x204060u&&rgb(640,640)==0xffffffu,
                "nearest neighbor blended a source pixel edge");
            else require(rgb(639,640)!=0x204060u&&rgb(640,640)!=0xffffffu,"Bicubic did not smooth the edge");
        }
        renderer.reset();
        require(renderer.prepare(window,1600,900)&&renderer.setFrameUpscale(*hd,false),"resize to an HD skin failed");
        std::pair<int,int> canvas{};
        require(renderer.drawFrame(nullptr,1600,nullptr,0,0,0,{
            [](void* data,ID2D1RenderTarget* target,int w,int h) {
                *static_cast<std::pair<int,int>*>(data)={w,h};
                target->Clear(D2D1::ColorF(D2D1::ColorF::Red));
            },&canvas}),"scene-only upscale failed");
        std::vector<uint32_t> pixels;
        require(canvas==std::pair{1280,720}&&renderer.captureLastFrame(pixels,1600,900)&&
            (pixels[450*1600+800]&0xffffffu)==0xff0000u,"scene-only frame did not use the HD native canvas");

        require(renderer.prepare(window,size,size),"resize back to SD output failed");
        const auto sd=frameScale(UpscaleFilter::Nearest,640,480,size,size,false);
        require(sd&&renderer.setFrameUpscale(*sd,true),"native target could not be recreated");
        renderer.nativeBackdrop_.Reset(); // Simulate loss of an upscale-specific resource before drawing.
        renderer.hybridReadyPermit_=true;
        require(!renderer.present(window,base.data(),size,size)&&
            std::string_view(renderer.lastFailureStage())=="lr2-upscale-draw","failed upscale was not diagnosed");
        require(renderer.prepare(window,size,size)&&!renderer.setFrameUpscale(*sd,true)&&
            renderer.drawFrame(base.data(),size,nullptr,0,0,0)&&renderer.captureLastFrame(pixels,size,size)&&
            (pixels[0]&0xffffffu)==0x204060u,"original drawing did not recover after a failed upscale");
        renderer.retryUpscale();
        require(renderer.setFrameUpscale(*sd,true),"changing the filter did not allow upscale recreation");

#if BMS_HAS_LR2_HYBRID
        std::vector<uint32_t> hybridPixels;
        for (const auto filter:{UpscaleFilter::Hybrid,UpscaleFilter::Bicubic}) {
            require(renderer.prepare(window,size,size),"hybrid output prepare failed");
            const auto scale=frameScale(filter,640,480,size,size,false);
            require(scale&&renderer.setFrameUpscale(*scale,true)&&renderer.frameScale_->filter==filter,
                "hybrid source or shader failed to initialize");
            renderer.rectangle(320,0,640,480,RGB(255,255,255));
            renderer.markOutputOverlayBoundary();
            renderer.rectangle(20,20,40,40,RGB(0,255,0));
            require(renderer.drawFrame(base.data(),size,nullptr,0,0,0)&&renderer.captureLastFrame(pixels,size,size),
                "hybrid draw or readback failed");
            const auto rgb=[&](int px,int py) { return pixels[size_t(py)*size+px]&0xffffffu; };
            require(rgb(10,10)==0&&rgb(30,30)==0x00ff00u&&rgb(200,640)==0x204060u&&rgb(900,640)==0xffffffu,
                "hybrid output broke letterbox, native overlay order or flat colors");
            if (filter==UpscaleFilter::Hybrid) {
                hybridPixels=pixels;
                for (int xEdge=635;xEdge<645;++xEdge) {
                    const auto color=rgb(xEdge,640);
                    require(((color>>16)&255)>=0x20&&((color>>8)&255)>=0x40&&(color&255)>=0x60,
                        "hybrid undershot the local color range");
                }
            } else {
                require(!std::equal(pixels.begin()+640*size+635,pixels.begin()+640*size+645,
                    hybridPixels.begin()+640*size+635),"hybrid and Bicubic produced identical edge samples");
            }
        }
        constexpr int fractionalWidth=1366,fractionalHeight=768;
        require(renderer.prepare(window,fractionalWidth,fractionalHeight),"fractional hybrid output prepare failed");
        const auto fractional=frameScale(UpscaleFilter::Hybrid,640,480,fractionalWidth,fractionalHeight,false);
        require(fractional&&renderer.setFrameUpscale(*fractional,true),"fractional hybrid resource unavailable");
        std::vector<uint32_t> gradient(size_t(fractionalWidth)*fractionalHeight,0xff000000u);
        for(int y=0;y<480;++y)for(int x=0;x<640;++x)
            gradient[size_t(y)*fractionalWidth+x]=0xff000000u+uint32_t(x*255/639)*0x010101u;
        const uint32_t halfRed=0x80800000u;
        require(renderer.sprite(&halfRed,1,1,20,20,12,12,142),"translucent sprite setup failed");
        require(renderer.drawFrame(gradient.data(),fractionalWidth,nullptr,0,0,0)&&
            renderer.captureLastFrame(pixels,fractionalWidth,fractionalHeight),"fractional hybrid readback failed");
        const auto sample=[&](int x,int y) { return pixels[size_t(y)*fractionalWidth+x]&0xffffffu; };
        require(sample(170,384)==0&&sample(1195,384)==0,"fractional letterbox boundaries moved");
        require(std::abs(int(sample(683,384)&255)-127)<=2&&sample(500,384)<sample(800,384),
            "flat gradient developed a discontinuity");
        const auto red=sample(211,40);
        require(((red>>16)&255)>=130&&(red&255)<=7,"premultiplied sprite blend changed");
        require(renderer.prepare(window,size,size),"could not restore output size after fractional test");
#endif
        require(frameScale(UpscaleFilter::Hybrid,1280,720,1920,1080,false)->filter==UpscaleFilter::Bicubic,
            "HD should fall back to Bicubic");
        renderer.hybridUpscale_.disable();
        require(renderer.prepare(window,size,size)&&
            renderer.setFrameUpscale(*frameScale(UpscaleFilter::Hybrid,640,480,size,size,false),true)&&
            renderer.frameScale_->filter==UpscaleFilter::Bicubic,"unavailable hybrid did not fall back to Bicubic");
    }

    static void verifyLr2OutputNotes(HWND window) {
        using namespace sevenmania::skin::lr2;
        CompositionPresenter renderer;
        renderer.setModernDrawing(true);
        renderer.setAntialias(false);
        constexpr int width=1600,height=1280;
        const std::vector<uint32_t> base(size_t(width)*height,0xff000000u);
        std::vector<uint32_t> pixels;
        for(const auto filter:{UpscaleFilter::Bicubic,UpscaleFilter::Nearest,UpscaleFilter::Hybrid}) {
            for(const bool stretch:{false,true}) {
                require(renderer.prepare(window,width,height),"LR2 note output prepare failed");
                const auto scale=frameScale(filter,640,480,width,height,stretch);
                require(scale&&renderer.setFrameUpscale(*scale,true),"LR2 note canvas unavailable");
                renderer.drawPlayLayersAtOutput([](ID2D1RenderTarget* target,int,int) {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
                    if(SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Purple),&brush)))
                        target->FillRectangle(D2D1::RectF(102,110,110,170),brush.Get());
                });
                renderer.setFrameForeground([](ID2D1RenderTarget* target,int,int) {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
                    if(SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(0,0,1,.5f),&brush)))
                        target->FillRectangle(D2D1::RectF(100,90,130,110),brush.Get());
                });
                // A note source and its lane clip are expressed in authored coordinates.
                const uint32_t white=0xffffffff;
                const RECT clip{100,90,111,160};
                auto* target=renderer.renderTarget();
                Microsoft::WRL::ComPtr<ID2D1Bitmap> note;
                require(SUCCEEDED(target->CreateBitmap(D2D1::SizeU(1,1),&white,4,
                    D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),
                    &note)),"output note sprite creation failed");
                require(renderer.bitmapSprite(note.Get(),D2D1::RectF(98,95,120,150),1,&clip,D2D1::RectF(0,0,1,1)),
                    "output note sprite queue failed");
                renderer.rectangle(100,95,120,100,RGB(255,0,0)); // Player cover above the note.
                renderer.markFrameForegroundBoundary();
                renderer.markOutputOverlayBoundary();
                renderer.rectangle(10,10,20,20,RGB(0,255,0)); // Native pause UI coordinates.
                require(renderer.drawFrame(base.data(),width,nullptr,0,0,0)&&renderer.captureLastFrame(pixels,width,height),
                    "output note capture failed");
                const auto rgb=[&](int x,int y) { return pixels[size_t(y)*width+x]&0xffffffu; };
                const auto at=[&](float x,float y) {
                    return rgb(int(scale->x+x*scale->outputWidth/scale->width),
                               int(scale->y+y*scale->outputHeight/scale->height));
                };
                require(at(105,120)==0xffffffu&&at(113,120)==0&&at(97,120)==0,"note or lane clip in wrong coordinates");
                require(at(105,160)==0x800080u,"pre-note output layer is missing or drawn over the notes");
                const auto covered=at(105,97);
                require((covered>>16)>=126&&(covered&255)>=126&&(covered&0xff00u)==0,
                    "player cover or authored translucent cover changed order");
                const auto translucent=at(105,105);
                require((translucent>>16)>=126&&(translucent&255)==255,
                    "authored foreground did not see the output note underneath");
                require(rgb(15,15)==0x00ff00u,"client-space overlay inherited the playfield transform");
                const int edge=int(std::lround(scale->x+100*scale->outputWidth/scale->width));
                const int noteY=int(scale->y+120*scale->outputHeight/scale->height);
                require(rgb(edge-1,noteY)==0&&rgb(edge,noteY)==0xffffffu,"background filter softened the note edge");
            }
        }
    }

#if BMS_HAS_LR2_HYBRID
    static void measureLr2Upscale(HWND window) {
        using namespace sevenmania::skin::lr2;
        CompositionPresenter renderer;
        renderer.setModernDrawing(true);
        constexpr int frames=30;
        for (const int height : {1080,1440}) {
            const int width=height*16/9;
            std::vector<uint32_t> base(size_t(width)*height,0xff18304au);
            require(renderer.prepare(window,width,height),"benchmark output prepare failed");
            for (const auto filter : {UpscaleFilter::Bicubic,UpscaleFilter::Hybrid}) {
                require(renderer.prepare(window,width,height),"benchmark frame prepare failed");
                const auto scale=frameScale(filter,640,480,width,height,false);
                require(scale&&renderer.setFrameUpscale(*scale,true)&&renderer.frameScale_->filter==filter,
                    "benchmark filter unavailable");
                // Simulate narrow lanes, hard diagonals and translucent HUD details.
                for (int x=12;x<640;x+=26)renderer.rectangle(float(x),10,float(x+1),470,RGB(220,220,255));
                for (int y=0;y<300;++y)renderer.rectangle(float(y),float(y),float(y+2),float(y+1),RGB(255,190,0),.6f);
                auto draw=[&] { return renderer.drawFrame(base.data(),width,nullptr,0,0,0); };
                require(draw(),"benchmark warmup failed");
                std::vector<double> timings;
                auto* immediate=renderer.context_.Get();
                using Query=Microsoft::WRL::ComPtr<ID3D11Query>;
                auto create=[&](D3D11_QUERY type) {
                    Query query;
                    const D3D11_QUERY_DESC desc{type,0};
                    require(SUCCEEDED(renderer.device_->CreateQuery(&desc,&query)),"GPU timing query unavailable");
                    return query;
                };
                const auto disjoint=create(D3D11_QUERY_TIMESTAMP_DISJOINT);
                std::array<Query,frames> start,end;
                for(int i=0;i<frames;++i) {
                    start[i]=create(D3D11_QUERY_TIMESTAMP);
                    end[i]=create(D3D11_QUERY_TIMESTAMP);
                }
                immediate->Begin(disjoint.Get());
                for(int i=0;i<frames;++i) {
                    immediate->End(start[i].Get());
                    require(draw(),"benchmark frame failed");
                    immediate->End(end[i].Get());
                }
                immediate->End(disjoint.Get());
                immediate->Flush();
                D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock{};
                HRESULT ready=S_FALSE;
                for (int wait=0;wait<2000 && ready==S_FALSE;++wait) {
                    ready=immediate->GetData(disjoint.Get(),&clock,sizeof(clock),D3D11_ASYNC_GETDATA_DONOTFLUSH);
                    if(ready==S_FALSE)Sleep(1);
                }
                if(ready!=S_OK || clock.Disjoint || !clock.Frequency) {
                    std::cout << "GPU timing unavailable at " << height << "p\n";
                    continue;
                }
                for(int i=0;i<frames;++i) {
                    UINT64 first{},last{};
                    if(immediate->GetData(start[i].Get(),&first,sizeof(first),0)==S_OK &&
                       immediate->GetData(end[i].Get(),&last,sizeof(last),0)==S_OK && last>=first)
                        timings.push_back(double(last-first)*1000/clock.Frequency);
                }
                if(timings.empty())continue;
                std::sort(timings.begin(),timings.end());
                std::cout << height << "p " << (filter==UpscaleFilter::Hybrid?"Hybrid":"Bicubic")
                          << " GPU median " << timings[timings.size()/2] << " ms, p95 "
                          << timings[(95*timings.size()+99)/100-1] << " ms\n";
            }
        }
    }
#endif
};
int main(int argc,char** argv) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HWND window = CreateWindowW(L"STATIC", L"GPU test", WS_OVERLAPPEDWINDOW, 0, 0, 128, 128,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    int result = 0;
    try {
        require(window != nullptr, "GPU test window failed");
        if(argc==2&&std::string_view(argv[1])=="--lr2-upscale") {
            GpuTestAccess::verifyLr2Upscale(window);
            GpuTestAccess::verifyLr2OutputNotes(window);
#if BMS_HAS_LR2_HYBRID
            GpuTestAccess::measureLr2Upscale(window);
#endif
        } else {
        CompositionPresenter renderer;
        renderer.setVsync(true);
        for (int size : {64, 96}) {
            require(renderer.prepare(window, size, size), "hardware GPU renderer unavailable");
            std::vector<uint32_t> base(static_cast<size_t>(size) * size, 0xff204060u), overlay(base.size(), 0);
            renderer.rectangle(4, 4, 20, 20, RGB(255, 0, 0));
            renderer.rectangle(30, 10.5f, 35, 15.5f, RGB(255, 0, 0));
            renderer.rectangle(4, 18, 20, 20, RGB(0, 0, 0));
            overlay[8 * size + 8] = 0x80008000u;
            const uint32_t sprite = 0xff00ff00u;
            require(renderer.sprite(&sprite, 1, 1, 12, 12, 2, 2, 42, 0.5f), "GPU sprite upload failed");
            const auto pixels = GpuTestAccess::renderAndRead(renderer, base, overlay, size);
            std::vector<uint32_t> captured;
            require(renderer.captureLastFrame(captured, size, size) && captured == pixels,
                "result fade snapshot lost GPU notes or HUD");
            require(!renderer.captureLastFrame(captured, size + 1, size), "snapshot accepted a stale size");
            require((pixels[0] & 0xffffffu) == 0x204060u, "backdrop upload changed");
            require((pixels[6 * size + 6] & 0xffffffu) == 0xff0000u, "GPU note fill missing");
            require((pixels[19 * size + 6] & 0xffffffu) == 0, "cover is behind notes");
            const uint32_t blend = pixels[8 * size + 8];
            require(((blend >> 16) & 255) >= 126 && ((blend >> 16) & 255) <= 128 &&
                ((blend >> 8) & 255) >= 127 && ((blend >> 8) & 255) <= 129, "HUD alpha composition changed");
            const unsigned edge = (pixels[10 * size + 31] >> 16) & 255;
            require(edge > 32 && edge < 255, "fractional note movement lost antialiasing");
            renderer.setAntialias(false);
            const auto aliased = GpuTestAccess::renderAndRead(renderer, base, overlay, size);
            const unsigned sharpEdge = (aliased[10 * size + 31] >> 16) & 255;
            require(sharpEdge == 32 || sharpEdge == 255, "GPU antialias toggle retained a blended edge");
            renderer.setAntialias(true);
            require(renderer.lowLatency().active() == latency::Mode::Off &&
                renderer.lowLatency().waitOwner() == latency::WaitOwner::ExistingFramePacer,
                "GPU initialization required or activated an absent low-latency SDK");
            const uint32_t animated = pixels[12 * size + 12];
            require(((animated >> 16) & 255) >= 126 && ((animated >> 16) & 255) <= 129 &&
                ((animated >> 8) & 255) >= 126 && ((animated >> 8) & 255) <= 129, "GPU sprite opacity changed");
            require(renderer.present(window, base.data(), size, size, overlay.data(), size, size), "GPU presentation failed");
            renderer.setVsync(false);
        }
        {
            constexpr int size = 64;
            std::vector<uint32_t> background(size * size, 0xff804020u), cpu(size * size, 0xff204080u), empty(size * size, 0);
            require(!CompositionPresenter{}.skipUnchangedUploads(), "unchanged-upload skipping must be off by default");
            renderer.setSkipUnchangedUploads(true); // opt-in path
            require(renderer.prepare(window, size, size), "hybrid setup failed");
            renderer.hybridBackground(background.data(), 1, 50, {8, 0, 16, size}, 100, {{0, 0, 8, size}});
            auto pixels = GpuTestAccess::renderAndRead(renderer, cpu, empty, size);
            require((pixels[2 * size + 2] & 0xffffffu) == 0x204080u, "hybrid CPU region was lost");
            require((pixels[2 * size + 10] & 0xffffffu) == 0, "hybrid lane opacity was lost");
            const auto red = (pixels[2 * size + 20] >> 16) & 255;
            require(red >= 63 && red <= 65, "hybrid background darkness changed");
            const auto bytes = renderer.uploadedBytes();
            std::fill(background.begin(), background.end(), 0xff204080u);
            require(renderer.prepare(window, size, size), "hybrid reuse failed");
            renderer.hybridBackground(background.data(), 1, 50, {8, 0, 16, size}, 100, {{0, 0, 8, size}});
            pixels = GpuTestAccess::renderAndRead(renderer, cpu, empty, size);
            require(((pixels[2 * size + 20] >> 16) & 255) >= 63, "unchanged generation reuploaded the background");
            require(renderer.uploadedBytes() == bytes, "unchanged hybrid CPU region was uploaded again");
            {   // A/B switch off: the unchanged region is uploaded whole again (previous behaviour).
                renderer.setSkipUnchangedUploads(false);
                const auto before = renderer.uploadedBytes();
                require(renderer.prepare(window, size, size), "hybrid A/B reuse failed");
                renderer.hybridBackground(background.data(), 1, 50, {8, 0, 16, size}, 100, {{0, 0, 8, size}});
                GpuTestAccess::renderAndRead(renderer, cpu, empty, size);
                require(renderer.uploadedBytes() - before == 8 * size * 4, "disabled skip did not re-upload the CPU region");
                renderer.setSkipUnchangedUploads(true);
                // Snapshot was dropped while disabled: the first frame back uploads once.
                require(renderer.prepare(window, size, size), "hybrid A/B restore failed");
                renderer.hybridBackground(background.data(), 1, 50, {8, 0, 16, size}, 100, {{0, 0, 8, size}});
                GpuTestAccess::renderAndRead(renderer, cpu, empty, size);
            }
            // A changed CPU pixel uploads its region once (whole, no partial-update sync).
            const auto unchanged = renderer.uploadedBytes();
            cpu[5 * size + 3] = 0xff00ff00u;
            require(renderer.prepare(window, size, size), "hybrid dirty reuse failed");
            renderer.hybridBackground(background.data(), 1, 50, {8, 0, 16, size}, 100, {{0, 0, 8, size}});
            pixels = GpuTestAccess::renderAndRead(renderer, cpu, empty, size);
            require((pixels[5 * size + 3] & 0xffffffu) == 0x00ff00u, "dirty hybrid CPU pixel was not uploaded");
            require((pixels[2 * size + 2] & 0xffffffu) == 0x204080u, "dirty upload damaged the rest of the CPU region");
            require(renderer.uploadedBytes() - unchanged == 8 * size * 4, "changed hybrid CPU region was not uploaded whole");
            GpuTestAccess::verifyHybridDeferral(renderer, cpu, size);
            GpuTestAccess::verifyPendingRetry(renderer);
            require(renderer.prepare(window, size, size), "hybrid invalidation failed");
            renderer.hybridBackground(background.data(), 2, 50, {8, 0, 16, size}, 100, {{24, 0, 32, size}});
            pixels = GpuTestAccess::renderAndRead(renderer, cpu, empty, size);
            require(((pixels[2 * size + 2] >> 16) & 255) <= 17, "moving hybrid CPU region left stale pixels");
            require((pixels[2 * size + 26] & 0xffffffu) == 0x204080u, "moved hybrid CPU region disappeared");
            auto first = std::make_shared<std::vector<uint32_t>>(size * size,0xff556677u);
            auto last = std::make_shared<std::vector<uint32_t>>(*first);
            (*last)[31 * size + 5] = 0xff00ff00u;
            renderer.hybridBackground(first->data(),3,0,{},0,{}, {first,first->data()});
            GpuTestAccess::renderAndRead(renderer,cpu,empty,size);
            const auto priorBytes = renderer.uploadedBytes();
            renderer.hybridBackground(last->data(),5,0,{},0,{}, {last,last->data()});
            pixels = GpuTestAccess::renderAndRead(renderer,cpu,empty,size);
            // The swap target ignores alpha. Compare every displayed RGB channel.
            require(std::equal(pixels.begin(), pixels.end(), last->begin(),
                [](uint32_t a,uint32_t b){return ((a^b)&0xffffffu)==0;}),
                "partial upload changed an unchanged pixel or missed the new pixel");
            require(renderer.uploadedBytes() - priorBytes == size * 4,"partial upload did not isolate one changed row");
            {   // GPU-scaled BGA still: only the small source is uploaded, and it covers the area.
                auto source = std::make_shared<BgaSourceImage>();
                source->width = 4; source->height = 4; source->pixels.assign(16, 0xff00c000u);
                auto full = std::make_shared<std::vector<uint32_t>>(size * size, 0xff00c000u);
                const auto before = renderer.uploadedBytes();
                renderer.hybridBackground(full->data(), 7, 0, {}, 0, {}, {full, full->data()}, {}, source);
                pixels = GpuTestAccess::renderAndRead(renderer, cpu, empty, size);
                require(renderer.uploadedBytes() - before == 4 * 4 * 4, "GPU-scaled BGA uploaded more than its source");
                require(((pixels[40 * size + 40] >> 8) & 255) >= 0xbe && (pixels[40 * size + 40] & 0xff0000u) == 0, "GPU-scaled BGA not drawn");
                const auto again = renderer.uploadedBytes();
                renderer.hybridBackground(full->data(), 7, 0, {}, 0, {}, {full, full->data()}, {}, source);
                GpuTestAccess::renderAndRead(renderer, cpu, empty, size);
                require(renderer.uploadedBytes() == again, "unchanged GPU-scaled BGA was uploaded again");
                // Back to a CPU-scaled frame: the full backdrop must be refreshed whole.
                renderer.hybridBackground(full->data(), 8, 0, {}, 0, {}, {full, full->data()});
                GpuTestAccess::renderAndRead(renderer, cpu, empty, size);
                require(renderer.uploadedBytes() - again == uint64_t(size) * size * 4, "CPU backdrop not refreshed after GPU-scaled frames");
            }
            {   // NV12 video backdrop: 1.5 bytes/pixel upload, converted by the D3D11 video processor.
                auto frame = std::make_shared<std::vector<uint8_t>>(nv12::bytes(size, size), uint8_t(128));
                std::fill_n(frame->begin(), size * size, uint8_t(180)); // light gray (limited range)
                auto full = std::make_shared<std::vector<uint32_t>>(size * size, 0xff000000u);
                nv12::View view; view.width = size; view.height = size; view.matrix = nv12::Matrix::Bt709;
                view.generation = 1; view.data = frame->data(); view.owner = frame;
                const auto before = renderer.uploadedBytes();
                renderer.hybridBackground(full->data(), 9, 0, {}, 0, {}, {full, full->data()}, {}, {}, view);
                pixels = GpuTestAccess::renderAndRead(renderer, cpu, empty, size);
                if (GpuTestAccess::videoFailed(renderer)) { char hr[64]; std::snprintf(hr, sizeof hr, " hr=0x%08lX support=0x%X", (unsigned long)renderer.videoFailResult(), renderer.videoFormatSupport()); throw std::runtime_error(std::string("NV12 video path failed at ") + renderer.videoFailStage() + hr); }
                {
                    require(renderer.uploadedBytes() - before == nv12::bytes(size, size), "NV12 upload size wrong");
                    const auto p = pixels[40 * size + 40];
                    std::vector<uint32_t> converted(size_t(size) * size);
                    nv12::toBgra(frame->data(), size, size, nv12::Matrix::Bt709, false, converted.data());
                    const uint32_t expected = converted[40 * size + 40];
                    for (int shift : {0, 8, 16}) require(std::abs(int((p >> shift) & 255) - int((expected >> shift) & 255)) <= 4, "NV12 GPU color differs from CPU conversion");
                    const auto again = renderer.uploadedBytes();
                    renderer.hybridBackground(full->data(), 9, 0, {}, 0, {}, {full, full->data()}, {}, {}, view);
                    GpuTestAccess::renderAndRead(renderer, cpu, empty, size);
                    require(renderer.uploadedBytes() == again, "unchanged NV12 frame uploaded again");
                    // A real 1080p frame (the size BGA videos actually use).
                    auto hd = std::make_shared<std::vector<uint8_t>>(nv12::bytes(1920, 1080), uint8_t(128));
                    std::fill_n(hd->begin(), 1920 * 1080, uint8_t(180));
                    nv12::View big = view; big.width = 1920; big.height = 1080; big.generation = 2; big.data = hd->data(); big.owner = hd;
                    const auto beforeHd = renderer.uploadedBytes();
                    renderer.hybridBackground(full->data(), 9, 0, {}, 0, {}, {full, full->data()}, {}, {}, big);
                    pixels = GpuTestAccess::renderAndRead(renderer, cpu, empty, size);
                    if (GpuTestAccess::videoFailed(renderer)) throw std::runtime_error(std::string("1080p NV12 failed at ") + renderer.videoFailStage());
                    require(renderer.uploadedBytes() - beforeHd == nv12::bytes(1920, 1080), "1080p NV12 upload size wrong");
                    for (int shift : {0, 8, 16}) require(std::abs(int((pixels[40 * size + 40] >> shift) & 255) - int((expected >> shift) & 255)) <= 4, "1080p NV12 GPU color wrong");
                }
            }
            auto overlay = std::make_shared<std::vector<uint32_t>>(size * size,0x80800000u);
            renderer.hybridBackground(last->data(),5,0,{},0,{}, {last,last->data()},{overlay,overlay->data()});
            pixels=GpuTestAccess::renderAndRead(renderer,cpu,empty,size);
            const uint32_t mixed=pixels[31*size+5];
            require(((mixed>>16)&255)==128 && ((mixed>>8)&255)==127 && (mixed&255)==0,
                "GPU layer premultiplication differs from CPU blending");
        }
        {
            std::vector<uint32_t> reflexBase(64*64, 0xff202020u);
            for (const bool composition : {false, true, false, true}) {
                renderer.setDirectComposition(composition);
                require(renderer.prepare(window, 64, 64), "output backend switch failed");
                require(renderer.usesDirectComposition() == composition, "DirectComposition checkbox did not change output backend");
                if (!composition) {
                    const HWND output = GpuTestAccess::outputHwnd(renderer);
                    require(output && output != window && IsChild(window, output), "native flip chain contaminated the GDI browser HWND");
                    require((GetWindowLongPtrW(output,GWL_STYLE)&WS_DISABLED) != 0 &&
                        SendMessageW(output,WM_NCHITTEST,0,0)==HTTRANSPARENT, "GPU surface intercepts browse/game input");
                }
                const auto pixels = GpuTestAccess::renderAndRead(renderer, reflexBase, {}, 64);
                require((pixels[0] & 0xffffffu) == 0x202020u, "output switch changed the GPU frame");
                require(renderer.present(window, reflexBase.data(), 64, 64), "switched output could not present");
                if (!composition) {
                    const HWND output = GpuTestAccess::outputHwnd(renderer);
                    renderer.reset();
                    require(!IsWindow(output), "native game surface survived renderer reset");
                }
            }
            auto& reflex = renderer.lowLatency();
            if (reflex.status(latency::Mode::Reflex) == latency::Status::Ready) {
                ShowWindow(window, SW_SHOWNOACTIVATE);
                for (const bool composition : {true, false}) {
                renderer.setDirectComposition(composition);
                require(renderer.prepare(window, 64, 64), "Reflex output switch failed");
                require(reflex.request(latency::Mode::Reflex, 4167), "supported NVIDIA device rejected Reflex ON");
                for (int i=0; i<60; ++i) {
                    MSG message{}; while (PeekMessageW(&message, window, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
                    require(reflex.beginFrame(), "real Reflex Sleep/markers failed");
                    reflex.renderStart();
                    require(renderer.present(window, reflexBase.data(), 64, 64), "Reflex presentation failed");
                    reflex.endFrame();
                }
                require(reflex.active()==latency::Mode::Reflex, "real SDK disabled itself");
                require(reflex.enabledInDriver(), "driver did not confirm Reflex low latency mode after frames");
                require(reflex.request(latency::Mode::Off) && reflex.active()==latency::Mode::Off,
                    "real Reflex OFF failed");
                }
                // Exercise actual resource destruction while ON, then resize/recreate.
                const auto previousFrame = reflex.frameId();
                require(reflex.request(latency::Mode::Reflex,667), "pre-reset Reflex ON failed");
                renderer.reset();
                require(reflex.active()==latency::Mode::Off &&
                    reflex.waitOwner()==latency::WaitOwner::ExistingFramePacer,
                    "device reset retained SDK pacing");
                std::vector<uint32_t> resized(96*96,0xff202020u);
                require(renderer.prepare(window,96,96), "post-reset resized device failed");
                require(reflex.request(latency::Mode::Reflex,4167), "recreated device rejected Reflex");
                for (int i=0;i<60;++i) {
                    require(reflex.beginFrame(), "recreated device lost Sleep/markers");
                    reflex.renderStart();
                    require(renderer.present(window,resized.data(),96,96), "recreated device failed to present");
                    reflex.endFrame();
                }
                require(reflex.frameId()>previousFrame && reflex.enabledInDriver(),
                    "recreated device lost marker sequence/driver state");
                require(reflex.request(latency::Mode::Off), "post-recovery OFF failed");
                std::cout << "Real NVIDIA Reflex: composition/native output, ON, Sleep, frame markers, Present and OFF passed.\n";
            } else std::cout << "NVIDIA Reflex unsupported or driver runtime absent; ordinary GPU fallback verified.\n";
        }
        renderer.reset();
        std::cout << "Hardware GPU: notes, fractional edges, cover, alpha HUD, resizing and presentation passed.\n";
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    if (window) DestroyWindow(window);
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}
