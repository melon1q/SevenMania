void drawGameScreen(HDC dc) {
    gCompositionPresenter.setFrameIntro(nullptr);
    HUD_SCOPE(GamePaint);
    BMS_TRACE_ZONE("Game render CPU and GPU submission");
    RECT client{}; GetClientRect(gWindow, &client);
    ++gFpsFrameCount;
    const auto fpsNow = std::chrono::steady_clock::now();
    const double fpsElapsed = std::chrono::duration<double>(fpsNow - gFpsEpoch).count();
    if (fpsElapsed >= 1.0) {
        gMeasuredFps = static_cast<unsigned>(std::lround(gFpsFrameCount / fpsElapsed));
        gFpsFrameCount = 0;
        gFpsEpoch = fpsNow;
    }
    const int selected = gActiveChartIndex;
    if (selected < 0 || selected >= static_cast<int>(gCharts.size())) {
        fillRect(dc, client, RGB(10, 14, 21));
        return;
    }
    const auto& chart = gCharts[static_cast<size_t>(selected)];
    const int laneWidth=bms::chartDeckWidths(chart,gUiScale).key;
    const int scratchWidth=bms::chartDeckWidths(chart,gUiScale).scratch;
    const auto gearLayout=bms::chartLayout(chart,0,laneWidth,scratchWidth);
    const int gearWidth = gearLayout.totalWidth();
    const int edgeInset = std::min(ui(64), std::max(0, (gWindowWidth - gearWidth) / 2));
    const int available = std::max(0, gWindowWidth - gearWidth);
    int fieldX = gGearPosition == 0 ? edgeInset : gGearPosition == 2 ? std::max(edgeInset, available - edgeInset) : available / 2;
    struct RenderLayout : LaneLayout {
        std::array<std::pair<int,int>,16> edges{};
        bool authored{};
        int extent{};
        int left(int lane) const {return authored&&lane>=0&&lane<16?edges[lane].first:LaneLayout::left(lane);}
        int right(int lane) const {return authored&&lane>=0&&lane<16?edges[lane].second:LaneLayout::right(lane);}
        int width(int lane) const {return right(lane)-left(lane);}
        int totalWidth() const {return authored?extent:LaneLayout::totalWidth();}
    } layout{bms::chartLayout(chart,fieldX,laneWidth,scratchWidth)};
    bool lr2Play=false;
    int normalJudgeY=judgementLineY(gWindowHeight);
#ifdef BMS_ENABLE_SKIN_BINDINGS
    const auto lr2Lanes=gGpuFrame&&gLr2PlayScene&&!gLr2GpuFailed?
        gLr2PlayScene->playLanes(gWindowWidth,gWindowHeight):std::nullopt;
    if(lr2Lanes) {
        lr2Play=layout.authored=true;
        fieldX=gWindowWidth;
        int right=0;
        normalJudgeY=(*lr2Lanes)[0].noteBottom;
        for(std::size_t lane=0;lane<lr2Lanes->size();++lane) {
            const auto& edge=(*lr2Lanes)[lane];
            layout.edges[lane]={edge.left,edge.right};
            fieldX=std::min(fieldX,edge.left);
            right=std::max(right,edge.right);
        }
        layout.extent=right-fieldX;
        gCompositionPresenter.setFrameOverlay([](ID2D1RenderTarget* target,int width,int height) {
            if(gLr2PlayScene)gLr2PlayScene->draw(static_cast<ID2D1DeviceContext*>(target),gLr2Play.frame.playAge+gLr2Play.sceneLead,
                width,height,&gLr2Play.frame,sevenmania::skin::lr2::DrawPass::BeforeNotes);
        });
        gCompositionPresenter.setFrameForeground([](ID2D1RenderTarget* target,int width,int height) {
            if(gLr2PlayScene)gLr2PlayScene->draw(static_cast<ID2D1DeviceContext*>(target),gLr2Play.frame.playAge+gLr2Play.sceneLead,
                width,height,&gLr2Play.frame,sevenmania::skin::lr2::DrawPass::AfterNotes);
        });
    }
#endif
    const int top = PLAYFIELD_TOP;
    const int judgeY=bms::optionJudgeLine(gChartOptions,gPlayhead,normalJudgeY,gWindowHeight);
    const bool reverse=gChartOptions.crazy.contains(L"reverse");
    const auto noteY=[&](double distance){return judgeY+(reverse?distance:-distance);};
    const int fieldBottom=reverse?gWindowHeight:judgeY+ui(38);
    if(lr2Play)fillRect(dc,client,RGB(0,0,0));
    else { BMS_PERF_SCOPE(bgaMs); HUD_SCOPE(Backdrop);drawBgaBackplate(dc, chart, client, fieldX, laneWidth, top, judgeY, scratchWidth); }
    if (!lr2Play && dc == gBackBufferDc && gBackBufferPixels) {
        BMS_PERF_SCOPE(beamsMs);
        GdiFlush();
        gKeyBeams.draw(static_cast<uint32_t*>(gBackBufferPixels), gBackBufferWidth, gBackBufferHeight, fieldX, laneWidth, judgeY, gPlayhead, scratchWidth);
    }
    const bms::TempoEvent* tempo = chart.tempos.empty() ? nullptr : &chart.tempos.front();
    for (const auto& item : chart.tempos) { if (item.timeSeconds <= gPlayhead) tempo = &item; else break; }
    {
    BMS_PERF_SCOPE(hudMs);
    for (int lane = 0; !lr2Play && !gChartOptions.crazy.contains(L"dark") && !gChartOptions.crazy.contains(L"dark2") && lane < layout.laneCount(); ++lane) {
        const int index = layout.separatorIndex(lane);
        if (index < 0 || !gLaneSeparators[static_cast<size_t>(index)]) continue;
        const int x = layout.left(lane);
        const RECT separator{ x, 0, x + 1, gWindowHeight };
        drawLaneSeparator(dc,separator,judgeY);
    }
    RECT judge{ fieldX, judgeY, fieldX + layout.totalWidth(), judgeY + 3 };
    if(!lr2Play)fillRect(dc, judge, RGB(248, 250, 252));
    }

    // Scroll speed is FHD pixels per second scaled by the window height (the lane length), so the note
    // travel time matches FHD at every resolution, including 5:4/4:3 where gUiScale is width-limited.
    const double pixelsPerSecond = 270.0 * bms::ui::scrollScale(gWindowHeight) * (gChartOptions.hiSpeed>0?gChartOptions.hiSpeed:gNoteSpeed) *
        playHiSpeedScale(chart) * (gHiSpeedFix==bms::HiSpeedFix::Constant?1.0:bms::speedAt(chart,gPlayhead));
    const double scrollPosition = playScrollPosition(chart, bms::drawSeconds(gPlayhead, gTimingOffsets, playbackTimingScale()));
    const double visibleBehind = 42.0 * gUiScale / std::max(0.001,std::abs(pixelsPerSecond));
    const double visibleAhead = (std::max(judgeY-top,fieldBottom-judgeY) + gNoteHeight) / std::max(0.001,std::abs(pixelsPerSecond));
    const double firstVisiblePosition = scrollPosition - visibleBehind - 0.01;
    const double lastVisiblePosition = scrollPosition + visibleAhead;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(lr2Play) {
        auto& state=gLr2Play;
        auto& frame=state.frame;
        const auto age=[](double since){return since<0?-1.:std::max(0.,(gPlayhead-since)*1000);};
        frame.elapsed=std::max(0.,gPlayhead-state.start);
        frame.remaining=std::max(0.,chart.durationSeconds-gPlayhead);
        frame.playAge=frame.elapsed*1000;
        frame.readyAge=frame.playAge+state.readyLead;
        frame.loaded=true;frame.loadProgress=1.f;
        const bool hideJudge=gChartOptions.crazy.contains(L"hidjudge");
        frame.judge=hideJudge?-1:state.judge;
        frame.judgementAge=frame.judge<0?-1:age(state.judgeAt);
        frame.judge2P=hideJudge?-1:state.judge2P;
        frame.judgementAge2P=frame.judge2P<0?-1:age(state.judgeAt2P);
        for(std::size_t lane=0;lane<state.keyOn.size();++lane) {
            const int mapped=sevenmania::skin::lr2::lr2Lane(state.type,static_cast<int>(lane));
            if(mapped<0)continue;
            const auto skin=static_cast<std::size_t>(mapped);
            frame.keyOn[skin]=gPressed[lane]?age(state.keyOn[lane]):-1;
            frame.keyOff[skin]=gPressed[lane]?-1:age(state.keyOff[lane]);
            frame.bomb[skin]=age(state.bomb[lane]);
            frame.hold[skin]=gHolds[lane].active()?std::max(0.,(judgePlayhead()-gHolds[lane].startSeconds())*1000):-1;
        }
        frame.combo=static_cast<int>(gFeedback.combo());
        frame.maxCombo=static_cast<int>(gFeedback.maxCombo());
        for(std::size_t grade=0;grade<frame.judgementCounts.size();++grade)
            frame.judgementCounts[grade]=state.counts[grade]/2;
        frame.noteCount=state.noteCount;
        frame.exScore=gResultScore.exScore();
        frame.judgedNotes=0;
        for(const int units:state.counts)frame.judgedNotes+=units/2;
        frame.bestExScore=state.bestExScore;
        frame.targetExScore=state.bestExScore.value_or(0);
        frame.scoreGraph=state.scoreGraph;
        frame.autoplay=gAutoplay; // Options 32/33 and the skin's SRC_AUTO_* notes.
        gLr2PlayScene->setAutoplay(frame.autoplay);
        // Turntables turn steadily, a turn every two seconds, and three times as fast while scratched.
        if(state.turntableAt>=0&&gPlayhead>state.turntableAt)
            for(std::size_t side=0;side<state.turntableAngle.size();++side) {
                const bool scratched=frame.keyOn[side*10]>=0;
                state.turntableAngle[side]=static_cast<float>(std::fmod(state.turntableAngle[side]+
                    (gPlayhead-state.turntableAt)*180.*(scratched?3:1),360.));
            }
        state.turntableAt=gPlayhead;
        frame.turntableAngle=state.turntableAngle;
        if(state.fullComboAt<0&&frame.noteCount>0&&frame.judgedNotes>=frame.noteCount&&
           frame.judgementCounts[3]+frame.judgementCounts[4]==0)state.fullComboAt=gPlayhead;
        frame.fullComboAge=age(state.fullComboAt);
        frame.failedAge=frame.fadeoutAge=-1;
        if(state.endingClockPending) {state.endingAt=std::chrono::steady_clock::now();state.endingClockPending=false;}
        if(state.endingAt!=std::chrono::steady_clock::time_point{})
            (state.endingFailed?frame.failedAge:frame.fadeoutAge)=
                std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-state.endingAt).count();
        frame.points=gResultScore.value()/5;
        frame.bpm=tempo?tempo->bpm:chart.initialBpm;
        // The beat phase follows the current tempo only; it is not re-anchored at tempo changes.
        const double beat=frame.bpm>0?60000./frame.bpm:0;
        frame.rhythmAge=beat>0?std::fmod(frame.playAge,beat):-1;
        frame.minBpm=state.minBpm;
        frame.maxBpm=state.maxBpm;
        frame.hiSpeed=gChartOptions.hiSpeed>0?gChartOptions.hiSpeed:gNoteSpeed;
        frame.laneCover=std::max(gLaneCoverPercent,gChartOptions.hidden==L"m-sudden"?gAuthoredCoverPercent:0);
        if(state.allJudgedAt<0&&frame.noteCount>0&&frame.judgedNotes>=frame.noteCount&&
           std::none_of(std::begin(gHolds),std::end(gHolds),[](const auto& hold){return hold.active();}))
            state.allJudgedAt=gPlayhead;
        frame.laneCoverShare=lr2LaneCoverShare(frame.laneCover);
        frame.gauge=gGrooveGauge.value();
        if(state.previousGauge>=0&&frame.gauge>state.previousGauge)state.gaugeUpAt=gPlayhead;
        if(frame.gauge>=100&&state.previousGauge<100)state.gaugeFullAt=gPlayhead;
        if(frame.gauge<100)state.gaugeFullAt=-1;
        state.previousGauge=frame.gauge;
        frame.gaugeIncreaseAge=age(state.gaugeUpAt);
        frame.gaugeMaxAge=age(state.gaugeFullAt);
        frame.hardGauge=gGrooveGaugeMode==GrooveGaugeMode::Hard;
        frame.judgeLineOffset=float(judgeY-normalJudgeY);
        frame.hasLongNotes=state.hasLongNotes;
        frame.difficulty=chart.difficulty;
        frame.keys=sevenmania::skin::lr2::playKeys(state.type);
        frame.hasBga=!chart.bga.empty()||!chart.fallbackVideo.empty();
        frame.bgaEnabled=gBgaBrightness>0;
        frame.bga=nullptr;
        if(frame.hasBga&&frame.bgaEnabled&&gLr2PlayScene->hasBga()) {
            const auto canvas=playBgaCanvas();
            std::uint64_t revision=0;
            drawBgaBackplate(dc,chart,canvas,fieldX,laneWidth,top,judgeY,scratchWidth,&revision);
            if(revision)frame.bga=gLr2PlayScene->updateBga(static_cast<const uint32_t*>(gBgaBackplatePixels),
                canvas.right,canvas.bottom,revision);
        }
        state.visibleMeasures.clear();
        const auto& positions=state.measurePositions;
        for(auto bar=std::lower_bound(positions.begin(),positions.end(),firstVisiblePosition);
            bar!=positions.end()&&*bar<=lastVisiblePosition;++bar) {
            const double offset=noteY((*bar-scrollPosition)*pixelsPerSecond)-judgeY;
            if(judgeY+offset>=top&&judgeY+offset<=(reverse?fieldBottom:judgeY))
                state.visibleMeasures.push_back(static_cast<float>(offset));
        }
        frame.measureOffsets=state.visibleMeasures;
    }
