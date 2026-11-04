#pragma once
#include "PerformanceTrace.h"
#include "HudFrameProbe.h"
#include "LowLatency.h"
#include "ReflexBackend.h"
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <d2d1.h>
#include <memory>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <array>
#include <chrono>

// Hardware composition: CPU backdrop, GPU note rectangles, premultiplied HUD.
class CompositionPresenter {
    friend struct GpuTestAccess;
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device_;
    Ptr<ID3D11DeviceContext> context_;
    Ptr<ID3D11Texture2D> frame_;
    Ptr<IDXGISwapChain1> swap_;
    Ptr<IDCompositionDevice> composition_;
    Ptr<IDCompositionTarget> target_;
    Ptr<IDCompositionVisual> visual_;
    Ptr<ID2D1Factory> drawingFactory_;
    Ptr<ID2D1RenderTarget> drawingTarget_;
    Ptr<ID2D1SolidColorBrush> brush_;
    Ptr<ID2D1Bitmap> overlay_;
    Ptr<ID2D1Bitmap> deck_;
    struct GpuTimingSlot {
        Ptr<ID3D11Query> disjoint, start, end;
        bool pending{};
#ifdef BMS_HUD_FRAME_PROBE
        uint64_t serial{};
#endif
    };
    std::array<GpuTimingSlot, 4> gpuTiming_{};
    size_t nextGpuTiming_{};
    double pendingGpuMilliseconds_{};
    int gpuUsagePercent_{};
    bool gpuTimingReady_{};
    std::chrono::steady_clock::time_point gpuUsageWindow_{};
    int deckWidth_{}, deckHeight_{};
    bool frameFailed_{}, presentationDeferred_{}, pendingFrame_{};
    struct Rectangle { D2D1_RECT_F bounds; COLORREF color{}; Ptr<ID2D1Bitmap> sprite; float opacity = 1; bool clipped{};D2D1_RECT_F clip{};bool mask{}; };
    std::vector<Rectangle> rectangles_;
    struct CachedSprite { uint64_t id{}, used{}; Ptr<ID2D1Bitmap> bitmap; };
    std::vector<CachedSprite> sprites_;
    uint64_t spriteClock_{};
    HANDLE presentationReady_{};
    bool hybridReadyPermit_{};
    bool vsync_{};
    bool antialias_ = true;
    bool directComposition_ = true;
    HWND outputWindow_{};
    using FrameOverlay = void (*)(ID2D1RenderTarget*,int,int);
    FrameOverlay frameOverlay_{}, frameForeground_{};
    static LRESULT CALLBACK outputProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCHITTEST) return HTTRANSPARENT;
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_PAINT) {
            PAINTSTRUCT paint{}; BeginPaint(window, &paint); EndPaint(window, &paint); return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    bool createOutputWindow(HWND parent, int width, int height) {
        WNDCLASSW type{}; type.lpfnWndProc = outputProc;
        type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"BmsGpuOutputSurface";
        if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        // Flip-model presentation permanently disables GDI on its HWND. Keep it
        // off the application HWND, which must paint the song browser with GDI.
        outputWindow_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, type.lpszClassName, L"",
            WS_CHILD | WS_VISIBLE | WS_DISABLED | WS_CLIPSIBLINGS, 0, 0, width, height,
            parent, nullptr, type.hInstance, nullptr);
        if (outputWindow_) SetWindowPos(outputWindow_, HWND_BOTTOM, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return outputWindow_ != nullptr;
    }
    latency::Controller lowLatency_;
    int overlayWidth_{}, overlayHeight_{};
    int width_{}, height_{};
    Ptr<ID2D1Bitmap> hybridBackdrop_, hybridLayer_;
    std::shared_ptr<const uint32_t> hybridLayerPixels_, uploadedLayerPixels_;
    std::shared_ptr<const uint32_t> hybridPixelOwner_, uploadedPixels_;
    const uint32_t* hybridPixels_{};
    uint64_t hybridGeneration_{}, uploadedHybridGeneration_{};
    bool hybridFrame_{};
    float hybridDarkness_{}, hybridLaneOpacity_{};
    RECT hybridLane_{};
    struct CpuRegion { RECT rect{}; Ptr<ID2D1Bitmap> bitmap; };
    std::vector<CpuRegion> hybridRegions_;
    uint64_t submittedFrames_{}, deferredFrames_{}, uploadedBytes_{};
    double totalGpuMilliseconds_{};
    uint64_t gpuSamples_{};
    std::chrono::steady_clock::time_point presentationWindow_{};
    uint64_t presentationWindowFrames_{};
    unsigned presentationFps_{};
public:
    // Generic UI overlay, prepared by the owner before drawing. Native notes/HUD stay above it.
    ID2D1RenderTarget* renderTarget() const noexcept {return drawingTarget_.Get();}
    void setFrameOverlay(FrameOverlay overlay) noexcept {frameOverlay_=overlay;}
    void setFrameForeground(FrameOverlay overlay) noexcept {frameForeground_=overlay;}
    CompositionPresenter() { lowLatency_.install(latency::Mode::Reflex, std::make_unique<latency::ReflexBackend>()); }
    ~CompositionPresenter() { reset(); }
    void setVsync(bool enabled) { vsync_ = enabled; }
    void setAntialias(bool enabled) { antialias_ = enabled; }
    void setDirectComposition(bool enabled) {
        if (directComposition_ == enabled) return;
        reset(); directComposition_ = enabled;
    }
    bool usesDirectComposition() const { return composition_ != nullptr; }
    const latency::Controller& lowLatency() const { return lowLatency_; }
    latency::Controller& lowLatency() { return lowLatency_; }
    void reset() {
        frameOverlay_=nullptr;frameForeground_=nullptr;
        lowLatency_.detach();
        hybridLayer_.Reset(); hybridLayerPixels_.reset(); uploadedLayerPixels_.reset();
        hybridBackdrop_.Reset(); hybridPixelOwner_.reset(); uploadedPixels_.reset(); hybridPixels_ = nullptr; hybridFrame_ = false;
        hybridRegions_.clear(); uploadedHybridGeneration_ = 0;
        hybridReadyPermit_ = false;
        presentationWindow_ = {}; presentationWindowFrames_ = 0; presentationFps_ = 0;
        if (target_) { target_->SetRoot(nullptr); composition_->Commit(); }
        if (presentationReady_) { CloseHandle(presentationReady_); presentationReady_ = nullptr; }
        rectangles_.clear(); sprites_.clear(); frame_.Reset();
        presentationDeferred_ = false; pendingFrame_ = false;
        overlay_.Reset(); deck_.Reset(); deckWidth_ = deckHeight_ = 0;
        for (auto& timing : gpuTiming_) { timing.disjoint.Reset(); timing.start.Reset(); timing.end.Reset(); timing.pending = false; }
        nextGpuTiming_ = 0; pendingGpuMilliseconds_ = 0; gpuUsagePercent_ = 0; gpuTimingReady_ = false;
        gpuUsageWindow_ = {};
        brush_.Reset(); drawingTarget_.Reset(); drawingFactory_.Reset();
        rectangles_.clear(); overlayWidth_ = overlayHeight_ = 0;
        visual_.Reset(); target_.Reset(); composition_.Reset(); swap_.Reset();
        if (context_) { context_->ClearState(); context_->Flush(); }
        context_.Reset(); device_.Reset(); width_ = height_ = 0;
        if (outputWindow_) { DestroyWindow(outputWindow_); outputWindow_ = nullptr; }
    }
    bool prepare(HWND window, int width, int height) {
        HUD_SCOPE(PresenterPrepare);
        if (width <= 0 || height <= 0) return false;
        if (!swap_ || width != width_ || height != height_) {
            reset();
            HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                &device_, nullptr, &context_);
            Ptr<IDXGIDevice> dxgi; Ptr<IDXGIAdapter> adapter; Ptr<IDXGIFactory2> factory;
            if (FAILED(hr) || FAILED(device_.As(&dxgi)) || FAILED(dxgi->GetAdapter(&adapter)) ||
                FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) { reset(); return false; }
            DXGI_SWAP_CHAIN_DESC1 desc{};
            desc.Width = width; desc.Height = height; desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 2; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
            if (directComposition_) {
                if (FAILED(factory->CreateSwapChainForComposition(device_.Get(), &desc, nullptr, &swap_)) ||
                    FAILED(DCompositionCreateDevice(dxgi.Get(), IID_PPV_ARGS(&composition_))) ||
                    FAILED(composition_->CreateTargetForHwnd(window, TRUE, &target_)) ||
                    FAILED(composition_->CreateVisual(&visual_)) || FAILED(visual_->SetContent(swap_.Get())) ||
                    FAILED(target_->SetRoot(visual_.Get())) || FAILED(composition_->Commit())) { reset(); return false; }
            } else {
                if (!createOutputWindow(window, width, height) ||
                    FAILED(factory->CreateSwapChainForHwnd(device_.Get(), outputWindow_, &desc, nullptr, nullptr, &swap_))) {
                    reset(); return false;
                }
            }
            factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
            width_ = width; height_ = height;
            DXGI_ADAPTER_DESC adapterInfo{};
            adapter->GetDesc(&adapterInfo);
            lowLatency_.attach({device_.Get(), adapterInfo.VendorId});
            Ptr<IDXGISwapChain2> latencySwap;
            if (FAILED(swap_.As(&latencySwap)) || FAILED(latencySwap->SetMaximumFrameLatency(1)) ||
                !(presentationReady_ = latencySwap->GetFrameLatencyWaitableObject())) { reset(); return false; }
            // Draw into our own texture, which the compositor never holds. Updating a
            // displayed backbuffer can otherwise stall even when Present uses interval 0.
            D3D11_TEXTURE2D_DESC texture{};
            texture.Width = width; texture.Height = height; texture.MipLevels = 1; texture.ArraySize = 1;
            texture.Format = desc.Format; texture.SampleDesc.Count = 1;
            texture.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            Ptr<IDXGISurface> surface;
            if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, drawingFactory_.GetAddressOf())) ||
                FAILED(device_->CreateTexture2D(&texture, nullptr, &frame_)) || FAILED(frame_.As(&surface)) ||
                FAILED(drawingFactory_->CreateDxgiSurfaceRenderTarget(surface.Get(),
                    D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96), &drawingTarget_)) ||
                FAILED(drawingTarget_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush_))) { reset(); return false; }
            HUD_COUNT(TargetChanges);
            D3D11_QUERY_DESC queryDesc{};
            queryDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
            bool timingCreated = true;
            for (auto& timing : gpuTiming_) {
                if (FAILED(device_->CreateQuery(&queryDesc, &timing.disjoint))) { timingCreated = false; break; }
                queryDesc.Query = D3D11_QUERY_TIMESTAMP;
                if (FAILED(device_->CreateQuery(&queryDesc, &timing.start)) ||
                    FAILED(device_->CreateQuery(&queryDesc, &timing.end))) { timingCreated = false; break; }
                queryDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
            }
            if (!timingCreated) {
                for (auto& timing : gpuTiming_) { timing.disjoint.Reset(); timing.start.Reset(); timing.end.Reset(); }
            }
            gpuUsageWindow_ = std::chrono::steady_clock::now();
        }
        rectangles_.clear();
        frameFailed_ = false;
        presentationDeferred_ = false; pendingFrame_ = false;
        hybridFrame_ = false;
        return true;
    }
    void hybridBackground(const uint32_t* pixels, uint64_t generation, int darkness,
        RECT lane, int laneOpacity, const std::vector<RECT>& regions, std::shared_ptr<const uint32_t> owner = {},
        std::shared_ptr<const uint32_t> layer = {}) {
        hybridLayerPixels_ = std::move(layer);
        hybridPixelOwner_ = std::move(owner);
        hybridFrame_ = true; hybridPixels_ = pixels; hybridGeneration_ = generation;
        hybridDarkness_ = std::clamp(darkness, 0, 100) / 100.0f;
        hybridLane_ = lane; hybridLaneOpacity_ = std::clamp(laneOpacity, 0, 100) / 100.0f;
        if (hybridRegions_.size() != regions.size()) hybridRegions_.resize(regions.size());
        for (size_t i = 0; i < regions.size(); ++i) {
            if (!EqualRect(&hybridRegions_[i].rect, &regions[i])) hybridRegions_[i].bitmap.Reset();
            hybridRegions_[i].rect = regions[i];
        }
    }
    uint64_t submittedFrames() const { return submittedFrames_; }
    uint64_t deferredFrames() const { return deferredFrames_; }
    uint64_t uploadedBytes() const { return uploadedBytes_; }
    double gpuMilliseconds() const { return totalGpuMilliseconds_; }
    uint64_t gpuSamples() const { return gpuSamples_; }
    // Read back once at the end of a song, never in the gameplay frame loop.
    bool captureLastFrame(std::vector<uint32_t>& pixels, int width, int height) {
        if (!frame_ || width != width_ || height != height_) return false;
        D3D11_TEXTURE2D_DESC desc{}; frame_->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
        Ptr<ID3D11Texture2D> staging;
        if (FAILED(device_->CreateTexture2D(&desc, nullptr, &staging))) return false;
        context_->CopyResource(staging.Get(), frame_.Get());
        // Allocate before Map so an allocation failure cannot leave a mapped resource.
        pixels.resize(static_cast<size_t>(width) * height);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
        for (int y = 0; y < height; ++y)
            std::copy_n(reinterpret_cast<const uint32_t*>(static_cast<const unsigned char*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch), width, pixels.data() + static_cast<size_t>(y) * width);
        context_->Unmap(staging.Get(), 0);
        return true;
    }
    unsigned presentationFps() const { return presentationFps_; }
    // MsgWait consumes an auto-reset event just like WaitForSingleObject.
    // Transfer that permit to present(), rather than waiting for it twice.
    HANDLE presentationWaitHandle() const {
        return presentationDeferred_ && !hybridReadyPermit_ ? presentationReady_ : nullptr;
    }
    bool acceptPresentationWake(HANDLE signaled) {
        if (!signaled || signaled != presentationWaitHandle()) return false;
        hybridReadyPermit_ = true;
        return true;
    }
    bool hasPendingFrame() const { return pendingFrame_; }
    void rectangle(float left, float top, float right, float bottom, COLORREF color, float opacity = 1.0f) {
        if (left < right && top < bottom) rectangles_.push_back({{left, top, right, bottom}, color, {}, std::clamp(opacity, 0.0f, 1.0f)});
    }
    bool sprite(const uint32_t* pixels, int width, int height, int left, int top, int drawWidth, int drawHeight,
        uint64_t id, float opacity = 1, bool premultiplied = true, const RECT* clip = nullptr) {
        if (!drawingTarget_ || !pixels || width <= 0 || height <= 0) return false;
        auto found = std::find_if(sprites_.begin(), sprites_.end(), [&](const auto& entry) { return entry.id == id; });
        if (found == sprites_.end()) {
            std::vector<uint32_t> converted;
            if (!premultiplied) {
                converted.resize(static_cast<size_t>(width) * height);
                for (size_t i = 0; i < converted.size(); ++i) {
                    const uint32_t p = pixels[i]; const unsigned a = p >> 24;
                    const unsigned r = (((p >> 16) & 255) * a + 127) / 255;
                    const unsigned g = (((p >> 8) & 255) * a + 127) / 255;
                    const unsigned b = ((p & 255) * a + 127) / 255;
                    converted[i] = (a << 24) | (r << 16) | (g << 8) | b;
                }
                pixels = converted.data();
            }
            CachedSprite entry; entry.id = id;
            if (FAILED(drawingTarget_->CreateBitmap(D2D1::SizeU(width, height), pixels, width * 4,
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &entry.bitmap))) { frameFailed_ = true; return false; }
            if (sprites_.size() >= 32) sprites_.erase(std::min_element(sprites_.begin(), sprites_.end(),
                [](const auto& a, const auto& b) { return a.used < b.used; }));
            sprites_.push_back(std::move(entry)); found = std::prev(sprites_.end());
        }
        found->used = ++spriteClock_;
        rectangles_.push_back({D2D1::RectF(static_cast<float>(left), static_cast<float>(top),
            static_cast<float>(left + drawWidth), static_cast<float>(top + drawHeight)), 0, found->bitmap, opacity});
        if(clip && !rectangles_.empty()){auto& rectangle=rectangles_.back();rectangle.clipped=true;rectangle.clip=D2D1::RectF(static_cast<float>(clip->left),static_cast<float>(clip->top),static_cast<float>(clip->right),static_cast<float>(clip->bottom));}
        return true;
    }
    void deck(const uint32_t* pixels, int width, int height, int stride, int left, int top) {
        if (width <= 0 || height <= 0) return;
        HRESULT uploaded;
        if (!deck_ || width != deckWidth_ || height != deckHeight_) {
            deck_.Reset();
            uploaded = drawingTarget_->CreateBitmap(D2D1::SizeU(width, height), pixels, stride * 4,
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)), &deck_);
            if (SUCCEEDED(uploaded)) { deckWidth_ = width; deckHeight_ = height; }
        } else uploaded = deck_->CopyFromMemory(nullptr, pixels, stride * 4);
        if (FAILED(uploaded)) { frameFailed_ = true; return; }
        rectangles_.push_back({D2D1::RectF(static_cast<float>(left), static_cast<float>(top),
            static_cast<float>(left + width), static_cast<float>(top + height)), 0, deck_, 1});
    }
    bool maskSprite(const uint32_t* pixels,int w,int h,int x,int y,int dw,int dh,uint64_t id,COLORREF color) {
        if(!sprite(pixels,w,h,x,y,dw,dh,id))return false;
        auto& command=rectangles_.back();command.mask=true;command.color=color;return true;
    }
    // Reserve the auto-reset readiness signal before CPU painting, then consume
    // that reservation in present(). Never wait twice for the same frame.
    bool canBeginHybridFrame() {
        if (hybridReadyPermit_) return true;
        const DWORD ready = WaitForSingleObject(presentationReady_, 0);
        if (ready == WAIT_OBJECT_0) hybridReadyPermit_ = true;
        if (ready == WAIT_TIMEOUT) { presentationDeferred_ = true; ++deferredFrames_; }
        return ready != WAIT_TIMEOUT; // present() handles invalid/device-lost state.
    }
    bool present(HWND window, const void* pixels, int width, int height,
        const uint32_t* foreground = nullptr, int fw = 0, int fh = 0, int foregroundX = 0) {
        HUD_SCOPE(PresenterTotal);
        if (!pixels || width <= 0 || height <= 0) return false;
        if ((!swap_ || width != width_ || height != height_) && !prepare(window, width, height)) return false;
        if (frameFailed_) { reset(); return false; }
        presentationDeferred_ = false;
        pendingFrame_ = true;
        collectGpuTiming();
        // Check before uploading/drawing in both rendering modes. Retain only
        // the latest CPU frame while the output queue is occupied.
        const DWORD ready = hybridReadyPermit_ ? WAIT_OBJECT_0 : WaitForSingleObject(presentationReady_, 0);
        if (ready == WAIT_TIMEOUT) { HUD_COUNT(QueueDeferred);presentationDeferred_ = true; ++deferredFrames_; return true; }
        if (ready != WAIT_OBJECT_0) { reset(); return false; }
        hybridReadyPermit_ = true;
        GpuTimingSlot* timing = beginGpuTiming();
        const bool drawn = drawFrame(pixels, width, foreground, fw, fh, foregroundX);
        finishGpuTiming(timing);
        if (!drawn) { reset(); return false; }
        // A full presentation queue is normal, not a lost device. Keep drawing the
        // newest frame and submit only when there is room; never wait for a refresh.
        BMS_PERF_SCOPE(gpuQueueMs);
        Ptr<ID3D11Texture2D> buffer;
        {HUD_SCOPE(SwapBufferGet);if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&buffer)))) { reset(); return false; }}
        {HUD_SCOPE(Copy);context_->CopyResource(buffer.Get(), frame_.Get());}
        lowLatency_.renderEnd();
        lowLatency_.presentStart();
        // Synchronization is opt-in; numeric/unlimited limits submit the newest
        // frame without requesting an additional vertical-blank interval.
        HUD_COUNT(PresentCalls);
        HRESULT shown;{HUD_SCOPE(Present);shown = swap_->Present(vsync_ ? 1 : 0, DXGI_PRESENT_DO_NOT_WAIT);}
        lowLatency_.presentEnd();
        return finishPresentation(shown);
    }
    int gpuUtilizationPercent() const { return gpuTimingReady_ ? gpuUsagePercent_ : -1; }
    bool presentationDeferred() const { return presentationDeferred_; }
