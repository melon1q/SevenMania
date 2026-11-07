#include "../src/Main.cpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static void verifyTextMarquee() {
    const uint32_t mask[]{0u,0x80ffffffu,0xffffffffu};
    uint32_t colored[]{0xff0000ffu,0xff0000ffu,0xff0000ffu};
    blendTextMask(mask,3,1,colored,3,1,0,0,3,1,RGB(255,0,0));
    require(colored[0]==0xff0000ffu&&colored[1]==0xff80007fu&&colored[2]==0xffff0000u,"CPU glyph mask alpha/tint changed");
    uint32_t clipped[]{0xff0000ffu,0xff0000ffu};
    blendTextMask(mask+2,1,1,clipped,2,1,-1,0,2,1,RGB(0,255,0));
    require(clipped[0]==0xff00ff00u&&clipped[1]==0xff0000ffu,"CPU glyph mask clipping changed");

    using bms::ui::textMarqueeOffset;
    require(textMarqueeOffset(0, 10) == 0 && textMarqueeOffset(-10, 10) == 0, "fitting text must not move");
    require(textMarqueeOffset(96, 1.0) == 0, "marquee initial hold missing");
    require(textMarqueeOffset(96, 2.2) == 48, "marquee speed changed");
    require(textMarqueeOffset(96, 3.5) == 96 && textMarqueeOffset(96, 4.5) == 96, "marquee end hold missing");
    require(textMarqueeOffset(96, 4.8) == 0, "marquee loop did not restart");
}
struct GpuTestAccess {
    static void verifyOutputWake() {
        auto& renderer = gCompositionPresenter;
        const HANDLE real = renderer.presentationReady_;
        const bool playing = gPlaying, composition = gCompositionEnabled, unavailable = gGpuUnavailable, paused = gPaused;
        renderer.presentationReady_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        renderer.hybridReadyPermit_ = false;
        gPlaying = gCompositionEnabled = true; gGpuUnavailable = gPaused = false; gOutputWakeReady = false;
        const auto bytes = renderer.uploadedBytes();
        require(renderer.present(gWindow, gBackBufferPixels, gBackBufferWidth, gBackBufferHeight) && renderer.hasPendingFrame(),
            "busy output did not retain the latest frame");
        require(renderer.uploadedBytes() == bytes, "busy output uploaded a discarded frame");
        const HANDLE event = pendingGpuOutputHandle();
        require(event != nullptr, "pending frame did not register an output wake");
        const HANDLE timer = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        SetEvent(timer);
        for (int attempt = 0; attempt < 8; ++attempt) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            if (waitForPlaybackWork(timer, 1000) == WAIT_OBJECT_0) break;
        }
        require(!gOutputWakeReady && !renderer.hybridReadyPermit_, "frame timer was mistaken for GPU readiness");
        SetEvent(event);
        // The message queue can wake the wait first; mirror the app's input pump.
        for (int attempt = 0; attempt < 8 && !gOutputWakeReady; ++attempt) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            waitForPlaybackWork(timer, 1000);
        }
        CloseHandle(timer);
        require(gOutputWakeReady && renderer.hybridReadyPermit_ && WaitForSingleObject(event, 0) == WAIT_TIMEOUT,
            "main wait lost or double-consumed the presentation event");
        const auto draws = gFpsFrameCount;
        const double playhead = gPlayhead;
        retryPendingGpuOutput();
        require(renderer.uploadedBytes() > bytes && gFpsFrameCount == draws && gPlayhead == playhead,
            "output retry reran CPU rendering or modified the gameplay clock");
        gGpuUnavailable = true;
        require(!pendingGpuOutputHandle(), "CPU fallback waited for a stale GPU event");
        CloseHandle(renderer.presentationReady_); renderer.presentationReady_ = real;
        renderer.hybridReadyPermit_ = renderer.presentationDeferred_ = renderer.pendingFrame_ = false;
        gPlaying = playing; gCompositionEnabled = composition; gGpuUnavailable = unavailable; gPaused = paused; gOutputWakeReady = false;
    }
    static void verifyRetainedGameplay() {
        if (!hybridRendererEnabled) return;
        auto& renderer = gCompositionPresenter;
        require(renderer.hybridFrame_, "gameplay did not enable retained backdrop");
        require(renderer.drawFrame(gBackBufferPixels, gWindowWidth, nullptr, 0, 0, 0), "initial retained gameplay draw failed");
        const auto before = renderer.uploadedBytes();
        const auto generation = renderer.uploadedHybridGeneration_;
        drawGameScreen(gBackBufferDc); GdiFlush();
        require(renderer.drawFrame(gBackBufferPixels, gWindowWidth, nullptr, 0, 0, 0), "repeated retained gameplay draw failed");
        uint64_t regionBytes = 0;
        for (const auto& region : renderer.hybridRegions_)
            regionBytes += uint64_t(region.rect.right-region.rect.left)*(region.rect.bottom-region.rect.top)*4;
        const auto uploaded = renderer.uploadedBytes()-before;
        const auto full = uint64_t(gWindowWidth)*gWindowHeight*4;
        require(renderer.uploadedHybridGeneration_ == generation, "unchanged gameplay rebuilt the backdrop");
        require(uploaded == regionBytes && uploaded < full, "unchanged gameplay uploaded a full screen");
        std::cout << "Retained gameplay upload: " << uploaded << " / " << full << " bytes per unchanged frame\n";
    }
    static size_t rectangleCount(const CompositionPresenter& renderer) { return renderer.rectangles_.size(); }
    static size_t spriteCount(const CompositionPresenter& renderer) {
        return std::count_if(renderer.rectangles_.begin(), renderer.rectangles_.end(), [](const auto& command) { return command.sprite != nullptr; });
    }
    static uint32_t pixel(CompositionPresenter& renderer, const void* pixels, int width, int x, int y) {
        require(renderer.drawFrame(pixels, width, nullptr, 0, 0, 0), "gameplay GPU render failed");
        D3D11_TEXTURE2D_DESC desc{}; renderer.frame_->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        require(SUCCEEDED(renderer.device_->CreateTexture2D(&desc, nullptr, &staging)), "gameplay readback allocation failed");
        renderer.context_->CopyResource(staging.Get(), renderer.frame_.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        require(SUCCEEDED(renderer.context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "gameplay readback failed");
        const auto* row = reinterpret_cast<const uint32_t*>(static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch);
        const uint32_t result = row[x];
        renderer.context_->Unmap(staging.Get(), 0);
        return result;
    }
};
int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 5) return 2;
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&gGdiPlusToken, &input, nullptr) != Gdiplus::Ok) return 1;
    HDC dc = CreateCompatibleDC(nullptr);
    int result = 0;
    try {
        verifyTextMarquee();
        const std::filesystem::path path = std::filesystem::path(argv[1]) / L"display-white.bmp";
        BITMAPFILEHEADER file{}; file.bfType = 0x4d42; file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER); file.bfSize = file.bfOffBits + 16;
        BITMAPINFOHEADER info{}; info.biSize = sizeof(info); info.biWidth = 2; info.biHeight = -2; info.biPlanes = 1; info.biBitCount = 32;
        const uint32_t white[4]{0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};
        { std::ofstream output(path, std::ios::binary); output.write(reinterpret_cast<const char*>(&file), sizeof(file)); output.write(reinterpret_cast<const char*>(&info), sizeof(info)); output.write(reinterpret_cast<const char*>(white), sizeof(white)); }
        bms::Chart chart; chart.bitmaps[L"01"] = path;
        gBgaActiveImages[0] = L"01";
        const RECT client{0, 0, 900, 500};
        const int fieldX = 100, laneWidth = 76;
        gBgaDarkness = 0; gLaneTransparency = 0; gBgaBackplateDirty = true;
        drawBgaBackplate(dc, chart, client, fieldX, laneWidth, 92, 360); GdiFlush();
        const auto* pixels = static_cast<const uint32_t*>(gBgaBackplatePixels);
        const LaneLayout backplateLayout{ fieldX, laneWidth, laneWidth };
        for (int y : {0, 91, 360, 499}) for (int x = fieldX; x < fieldX + backplateLayout.totalWidth(); ++x)
            require(pixels[y * 900 + x] == 0xff000000u, "lane gaps, nonblack lanes or incomplete height");
        require(pixels[0] == 0xffffffffu, "BGA darkness zero is not off");
        gLaneTransparency = 100; gBgaDarkness = 100;
        drawBgaBackplate(dc, chart, client, fieldX, laneWidth, 92, 360); GdiFlush();
        require(pixels[0] == 0xff000000u && pixels[499 * 900 + 899] == 0xff000000u, "BGA darkness maximum");
        gBgaDarkness = 50;
        drawBgaBackplate(dc, chart, client, fieldX, laneWidth, 92, 360); GdiFlush();
        const unsigned gray = pixels[0] & 255;
        require(gray >= 127 && gray <= 129 && pixels[100 * 900 + fieldX] == pixels[0], "midpoint darkness or transparent lane");
        if (argc == 3 && std::filesystem::path(argv[2]).extension() != L".png") {
            require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) && SUCCEEDED(MFStartup(MF_VERSION)), "video test startup");
            require(gVideoBga.open(std::filesystem::path(argv[2])), "actual BGA video open");
            for (int frame = 0; frame < 300; ++frame) gVideoBga.update(frame / 30.0);
            require(gVideoBga.ready(), "actual BGA video has no frame");
            gBgaActiveImages.fill(L""); gBgaDarkness = 0;
            drawBgaBackplate(dc, chart, client, fieldX, laneWidth, 92, 360); GdiFlush();
            uint64_t light = 0;
            for (int y = 0; y < 500; y += 5) for (int x = 0; x < 900; x += 5) light += (pixels[y * 900 + x] & 255) + ((pixels[y * 900 + x] >> 8) & 255) + ((pixels[y * 900 + x] >> 16) & 255);
            require(light > 0, "actual BGA frame unexpectedly black");
            gBgaDarkness = 100;
            drawBgaBackplate(dc, chart, client, fieldX, laneWidth, 92, 360); GdiFlush();
            for (int i = 0; i < 900 * 500; ++i) require((pixels[i] & 0xffffffu) == 0, "actual MP4 darkness not applied");
            std::cout << "Actual MP4 BGA darkness: 0 visible, 100 black; automatic cache refresh passed.\n";
            gVideoBga.reset(); MFShutdown(); CoUninitialize();
        }
        require(reservedBinding(VK_F1) && reservedBinding(VK_F2) && reservedBinding(VK_PRIOR) && reservedBinding(VK_NEXT), "shortcut conflicts");
        gJudgeLineOffset = 0;
        require(judgementLineY(600) == 400 && judgementLineY(900) == 600 && judgementLineY(1080) == 720, "default judgement line ratio");
        gJudgeLineOffset = -40;
        require(judgementLineY(900) == 560, "custom judgement offset");
        gJudgeLineOffset = 0;
        require(!keyGuideVisible(-0.01) && keyGuideVisible(0.0) && keyGuideVisible(1.999) && !keyGuideVisible(2.0),
            "key guide should be visible only during the first two seconds");
        for (const auto [width, height] : {std::pair{960, 540}, std::pair{1280, 720}, std::pair{1920, 1080}}) {
            const auto pause = pauseMenuLayout(width, height);
            const int expectedWidth = std::min(std::clamp(width * 34 / 100, 280, 560), width - 48);
            require(pause.buttons[0].left == (width - expectedWidth) / 2 &&
                pause.buttons[0].right - pause.buttons[0].left == expectedWidth,
                "pause buttons are not centered at the reduced width");
            require(pause.buttons[0].top > pause.guardLine.bottom &&
                pause.buttons[3].bottom <= pause.shortcuts.top,
                "pause header, buttons and shortcut hint overlap");
            for (const auto& button : pause.buttons)
                require(button.bottom - button.top <= 144, "pause button height exceeds the reduced size");
        }
        gWindow = CreateWindowW(L"STATIC", L"Note origin fixture", WS_OVERLAPPEDWINDOW, 0, 0, 1000, 800, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        RECT gameClient{}; GetClientRect(gWindow, &gameClient);
        gWindowWidth = gameClient.right; gWindowHeight = gameClient.bottom;
        ensureBackBuffer(dc, gWindowWidth, gWindowHeight);
        {
            const auto oldMetadata = gBrowseMetadataFont, oldRank = gBrowseRankFont;
            gBrowseMetadataFont = CreateFontW(-28, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            gBrowseRankFont = CreateFontW(-64, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            auto* canvas = static_cast<uint32_t*>(gBackBufferPixels);
            const size_t count = static_cast<size_t>(gWindowWidth)*gWindowHeight;
            std::fill_n(canvas, count, 0xff404040u);
            const BrowsePanelLayout panel(gWindowWidth,gWindowHeight);
            drawBrowsePlayRecord(gBackBufferDc,nullptr,panel); GdiFlush();
            require(std::all_of(canvas,canvas+count,[](uint32_t p){ return p==0xff404040u; }),
                "unplayed chart still painted record labels");
            PlayRecord record; record.played=true; record.maxCombo=1234; record.bestAccuracyScore=950000;
            drawBrowsePlayRecord(gBackBufferDc,&record,panel); GdiFlush();
            int rankTop=gWindowHeight, rankBottom=0;
            for(int y=0;y<gWindowHeight;++y) for(int x=24+panel.width*2/3;x<24+panel.width;++x) {
                const auto pixel=canvas[y*gWindowWidth+x];
                if ((pixel&0xffffffu)!=0x404040u) { rankTop=std::min(rankTop,y); rankBottom=std::max(rankBottom,y); }
            }
            require(rankBottom-rankTop>40, "rank grade was not enlarged");
            DeleteObject(gBrowseMetadataFont); DeleteObject(gBrowseRankFont);
            gBrowseMetadataFont=oldMetadata; gBrowseRankFont=oldRank;
        }
        {
            EmbeddedHudFont embedded; require(embedded.load(), "embedded Michroma resource did not load");
            HFONT bank = CreateFontW(-64,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Michroma");
            const auto old = SelectObject(dc,bank); wchar_t face[LF_FACESIZE]{}; GetTextFaceW(dc,LF_FACESIZE,face); SelectObject(dc,old);
            require(std::wstring(face)==L"Michroma", "HUD font silently fell back to a system font");
            for(int weight:{FW_NORMAL,FW_BOLD}){const auto ox=CreateFontW(-48,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Oxanium");const auto before=SelectObject(dc,ox);wchar_t actual[LF_FACESIZE]{};GetTextFaceW(dc,LF_FACESIZE,actual);TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SelectObject(dc,before);DeleteObject(ox);require(std::wstring(actual).starts_with(L"Oxanium"),"Bundled Oxanium fell back");require(weight!=FW_BOLD||metrics.tmWeight>=FW_BOLD,"Oxanium Bold weight missing");}

            constexpr int w=440,h=150; const RECT bounds{0,0,w,h};
            std::vector<uint32_t> background(w*h,0xff404040u), rainbow=background;
            TextRasterCache cache;
            int renders=0;
            auto renderer=[&](HDC target,const std::wstring& text,RECT rect,HFONT font,COLORREF) {
                ++renders; drawHudTextBody(target,text,rect,font,RGB(255,214,20),0);
            };
            require(cache.draw(rainbow.data(),w,h,L"GREAT",bounds,bank,RGB(0,1,2),1.0,renderer,false), "HUD raster failed");
            const int initialRenders=renders;
            auto repeat=background; cache.draw(repeat.data(),w,h,L"GREAT",bounds,bank,RGB(0,1,2),1.0,renderer,false);
            require(repeat==rainbow && renders==initialRenders, "unchanged HUD was rasterized again");
            auto fitted=background;
            require(cache.draw(fitted.data(),w,h,L"GREAT 999999",{90,0,350,h},bank,RGB(0,1,2),1.2,renderer,false,{},false,true), "combined HUD width fitting failed");
            bool fittedText=false;
            for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
                if (fitted[y*w+x]!=background[y*w+x]) {
                    fittedText=true; require(x>=90 && x<350,"combined HUD spilled beyond the gear width");
                }
            }
            require(fittedText,"combined HUD disappeared while fitting the gear");
            size_t black=rainbow.size(),white=rainbow.size(),colored=rainbow.size();
            for(size_t i=0;i<rainbow.size();++i) {
                const auto rgb=rainbow[i]&0xffffffu;
                if(rgb==0) black=i;
                if(rgb==0xffffffu) white=i;
                if(((rgb>>16)&255)>200 && (rgb&255)<80) colored=i;
            }
            require(black<rainbow.size() && white<rainbow.size() && colored<rainbow.size(), "HUD lost black outer/white inner/colored body layers");
            auto changed=background;
            TextRasterCache next;
            next.draw(changed.data(),w,h,L"GREAT",bounds,bank,RGB(1,1,2),1.0,
                [](HDC target,const std::wstring& text,RECT rect,HFONT font,COLORREF) {drawHudTextBody(target,text,rect,font,0,1);},false);
            require(combinedJudgementText(L"GREAT",10)==L"GREAT 10" && combinedJudgementText(L"POOR",0)==L"POOR 0" && combinedJudgementText(L"",10).empty(), "combined judgement/combos formatted incorrectly");
            bool red=false,green=false,blue=false;
            for(const auto pixel:rainbow){const int r=(pixel>>16)&255,g=(pixel>>8)&255,b=pixel&255;
                red|=r>200&&g<100&&b<100;green|=g>200&&r<100&&b<100;blue|=b>200&&r<100&&g<100;}
            require(red&&green&&blue,"PGREAT spatial rainbow colors missing");
            require(changed==rainbow,"PGREAT changed colors with obsolete animation phase");
            gCompositionPresenter.reset(); require(gCompositionPresenter.prepare(gWindow,w,h), "HUD GPU setup failed");
            require(cache.draw(nullptr,w,h,L"GREAT",bounds,bank,RGB(0,1,2),1.0,renderer,false,queueGpuText), "HUD GPU sprite failed");
            for(const size_t i : {black,white,colored})
                require((GpuTestAccess::pixel(gCompositionPresenter,background.data(),w,static_cast<int>(i%w),static_cast<int>(i/w))&0xffffffu)==(rainbow[i]&0xffffffu),
                    "CPU and GPU HUD layers differ");
            gCompositionPresenter.reset();
            BITMAPFILEHEADER header{}; header.bfType=0x4d42; header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER); header.bfSize=header.bfOffBits+w*h*4;
            BITMAPINFOHEADER bitmap{}; bitmap.biSize=sizeof(bitmap); bitmap.biWidth=w; bitmap.biHeight=-h; bitmap.biPlanes=1; bitmap.biBitCount=32;
            std::ofstream preview(std::filesystem::path(argv[1])/L"michroma-hud-preview.bmp",std::ios::binary);
            preview.write(reinterpret_cast<const char*>(&header),sizeof(header)); preview.write(reinterpret_cast<const char*>(&bitmap),sizeof(bitmap));
            preview.write(reinterpret_cast<const char*>(rainbow.data()),rainbow.size()*sizeof(uint32_t));
            const auto savedGauge=gGrooveGauge; const auto savedScore=gResultScore;
            showTimingJudgement(TimingJudgement::PGreat); require(gJudgementText==L"GREAT" && gJudgementRainbow,"PGREAT label/style mismatch");
            showTimingJudgement(TimingJudgement::Great); require(gJudgementText==L"GREAT" && !gJudgementRainbow,"ordinary GREAT inherited rainbow");
            gGrooveGauge=savedGauge; gResultScore=savedScore;
            gJudgementText.clear(); gJudgementUntil={};
            cache.clear(); next.clear(); DeleteObject(bank);
        }
        {
            const auto oldScore = gResultScore;
            const auto oldGauge = gGrooveGauge;
            gFeedback.reset(); gFeedback.hit(1, 0);
            gResultScore.reset(1); gGrooveGauge.configure(1, GrooveGaugeMode::Normal);
            showTimingJudgement(TimingJudgement::PGreat, 1);
            const int score = gResultScore.value();
            const double gauge = gGrooveGauge.value();
            gJudgementUntil = std::chrono::steady_clock::now() - std::chrono::seconds(1);
            const auto expired = gJudgementUntil;
            showHoldCombo(0);
            require(gJudgementUntil == expired && gFeedback.combo() == 1, "empty hold tick changed HUD or combo");
            showHoldCombo(15);
            require(gFeedback.combo() == 16 && gJudgementText == L"GREAT" && gJudgementRainbow &&
                gJudgementUntil > std::chrono::steady_clock::now() &&
                combinedJudgementText(gJudgementText, gFeedback.combo()) == L"GREAT 16",
                "hold combo did not keep the combined judgement visible");
            require(gResultScore.value() == score && gGrooveGauge.value() == gauge,
                "hold body ticks changed accuracy or gauge");
            gFeedback.reset(); gJudgementText.clear(); gJudgementUntil = {};
            gResultScore = oldScore; gGrooveGauge = oldGauge;
        }
        for (const auto flags : {std::pair{true, true}, std::pair{false, true}, std::pair{true, false}}) {
            gNoteSubpixel = flags.first; gNoteAntialias = flags.second;
            auto* buffer = static_cast<uint32_t*>(gBackBufferPixels);
            std::fill_n(buffer, static_cast<size_t>(gWindowWidth) * gWindowHeight, 0u);
            fillGameRectSubpixel(gBackBufferDc, 20, 10.5, 40, 15.5, RGB(255, 255, 255), {0, 0, gWindowWidth, gWindowHeight});
            const unsigned edge = buffer[10 * gWindowWidth + 30] & 255;
            require(flags.first && flags.second ? edge >= 127 && edge <= 128 : edge == 0,
                "CPU note subpixel/antialias setting did not affect fractional edge pixels");
        }
        gNoteSubpixel = gNoteAntialias = true;
        bms::Chart noteChart; noteChart.initialBpm = 120;
        noteChart.notes.push_back({3, judgementLineY(gWindowHeight) / 270.0, 0.0, L""});
        noteChart.notes.push_back({5, 0.5, 0.5, L""});
        gCharts.push_back(std::move(noteChart)); gActiveChartIndex = 0; gHit.assign(2, 0);
        gNoteSpeed = 1; gPlayhead = 0; gBgaActiveImages.fill(L""); gBgaBackplateDirty = true;
        drawGameScreen(gBackBufferDc); GdiFlush();
        const LaneLayout gameLayout{ (gWindowWidth - 556) / 2, 68, 104 };
        const int originX = gameLayout.left(3) + 20;
        require(gGrooveGauge.value() == 20.0 &&
            (static_cast<const uint32_t*>(gBackBufferPixels)[(judgementLineY(gWindowHeight) + 126) * gWindowWidth + gameLayout.left(0) + 6] & 0xffffffu) == 0xffffffu,
            "gauge must start full and render white");
        require(gameLayout.width(0) == 104 && gameLayout.width(1) == 68 && gameLayout.width(2) == 60 &&
            gameLayout.width(3) == 68 && gameLayout.width(4) == 60 && gameLayout.width(5) == 68 &&
            gameLayout.width(6) == 60 && gameLayout.width(7) == 68 && gameLayout.totalWidth() == 556 &&
            gameLayout.right(0) == gameLayout.left(1) && gameLayout.width(1) < 74,
            "narrow white-key geometry and lane alignment");
        const COLORREF noteColor = gPalette[gLaneColors[3]].color;
        const uint32_t expectedColor = 0xff000000u | (GetRValue(noteColor) << 16) | (GetGValue(noteColor) << 8) | GetBValue(noteColor);
        require((static_cast<uint32_t*>(gBackBufferPixels)[2 * gWindowWidth + originX] & 0x00ffffffu) == (expectedColor & 0x00ffffffu), "note not visible at screen top");
        require(gCompositionPresenter.prepare(gWindow, gWindowWidth, gWindowHeight), "gameplay GPU initialization failed");
        gGpuFrame = true;
        drawGameScreen(gBackBufferDc); GdiFlush();
        GpuTestAccess::verifyRetainedGameplay();
        if (hybridRendererEnabled)
            require((static_cast<uint32_t*>(gBackBufferPixels)[2 * gWindowWidth + originX] & 0xffffffu) == (expectedColor & 0xffffffu),
                "hybrid gameplay lost CPU-rendered notes");
        else require(GpuTestAccess::rectangleCount(gCompositionPresenter) >= 3 &&
            (static_cast<uint32_t*>(gBackBufferPixels)[2 * gWindowWidth + originX] & 0xffffffu) != (expectedColor & 0xffffffu),
            "gameplay still draws notes on CPU instead of GPU");
        require(GpuTestAccess::spriteCount(gCompositionPresenter) >= 8,
            "GPU foreground lost initial key guide");
        require(gCompositionPresenter.present(gWindow, gBackBufferPixels, gWindowWidth, gWindowHeight), "gameplay GPU output failed");
        GpuTestAccess::verifyOutputWake();
        const auto originalNotes = gCharts[0].notes;
        gCharts[0].notes = {{3, 3, 0, L""}, {5, 3, 2, L""}};
        for (int distance : {96, 64, 32, 16, 8, 4, 1, 0}) {
            gPlayhead = 3 - distance / 270.0;
            require(gCompositionPresenter.prepare(gWindow, gWindowWidth, gWindowHeight), "moving note GPU preparation failed");
            drawGameScreen(gBackBufferDc); GdiFlush();
            const int row = judgementLineY(gWindowHeight) - distance + 1;
            require((GpuTestAccess::pixel(gCompositionPresenter, gBackBufferPixels, gWindowWidth, originX, row) & 0xffffffu) ==
                (expectedColor & 0xffffffu), "unhit note disappeared before the judgement line");
            const COLORREF longColor = gPalette[gLaneColors[5]].color;
            const uint32_t expectedLong = (GetRValue(longColor) << 16) | (GetGValue(longColor) << 8) | GetBValue(longColor);
            require((GpuTestAccess::pixel(gCompositionPresenter, gBackBufferPixels, gWindowWidth, gameLayout.left(5) + 20, row) & 0xffffffu) ==
                expectedLong, "long-note head disappeared before the judgement line");
            require(gCompositionPresenter.present(gWindow, gBackBufferPixels, gWindowWidth, gWindowHeight), "moving note presentation failed");
        }
        // STOP freezes both CPU and GPU note placement without freezing the
        // real playback clock. Resume movement at the end of the plateau.
        gCharts[0].scroll = {{0, 0, 1}, {1, 1, 0}, {2, 1, 1}};
        for (bool gpu : {false, true}) for (double time : {0.9, 1.1, 1.9, 2.1}) {
            gPlayhead = time; gGpuFrame = gpu;
            if (gpu) require(gCompositionPresenter.prepare(gWindow, gWindowWidth, gWindowHeight), "STOP GPU preparation failed");
            drawGameScreen(gBackBufferDc); GdiFlush();
            const double position = time < 1 ? time : time < 2 ? 1 : time - 1;
            const int row = static_cast<int>(std::ceil(judgementLineY(gWindowHeight) - (2 - position) * 270)) + 1;
            const auto pixel = gpu ? GpuTestAccess::pixel(gCompositionPresenter, gBackBufferPixels, gWindowWidth, originX, row) :
                static_cast<uint32_t*>(gBackBufferPixels)[row * gWindowWidth + originX];
            require((pixel & 0xffffffu) == (expectedColor & 0xffffffu), "STOP scroll freeze/resume differs between CPU and GPU");
        }
        gCharts[0].scroll.clear();
        gCharts[0].notes = originalNotes; gPlayhead = 0;
        gGpuFrame = false; gCompositionPresenter.reset();
        drawGameScreen(gBackBufferDc); GdiFlush();
        const auto* notePixels = static_cast<const uint32_t*>(gBackBufferPixels);
        require((notePixels[10 * gWindowWidth + originX] & 0x00ffffffu) == (expectedColor & 0x00ffffffu) &&
            (notePixels[18 * gWindowWidth + originX] & 0x00ffffffu) != (expectedColor & 0x00ffffffu),
            "default note height should remain 12 pixels");
        gNoteHeight = 20;
        drawGameScreen(gBackBufferDc); GdiFlush();
        require((static_cast<const uint32_t*>(gBackBufferPixels)[18 * gWindowWidth + originX] & 0x00ffffffu) == (expectedColor & 0x00ffffffu),
            "increased note height was not rendered");
        gNoteHeight = 12;
        drawGameScreen(gBackBufferDc); GdiFlush();
        const int bodyY = 300;
        const int bodyLeft = gameLayout.left(5);
        const int bodyRight = gameLayout.right(5) - 1;
        notePixels = static_cast<const uint32_t*>(gBackBufferPixels);
        require(notePixels[bodyY * gWindowWidth + bodyLeft] == notePixels[bodyY * gWindowWidth + bodyRight], "long-note body width does not match its note head");
        require(notePixels[bodyY * gWindowWidth + bodyLeft - 1] != notePixels[bodyY * gWindowWidth + bodyLeft], "long-note body spills outside note width");
        require(notePixels[bodyY * gWindowWidth + bodyRight + 1] != notePixels[bodyY * gWindowWidth + bodyRight], "long-note body does not reach the lane's right edge");
        gLaneCoverPercent = 50;
        drawGameScreen(gBackBufferDc); GdiFlush();
        require((static_cast<uint32_t*>(gBackBufferPixels)[2 * gWindowWidth + originX] & 0xffffffu) == 0x0c0c0cu, "cover must mask top notes");
        gLaneCoverPercent = 0;
        drawGameScreen(gBackBufferDc); GdiFlush();
        require((static_cast<uint32_t*>(gBackBufferPixels)[2 * gWindowWidth + originX] & 0xffffffu) == (expectedColor & 0xffffffu), "removing cover must reveal notes");
        for (int x = gameLayout.fieldX; x < gameLayout.fieldX + gameLayout.totalWidth(); ++x)
            require(static_cast<uint32_t*>(gBackBufferPixels)[(gWindowHeight - 1) * gWindowWidth + x] == 0xff000000u, ("gear backdrop is not pure black to the screen bottom: x=" + std::to_string(x) + " pixel=" + std::to_string(static_cast<uint32_t*>(gBackBufferPixels)[(gWindowHeight - 1) * gWindowWidth + x])).c_str());
        RECT fractionalClip{0, 0, gWindowWidth, gWindowHeight};
        std::fill_n(static_cast<uint32_t*>(gBackBufferPixels), static_cast<size_t>(gWindowWidth) * gWindowHeight, 0u);
        fillGameRectSubpixel(gBackBufferDc, 10, 20.5, 30, 32.5, RGB(255, 0, 0), fractionalClip);
        const auto* subpixel = static_cast<uint32_t*>(gBackBufferPixels);
        require(((subpixel[20 * gWindowWidth + 15] >> 16) & 255) == 128 && ((subpixel[21 * gWindowWidth + 15] >> 16) & 255) == 255 && ((subpixel[32 * gWindowWidth + 15] >> 16) & 255) == 128, "fractional note edges lost");
        ScratchDisc disc;
        disc.update(0.2, false); const double firstAngle = disc.angle();
        disc.update(0.4, true); const double stoppedAngle = disc.angle();
        require(stoppedAngle > firstAngle, "idle scratch disc does not rotate");
        disc.update(1.0, true); require(disc.angle() == stoppedAngle, "pressed scratch disc rotates");
        disc.update(1.0, false); disc.update(1.2, false); require(disc.angle() > stoppedAngle, "released scratch disc does not resume");
        std::array<bool, 8> pressed{}; pressed[3] = true;
        std::vector<uint32_t> deck(580 * 120, 0xff000000u), rotated(deck);
        KeyDeck::draw(deck.data(), 580, 120, 0, 68, 0, pressed, 0, 104);
        const uint32_t whiteKeyTop = deck[28 * 580 + 104 + 34];
        const uint32_t whiteKeyShadow = deck[68 * 580 + 104 + 34];
        const uint32_t blackKeyTop = deck[12 * 580 + 104 + 68 + 30];
        const uint32_t blackKeyBottom = deck[73 * 580 + 104 + 68 + 30];
        require(((whiteKeyTop >> 16) & 255) > 230 && ((whiteKeyShadow >> 16) & 255) < ((whiteKeyTop >> 16) & 255) &&
            ((blackKeyTop >> 16) & 255) > ((blackKeyBottom >> 16) & 255) && (blackKeyBottom & 0xffffffu) == 0,
            "keys should have requested bottom shadows and black-key shading");
        KeyDeck::draw(rotated.data(), 580, 120, 0, 68, 0, pressed, 1.5, 104);
        require(deck != rotated, "disc rotation has no visible marker");
        {
            TurntableImage whiteFace, blackFace;
            const std::array<uint32_t, 4> whitePixels{0x80ff0000u, 0xff00ff00u, 0xff0000ffu, 0xffffffffu};
            const uint32_t blackPixel = 0xff505050u;
            whiteFace.setPixels(2, 2, whitePixels.data()); blackFace.setPixels(1, 1, &blackPixel);
            const LaneLayout keys{0, 68, 104};
            std::array<bool, 8> releasedKeys{};
            std::vector<uint32_t> imageKeys(580 * 120);
            const auto drawKeys = [&] { KeyDeck::draw(imageKeys.data(), 580, 120, 0, 68, 0, releasedKeys, 0, 104, nullptr, &blackFace, &whiteFace); };
            drawKeys();
            std::vector<uint32_t> cachedKeys(580 * 120, 0);
            KeyDeck::drawCached(cachedKeys.data(), 580, 120, 0, 68, 0, releasedKeys, 0, 104, nullptr, &blackFace, &whiteFace);
            require(cachedKeys == imageKeys, "whole deck cache changed key/rim pixels");
            const size_t whiteCorner = 11 * 580 + keys.left(1) + 4;
            const size_t blackCorner = 11 * 580 + keys.left(2) + 7;
            require(imageKeys[whiteCorner] == 0xff800000u && imageKeys[blackCorner] == blackPixel &&
                imageKeys[73 * 580 + keys.right(1) - 5] == 0xffffffffu, "cached key scaling or alpha changed");
            releasedKeys[1] = releasedKeys[2] = true;
            drawKeys();
            KeyDeck::drawCached(cachedKeys.data(), 580, 120, 0, 68, 0, releasedKeys, 0, 104, nullptr, &blackFace, &whiteFace);
            require(cachedKeys == imageKeys, "whole deck cache retained stale key presses");
            require(imageKeys[whiteCorner] == 0xff4c0000u && imageKeys[blackCorner] == 0xff303030u,
                "cached pressed keys lost their tint");
            const std::array<uint32_t, 4> replacement{0xff0000ffu, 0xff0000ffu, 0xff0000ffu, 0xff0000ffu};
            whiteFace.setPixels(2, 2, replacement.data()); releasedKeys[1] = false;
            drawKeys();
            require(imageKeys[whiteCorner] == 0xff0000ffu, "same-size key image reload retained a stale cached face");
            KeyDeck::drawCached(cachedKeys.data(), 580, 120, 0, 68, 0, releasedKeys, 0, 104, nullptr, &blackFace, &whiteFace);
            require(cachedKeys == imageKeys, "whole deck cache retained a reloaded image");
        }
        if (argc >= 3 && std::filesystem::path(argv[2]).extension() == L".png") {
            TurntableImage turntable;
            require(loadTurntableImageFile(argv[2], turntable), "turntable asset must decode as a 64x64 PNG");
            std::array<uint32_t, 64 * 64> centerMarker{};
            centerMarker[32 * 64 + 32] = 0xffff0000u;
            TurntableImage centeredImage;
            require(centeredImage.setPixels(64, 64, centerMarker.data()), "center marker image setup failed");
            std::vector<uint32_t> centeredAtZero(200 * 180, 0xff000000u), centeredAtAngle(centeredAtZero);
            centeredImage.draw(centeredAtZero.data(), 200, 180, 100, 90, 0);
            centeredImage.draw(centeredAtAngle.data(), 200, 180, 100, 90, 1.5);
            require(centeredAtZero[90 * 200 + 100] == 0xffff0000u && centeredAtAngle[90 * 200 + 100] == 0xffff0000u,
                "turntable rotation pivot must remain at the image center");
            std::vector<uint32_t> imageDeck(580 * 120, 0xff171c22u), imageDeckRotated(imageDeck);
            KeyDeck::draw(imageDeck.data(), 580, 120, 0, 68, 0, pressed, 0, 104, &turntable);
            KeyDeck::draw(imageDeckRotated.data(), 580, 120, 0, 68, 0, pressed, 1.5, 104, &turntable);
            require(imageDeck != imageDeckRotated, "loaded turntable image does not rotate");
            const auto preview = std::filesystem::current_path() / L"turntable-preview.bmp";
            BITMAPFILEHEADER previewFile{}; previewFile.bfType = 0x4d42; previewFile.bfOffBits = sizeof(previewFile) + sizeof(BITMAPINFOHEADER); previewFile.bfSize = previewFile.bfOffBits + static_cast<DWORD>(imageDeck.size() * 4);
            BITMAPINFOHEADER previewInfo{}; previewInfo.biSize = sizeof(previewInfo); previewInfo.biWidth = 580; previewInfo.biHeight = -120; previewInfo.biPlanes = 1; previewInfo.biBitCount = 32;
            std::ofstream previewOutput(preview, std::ios::binary);
            previewOutput.write(reinterpret_cast<const char*>(&previewFile), sizeof(previewFile));
            previewOutput.write(reinterpret_cast<const char*>(&previewInfo), sizeof(previewInfo));
            previewOutput.write(reinterpret_cast<const char*>(imageDeck.data()), static_cast<std::streamsize>(imageDeck.size() * 4));
        }
        if (argc >= 4) {
            TurntableImage iconBackground;
            require(loadRasterPngFile(argv[3], iconBackground), "HUD icon background PNG must decode");
            std::vector<uint32_t> iconCanvas(80 * 80, 0xff101010u);
            iconBackground.draw(iconCanvas.data(), 80, 80, 40, 40, 0.0, 52);
            require(iconCanvas[40 * 80 + 40] != 0xff101010u, "HUD icon background did not render");
        }
        if (argc == 5) {
            TurntableImage splashSheet;
            require(loadRasterPngFile(argv[4], splashSheet) && splashSheet.width() == 600 && splashSheet.height() == 100,
                "cropped transparent six-frame splash asset must decode");
            PlayFeedback animated;
            require(animated.setSpriteSheet(splashSheet.width(), splashSheet.height(), splashSheet.pixels()), "splash sheet frame extraction");
            animated.hit(3, 0.0);
            std::vector<uint32_t> first(900 * 500, 0xff101010u), last(first);
            animated.draw(first.data(), 900, 500, 100, 68, 250, 0.01, 104);
            animated.draw(last.data(), 900, 500, 100, 68, 250, 0.16, 104);
            require(first != last, "decoded video splash does not animate");
        }
        auto released = deck;
        pressed[3] = false;
        KeyDeck::draw(released.data(), 580, 120, 0, 68, 0, pressed, 0, 104);
        const size_t keyPixel = 34 * 580 + 104 + 2 * 68 + 34;
        require((deck[keyPixel] & 255) < (released[keyPixel] & 255), "pressed key does not darken");
        auto restored = deck;
        KeyDeck::draw(restored.data(), 580, 120, 0, 68, 0, pressed, 0, 104);
        require(restored == released, "released key does not restore original appearance");
        CompositionPresenter presenter;
        require(presenter.present(gWindow, gBackBufferPixels, gWindowWidth, gWindowHeight), "DirectComposition presentation failed");
        require(presenter.present(gWindow, gBackBufferPixels, gWindowWidth, gWindowHeight), "DirectComposition second presentation failed");
        presenter.reset();
        require(deck[46 * 580 + 51] != deck[46 * 580 + 104 + 34], "scratch and keyboard must differ");
        for (int lane = 1; lane < 8; ++lane) {
            const uint32_t cap = deck[59 * 580 + 104 + (lane - 1) * 68 + 34];
            require(((cap >> 16) & 255) == ((cap >> 8) & 255) && ((cap >> 8) & 255) == (cap & 255), "green stripe remains on keyboard");
        }
        gSettingsFile = std::filesystem::path(argv[1]) / L"display-note-height.ini";
        gNoteHeight = 22; saveDisplaySettings();
        gNoteHeight = 12; loadDisplaySettings();
        require(gNoteHeight == 22, "note height setting was not persisted and restored");
        const bool compositionBefore = gCompositionEnabled, directCompositionBefore = gDirectCompositionEnabled;
        WritePrivateProfileStringW(L"Display", L"GpuRendering", nullptr, gSettingsFile.c_str());
        WritePrivateProfileStringW(L"Display", L"DirectComposition", L"0", gSettingsFile.c_str());
        loadDisplaySettings();
        require(!gCompositionEnabled, "legacy DirectComposition setting was ignored");
        WritePrivateProfileStringW(L"Display", L"DirectComposition", L"1", gSettingsFile.c_str());
        WritePrivateProfileStringW(L"Display", L"GpuRendering", L"0", gSettingsFile.c_str());
        loadDisplaySettings();
        require(!gCompositionEnabled && gDirectCompositionEnabled, "GPU OFF changed the independent DirectComposition preference");
        gCompositionEnabled = true; gDirectCompositionEnabled = false;
        saveDisplaySettings();
        gCompositionEnabled = false; gDirectCompositionEnabled = true;
        loadDisplaySettings();
        require(gCompositionEnabled && !gDirectCompositionEnabled, "GPU and DirectComposition settings were not saved independently");
        WritePrivateProfileStringW(L"Display", L"GpuRendering", L"0", gSettingsFile.c_str());
        loadDisplaySettings();
        require(!gCompositionEnabled, "legacy setting overrode the newer GPU preference");
        gCompositionEnabled = compositionBefore; gDirectCompositionEnabled = directCompositionBefore;
        std::filesystem::remove(gSettingsFile);
        gSettingsFile.clear();
        const auto alphaPath = std::filesystem::path(argv[1]) / L"display-miss-alpha.png";
        const auto bluePath = std::filesystem::path(argv[1]) / L"display-miss-blue.png";
        UINT encoderCount{}, encoderBytes{}; Gdiplus::GetImageEncodersSize(&encoderCount, &encoderBytes);
        std::vector<BYTE> encoderStorage(encoderBytes);
        auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(encoderStorage.data());
        Gdiplus::GetImageEncoders(encoderCount, encoderBytes, encoders);
        CLSID pngEncoder{};
        for (UINT i = 0; i < encoderCount; ++i) if (wcscmp(encoders[i].MimeType, L"image/png") == 0) pngEncoder = encoders[i].Clsid;
        Gdiplus::Bitmap alphaImage(1, 1, PixelFormat32bppARGB), blueImage(1, 1, PixelFormat32bppARGB);
        alphaImage.SetPixel(0, 0, Gdiplus::Color(128, 255, 0, 0)); blueImage.SetPixel(0, 0, Gdiplus::Color(255, 0, 0, 255));
        require(alphaImage.Save(alphaPath.c_str(), &pngEncoder) == Gdiplus::Ok && blueImage.Save(bluePath.c_str(), &pngEncoder) == Gdiplus::Ok, "miss layer fixtures could not be saved");
        {
            BrowseBackdrop background;
            bms::Chart previewChart; previewChart.titleImage = bluePath;
            previewChart.bitmaps[L"01"] = alphaPath; previewChart.bitmaps[L"02"] = bluePath;
            previewChart.bga = {{0, bms::BgaLayer::Base, L"01"}, {3, bms::BgaLayer::Base, L"02"}};
            require(background.render(&previewChart, 0, {}, 0, 100, 64, 64, 0).empty(), "gray backdrop decoded media");
            require(background.render(&previewChart, 2, {}, 0, 100, 64, 64, 0)[32 * 64 + 32] == 0xff0000ffu, "title background missing");
            require(background.render(&previewChart, 3, bluePath, 100, 0, 64, 64, 0)[32 * 64 + 32] == 0xff404040u, "zero background opacity not gray");
            const auto opening = background.render(&previewChart, 1, {}, 0, 100, 64, 64, 0)[32 * 64 + 32];
            const auto highlight = background.render(&previewChart, 1, {}, 0, 100, 64, 64, 5)[32 * 64 + 32];
            require(opening != highlight && highlight == 0xff0000ffu, "BGA highlight event state not selected");
        }
        bms::Chart layers; layers.bitmaps[L"01"] = path; layers.bitmaps[L"02"] = alphaPath; layers.bitmaps[L"03"] = bluePath;
        const int savedBrightness = gBgaBrightness, savedDarkness = gBgaDarkness, savedTransparency = gLaneTransparency;
        gBgaBrightness = 100; gBgaDarkness = 0; gLaneTransparency = 100;
        gBgaRaster.reset(); gBgaBackplateDirty = true; gMissBgaUntil = 100; gPlayhead = 0;
        gBgaActiveImages = {L"01", L"02", L"", L""};
        const auto drawLayers = [&] {
            drawBgaBackplate(gBackBufferDc, layers, gameClient, 100, 68, 0, 360, 104); GdiFlush();
            return static_cast<const uint32_t*>(gBackBufferPixels)[10 * gWindowWidth + gWindowWidth - 10] & 0xffffffu;
        };
        require(drawLayers() == 0xff7f7fu, "transparent miss layer lost the white background");
        gBgaActiveImages[0] = L"03";
        require(drawLayers() == 0x80007fu, "background changes under transparent miss did not redraw");
        gBgaActiveImages[1] = L"01";
        require(drawLayers() == 0xffffffu && gBgaBackplateImages[0].empty(), "opaque miss did not replace lower layers");
        gBgaActiveImages[0] = L"02";
        require(drawLayers() == 0xffffffu && gBgaBackplateImages[0].empty(), "hidden background invalidated opaque miss");
        gMissBgaUntil = -1;
        drawLayers();
        require(gBgaBackplateImages[0] == L"02" && gBgaBackplateImages[1].empty(), "miss expiry did not restore current background");
        gBgaBrightness = savedBrightness; gBgaDarkness = savedDarkness; gLaneTransparency = savedTransparency;
        gBgaRaster.reset(); gBgaBackplateDirty = true; gBgaActiveImages.fill(L""); gMissBgaUntil = 0;
        std::filesystem::remove(alphaPath); std::filesystem::remove(bluePath);
        gPlaying = true; gShowingResult = false;
        gGrooveGauge.configure(40, GrooveGaugeMode::Hard); gResultScore.reset(40);
        showTimingJudgement(TimingJudgement::PGreat);
        gPressed.fill(true); gHolds[0].start(0, 100.0);
        const HWND savedResultNext = gResultNext;
        gResultNext = CreateWindowW(L"BUTTON", L"Result next fixture", WS_CHILD, 0, 0, 100, 30,
            gWindow, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(gResultNext != nullptr, "result fade button fixture failed");
        for (int i = 0; i < 10; ++i) showTimingJudgement(TimingJudgement::Poor);
        require(gPlaying && !gShowingResult && gGrooveGauge.value() == 10.0, "playback ended before hard gauge reached zero");
        showTimingJudgement(TimingJudgement::Poor);
        require(gPlaying && gGrooveGauge.value() == 1.0, "hard gauge must allow the final 1 percent");
        const int scoreBeforeFailure = gResultScore.value();
        showTimingJudgement(TimingJudgement::Poor);
        require(!gPlaying && gShowingResult && gGrooveGauge.depleted() && !gResultGaugeCleared && scoreBeforeFailure > 0 && gResultScore.value() == scoreBeforeFailure,
            "game over must retain earned points and open result");
        require(!gAudio.isRunning() && !gHolds[0].active() && std::none_of(gPressed.begin(), gPressed.end(), [](bool down) { return down; }),
            "game over must stop audio and clear held inputs");
        require(gResultFade.active() && !gResultFadePixels.empty(), "result fade did not capture gameplay");
        require(!(GetWindowLongPtrW(gResultNext, GWL_STYLE) & WS_VISIBLE), "result button flashed during fade");
        const auto fadeSnapshot = gResultFadePixels;
        finishPlayback();
        require(gResultFadePixels == fadeSnapshot, "duplicate finish recaptured the result screen");
        std::fill(gResultFadePixels.begin(), gResultFadePixels.end(), 0xff808080u);
        gResultFade.start(ResultFade::Clock::now() - std::chrono::milliseconds(100));
        drawResultFade(gBackBufferDc); GdiFlush();
        const auto fadePixel = static_cast<const uint32_t*>(gBackBufferPixels)[0] & 255;
        require(fadePixel >= 45 && fadePixel <= 80, "outgoing snapshot did not fade to black");
        gResultFade.start(ResultFade::Clock::now() - std::chrono::milliseconds(500));
        windowProc(gWindow, WM_TIMER, ID_RESULT_FADE, 0);
        require(!gResultFade.active() && gResultFadePixels.empty() && (GetWindowLongPtrW(gResultNext, GWL_STYLE) & WS_VISIBLE),
            "result fade did not release snapshot and enable result controls");
        DestroyWindow(gResultNext); gResultNext = savedResultNext;
        const int failedScore = gResultScore.value();
        showTimingJudgement(TimingJudgement::PGreat);
        require(gResultScore.value() == failedScore, "queued judgement changed result score");

        bms::Chart missBatch; missBatch.durationSeconds = 10.0;
        for (int i = 0; i < 12; ++i) missBatch.notes.push_back({0, -1.0, 0.0, L""});
        std::wstring prepareError;
        require(gAudio.prepare(missBatch, prepareError), "silent miss-batch audio timeline preparation failed");
        gCharts.push_back(std::move(missBatch)); gActiveChartIndex = static_cast<int>(gCharts.size()) - 1;
        gHit.assign(12, 0); gMissScanIndex = 0;
        gPlaying = true; gShowingResult = false; gGrooveGauge.configure(12, GrooveGaugeMode::Hard); gResultScore.reset(12);
        updatePlaybackFrame();
        require(gShowingResult && !gPlaying && gMissScanIndex == 12 && gHit[11], "miss batch must stop at hard gauge failure");
        gShowingResult = false; gGrooveGauge.configure(2, gGrooveGaugeMode);
        require(gGrooveGauge.value() == 20.0 && !gGrooveGauge.depleted(), "new normal play must restore its 20 percent start gauge");
        gResultScore.reset(2);
        showTimingJudgement(TimingJudgement::PGreat);
        require(gResultScore.value() == 500'000, "normal note must earn full note weight");
        showTimingJudgement(TimingJudgement::PGreat, 1);
        require(gResultScore.value() == 750'000, "long-note head must earn half note weight");
        showHoldJudgement(HoldResult::PGreat);
        require(gResultScore.value() == 1'000'000, "long-note release must complete the other half");
        gCharts[0].title = L"Fixture Result Song";
        gCharts[0].artist = L"Fixture Composer";
        gCharts[0].playLevel = L"7";
        gCharts[0].initialBpm = 145.0;
        gActiveChartIndex = 0;
        captureBrowseBackdrop();
        require(gResultBackdropBitmap && gResultBackdropPixels, "result screen did not capture the song-selection background");
        const auto* capturedBackdrop = static_cast<const uint32_t*>(gResultBackdropPixels);
        const uint32_t backgroundPixel = capturedBackdrop[(gWindowHeight - 8) * gWindowWidth + gWindowWidth - 8];
        gShowingResult = true;
        drawResultScreen(gBackBufferDc);
        GdiFlush();
        const uint32_t resultBackgroundPixel = static_cast<const uint32_t*>(gBackBufferPixels)[(gWindowHeight - 8) * gWindowWidth + gWindowWidth - 8];
        const auto overlayPixel = [](uint32_t source) {
            constexpr unsigned overlayAlpha = 118, baseAlpha = 255 - overlayAlpha;
            const unsigned red = (((source >> 16) & 255) * baseAlpha + 10 * overlayAlpha) / 255;
            const unsigned green = (((source >> 8) & 255) * baseAlpha + 14 * overlayAlpha) / 255;
            const unsigned blue = ((source & 255) * baseAlpha + 21 * overlayAlpha) / 255;
            return 0xff000000u | (red << 16) | (green << 8) | blue;
        };
        require(backgroundPixel != resultBackgroundPixel && resultBackgroundPixel == overlayPixel(backgroundPixel),
            "result background was not translucently darkened");
        size_t titleInk = 0, detailsInk = 0;
        const auto* resultPixels = static_cast<const uint32_t*>(gBackBufferPixels);
        for (int y = 45; y < 104; ++y) for (int x = 32; x < gWindowWidth - 32; ++x)
            titleInk += resultPixels[y * gWindowWidth + x] != overlayPixel(capturedBackdrop[y * gWindowWidth + x]);
        for (int y = 145; y < 178; ++y) for (int x = 32; x < gWindowWidth - 32; ++x)
            detailsInk += resultPixels[y * gWindowWidth + x] != overlayPixel(capturedBackdrop[y * gWindowWidth + x]);
        require(titleInk > 20 && detailsInk > 20, "result title and BPM/composer details were not drawn");
        gShowingResult = false;
        TextPopAnimation pop;
        const auto born = TextPopAnimation::Clock::now();
        require(pop.scale(born) == 1, "inactive text animation should be full size");
        pop.start(born);
        require(std::abs(pop.scale(born) - 0.85) < 0.001 && pop.scale(born + std::chrono::milliseconds(120)) > 1.0 &&
            pop.scale(born + std::chrono::milliseconds(200)) == 1.0, "text pop must grow, overshoot and settle using easeOutBack");
        pop.start(born + std::chrono::milliseconds(150));
        require(std::abs(pop.scale(born + std::chrono::milliseconds(150)) - 0.85) < 0.001, "repeated combo hits must restart text pop");
        pop.reset(); require(pop.scale(born) == 1.0, "text animation reset did not clear playback state");
        gJudgementText.clear(); gJudgementUntil = {};
        setJudgement(L"GREAT!!", RGB(255, 214, 20));
        require(gJudgementPop.scale() < 0.95, "fresh judgement should animate");
        const auto previousDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
        gJudgementUntil = previousDeadline;
        setJudgement(L"GREAT!!", RGB(255, 214, 20));
        require(gJudgementPop.scale() == 1.0 && gJudgementUntil > previousDeadline, "visible judgement should refresh without animation");
        setJudgement(L"GOOD", RGB(56, 189, 248));
        require(gJudgementPop.scale() == 1.0 && gJudgementText == L"GOOD", "changing visible judgement must not replay animation");
        gJudgementUntil = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
        setJudgement(L"GREAT!!", RGB(255, 214, 20));
        require(gJudgementPop.scale() < 0.95, "judgement after expiry should animate again");
        gJudgementText.clear(); gJudgementUntil = {}; gJudgementPop.reset();
        HFONT popFont = CreateFontW(-64, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        const auto textBounds = [&](double scale) {
            fillRect(gBackBufferDc, {0, 0, gWindowWidth, gWindowHeight}, RGB(0, 0, 0));
            drawAnimatedCenteredText(gBackBufferDc, L"GREAT!!", {100, 100, gWindowWidth - 100, 300}, popFont, RGB(255, 214, 20), scale);
            GdiFlush();
            int left = gWindowWidth, right = -1;
            for (int y = 0; y < gWindowHeight; ++y) for (int x = 0; x < gWindowWidth; ++x)
                if ((static_cast<const uint32_t*>(gBackBufferPixels)[y * gWindowWidth + x] & 0xffffffu) != 0) {
                    left = std::min(left, x); right = std::max(right, x);
                }
            require(right >= left, "animated text did not render");
            return std::pair<int, int>{right - left + 1, left + right};
        };
        const auto smallText = textBounds(0.85), fullText = textBounds(1.0), overshootText = textBounds(1.015);
        require(smallText.first < fullText.first && fullText.first < overshootText.first, "GDI text size did not follow animation scale");
        require(std::abs(smallText.second - fullText.second) <= 4 && std::abs(overshootText.second - fullText.second) <= 4,
            "text pop must scale around the text center");
        require(GetGraphicsMode(gBackBufferDc) == GM_COMPATIBLE, "text animation leaked its transform into the game renderer");
        TextRasterCache cache;
        std::vector<uint32_t> textCanvas(400 * 180, 0xff223344u);
        int renders = 0;
        const auto renderText = [&](HDC target, const std::wstring& value, RECT bounds, HFONT font, COLORREF tint) {
            ++renders; drawTextBody(target, value, bounds, font, tint);
        };
        require(cache.draw(textCanvas.data(), 400, 180, L"GREAT!!", {0, 0, 400, 180}, popFont, RGB(255, 214, 20), 1.0, renderText), "text sprite did not build");
        const auto firstText = textCanvas;
        require(textCanvas.front() == 0xff223344u && textCanvas.back() == 0xff223344u &&
            std::find(textCanvas.begin(), textCanvas.end(), 0xffffd614u) != textCanvas.end(), "text sprite transparency or body color changed");
        std::fill(textCanvas.begin(), textCanvas.end(), 0xff223344u);
        cache.draw(textCanvas.data(), 400, 180, L"GREAT!!", {0, 0, 400, 180}, popFont, RGB(255, 214, 20), 1.0, renderText);
        require(renders == 2 && textCanvas == firstText, "cached text was repainted or changed pixels");
        std::fill(textCanvas.begin(), textCanvas.end(), 0xff223344u);
        const auto renderLeftText = [&](HDC target, const std::wstring& value, RECT bounds, HFONT font, COLORREF tint) {
            drawLeftTextBody(target, value, bounds, font, tint);
        };
        require(cache.draw(textCanvas.data(), 400, 180, L"GREAT!!", {40, 0, 390, 180}, popFont,
            RGB(255, 214, 20), 1.0, renderLeftText, false, {}, true), "left-aligned text sprite did not build");
        int firstLeftPixel = 400;
        for (int y = 0; y < 180; ++y) for (int x = 0; x < 80; ++x)
            if (textCanvas[static_cast<size_t>(y) * 400 + x] != 0xff223344u) firstLeftPixel = std::min(firstLeftPixel, x);
        require(firstLeftPixel >= 40 && firstLeftPixel < 60, "left-aligned cached text did not stay at the requested x position");
        std::vector<uint32_t> guarded(100 * 50 + 2, 0xdeadbeefu);
        cache.draw(guarded.data() + 1, 100, 50, L"GREAT!!", {-90, -60, 130, 80}, popFont, RGB(255, 214, 20), 1.015, renderText);
        require(guarded.front() == 0xdeadbeefu && guarded.back() == 0xdeadbeefu && renders == 2, "scaled text escaped canvas bounds or rebuilt sprite");
        cache.clear();
        DeleteObject(popFont);
        {
            const auto savedSmallFont = gSmallFont;
            const bool savedUsage = gShowResourceUsage, savedComposition = gCompositionEnabled;
            gSmallFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            gShowResourceUsage = true; gCompositionEnabled = false;
            auto* canvas = static_cast<uint32_t*>(gBackBufferPixels);
            const size_t count = static_cast<size_t>(gBackBufferWidth) * gBackBufferHeight;
            constexpr uint32_t background = 0xff223344u;
            GdiFlush(); std::fill_n(canvas, count, background);
            drawResourceUsageOverlay(gBackBufferDc);
            const std::vector<uint32_t> firstOverlay(canvas, canvas + count);
            require(std::find(firstOverlay.begin(), firstOverlay.end(), 0xfff8fafcu) != firstOverlay.end() &&
                std::find(firstOverlay.begin(), firstOverlay.end(), 0xff000000u) != firstOverlay.end() &&
                firstOverlay.front() == background, "resource overlay lost white text, black outline or transparency");
            std::fill_n(canvas, count, background); drawResourceUsageOverlay(gBackBufferDc);
            require(std::equal(firstOverlay.begin(), firstOverlay.end(), canvas), "cached resource text changed between identical frames");
            gShowResourceUsage = false; std::fill_n(canvas, count, background); drawResourceUsageOverlay(gBackBufferDc);
            require(std::all_of(canvas, canvas + count, [&](uint32_t p) { return p == background; }), "disabled resource overlay still drew text");
            gResourceTextRaster.clear(); DeleteObject(gSmallFont); gSmallFont = savedSmallFont;
            gShowResourceUsage = savedUsage; gCompositionEnabled = savedComposition;
        }
        {
            for (int height : {680, 720, 1080}) {
                const BrowsePanelLayout panel(960, height);
                require(panel.titleY + panel.titleHeight <= panel.composerY && panel.composerY + 48 <= panel.bpmY &&
                    panel.bpmY + 48 <= panel.levelY && panel.startY + panel.startHeight <= panel.slotTop &&
                    panel.slotTop + panel.slotSize < height - 40,
                    "enlarged song details overlap the browse controls at a supported window size");
            }
            const int savedWidth = gWindowWidth, savedHeight = gWindowHeight;
            gWindowWidth = 640; gWindowHeight = 360; ensureBackBuffer(dc, 640, 360);
            gPracticeHudFont = CreateFontW(-18, 0, 0, 0, FW_LIGHT, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            gPractice.configure(60); gPractice.startSeconds = 10; gPractice.endSeconds = 20; gPractice.enabled = true;
            auto* practicePixels = static_cast<uint32_t*>(gBackBufferPixels);
            GdiFlush(); std::fill_n(practicePixels, 640 * 360, 0xff000000u);
            gGpuFrame = false; drawPracticeHud(gBackBufferDc); GdiFlush();
            const auto whitePixel = std::find(practicePixels, practicePixels + 640 * 360, 0xffffffffu);
            require(whitePixel != practicePixels + 640 * 360, "practice HUD did not draw thin white text");
            const size_t position = static_cast<size_t>(whitePixel - practicePixels);
            require(gCompositionPresenter.prepare(gWindow, 640, 360), "practice GPU overlay initialization failed");
            std::fill_n(practicePixels, 640 * 360, 0xff000000u); gGpuFrame = true; drawPracticeHud(gBackBufferDc);
            require(GpuTestAccess::spriteCount(gCompositionPresenter) == 2 &&
                (GpuTestAccess::pixel(gCompositionPresenter, practicePixels, 640, static_cast<int>(position % 640), static_cast<int>(position / 640)) & 0xffffffu) == 0xffffffu,
                "CPU and GPU practice HUD placement differ");
            gGpuFrame = false; gCompositionPresenter.reset();

            const auto savedRecordsFile = gPlayRecordsFile;
            gPlayRecordsFile = std::filesystem::path(argv[1]) / (L"practice-records-" + std::to_wstring(GetTickCount64()) + L".ini");
            const auto recordCount = gPlayRecords.size();
            gPlaying = true; gShowingResult = false; gGrooveGauge.configure(1, GrooveGaugeMode::Hard); gResultScore.reset(1);
            for (int i = 0; i < 20; ++i) showTimingJudgement(TimingJudgement::Poor);
            require(gPlaying && !gShowingResult, "practice mode ended when its gauge depleted");
            gActiveChartIndex = gPracticeNotesChartIndex = 0;
            gPracticeOriginalDuration = gCharts[0].durationSeconds;
            const auto originalCount = gCharts[0].notes.size();
            gPracticeOriginalNotes = std::move(gCharts[0].notes);
            gCharts[0].notes = gPractice.notesFor(gPracticeOriginalNotes);
            finishPlayback();
            require(!std::filesystem::exists(gPlayRecordsFile) && gPlayRecords.size() == recordCount,
                "practice result modified normal play records");
            restorePracticeNotes();
            require(gCharts[0].notes.size() == originalCount && !gPractice.enabled, "leaving practice lost the full chart");
            gShowingResult = false; gPlayRecordsFile = savedRecordsFile;

            WNDCLASSW setup{}; setup.lpfnWndProc = practiceSetupProc; setup.hInstance = GetModuleHandleW(nullptr);
            setup.lpszClassName = L"PracticeSetupTest"; RegisterClassW(&setup);
            gPractice.configure(60); gPracticeStage = gPracticeBoundary = 0;
            gPracticeWindow = CreateWindowW(setup.lpszClassName, L"Practice test", WS_POPUP, 0, 0, 600, 370, gWindow, nullptr, setup.hInstance, nullptr);
            require(gPracticeWindow != nullptr, "practice setup window failed");
            SendMessageW(gPracticeWindow, WM_KEYDOWN, VK_RIGHT, 0);
            require(gPractice.ratePercent == 105, "arrow key did not change practice playback rate");
            SendMessageW(gPracticeWindow, WM_KEYDOWN, VK_RETURN, 0);
            require(gPracticeStage == 1, "Enter did not advance to section selection");
            SendMessageW(gPracticeWindow, WM_KEYDOWN, VK_RIGHT, 0);
            require(gPractice.startSeconds == 1, "section start arrow control failed");
            SendMessageW(gPracticeWindow, WM_KEYDOWN, VK_DOWN, 0);
            SendMessageW(gPracticeWindow, WM_KEYDOWN, VK_LEFT, 0);
            require(gPractice.endSeconds == 60, "section end arrow control failed");
            SendMessageW(gPracticeWindow, WM_CLOSE, 0, 0);
            require(!gPracticeWindow && IsWindowEnabled(gWindow), "cancel did not restore browse input");
            gTextRaster.clear(); DeleteObject(gPracticeHudFont); gPracticeHudFont = nullptr;
            gWindowWidth = savedWidth; gWindowHeight = savedHeight;
        }
        {
            const bool savedGpu=gCompositionEnabled, savedDirect=gDirectCompositionEnabled;
            const bool savedPreview=gSongPreviewEnabled; const int savedBackground=gBrowseBackgroundMode;
            gCompositionEnabled=true; gDirectCompositionEnabled=false; gSongPreviewEnabled=false; gBrowseBackgroundMode=0;
            gCompositionPresenter.setDirectComposition(false);
            require(gCompositionPresenter.prepare(gWindow,64,64), "QUIT native output setup failed");
            const HWND output=FindWindowExW(gWindow,nullptr,L"BmsGpuOutputSurface",nullptr);
            require(output!=nullptr,"native output was not isolated from the browse window");
            std::vector<uint32_t> paused(64*64,0xff002040u);
            require(gCompositionPresenter.present(gWindow,paused.data(),64,64), "paused native frame failed");
            gPlaying=true; gPaused=true; gGpuFrame=true;
            showBrowseView();
            require(!gPlaying && !gPaused && !gGpuFrame && !gShowingResult && !IsWindow(output),
                "QUIT retained pause state or its GPU output surface");
            require(gCompositionPresenter.lowLatency().active()==latency::Mode::Off,"QUIT retained Reflex pacing");
            gAudio.stop();
            gCompositionEnabled=savedGpu; gDirectCompositionEnabled=savedDirect;
            gSongPreviewEnabled=savedPreview; gBrowseBackgroundMode=savedBackground;
        }
        const std::filesystem::path deckPath = std::filesystem::path(argv[1]) / L"key-deck-preview.bmp";
        BITMAPFILEHEADER deckFile{}; deckFile.bfType = 0x4d42; deckFile.bfOffBits = sizeof(deckFile) + sizeof(BITMAPINFOHEADER); deckFile.bfSize = deckFile.bfOffBits + static_cast<DWORD>(deck.size() * 4);
        BITMAPINFOHEADER deckInfo{}; deckInfo.biSize = sizeof(deckInfo); deckInfo.biWidth = 580; deckInfo.biHeight = -120; deckInfo.biPlanes = 1; deckInfo.biBitCount = 32;
        { std::ofstream output(deckPath, std::ios::binary); output.write(reinterpret_cast<const char*>(&deckFile), sizeof(deckFile)); output.write(reinterpret_cast<const char*>(&deckInfo), sizeof(deckInfo)); output.write(reinterpret_cast<const char*>(deck.data()), static_cast<std::streamsize>(deck.size() * 4)); }
        SelectObject(gBackBufferDc, gBackBufferOriginalBitmap); DeleteObject(gBackBufferBitmap); DeleteDC(gBackBufferDc); DestroyWindow(gWindow);
        if (gResultBackdropDc && gResultBackdropOriginalBitmap) SelectObject(gResultBackdropDc, gResultBackdropOriginalBitmap);
        if (gResultBackdropBitmap) DeleteObject(gResultBackdropBitmap);
        if (gResultBackdropDc) DeleteDC(gResultBackdropDc);
        std::cout << "Display: lanes, BGA, result backdrop, translucent overlay, title/details, and reserved keys passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    if (gBgaBackplateDc && gBgaBackplateOriginalBitmap) SelectObject(gBgaBackplateDc, gBgaBackplateOriginalBitmap);
    if (gBgaBackplateBitmap) DeleteObject(gBgaBackplateBitmap);
    if (gBgaBackplateDc) DeleteDC(gBgaBackplateDc);
    DeleteDC(dc); gBgaRaster.reset(); Gdiplus::GdiplusShutdown(gGdiPlusToken);
    return result;
}
