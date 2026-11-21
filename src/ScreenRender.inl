void drawBrowseScreen(HDC dc) {
#ifdef BMS_ENABLE_SKIN_BINDINGS
    drawSkinOnly(dc);
    if(gSongInfoVisible&&!(gSkinApplication.supports(sevenmania::skin::ScreenKind::SongInformation)&&gSkinApplication.drawDc(dc,gWindowWidth,gWindowHeight,gSongInformationData.snapshot()))){
        RECT box{gWindowWidth/12,gWindowHeight/5,gWindowWidth*2/3-16,gWindowHeight*4/5};fillRect(dc,box,RGB(28,30,34));
        InflateRect(&box,-28,-20);SetTextColor(dc,RGB(240,240,240));SetBkMode(dc,TRANSPARENT);
        auto old=SelectObject(dc,gFont);DrawTextW(dc,gSongInfoText.c_str(),-1,&box,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,old);
    }
    return;
#endif
    gBrowseTextOverflow = false;
    drawBrowseBackground(dc);
    drawText(dc, BROWSE_SHORTCUT_HINT, 24, gWindowHeight - 22, RGB(226, 232, 240), gSmallFont);
    const int rightPanel = gWindowWidth - gWindowWidth / 3;
    drawText(dc, gShowHiddenCharts ? L"HIDDEN FILES" : L"SONG LIST", rightPanel, 78, RGB(162, 213, 236), gSmallFont);
    const BrowsePanelLayout panel(gWindowWidth, gWindowHeight);
    const int index = selectedIndex();
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, 24, panel.genreY, 24 + panel.width, panel.infoY);
    if (index >= 0 && index < static_cast<int>(gCharts.size())) {
        const auto& chart = gCharts[static_cast<size_t>(index)];
        const int groupIndex = selectedGroupIndex();
        const auto& groups = gSongSelection.groups();
        const auto& title = groupIndex >= 0 && groupIndex < static_cast<int>(groups.size()) ?
            groups[static_cast<size_t>(groupIndex)].title : chart.title;
        auto drawSongInfo = [&](const std::wstring& text, int top, int bottom, COLORREF color) {
            RECT bounds{24, top, 24 + panel.width, bottom};
            const auto previous = SelectObject(dc, gBrowseSongInfoFont ? gBrowseSongInfoFont : gBrowseMetadataFont);
            SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT);
            RECT measured{0, 0, 0, 0};
            DrawTextW(dc, text.c_str(), -1, &measured, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
            const int overflow = std::max(0L, measured.right - (bounds.right - bounds.left));
            gBrowseTextOverflow |= overflow > 0;
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - gBrowseTextStartedAt).count();
            const int savedText = SaveDC(dc);
            IntersectClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
            bounds.left -= bms::ui::textMarqueeOffset(overflow, seconds);
            bounds.right = bounds.left + std::max(measured.right, bounds.right - bounds.left);
            DrawTextW(dc, text.c_str(), -1, &bounds, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | (overflow > 0 ? DT_LEFT : DT_CENTER));
            if (savedText) RestoreDC(dc, savedText);
            SelectObject(dc, previous);
        };
        drawSongInfo(chart.genre.empty() ? L"장르 미표기" : chart.genre, panel.genreY, panel.titleY, RGB(168, 179, 193));
        RECT titleRect{24, panel.titleY, 24 + panel.width, panel.titleY + panel.titleHeight};
        const auto oldFont = SelectObject(dc, gBrowseSongTitleFont ? gBrowseSongTitleFont : gBrowseTitleFont);
        SetTextColor(dc, RGB(245, 248, 252)); SetBkMode(dc, TRANSPARENT);
        RECT measuredTitle{};
        DrawTextW(dc, title.c_str(), -1, &measuredTitle, DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
        const int titleOverflow = std::max(0L, measuredTitle.right - panel.width);
        gBrowseTextOverflow |= titleOverflow > 0;
        const int titleClip = SaveDC(dc);
        IntersectClipRect(dc, titleRect.left, titleRect.top, titleRect.right, titleRect.bottom);
        titleRect.left -= bms::ui::textMarqueeOffset(titleOverflow,
            std::chrono::duration<double>(std::chrono::steady_clock::now() - gBrowseTextStartedAt).count());
        titleRect.right = titleRect.left + std::max(measuredTitle.right, static_cast<LONG>(panel.width));
        DrawTextW(dc, title.c_str(), -1, &titleRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | (titleOverflow > 0 ? DT_LEFT : DT_CENTER));
        if (titleClip) RestoreDC(dc, titleClip);
        SelectObject(dc, oldFont);
        drawSongInfo((chart.artist.empty()?std::wstring(L"작곡가 미표기"):chart.artist)+(chart.subartist.empty()?L"":L" / "+chart.subartist), panel.composerY, panel.bpmY, RGB(210, 220, 232));
        drawSongInfo(L"BPM  " + number(chart.initialBpm, 1), panel.bpmY, panel.levelY, RGB(210, 220, 232));
        const auto* group = groupIndex >= 0 && groupIndex < static_cast<int>(groups.size()) ?
            &groups[static_cast<size_t>(groupIndex)] : nullptr;
        drawBrowseDifficultyCarousel(dc, panel, group, chart);
        const auto* record = playRecordForChart(static_cast<size_t>(index));
        drawBrowsePlayRecord(dc, record, panel);
    } else drawText(dc, L"곡을 선택하세요", 24, panel.titleY, RGB(245, 248, 252), gBrowseSongTitleFont ? gBrowseSongTitleFont : gBrowseTitleFont);
    if (saved) RestoreDC(dc, saved);
    constexpr std::array<const wchar_t*, 4> judgementNames{L"BASIC", L"EASY", L"NORMAL", L"HARD"};
    const std::array<std::wstring, 3> values{number(gNoteSpeed, 2) + L"x", judgementNames[static_cast<size_t>(gJudgementProfile)], activeGaugeModeText()};
    constexpr std::array<const wchar_t*, 3> labels{L"SPEED", L"JUDGE", L"GAUGE"};
    for (int slot = 0; slot < 5; ++slot) {
        const RECT bounds = panel.slot(slot, gWindowHeight);
        const auto& slotImage=slot==0&&gHiSpeedSlotImage.loaded()?gHiSpeedSlotImage:
            slot==2&&gGaugeSlotImage.loaded()?gGaugeSlotImage:gHudSlotImage;
        if (slotImage.loaded() && dc == gBackBufferDc && gBackBufferPixels) {
            GdiFlush();
            slotImage.draw(static_cast<uint32_t*>(gBackBufferPixels), gBackBufferWidth, gBackBufferHeight,
                bounds.left + panel.slotSize / 2, bounds.top + panel.slotSize / 2, 0, panel.slotSize);
        } else if (slotImage.loaded() && gGdiPlusToken) {
            Gdiplus::Bitmap image(slotImage.width(), slotImage.height(), slotImage.width() * 4,
                PixelFormat32bppARGB, reinterpret_cast<BYTE*>(const_cast<uint32_t*>(slotImage.pixels())));
            Gdiplus::Graphics graphics(dc);
            graphics.DrawImage(&image, Gdiplus::Rect(bounds.left, bounds.top, panel.slotSize, panel.slotSize));
        } else fillRect(dc, bounds, RGB(155, 155, 155));
        if (slot < 3) {
            RECT label{bounds.left, bounds.top + panel.slotSize / 5, bounds.right, bounds.top + panel.slotSize / 2};
            RECT value{bounds.left, bounds.top + panel.slotSize / 2, bounds.right, bounds.bottom - 6};
            if(slot==1||&slotImage==&gHudSlotImage)drawTextBody(dc, labels[static_cast<size_t>(slot)], label, gSmallFont, RGB(35, 35, 35));
            drawTextBody(dc, values[static_cast<size_t>(slot)], value, gHudBoldFont, RGB(10, 10, 10));
        }
    }

    // The browse page paints its own animated background. Keep these labels on
    // that same surface instead of asking child STATIC controls to erase over it.
    const int margin = 24;
    const int leftWidth = gWindowWidth - gWindowWidth / 3 - margin - 20;
    const int infoTop = index >= 0 ? panel.infoY : panel.composerY;
    const int infoHeight = index >= 0 ? panel.infoHeight : 48;
    auto drawStoredLabel = [&](HWND control, RECT bounds, bool multiline, bool path = false) {
        if (!control) return;
        const int length = GetWindowTextLengthW(control);
        if (length <= 0) return;
        std::wstring text(static_cast<size_t>(length) + 1, L'\0');
        const int copied = GetWindowTextW(control, text.data(), length + 1);
        if (copied <= 0) return;
        text.resize(static_cast<size_t>(copied));
        const auto oldFont = gSmallFont ? SelectObject(dc, gSmallFont) : nullptr;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(218, 228, 240));
        const UINT flags = (path ? DT_RIGHT | DT_PATH_ELLIPSIS : DT_LEFT | DT_END_ELLIPSIS) | DT_NOPREFIX |
            (multiline ? DT_TOP | DT_WORDBREAK : DT_SINGLELINE | DT_VCENTER);
        DrawTextW(dc, text.c_str(), copied, &bounds, flags);
        if (oldFont) SelectObject(dc, oldFont);
    };
    drawStoredLabel(gFolder, {rightPanel, gWindowHeight - 46, gWindowWidth - margin, gWindowHeight - 24}, false, true);
    drawStoredLabel(gInfo, {margin, infoTop, margin + leftWidth, infoTop + infoHeight}, true);
    drawStoredLabel(gStatus, {margin, gWindowHeight - 46, rightPanel - 16, gWindowHeight - 24}, false);