private:
    bool finishPresentation(HRESULT shown) {
        presentationDeferred_ = false;
        if (shown == DXGI_ERROR_WAS_STILL_DRAWING) { HUD_COUNT(QueueDeferred);presentationDeferred_ = true; ++deferredFrames_; }
        else if (SUCCEEDED(shown)) {
            hybridReadyPermit_ = false; pendingFrame_ = false;
        }
        if (shown == S_OK) {
#ifdef BMS_HUD_FRAME_PROBE
            hudprobe::recorder.submit();
#endif
            BMS_PERF_EVENT(4,0,0); // Accepted submission, not physical scanout.
            ++submittedFrames_;
            const auto now = std::chrono::steady_clock::now();
            if (presentationWindow_ == std::chrono::steady_clock::time_point{}) presentationWindow_ = now;
            ++presentationWindowFrames_;
            const double elapsed = std::chrono::duration<double>(now - presentationWindow_).count();
            if (elapsed >= 1) { presentationFps_ = static_cast<unsigned>(presentationWindowFrames_ / elapsed + 0.5); presentationWindowFrames_ = 0; presentationWindow_ = now; }
        }
        if (shown != DXGI_ERROR_WAS_STILL_DRAWING && FAILED(shown)) { reset(); return false; }
        return true;
    }
    bool uploadHybrid(const uint32_t* pixels, int width) {
        HUD_SCOPE(UploadHybrid);
        BMS_PERF_SCOPE(uploadMs);
        if (!hybridPixels_) return false;
        {HUD_SCOPE(UploadBackdrop);
        if (!hybridBackdrop_) {
            HUD_SCOPE(BackdropCreate);
            if (FAILED(drawingTarget_->CreateBitmap(D2D1::SizeU(width_, height_), hybridPixels_, width_ * 4,
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)), &hybridBackdrop_))) return false;
            {HUD_SCOPE(BackdropRetain);uploadedPixels_ = hybridPixelOwner_;}
            uploadedHybridGeneration_ = hybridGeneration_; uploadedBytes_ += uint64_t(width_) * height_ * 4;
        } else if (uploadedHybridGeneration_ != hybridGeneration_) {
            int top = 0, bottom = height_;
            // Compare with the last successful upload, even when presentation skipped generations.
            if (uploadedPixels_ && hybridPixelOwner_) {
                HUD_SCOPE(BackdropCompare);
                const auto sameRow = [&](int y) {
                    HUD_COUNT(BackdropComparedRows);
                    const size_t offset = static_cast<size_t>(y) * width_;
                    return std::memcmp(uploadedPixels_.get() + offset, hybridPixels_ + offset, width_ * 4) == 0;
                };
                while (top < bottom && sameRow(top)) ++top;
                while (bottom > top && sameRow(bottom - 1)) --bottom;
            }
            if (top < bottom) {
                const auto rect = D2D1::RectU(0, top, width_, bottom);
                HUD_SCOPE(BackdropCopy);HUD_COUNT(BackdropCopyCalls);
                if (FAILED(hybridBackdrop_->CopyFromMemory(&rect, hybridPixels_ + static_cast<size_t>(top) * width_, width_ * 4))) return false;
                uploadedBytes_ += uint64_t(width_) * (bottom - top) * 4;
#ifdef BMS_HUD_FRAME_PROBE
                hudprobe::recorder.count(hudprobe::BackdropUploadBytes,unsigned(width_*(bottom-top)*4));
#endif
            }
            BMS_PERF_SET(backdropSavedBytes, uint64_t(width_) * (height_ - bottom + top) * 4);
            {HUD_SCOPE(BackdropRetain);uploadedPixels_ = hybridPixelOwner_;}
            uploadedHybridGeneration_ = hybridGeneration_;
        }
        }
        {HUD_SCOPE(UploadOverlay);
        if (hybridLayerPixels_ != uploadedLayerPixels_) {
            if (!hybridLayerPixels_) hybridLayer_.Reset();
            else if (!hybridLayer_) {
                if (FAILED(drawingTarget_->CreateBitmap(D2D1::SizeU(width_,height_),hybridLayerPixels_.get(),width_*4,
                    D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)), &hybridLayer_))) return false;
            } else if (FAILED(hybridLayer_->CopyFromMemory(nullptr,hybridLayerPixels_.get(),width_*4))) return false;
            if (hybridLayerPixels_) uploadedBytes_ += uint64_t(width_)*height_*4;
            uploadedLayerPixels_=hybridLayerPixels_;
        }
        }