#endif
    const bool fullScan=chart.nonMonotonicScroll || pixelsPerSecond<=0 || gChartOptions.speedEffect!=L"off";
    const auto firstNote = fullScan?chart.notes.begin():std::lower_bound(chart.notes.begin(), chart.notes.end(), firstVisiblePosition,
        [&](const bms::Note& note, double position) { return playScrollPosition(chart, note.timeSeconds) < position; });
    const RECT noteBounds{ fieldX, top, fieldX + layout.totalWidth(), fieldBottom };
    const bool directRaster = dc == gBackBufferDc && gBackBufferPixels;
    gGpuNotePass = gGpuFrame && directRaster && (!hybridRendererEnabled || lr2Play);
    int noteClip = 0;
    if (directRaster) { BMS_PERF_SCOPE(syncMs);HUD_SCOPE(GdiSync); GdiFlush(); }
    else {
        noteClip = SaveDC(dc);
        IntersectClipRect(dc, noteBounds.left, noteBounds.top, noteBounds.right, noteBounds.bottom);
    }
    {
    BMS_PERF_SCOPE(notesMs);
    size_t visibleNotes = 0;
    const bool useNoteImages = !gNoteImages.empty();
    const bool star = gChartOptions.crazy.contains(L"star");
    const bool dark = gChartOptions.crazy.contains(L"dark2");
#ifdef BMS_ENABLE_SKIN_BINDINGS
    using sevenmania::skin::lr2::PlayNotePart;
    const auto lr2Note=[&](int lane,PlayNotePart part,int x,double y,int right,double bottom,
                           const RECT& bounds,float opacity,double heldMs=-1) {
        if(!lr2Play||star||right<=x||bottom<=y)return false;
        const auto sprite=gLr2PlayScene->noteSprite(lane,std::max(0.,gPlayhead*1000),part,heldMs);
        return sprite.bitmap&&gCompositionPresenter.bitmapSprite(sprite.bitmap,
            D2D1::RectF(float(x),float(y),float(right),float(bottom)),opacity,&bounds,
            D2D1::RectF(sprite.x,sprite.y,sprite.x+sprite.width,sprite.y+sprite.height));
    };
#endif
    const auto drawLongNote = [&](int lane,int x, int right, double headY, double tailY, int height,
        double fallbackTop, double fallbackBottom, COLORREF color, bool textured, bool scratch, int colorIndex,
        float opacity, const LongNoteHold* hold = nullptr) {
        const bool held=hold!=nullptr;
        RECT clip=noteBounds;
#ifdef BMS_ENABLE_SKIN_BINDINGS
        if(lr2Lanes&&lane>=0&&lane<16) {
            const auto& geometry=(*lr2Lanes)[lane];
            height=geometry.noteHeight;
            const auto shift=geometry.noteBottom-normalJudgeY-height;
            headY+=shift;tailY+=shift;fallbackTop+=shift;fallbackBottom+=shift;
            if(!reverse)clip.bottom=std::min(clip.bottom,static_cast<LONG>(judgeY+geometry.noteBottom-normalJudgeY));
        }
#else
        (void)lane;
#endif
        // While held, the long note uses its held images (they fall back to the normal ones).
        const auto bodyTick = static_cast<long long>(std::floor(gPlayhead * gNoteImages.bodyFps(held)));
        const auto image = [&](NotePart part) {
            if (held) part = part == NotePart::LongBody ? NotePart::HeldBody : part == NotePart::LongEnd ? NotePart::HeldEnd : NotePart::HeldStart;
            return textured ? gNoteImages.find(part, scratch, colorIndex, bodyTick) : nullptr;
        };
        const auto drawPart=[&](NotePart part,double y,double bottom) {
#ifdef BMS_ENABLE_SKIN_BINDINGS
            const auto lr2Part=part==NotePart::LongBody?(held?PlayNotePart::HeldBody:PlayNotePart::Body):
                part==NotePart::LongEnd?PlayNotePart::End:PlayNotePart::Start;
            const double heldMs=held?std::max(0.,(judgePlayhead()-hold->startSeconds())*1000):-1;
            if(lr2Note(lane,lr2Part,x,y,right,bottom,clip,opacity,heldMs))return true;
#endif
            return drawNoteImage(dc,image(part),x,y,right,bottom,clip,opacity);
        };
        if (!drawPart(NotePart::LongBody,std::min(headY,tailY),std::max(headY,tailY))) {
            fillGameRectSubpixel(dc, x, fallbackTop, right, fallbackBottom,
                RGB(GetRValue(color) / 2, GetGValue(color) / 2, GetBValue(color) / 2), clip);
        }
        if ((textured||lr2Play) && tailY != headY) {
            const double tailTop = tailY < headY ? tailY : std::max(headY, tailY - height);
            drawPart(NotePart::LongEnd,tailTop,tailTop+height);
        }
        if (drawPart(NotePart::LongStart,headY,headY+height))return;
        if (held) fillGameRect(dc, {x, static_cast<LONG>(headY), right, static_cast<LONG>(headY) + height}, color, clip);
        else fillGameRectSubpixel(dc, x, headY, right, headY + height, color, clip);
    };
    auto drawNote = [&](size_t noteIndex) {
        const auto& note = chart.notes[noteIndex];
        const double notePosition = playScrollPosition(chart, note.timeSeconds);
        double distance=(notePosition-scrollPosition)*pixelsPerSecond;
        distance*=bms::optionVisualSpeed(gChartOptions,note,gPlayhead,gFeedback.combo(),distance);
        const auto visual=bms::optionNoteVisual(gChartOptions,note,gPlayhead,distance);distance=visual.distance;
        const double visibility=bms::optionVisibility(gChartOptions,distance,gPlayhead);
        int x=layout.left(note.lane),right=layout.right(note.lane);
        const int shift=static_cast<int>(visual.shift*layout.width(note.lane));x+=shift;right+=shift;
        const int inset=static_cast<int>((right-x)*(1-visual.scale)/2);x+=inset;right-=inset;
        int baseNoteHeight=gNoteHeight;
        double headY=noteY(distance);
        RECT tapBounds=noteBounds;
#ifdef BMS_ENABLE_SKIN_BINDINGS
        if(lr2Lanes&&!note.longNote()&&note.lane>=0&&note.lane<8) {
            const auto& lane=(*lr2Lanes)[note.lane];
            baseNoteHeight=lane.noteHeight;
            headY+=lane.noteBottom-normalJudgeY-lane.noteHeight;
            if(!reverse)tapBounds.bottom=std::min(tapBounds.bottom,
                static_cast<LONG>(judgeY+lane.noteBottom-normalJudgeY));
        }
#endif
        const int noteHeight=std::max(1,static_cast<int>(baseNoteHeight*visual.scale));
        double tailY=headY;
        if(note.longNote())tailY=noteY((playScrollPosition(chart,note.timeSeconds+note.lengthSeconds)-scrollPosition)*pixelsPerSecond);
        const double drawTop=note.longNote()?std::min(headY,tailY):headY;
        const double drawBottom=note.longNote()?std::max(headY+noteHeight,tailY):headY+noteHeight;
        if(drawBottom<top || drawTop>fieldBottom)return false;
        if(visibility<=0)return true;
        const int colorIndex = gLaneColors[static_cast<size_t>(layout.colorSlot(note.lane))];
        COLORREF color = gPalette[colorIndex].color;
        if(star)color=RGB(static_cast<int>((std::sin(gPlayhead*12)+1)*127),static_cast<int>((std::sin(gPlayhead*12+2)+1)*127),static_cast<int>((std::sin(gPlayhead*12+4)+1)*127));
        if(gChartOptions.crazy.contains(L"clone") || gChartOptions.crazy.contains(L"clone2") || gChartOptions.crazy.contains(L"s-clone")) {
            const auto shadow=RGB(GetRValue(color)/3,GetGValue(color)/3,GetBValue(color)/3);
            const int copies=gChartOptions.crazy.contains(L"s-clone")?6:gChartOptions.crazy.contains(L"clone2")?3:1;
            for(int copy=0;copy<copies;++copy){const int dx=copy==0?0:(copy%2?1:-1)*(right-x)/2;const double y=headY-(reverse?-1:1)*(24+copy*12)*gUiScale;fillGameRectSubpixel(dc,x+dx,y,right+dx,y+noteHeight,shadow,noteBounds);}
        }
        const double intensity = visibility * (dark ? 0.5 : 1.0);
        if(dark || visibility<1)color=RGB(GetRValue(color)*intensity,GetGValue(color)*intensity,GetBValue(color)*intensity);
        const bool textured = useNoteImages && !star;
        const bool scratch = textured && bms::isScratchLane(chart, note.lane);
        const float opacity = static_cast<float>(std::clamp(intensity, 0.0, 1.0));
#ifdef BMS_ENABLE_SKIN_BINDINGS
        if(!note.longNote()&&!gChartOptions.crazy.contains(L"spin")&&
            lr2Note(note.lane,PlayNotePart::Tap,x,headY,right,headY+noteHeight,tapBounds,opacity))return true;
#endif
        if(note.longNote()) {
            drawLongNote(note.lane,x, right, headY, tailY, noteHeight, drawTop, drawBottom, color, textured, scratch, colorIndex, opacity);
        } else if(gChartOptions.crazy.contains(L"spin")) {
            const auto& rotated=noteEffects::rotated(right-x,noteHeight,gPlayhead,color);
            const int left=x+(right-x-rotated.size)/2,topY=static_cast<int>(std::lround(headY))+(noteHeight-rotated.size)/2;
            if(gGpuFrame)gCompositionPresenter.sprite(rotated.pixels.data(),rotated.size,rotated.size,left,topY,rotated.size,rotated.size,rotated.id,1,true,&noteBounds);
            else if(directRaster)noteEffects::draw(static_cast<uint32_t*>(gBackBufferPixels),gBackBufferWidth,gBackBufferHeight,rotated,left,topY,noteBounds);
        } else if (!textured || !drawNoteImage(dc, gNoteImages.find(NotePart::Tap, scratch, colorIndex),
            x, headY, right, headY + noteHeight, tapBounds, opacity)) {
            fillGameRectSubpixel(dc,x,headY,right,headY+noteHeight,color,tapBounds);
        }
        return true;
    };
    for (auto it = firstNote; it != chart.notes.end(); ++it) {
        const double notePosition = playScrollPosition(chart, it->timeSeconds);
        if (!fullScan && notePosition > lastVisiblePosition) break;
        const size_t i = static_cast<size_t>(it - chart.notes.begin());
        if (gHit[i]) continue;
        ++visibleNotes;
        drawNote(i);
    }
    if (!fullScan) {
        if (gLongNoteIndex.data != chart.notes.data() || gLongNoteIndex.size != chart.notes.size()) {
            gLongNoteIndex = {chart.notes.data(), chart.notes.size(), {}};
            for (size_t i = 0; i < chart.notes.size(); ++i) if (chart.notes[i].longNote()) gLongNoteIndex.indices.push_back(i);
        }
        const size_t firstIndex = static_cast<size_t>(firstNote - chart.notes.begin());
        for (const size_t i : gLongNoteIndex.indices) {
            if (i >= firstIndex) break;
            if (gHit[i]) continue;
            const auto& note = chart.notes[i];
            if (playScrollPosition(chart, note.timeSeconds + note.lengthSeconds) < firstVisiblePosition) continue;
            ++visibleNotes;
            drawNote(i);
        }
    }
    // Missed or released long notes remain visible until their body has scrolled
    // out of the playfield; their judgement and score are already finalized.
    for(auto it=gLingeringLongNotes.begin();it!=gLingeringLongNotes.end();) {
        if(*it>=chart.notes.size() || !drawNote(*it))it=gLingeringLongNotes.erase(it);
        else {++visibleNotes;++it;}
    }
    for(size_t i=0;i<chart.mines.size();++i) {
        const auto& mine=chart.mines[i];if(i<gMineHit.size() && gMineHit[i])continue;
        double y=noteY((playScrollPosition(chart,mine.timeSeconds)-scrollPosition)*pixelsPerSecond);
        int height=gNoteHeight;
        RECT clip=noteBounds;
#ifdef BMS_ENABLE_SKIN_BINDINGS
        if(lr2Lanes&&mine.lane>=0&&mine.lane<8) {
            const auto& geometry=(*lr2Lanes)[mine.lane];
            height=geometry.noteHeight;
            y+=geometry.noteBottom-normalJudgeY-height;
            if(!reverse)clip.bottom=std::min(clip.bottom,static_cast<LONG>(judgeY+geometry.noteBottom-normalJudgeY));
        }
#endif
        if(y+height<top || y>fieldBottom)continue;
        const int left=layout.left(mine.lane),right=layout.right(mine.lane);
#ifdef BMS_ENABLE_SKIN_BINDINGS
        if(lr2Note(mine.lane,PlayNotePart::Mine,left,y,right,y+height,clip,1))continue;
#endif
        if (!useNoteImages || !drawNoteImage(dc, gNoteImages.find(NotePart::Mine, false),
            left,y,right,y+height,clip)) {
            fillGameRectSubpixel(dc,left,y,right,y+height,RGB(255,48,96),clip);
        }
    }
    // Held notes must be drawn independently of the start-time visibility range:
    // their head can be many seconds in the past while the tail is still above us.
    for (size_t lane = 0; lane < gHolds.size(); ++lane) {
        const auto& hold = gHolds[lane];
        if (!hold.active()) continue;
        const auto& note = chart.notes[hold.noteIndex()];
        const int x = layout.left(note.lane);
        const int right = layout.right(note.lane);
        const double tailY=noteY((playScrollPosition(chart,note.timeSeconds+note.lengthSeconds)-scrollPosition)*pixelsPerSecond);
        const double clippedTail=std::clamp(tailY,static_cast<double>(reverse?judgeY:top),static_cast<double>(reverse?fieldBottom:judgeY));
        const int colorIndex = gLaneColors[static_cast<size_t>(layout.colorSlot(note.lane))];
        const COLORREF color = gPalette[colorIndex].color;
        const double imageTail = reverse ? std::max(tailY, double(judgeY)) : std::min(tailY, double(judgeY));
        drawLongNote(note.lane,x, right, judgeY, imageTail, gNoteHeight, std::min(clippedTail, double(judgeY)),
            std::max(clippedTail, double(judgeY)), color, useNoteImages, useNoteImages && bms::isScratchLane(chart, note.lane), colorIndex, 1.0f, &hold);
    }
    // A cleanly released long note keeps its remaining body, inactive, until the tail reaches the line.
    for(auto it=gReleasedLongNotes.begin();it!=gReleasedLongNotes.end();) {
        if(*it>=chart.notes.size()){it=gReleasedLongNotes.erase(it);continue;}
        const auto& note=chart.notes[*it];
        const double tailY=noteY((playScrollPosition(chart,note.timeSeconds+note.lengthSeconds)-scrollPosition)*pixelsPerSecond);
        if(reverse?tailY<=judgeY:tailY>=judgeY){it=gReleasedLongNotes.erase(it);continue;}
        const int colorIndex=gLaneColors[static_cast<size_t>(layout.colorSlot(note.lane))];
        const double clippedTail=std::clamp(tailY,static_cast<double>(reverse?judgeY:top),static_cast<double>(reverse?fieldBottom:judgeY));
        drawLongNote(note.lane,layout.left(note.lane),layout.right(note.lane),judgeY,tailY,gNoteHeight,std::min(clippedTail,double(judgeY)),std::max(clippedTail,double(judgeY)),
            gPalette[colorIndex].color,useNoteImages,useNoteImages&&bms::isScratchLane(chart,note.lane),colorIndex,1.0f);
        ++it;
    }
    if (noteClip) RestoreDC(dc, noteClip);
    gGpuNotePass = false;
    BMS_PERF_SET(visibleNotes, visibleNotes);
    }
    {
    BMS_PERF_SCOPE(judgementMs);
    const int coverSetting=std::max(gLaneCoverPercent,gChartOptions.hidden==L"m-sudden"?gAuthoredCoverPercent:0);
    double coverShare=coverSetting/100.;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    // An LR2 skin with its own cover slider draws it; otherwise the cover also closes during the LR2 ending.
    if(gLr2PlayScene&&!gLr2GpuFailed)coverShare=gLr2PlayScene->hasLaneCover()?0.:lr2LaneCoverShare(coverSetting);
#endif
    if (coverShare > 0) {
        const int coverBottom = static_cast<int>(judgeY * coverShare);
        const RECT coverRect{ fieldX, 0, fieldX + layout.totalWidth(), coverBottom };
        fillGameRect(dc, coverRect, RGB(12, 12, 12), client);
        if (gGpuFrame) gCompositionPresenter.rectangle(static_cast<float>(coverRect.left), 0,
            static_cast<float>(coverRect.right), static_cast<float>(coverRect.bottom), RGB(12, 12, 12));
    }
    gScratchDisc.update(gPlayhead, gPressed[0]);
    // Non-7K modes: when the skin provides the gear, its 7K button textures are the deck; the plain engine deck would cover them.
    bool skinGearDeck=false, externalGearDeck=false;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    skinGearDeck=skinGearForMode(bms::gearModeIndex(chart.mode));
    externalGearDeck=skinGearDeck&&gSkinApplication.hasExternalGear();
#endif
    const bool plainDeck=(chart.mode!=bms::ChartMode::SevenKey && chart.mode!=bms::ChartMode::Unknown)&&!skinGearDeck;
    if (!lr2Play && directRaster && !externalGearDeck && !gChartOptions.crazy.contains(L"dark2")) {
        BMS_PERF_SCOPE(deckMs);HUD_SCOPE(LegacyDeck);
        if(plainDeck) {
            for(int lane=0;lane<layout.laneCount();++lane) {
                if(layout.width(lane)<=0)continue;
                RECT key{layout.left(lane)+ui(2),judgeY+ui(8),layout.right(lane)-ui(2),std::min(gWindowHeight,judgeY+ui(75))};
                // Buttons take the lane's note color (dim when idle, bright when pressed) so scratch,
                // white/black keys and PMS buttons stay distinguishable without the 7K skin deck.
                const COLORREF base=gPalette[gLaneColors[static_cast<size_t>(layout.colorSlot(lane))]].color;
                const auto scale=[&](int percent){return RGB(std::min(255,GetRValue(base)*percent/100),std::min(255,GetGValue(base)*percent/100),std::min(255,GetBValue(base)*percent/100));};
                fillRect(dc,key,gPressed[static_cast<size_t>(lane)]?scale(135):scale(45));
            }
        } else if (chart.mode!=bms::ChartMode::SevenKey && chart.mode!=bms::ChartMode::Unknown) {
            // skin gear draws the buttons
        } else if (std::abs(gUiScale - 1.0) > 1e-6) {
            const auto design=bms::chartDeckWidths(chart);
            KeyDeck::drawScaled(static_cast<uint32_t*>(gBackBufferPixels), gBackBufferWidth, gBackBufferHeight, fieldX, laneWidth, judgeY, primaryPressed(), gScratchDisc.angle(), scratchWidth, design.key, design.scratch, gUiScale, &gTurntableImage, &gBlackKeyImage, &gWhiteKeyImage);
        } else if (hybridRendererEnabled && gGpuFrame)
            KeyDeck::drawCached(static_cast<uint32_t*>(gBackBufferPixels), gBackBufferWidth, gBackBufferHeight, fieldX, laneWidth, judgeY, primaryPressed(), gScratchDisc.angle(), scratchWidth, &gTurntableImage, &gBlackKeyImage, &gWhiteKeyImage);
        else KeyDeck::draw(static_cast<uint32_t*>(gBackBufferPixels), gBackBufferWidth, gBackBufferHeight, fieldX, laneWidth, judgeY, primaryPressed(), gScratchDisc.angle(), scratchWidth, &gTurntableImage, &gBlackKeyImage, &gWhiteKeyImage);
    }
    {
    BMS_PERF_SCOPE(feedbackMs);
    if (gGpuFrame && !lr2Play) {
        const int left = std::max(0, fieldX), deckTop = std::clamp(judgeY + ui(4), 0, gWindowHeight);
        GdiFlush();
        if (!externalGearDeck && (!hybridRendererEnabled || plainDeck)) gCompositionPresenter.deck(static_cast<const uint32_t*>(gBackBufferPixels) + static_cast<size_t>(deckTop) * gBackBufferWidth + left,
            std::max(0, std::min(gWindowWidth, fieldX + gearWidth) - left), std::min(ui(108), gWindowHeight - deckTop), gBackBufferWidth, left, deckTop);
        gFeedback.drawSprites(fieldX, laneWidth, judgeY, gPlayhead, scratchWidth,
            [](const uint32_t* pixels, int width, int height, int left, int top, int dw, int dh, uint64_t id, float opacity) {
                gCompositionPresenter.sprite(pixels, width, height, left, top, dw, dh, id, opacity, false);
            });
    } else if (!lr2Play && directRaster) gFeedback.draw(static_cast<uint32_t*>(gBackBufferPixels), gBackBufferWidth, gBackBufferHeight, fieldX, laneWidth, judgeY, gPlayhead, scratchWidth);
    }
    if(!lr2Play) {
    bool skinGuides=false;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    skinGuides=skinDrawsKeyGuides(bms::gearModeIndex(chart.mode));
#endif
    if (!skinGuides && keyGuideVisible((gPlayhead - (gPractice.enabled ? gPractice.startSeconds : 0)) / playbackTimingScale())) {
        for (int lane = 0; lane < layout.laneCount(); ++lane) {
            if(layout.width(lane)==0)continue;
            const std::wstring label = keyName(laneKey(lane));
            RECT keyLabel{ layout.left(lane) + ui(3), judgeY - ui(30), layout.right(lane) - ui(3), judgeY - ui(6) };
            if (gGpuFrame) {
                gTextRaster.draw(nullptr, gBackBufferWidth, gBackBufferHeight,
                    label, keyLabel, gPlaySmallFont, RGB(235, 235, 235), 1.0, drawTextBody, false, queueGpuText);
                continue;
            }
            SetTextColor(dc, RGB(235, 235, 235)); SetBkMode(dc, TRANSPARENT);
            HGDIOBJ oldFont = SelectObject(dc, gPlaySmallFont);
            DrawTextW(dc, label.c_str(), static_cast<int>(label.size()), &keyLabel, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(dc, oldFont);
        }
    }
    const int gaugeX = fieldX + ui(2);
    const int gaugeY = normalJudgeY + ui(121);
    const int gaugeWidth = std::max(1, layout.totalWidth() - ui(4));
    const int gaugeHeight = ui(28), gaugeBorder = ui(2);
    const RECT gaugeBackground{ gaugeX, gaugeY, gaugeX + gaugeWidth, gaugeY + gaugeHeight };
    fillRect(dc, gaugeBackground, RGB(25, 32, 42));
    const RECT gaugeInterior{ gaugeX + gaugeBorder, gaugeY + gaugeBorder, gaugeX + gaugeWidth - gaugeBorder, gaugeY + gaugeHeight - gaugeBorder };
    fillRect(dc, gaugeInterior, RGB(43, 54, 65));
    const int gaugeFillWidth = static_cast<int>(std::lround((gaugeWidth - gaugeBorder * 2) * gGrooveGauge.value() / 100.0));
    if (gaugeFillWidth > 0) {
        const RECT gaugeFill{ gaugeInterior.left, gaugeInterior.top, gaugeInterior.left + gaugeFillWidth, gaugeInterior.bottom };
        fillRect(dc, gaugeFill, RGB(255, 255, 255));
        const RECT gaugeHighlight{ gaugeFill.left, gaugeFill.top, gaugeFill.right, gaugeFill.top + gaugeBorder };
        fillRect(dc, gaugeHighlight, RGB(255, 255, 255));
    }
    frameRect(dc, gaugeBackground, RGB(157, 178, 193));

    const int iconSize = ui(52), iconGap = ui(7);
    constexpr int iconCount = 4;
    const int iconRowWidth = iconSize * iconCount + iconGap * (iconCount - 1);
    const int iconLeft = fieldX + (layout.totalWidth() - iconRowWidth) / 2;
    const int iconTop = gaugeY + gaugeHeight + ui(8);
    bool drawLegacySlots=true;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    drawLegacySlots=!skinCoversLegacySlots(iconLeft,iconTop,iconLeft+iconRowWidth,iconTop+iconSize);
#endif
#ifdef BMS_HUD_FRAME_PROBE
    if(hudprobe::recorder.forceLegacyIcons)drawLegacySlots=true;
#endif
    if(drawLegacySlots){ HUD_SCOPE(LegacyIcons);
    for (int slot = 0; slot < iconCount; ++slot) {
        const int x = iconLeft + slot * (iconSize + iconGap);
        const RECT iconRect{ x, iconTop, x + iconSize, iconTop + iconSize };
        const auto& slotImage=slot==0&&gHiSpeedSlotImage.loaded()?gHiSpeedSlotImage:
            slot==1&&gGaugeSlotImage.loaded()?gGaugeSlotImage:gHudSlotImage;
        if (directRaster && slotImage.loaded()) {
            GdiFlush();
            slotImage.draw(static_cast<uint32_t*>(gBackBufferPixels), gBackBufferWidth, gBackBufferHeight,
                x + iconSize / 2, iconTop + iconSize / 2, 0.0, iconSize);
        } else {
            fillRect(dc, iconRect, RGB(48, 52, 58));
            frameRect(dc, iconRect, RGB(139, 148, 160));
        }
        if (slot <= 1) {
            const std::wstring speed = slot==0?number(gNoteSpeed,2)+L"x":activeGaugeModeText();
            RECT valueRect{ x + ui(2), iconTop + ui(12), x + iconSize - ui(2), iconTop + iconSize - ui(2) };
            SetTextColor(dc, RGB(24, 28, 33));
            SetBkMode(dc, TRANSPARENT);
            HGDIOBJ oldValueFont = SelectObject(dc, slot==1&&gGaugeModeFont?gGaugeModeFont:gHudBoldFont?gHudBoldFont:gFont);
            DrawTextW(dc, speed.c_str(), static_cast<int>(speed.size()), &valueRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(dc, oldValueFont);
        }
    }
    }
    {
    BMS_PERF_SCOPE(textMs);
    if (
#ifdef BMS_ENABLE_SKIN_BINDINGS
        !skinOwnsJudgement() &&
#endif
        !gChartOptions.crazy.contains(L"hidjudge") && !gJudgementText.empty() && std::chrono::steady_clock::now() < gJudgementUntil) {
        RECT judgement{fieldX, top + ui(120 + gJudgementTextYOffset), fieldX + layout.totalWidth() - ui(2), top + ui(220 + gJudgementTextYOffset)};
        drawAnimatedCenteredText(dc, combinedJudgementText(gJudgementText, gFeedback.combo()), judgement,
            gJudgementFont ? gJudgementFont : gFont, gJudgementColor, gJudgementPop.scale(), gJudgementRainbow);
    }
    }
    const int infoLine = ui(16), infoX = ui(26);
    const int infoTop = std::max(judgeY + ui(116), gWindowHeight - ui(64));
    const COLORREF infoColor = RGB(255, 255, 255);
    const auto chartText=bms::textAt(chart,gPlayhead);
    if(!chartText.empty())drawText(dc,chartText,infoX,infoTop-ui(32),RGB(255,255,255),gPlayFont);
    if (gShowResourceUsage) {
#ifdef BMS_FRAME_DIAGNOSTICS
    static std::wstring diagnosticFps;
    static auto diagnosticTextTime=fpsNow-std::chrono::seconds(1);
    if(diagnosticFps.empty() || fpsNow-diagnosticTextTime>=std::chrono::milliseconds(250)) {
        diagnosticFps=L"FPS NOW "+number(gFrameStatistics.current(),0)+L"  AVG "+number(gFrameStatistics.average(),0)+
            L"  1% LOW "+number(gFrameStatistics.lowOnePercent(),0)+
            (gGpuFrame?L"  OUTPUT "+std::to_wstring(gCompositionPresenter.presentationFps()):std::wstring{});
        diagnosticTextTime=fpsNow;
    }
    drawText(dc,diagnosticFps,infoX,infoTop,infoColor,gPlaySmallFont);
#else
    const auto fpsText = hybridRendererEnabled && gGpuFrame ? L"FPS  " + std::to_wstring(gMeasuredFps) + L" / OUTPUT  " + std::to_wstring(gCompositionPresenter.presentationFps()) :
        L"FPS  " + std::to_wstring(gMeasuredFps) + L" / " + (gTargetFps == vsyncFps ? std::wstring(L"VSYNC") :
            gTargetFps == 0 ? std::wstring(L"UNLIMITED (MAX 1500)") : gTargetFps == uncappedFps ? std::wstring(L"UNCAPPED (TEST)") : std::to_wstring(gTargetFps)) +
        (gGpuUnavailable || !gCompositionEnabled ? std::wstring(L"  [GDI]") : std::wstring{}) +
        (gNoInputBenchmark ? std::wstring(L"  [BENCH]") : std::wstring{});
    drawText(dc, fpsText, infoX, infoTop, infoColor, gPlaySmallFont);
#endif
    drawText(dc, L"BPM  " + number(tempo ? tempo->bpm : chart.initialBpm, 1), infoX, infoTop + infoLine, infoColor, gPlaySmallFont);
    drawText(dc, L"NOTE SPEED  " + number(gNoteSpeed, 2) + L"x", infoX, infoTop + infoLine * 2, infoColor, gPlaySmallFont);
    }
    }
    }
    drawPracticeHud(dc);
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(!lr2Play)drawAppliedSkinDc(dc);
#endif
    if (!(gAutoplay && lr2Play)) drawBenchmarkBadge(dc);
    if(!gGpuFrame&&!gPaused&&gTimingHint&&std::chrono::steady_clock::now()<gJudgementUntil){const auto& hint=gTimingHint<0?gFastHintImage:gSlowHintImage;if(hint.loaded()&&gBackBufferPixels){GdiFlush();hint.draw(static_cast<uint32_t*>(gBackBufferPixels),gBackBufferWidth,gBackBufferHeight,int(gTimingHintX),int(gTimingHintY),0, int(90*gTimingHintScale));}}
    if(lr2Play)gCompositionPresenter.markFrameForegroundBoundary();
    if (gPaused) drawPauseOverlay(dc);
}