#ifdef BMS_ENABLE_SKIN_BINDINGS
    drawAppliedSkinDc(dc);
#endif
}

unsigned playbackTitleOpacity(std::chrono::steady_clock::time_point now) {
    const auto smoothstep = [](double value) { return value * value * (3.0 - 2.0 * value); };
    if (gTitleFadeOutStarted == std::chrono::steady_clock::time_point{}) {
        const double t = std::chrono::duration<double>(now - gPlaybackLoadStarted).count() /
            std::chrono::duration<double>(titleFadeDuration).count();
        return static_cast<unsigned>(std::lround(smoothstep(std::clamp(t, 0.0, 1.0)) * 255));
    }
    const double t = std::chrono::duration<double>(now - gTitleFadeOutStarted).count() /
        std::chrono::duration<double>(titleFadeDuration).count();
    return static_cast<unsigned>(std::lround((1.0 - smoothstep(std::clamp(t, 0.0, 1.0))) * 255));
}

void drawPlaybackLoadingScreen(HDC dc) {
    RECT client{}; GetClientRect(gWindow, &client);
    const int width = client.right - client.left, height = client.bottom - client.top;
    if (gResultBackdropValid && width == gResultBackdropWidth && height == gResultBackdropHeight)
        BitBlt(dc, 0, 0, width, height, gResultBackdropDc, 0, 0, SRCCOPY);
    else drawBrowseScreen(dc);
    if (gLoadResult.index < 0 || gLoadResult.index >= static_cast<int>(gCharts.size())) return;
    const auto& chart = gCharts[static_cast<size_t>(gLoadResult.index)];
    if (chart.titleImage.empty() || !gGdiPlusToken) return;
    if (dc == gBackBufferDc && gBackBufferPixels) {
        GdiFlush();
        gBgaRaster.canvas(width, height);
        gBgaRaster.draw(chart, L"__titlecard", static_cast<uint32_t*>(gBackBufferPixels),
            playbackTitleOpacity(std::chrono::steady_clock::now()));
    }
}