#ifdef BMS_HUD_FRAME_PROBE
        unsigned probeRegion=0;
#endif
        for (auto& region : hybridRegions_) {
            const auto& r = region.rect;
            const int rw = r.right - r.left, rh = r.bottom - r.top;
            const auto* source = pixels + static_cast<size_t>(r.top) * width + r.left;
            HUD_REGION(probeRegion,unsigned(rw*rh*4));
            HRESULT result;
            if (!region.bitmap) result = drawingTarget_->CreateBitmap(D2D1::SizeU(rw, rh), source, width * 4,
                D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)), &region.bitmap);
            else result = region.bitmap->CopyFromMemory(nullptr, source, width * 4);
            if (FAILED(result)) return false;
            uploadedBytes_ += uint64_t(rw) * rh * 4;
        }
        return true;
    }
    GpuTimingSlot* beginGpuTiming() {
        if (!device_ || !context_) return nullptr;
        GpuTimingSlot& slot = gpuTiming_[nextGpuTiming_++ % gpuTiming_.size()];
        if (!slot.disjoint || !slot.start || !slot.end || slot.pending) return nullptr;
#ifdef BMS_HUD_FRAME_PROBE
        slot.serial=hudprobe::recorder.active?hudprobe::recorder.row.serial:0;
#endif
        context_->Begin(slot.disjoint.Get());
        context_->End(slot.start.Get());
        if (gpuUsageWindow_.time_since_epoch().count() == 0) gpuUsageWindow_ = std::chrono::steady_clock::now();
        return &slot;
    }
    void finishGpuTiming(GpuTimingSlot* slot) {
        if (!slot) return;
        context_->End(slot->end.Get());
        context_->End(slot->disjoint.Get());
        slot->pending = true;
    }
    void collectGpuTiming() {
        HUD_SCOPE(GpuQueryCollect);
        if (!context_) return;
        for (auto& slot : gpuTiming_) {
            if (!slot.pending) continue;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
            uint64_t start = 0, end = 0;
            const HRESULT ready = context_->GetData(slot.disjoint.Get(), &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (ready == S_FALSE) continue;
#ifdef BMS_HUD_FRAME_PROBE
            if(FAILED(ready)||disjoint.Disjoint||!disjoint.Frequency){slot.pending=false;continue;}
            const auto sr=context_->GetData(slot.start.Get(),&start,sizeof(start),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            const auto er=context_->GetData(slot.end.Get(),&end,sizeof(end),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(sr==S_FALSE||er==S_FALSE)continue;
            slot.pending=false;if(sr!=S_OK||er!=S_OK||end<start)continue;
#else
            slot.pending = false;
            if (FAILED(ready) || disjoint.Disjoint || disjoint.Frequency == 0 ||
                FAILED(context_->GetData(slot.start.Get(), &start, sizeof(start), D3D11_ASYNC_GETDATA_DONOTFLUSH)) ||
                FAILED(context_->GetData(slot.end.Get(), &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH)) || end < start) continue;
#endif
            const double elapsed = static_cast<double>(end - start) * 1000.0 / static_cast<double>(disjoint.Frequency);
#ifdef BMS_HUD_FRAME_PROBE
            hudprobe::recorder.gpuSample(slot.serial,elapsed);
#endif
            pendingGpuMilliseconds_ += elapsed; totalGpuMilliseconds_ += elapsed;
            ++gpuSamples_;
            gpuTimingReady_ = true;
        }
        if (!gpuTimingReady_) return;
        const auto now = std::chrono::steady_clock::now();
        if (gpuUsageWindow_.time_since_epoch().count() == 0) gpuUsageWindow_ = now;
        const double elapsed = std::chrono::duration<double, std::milli>(now - gpuUsageWindow_).count();
        if (elapsed >= 500.0) {
            gpuUsagePercent_ = std::clamp(static_cast<int>(std::lround(pendingGpuMilliseconds_ * 100.0 / elapsed)), 0, 100);
            pendingGpuMilliseconds_ = 0;
            gpuUsageWindow_ = now;
        }
    }
    bool drawFrame(const void* pixels, int width, const uint32_t* foreground, int fw, int fh, int foregroundX) {
        if (hybridFrame_) { if (!uploadHybrid(static_cast<const uint32_t*>(pixels), width)) return false; }
        else { BMS_PERF_SCOPE(uploadMs);HUD_SCOPE(UploadFrame); context_->UpdateSubresource(frame_.Get(), 0, nullptr, pixels, width * 4, 0); uploadedBytes_ += uint64_t(width) * height_ * 4; }
        BMS_PERF_SCOPE(gpuDrawMs);HUD_SCOPE(CompositionDraw);
#ifdef BMS_HUD_FRAME_PROBE
        hudprobe::recorder.lastBitmap=nullptr;
#endif
        {HUD_SCOPE(D2DBegin);drawingTarget_->BeginDraw();}
        drawingTarget_->SetAntialiasMode(antialias_ ? D2D1_ANTIALIAS_MODE_PER_PRIMITIVE : D2D1_ANTIALIAS_MODE_ALIASED);
        if (hybridFrame_) {
            drawingTarget_->DrawBitmap(hybridBackdrop_.Get());
            if (hybridLayer_) drawingTarget_->DrawBitmap(hybridLayer_.Get());
            brush_->SetColor(D2D1::ColorF(0, 0, 0)); brush_->SetOpacity(hybridDarkness_);
            drawingTarget_->FillRectangle(D2D1::RectF(0, 0, static_cast<float>(width_), static_cast<float>(height_)), brush_.Get());
            brush_->SetOpacity(hybridLaneOpacity_);
            drawingTarget_->FillRectangle(D2D1::RectF(static_cast<float>(hybridLane_.left), static_cast<float>(hybridLane_.top),
                static_cast<float>(hybridLane_.right), static_cast<float>(hybridLane_.bottom)), brush_.Get());
            brush_->SetOpacity(1);
            for (const auto& region : hybridRegions_) drawingTarget_->DrawBitmap(region.bitmap.Get(),
                D2D1::RectF(static_cast<float>(region.rect.left), static_cast<float>(region.rect.top),
                    static_cast<float>(region.rect.right), static_cast<float>(region.rect.bottom)), 1, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        }
        if(frameOverlay_)frameOverlay_(drawingTarget_.Get(),width_,height_);
        for (const auto& rect : rectangles_) {
            if(rect.clipped)drawingTarget_->PushAxisAlignedClip(rect.clip,D2D1_ANTIALIAS_MODE_ALIASED);
            if (rect.sprite) {
                if(rect.mask) {
                    brush_->SetColor(D2D1::ColorF(GetRValue(rect.color)/255.f,GetGValue(rect.color)/255.f,GetBValue(rect.color)/255.f));brush_->SetOpacity(rect.opacity);
                    const auto aa=drawingTarget_->GetAntialiasMode();drawingTarget_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
                    drawingTarget_->FillOpacityMask(rect.sprite.Get(),brush_.Get(),D2D1_OPACITY_MASK_CONTENT_GRAPHICS,&rect.bounds,nullptr);
                    drawingTarget_->SetAntialiasMode(aa);brush_->SetOpacity(1);
                }else drawingTarget_->DrawBitmap(rect.sprite.Get(), rect.bounds, rect.opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                if(rect.clipped)drawingTarget_->PopAxisAlignedClip();
                continue;
            }
            brush_->SetColor(D2D1::ColorF(GetRValue(rect.color) / 255.0f,
                GetGValue(rect.color) / 255.0f, GetBValue(rect.color) / 255.0f));
            brush_->SetOpacity(rect.opacity);
            drawingTarget_->FillRectangle(rect.bounds, brush_.Get());
            brush_->SetOpacity(1.0f);
            if(rect.clipped)drawingTarget_->PopAxisAlignedClip();
        }
        if(frameForeground_)frameForeground_(drawingTarget_.Get(),width_,height_);
        HRESULT uploaded = S_OK;
        if (foreground && fw > 0 && fh > 0) {
            if (!overlay_ || fw != overlayWidth_ || fh != overlayHeight_) {
                overlay_.Reset();
                uploaded = drawingTarget_->CreateBitmap(D2D1::SizeU(fw, fh), foreground, fw * 4,
                    D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &overlay_);
                if (SUCCEEDED(uploaded)) { overlayWidth_ = fw; overlayHeight_ = fh; }
            } else uploaded = overlay_->CopyFromMemory(nullptr, foreground, fw * 4);
            if (SUCCEEDED(uploaded)) drawingTarget_->DrawBitmap(overlay_.Get(),
                D2D1::RectF(static_cast<float>(foregroundX), 0, static_cast<float>(foregroundX + fw), static_cast<float>(fh)),
                1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        }
        HRESULT drawn;{HUD_SCOPE(D2DEnd);drawn = drawingTarget_->EndDraw();}
        return SUCCEEDED(uploaded) && SUCCEEDED(drawn);
    }
};
