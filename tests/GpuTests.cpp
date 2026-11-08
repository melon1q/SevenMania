#include <windows.h>
#include "CompositionPresenter.h"
#include <iostream>
#include <stdexcept>
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
};
int main() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HWND window = CreateWindowW(L"STATIC", L"GPU test", WS_OVERLAPPEDWINDOW, 0, 0, 128, 128,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    int result = 0;
    try {
        require(window != nullptr, "GPU test window failed");
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
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    if (window) DestroyWindow(window);
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}