void captureBrowseBackdrop() {
    gResultBackdropValid = false;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    // The native loading fallback draws the current selection when there is no fresh capture.
    if (gLr2SelectActive && !gLr2ResultFile.empty()) return;
#endif
    RECT client{};
    if (!gWindow || !GetClientRect(gWindow, &client)) return;
    const int width = client.right, height = client.bottom;
    if (width <= 0 || height <= 0) return;
    HDC windowDc = GetDC(gWindow);
    if (!windowDc) return;
    if (!gResultBackdropDc) gResultBackdropDc = CreateCompatibleDC(windowDc);
    if (gResultBackdropBitmap && (width != gResultBackdropWidth || height != gResultBackdropHeight)) {
        SelectObject(gResultBackdropDc, gResultBackdropOriginalBitmap);
        DeleteObject(gResultBackdropBitmap);
        gResultBackdropBitmap = nullptr;
        gResultBackdropPixels = nullptr;
    }
    if (gResultBackdropDc && !gResultBackdropBitmap) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        gResultBackdropBitmap = CreateDIBSection(windowDc, &info, DIB_RGB_COLORS, &gResultBackdropPixels, nullptr, 0);
        if (gResultBackdropBitmap) {
            gResultBackdropOriginalBitmap = SelectObject(gResultBackdropDc, gResultBackdropBitmap);
            gResultBackdropWidth = width;
            gResultBackdropHeight = height;
        }
    }
    if (!gResultBackdropDc || !gResultBackdropBitmap || !gResultBackdropPixels) {
        ReleaseDC(gWindow, windowDc);
        return;
    }

    drawBrowseScreen(gResultBackdropDc);
    const std::array<HWND, 5> controls{gFolder, gInfo, gList, gStatus, gHiddenSongs};
    for (HWND control : controls) {
        if (!control || !IsWindowVisible(control)) continue;
        RECT bounds{}, controlClient{};
        if (!GetWindowRect(control, &bounds) || !GetClientRect(control, &controlClient)) continue;
        const int controlWidth = controlClient.right, controlHeight = controlClient.bottom;
        if (controlWidth <= 0 || controlHeight <= 0) continue;
        POINT origin{bounds.left, bounds.top};
        ScreenToClient(gWindow, &origin);
        HDC controlDc = CreateCompatibleDC(windowDc);
        HBITMAP controlBitmap = CreateCompatibleBitmap(windowDc, controlWidth, controlHeight);
        if (!controlDc || !controlBitmap) {
            if (controlBitmap) DeleteObject(controlBitmap);
            if (controlDc) DeleteDC(controlDc);
            continue;
        }
        HGDIOBJ oldBitmap = SelectObject(controlDc, controlBitmap);
        if (PrintWindow(control, controlDc, PW_CLIENTONLY))
            BitBlt(gResultBackdropDc, origin.x, origin.y, controlWidth, controlHeight, controlDc, 0, 0, SRCCOPY);
        SelectObject(controlDc, oldBitmap);
        DeleteObject(controlBitmap);
        DeleteDC(controlDc);
    }

    // A small downsample/upscale pass softens text and list edges without a costly per-pixel blur.
    const int blurWidth = std::max(1, width / 18), blurHeight = std::max(1, height / 18);
    HDC blurDc = CreateCompatibleDC(windowDc);
    HBITMAP blurBitmap = CreateCompatibleBitmap(windowDc, blurWidth, blurHeight);
    if (blurDc && blurBitmap) {
        HGDIOBJ oldBlur = SelectObject(blurDc, blurBitmap);
        SetStretchBltMode(blurDc, HALFTONE);
        SetBrushOrgEx(blurDc, 0, 0, nullptr);
        if (StretchBlt(blurDc, 0, 0, blurWidth, blurHeight, gResultBackdropDc, 0, 0, width, height, SRCCOPY)) {
            SetStretchBltMode(gResultBackdropDc, HALFTONE);
            SetBrushOrgEx(gResultBackdropDc, 0, 0, nullptr);
            StretchBlt(gResultBackdropDc, 0, 0, width, height, blurDc, 0, 0, blurWidth, blurHeight, SRCCOPY);
        }
        SelectObject(blurDc, oldBlur);
    }
    if (blurBitmap) DeleteObject(blurBitmap);
    if (blurDc) DeleteDC(blurDc);
    ReleaseDC(gWindow, windowDc);
    gResultBackdropValid = true;
}

void drawResultScreen(HDC dc) {
#ifdef BMS_ENABLE_SKIN_BINDINGS
    drawSkinOnly(dc);return;
#endif
    RECT client{}; GetClientRect(gWindow, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (gResultBackdropValid && width == gResultBackdropWidth && height == gResultBackdropHeight)
        BitBlt(dc, 0, 0, width, height, gResultBackdropDc, 0, 0, SRCCOPY);
    else {
        fillRect(dc, client, RGB(16, 20, 28));
        drawBrowseScreen(dc);
    }
    const auto* chart = gActiveChartIndex >= 0 && gActiveChartIndex < static_cast<int>(gCharts.size())
        ? &gCharts[static_cast<size_t>(gActiveChartIndex)] : nullptr;
    if (chart && !chart->finishImage.empty() && dc == gBackBufferDc && gBackBufferPixels) {
        GdiFlush();
        auto* pixels = static_cast<uint32_t*>(gBackBufferPixels);
        std::fill_n(pixels, static_cast<size_t>(width) * height, 0xff0a0e15u);
        gBgaRaster.canvas(width, height);
        gBgaRaster.draw(*chart, L"__finishcard", pixels);
    } else if (chart && !chart->finishImage.empty()) {
        Gdiplus::Bitmap image(chart->finishImage.c_str());
        if (image.GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics graphics(dc);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.DrawImage(&image, Gdiplus::Rect(0, 0, width, height));
        }
    }
    if (dc == gBackBufferDc && gBackBufferPixels) {
        GdiFlush();
        auto* pixels = static_cast<uint32_t*>(gBackBufferPixels);
        constexpr unsigned overlayAlpha = 118;
        constexpr unsigned baseAlpha = 255 - overlayAlpha;
        for (int i = 0, count = width * height; i < count; ++i) {
            const uint32_t source = pixels[i];
            const unsigned red = (((source >> 16) & 255) * baseAlpha + 10 * overlayAlpha) / 255;
            const unsigned green = (((source >> 8) & 255) * baseAlpha + 14 * overlayAlpha) / 255;
            const unsigned blue = ((source & 255) * baseAlpha + 21 * overlayAlpha) / 255;
            pixels[i] = 0xff000000u | (red << 16) | (green << 8) | blue;
        }
    } else {
        Gdiplus::Graphics graphics(dc);
        Gdiplus::SolidBrush overlay(Gdiplus::Color(118, 10, 14, 21));
        graphics.FillRectangle(&overlay, 0, 0, width, height);
    }

    const std::wstring title = chart ? (chart->title.empty() ? chart->path.stem().wstring() : chart->title) : L"RESULT";
    const std::wstring level = chart && !chart->playLevel.empty() ? chart->playLevel : L"--";
    const std::wstring artist = chart && !chart->artist.empty() ? chart->artist : L"--";
    const std::wstring bpm = chart ? number(chart->initialBpm, 1) : L"--";
    RECT titleRect{ ui(32), ui(44), width - ui(32), ui(104) };
    RECT levelRect{ ui(32), ui(105), width - ui(32), ui(145) };
    RECT detailsRect{ ui(32), ui(145), width - ui(32), ui(178) };
    RECT rankRect{ 0, height / 2 - ui(115), width, height / 2 + ui(15) };
    RECT scoreRect{ 0, height / 2 + ui(26), width, height / 2 + ui(95) };
    drawShadowedCenteredText(dc, title, titleRect, gResultTitleFont ? gResultTitleFont : gFont, RGB(245, 248, 252), false);
    drawShadowedCenteredText(dc, L"LV " + level, levelRect, gResultDetailsFont ? gResultDetailsFont : gFont, RGB(162, 213, 236), false);
    drawShadowedCenteredText(dc, L"BPM: " + bpm + L"    composer: " + artist, detailsRect, gResultDetailsFont ? gResultDetailsFont : gSmallFont, RGB(218, 228, 240), false);
    drawShadowedCenteredText(dc, std::wstring(gResultScore.rank()), rankRect, gResultRankFont ? gResultRankFont : gFont, RGB(255, 214, 20));
    drawShadowedCenteredText(dc, std::to_wstring(gResultScore.value()), scoreRect, gResultScoreFont ? gResultScoreFont : gFont, RGB(245, 248, 252));
    RECT clearRect{ 0, height / 2 + ui(98), width, height / 2 + ui(144) };
    const wchar_t* gaugeOutcome = gPractice.enabled ? L"PRACTICE" : gAutoplay ? L"AUTO PLAY" : gResultGaugeFailed ? L"FAILED" : gResultGaugeCleared ? L"CLEAR" : L"NO CLEAR";
    const COLORREF gaugeOutcomeColor = gPractice.enabled ? RGB(255, 255, 255) : gAutoplay ? RGB(56, 189, 248) : gResultGaugeFailed ? RGB(248, 113, 113) :
        gResultGaugeCleared ? RGB(74, 222, 128) : RGB(251, 191, 36);
    drawShadowedCenteredText(dc, gaugeOutcome, clearRect,
        gResultDetailsFont ? gResultDetailsFont : gFont, gaugeOutcomeColor, false);
#ifdef BMS_ENABLE_SKIN_BINDINGS
    drawAppliedSkinDc(dc);
#endif
}

struct PauseMenuLayout {
    RECT title{};
    RECT speedLine{};
    RECT guardLine{};
    RECT shortcuts{};
    std::array<RECT, 4> buttons{};
};

PauseMenuLayout pauseMenuLayout(int width, int height) {
    PauseMenuLayout layout{};
    layout.title = {ui(24), ui(16), width - ui(24), ui(96)};
    layout.speedLine = {ui(24), ui(104), width - ui(24), ui(134)};
    layout.guardLine = {ui(24), ui(138), width - ui(24), ui(168)};
    layout.shortcuts = {ui(24), std::max(0, height - ui(28)), width - ui(24), height - ui(8)};
    const int buttonWidth = std::min(std::clamp(width * 34 / 100, ui(280), ui(560)), std::max(1, width - ui(48)));
    const int buttonGap = std::max(ui(12), height * 2 / 100);
    const int left = (width - buttonWidth) / 2;
    const int top = std::min(std::max(ui(184), height * 18 / 100), std::max(ui(160), height / 2));
    const int availableHeight = std::max(0, (height - top - ui(32) - 3 * buttonGap) / 4);
    const int buttonHeight = std::max(ui(32), std::min(ui(144), availableHeight));
    for (size_t i = 0; i < layout.buttons.size(); ++i) {
        const int buttonTop = top + static_cast<int>(i) * (buttonHeight + buttonGap);
        layout.buttons[i] = {left, buttonTop, left + buttonWidth, buttonTop + buttonHeight};
    }
    return layout;
}

void drawPauseOverlay(HDC dc) {
    RECT client{}; GetClientRect(gWindow, &client);
    const PauseMenuLayout layout = pauseMenuLayout(client.right, client.bottom);
    const auto pauseText = [&](const std::wstring& text, RECT bounds, HFONT font, COLORREF color, bool leftAligned) {
        const auto render = leftAligned ? drawLeftTextBody : drawTextBody;
        const auto gpu = gGpuFrame ? TextRasterCache::GpuDraw(queueGpuText) : TextRasterCache::GpuDraw{};
        const bool drawn = gTextRaster.draw(gGpuFrame ? nullptr : static_cast<uint32_t*>(gBackBufferPixels),
            gBackBufferWidth, gBackBufferHeight, text, bounds, font, color, 1.0, render, false, gpu, leftAligned);
        if (!drawn && !gGpuFrame) render(dc, text, bounds, font, color);
    };
    const auto drawButton = [&](RECT button, size_t index, bool gpu) {
        const int border = index == gPauseSelection ? 8 : 6;
        const auto edge = [&](RECT rect, COLORREF color) {
            if (gpu) gCompositionPresenter.rectangle(static_cast<float>(rect.left), static_cast<float>(rect.top),
                static_cast<float>(rect.right), static_cast<float>(rect.bottom), color);
            else fillRect(dc, rect, color);
        };
        edge(button, RGB(0, 0, 0));
        if (index == gPauseSelection && button.right - button.left > border * 4 && button.bottom - button.top > border * 4) {
            const int inset = border + 2;
            edge({button.left + inset, button.top + inset, button.right - inset, button.top + inset + 2}, RGB(220, 220, 220));
            edge({button.left + inset, button.bottom - inset - 2, button.right - inset, button.bottom - inset}, RGB(220, 220, 220));
            edge({button.left + inset, button.top + inset, button.left + inset + 2, button.bottom - inset}, RGB(220, 220, 220));
            edge({button.right - inset - 2, button.top + inset, button.right - inset, button.bottom - inset}, RGB(220, 220, 220));
        }
        wchar_t label[32]{};
        if (gPauseButtons[index]) GetWindowTextW(gPauseButtons[index], label, static_cast<int>(std::size(label)));
        pauseText(label, button, gResultNextFont ? gResultNextFont : gFont, RGB(245, 250, 255), false);
    };
    if (gGpuFrame) {
        gCompositionPresenter.rectangle(0, 0, static_cast<float>(client.right), static_cast<float>(client.bottom), RGB(0, 0, 0), 0.48f);
        for (size_t i = 0; i < layout.buttons.size(); ++i) drawButton(layout.buttons[i], i, true);
    } else if (dc == gBackBufferDc && gBackBufferPixels) {
        auto* pixels = static_cast<uint32_t*>(gBackBufferPixels);
        const size_t count = static_cast<size_t>(gBackBufferWidth) * gBackBufferHeight;
        for (size_t i = 0; i < count; ++i) {
            const uint32_t p = pixels[i];
            pixels[i] = (p & 0xff000000u) |
                ((static_cast<uint32_t>((p >> 16 & 0xffu) * 0.52) & 0xffu) << 16) |
                ((static_cast<uint32_t>((p >> 8 & 0xffu) * 0.52) & 0xffu) << 8) |
                (static_cast<uint32_t>((p & 0xffu) * 0.52) & 0xffu);
        }
    } else {
        Gdiplus::Graphics graphics(dc);
        Gdiplus::SolidBrush overlay(Gdiplus::Color(122, 0, 0, 0));
        graphics.FillRectangle(&overlay, Gdiplus::Rect(client.left, client.top,
            client.right - client.left, client.bottom - client.top));
    }
    pauseText(L"MENU", layout.title, gBrowseTitleFont ? gBrowseTitleFont : gResultTitleFont, RGB(245, 248, 252), true);
    pauseText(L"F1 / F2     노트 배속: " + number(gNoteSpeed, 2) + L"x", layout.speedLine, gFont, RGB(235, 240, 246), true);
    pauseText(L"PageUp / PageDown     레인 가드: " + std::to_wstring(gLaneCoverPercent) + L"%", layout.guardLine, gFont, RGB(235, 240, 246), true);
    pauseText(L"ARROWS: SELECT   ENTER: OK   ESC: RESUME   F5/R: RESTART   Q: QUIT", layout.shortcuts, gSmallFont, RGB(220, 226, 232), true);
    if (!gGpuFrame) for (size_t i = 0; i < layout.buttons.size(); ++i) drawButton(layout.buttons[i], i, false);
}
