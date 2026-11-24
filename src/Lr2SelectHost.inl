// The native list remains the navigation and accessibility model.
bool gLr2SelectActive{};
int gLr2SelectPanel{};
using Lr2SelectClock=std::chrono::steady_clock;
std::array<Lr2SelectClock::time_point,6> gLr2PanelOpened{},gLr2PanelClosed{};
// When the LR2 select scene last came on screen (its timer 0); #STARTINPUT counts from here.
Lr2SelectClock::time_point gLr2SelectShownAt{};
std::optional<sevenmania::skin::lr2::SelectControl> gLr2SliderDrag;
bool gLr2SliderVolumeChanged{};

constexpr std::array<int,3> lr2VolumeChannels{0,2,1}; // LR2 master, keys, BGM.
// When each LR2 key (1P 0-9, 2P 10-19) went down on the select screen, and its virtual key.
std::array<Lr2SelectClock::time_point,20> gLr2KeyDown{};
std::array<UINT,20> gLr2KeyVk{};

void finishLr2SliderDrag() {
    if(!gLr2SliderDrag)return;
    gLr2SliderDrag.reset();
    if(gLr2SliderVolumeChanged) {
        gLr2SliderVolumeChanged=false;
        saveDisplaySettings();
    }
    if(GetCapture()==gWindow)ReleaseCapture();
}

void resetLr2SelectControls() {
    finishLr2SliderDrag();
    gLr2SelectPanel=0;
    gLr2PanelOpened={};
    gLr2PanelClosed={};
    gLr2KeyDown={};
}

// The LR2 key a lane key stands for: the 7KEY lanes are 1P scratch and keys 1-7, the 14KEY 2P lanes
// are 2P 10-17. -1 for any other key.
int lr2SelectKey(WPARAM key,LPARAM detail) {
    if(key==VK_SHIFT)key=MapVirtualKeyW((static_cast<UINT>(detail)>>16)&0xff,MAPVK_VSC_TO_VK_EX);
    for(int lane=0;lane<8;++lane)if(bms::laneKeyFor(gKeyProfiles,bms::ChartMode::SevenKey,lane)==key)return lane;
    for(int lane=8;lane<16;++lane)if(bms::laneKeyFor(gKeyProfiles,bms::ChartMode::FourteenKey,lane)==key)return lane+2;
    return -1;
}

// [LR2Skin] ScoreGraph, which LR2 play skins read for options 38/39; read once and kept here.
bool lr2ScoreGraph(std::optional<bool> set=std::nullopt) {
    static std::optional<bool> shown;
    if(set) {
        shown=*set;
        WritePrivateProfileStringW(L"LR2Skin",L"ScoreGraph",*set?L"1":L"0",gSettingsFile.c_str());
    }
    if(!shown)shown=GetPrivateProfileIntW(L"LR2Skin",L"ScoreGraph",1,gSettingsFile.c_str())!=0;
    return *shown;
}

// Gauge cell (0 groove, 1 survival, 3 easy) of the chart's best known clear, or -1.
int lr2BestGauge(const PlayRecord* record) {
    if(!record||!record->bestKnownClear)return -1;
    switch(record->bestKnownClear->lamp) {
        case RecordClearLamp::AssistEasy: case RecordClearLamp::Easy: return 3;
        case RecordClearLamp::Normal: return 0;
        case RecordClearLamp::Hard: case RecordClearLamp::ExHard: return 1;
        default: return -1; // A full combo does not tell the gauge.
    }
}

// Song-select state the skin's IF blocks see at load time.
std::map<int,bool> lr2SelectLoadOptions() {
    return {{0,false},{3,false},{21,false},{22,false},{23,false},{24,false},{25,false},{26,false},
        {30,true},{31,false},{40,false},{41,true},{46,false},{50,true},{51,false},{52,true},{53,false}};
}

bool postSetting(int id, int value);

// LR2's gauge button cells: groove, survival, death, easy, p-attack, g-attack.
int lr2GaugeCell() {
    return gGrooveGaugeMode==GrooveGaugeMode::Hard?1:gGrooveGaugeMode==GrooveGaugeMode::Easy?3:0;
}

// LR2 lamps: 0 not played, 1 failed, 2 easy, 3 normal, 4 hard, 5 full combo.
// A clear whose gauge is unknown counts as normal.
int lr2ClearLamp(const PlayRecord* record) {
    if(!record||(!record->played&&!record->cleared))return 0;
    if(record->bestKnownClear) {
        constexpr std::array<int,9> lamps{1,2,2,3,4,4,5,5,5}; // Indexed by RecordClearLamp.
        const int lamp=lamps[std::min<std::size_t>(static_cast<std::size_t>(record->bestKnownClear->lamp),lamps.size()-1)];
        return record->cleared&&lamp<2?3:lamp; // a known failure never hides an older, unclassified clear
    }
    return record->cleared?3:1;
}

const Lr2ChartSummary& lr2ChartSummary(size_t chartIndex) {
    const auto found = gLr2ChartSummaries.find(chartIndex);
    if (found != gLr2ChartSummaries.end()) return found->second;

    const auto& chart = gCharts[chartIndex];
    Lr2ChartSummary summary;
    summary.judgementNotes = static_cast<int>(std::min<size_t>(bms::judgementNoteCount(chart), INT32_MAX));
    summary.hasLongNotes = std::any_of(chart.notes.begin(), chart.notes.end(),
        [](const auto& note) { return note.longNote(); });
    summary.minBpm = summary.maxBpm = chart.initialBpm;
    for (const auto& tempo : chart.tempos) {
        summary.minBpm = std::min(summary.minBpm, tempo.bpm);
        summary.maxBpm = std::max(summary.maxBpm, tempo.bpm);
    }
    return gLr2ChartSummaries.emplace(chartIndex, summary).first->second;
}

// Notes the record's EX score is out of: the count saved with the record, else (older records) the
// chart's when its notes are loaded. Library rows may hold only metadata, and the bars ask every frame,
// so the notes are scanned only for an older record with an EX score. 0 when unknown.
int lr2RecordNotes(const PlayRecord* record, size_t chartIndex) {
    if(!record)return 0;
    if(record->noteCount)return *record->noteCount;
    if(!record->bestExScore)return 0;
    return lr2ChartSummary(chartIndex).judgementNotes;
}

// Bar rank: 0 without a score, then 1 F up to 8 AAA in ninths of the maximum EX score, as LR2 ranks it.
// A record saved before EX scores were kept (or without a known note count) falls back to the
// accuracy score's share of its maximum.
int lr2BarRank(const PlayRecord* record,int notes) {
    if(!record||!record->played)return 0;
    int rank=1;
    if(record->bestExScore&&notes>0&&*record->bestExScore<=2LL*notes)
        return sevenmania::skin::lr2::exScoreRank(*record->bestExScore,notes);
    if(record->bestAccuracyScore<=0)return 0;
    for(const int bound:{6,9,12,15,18,21,24})
        if(27LL*record->bestAccuracyScore>=bound*1'000'000LL)++rank;
    return rank;
}

// The selected chart's best record for the record numbers.
sevenmania::skin::lr2::SelectRecord lr2SelectRecord(const PlayRecord* record,int notes) {
    sevenmania::skin::lr2::SelectRecord out;
    out.noteCount=notes;
    if(!record||!record->played)return out;
    // A score or count beyond the chart's notes belongs to a different version of the chart.
    const auto fits=[notes](const std::optional<int>& value,long long limit) {
        return value&&(notes<=0||*value<=limit)?value:std::nullopt;
    };
    out.exScore=fits(record->bestExScore,2LL*notes);
    out.missCount=fits(record->minMissCount,notes);
    out.playCount=record->playCount;
    out.clearCount=record->clearCount;
    out.maxCombo=static_cast<int>(std::min<unsigned>(record->maxCombo,INT32_MAX));
    if(record->bestJudgements) {
        long long total=0;
        for(const int count:*record->bestJudgements)total+=count;
        if(notes<=0||total<=notes)out.judgements=record->bestJudgements;
    }
    return out;
}

int lr2ChartLevel(const bms::Chart& chart) {
    const auto level=bms::numericPlayLevel(chart);
    return level?static_cast<int>(std::clamp(double(*level),0.,9999.)):-1;
}

// Rows the list still has to travel after the selection moved, for the bar slide.
float lr2SelectScroll(int selected,int count) {
    constexpr double slideMilliseconds=120;
    static int lastSelected=-1,lastCount=0;
    static float from=0;
    static Lr2SelectClock::time_point moved{};
    const auto now=Lr2SelectClock::now();
    const auto remaining=[&] {
        const double age=std::chrono::duration<double,std::milli>(now-moved).count();
        return moved==Lr2SelectClock::time_point{}||age>=slideMilliseconds?0.f:from*float(1-age/slideMilliseconds);
    };
    if(count!=lastCount||lastSelected<0||selected<0) {
        // A different list, not a move within it.
        from=0;moved={};
    } else if(selected!=lastSelected) {
        int delta=selected-lastSelected;
        if(delta>count/2)delta-=count;
        else if(delta<-count/2)delta+=count;
        from=std::clamp(remaining()+float(delta),-1.f,1.f);
        moved=now;
    }
    lastSelected=selected;lastCount=count;
    return remaining();
}

sevenmania::skin::lr2::SelectFrame captureLr2Select(const sevenmania::skin::lr2::PreviewScene& scene,int width,int height) {
    using namespace sevenmania::skin::lr2;
    SelectFrame frame;
    const int index=selectedIndex();
    if(index>=0&&index<static_cast<int>(gCharts.size())) {
        const auto& chart=gCharts[index];
        frame.song=lr2SongData(chart);
        frame.songSelected=true;
        frame.difficulty=chart.difficulty;
        frame.level=lr2ChartLevel(chart);
        frame.banner=chart.banner;
        constexpr std::array<int,7> keyCounts{0,5,7,10,14,9,0}; // Indexed by bms::ChartMode.
        frame.keys=keyCounts[std::min<std::size_t>(static_cast<std::size_t>(chart.mode),keyCounts.size()-1)];
        frame.judgeRank=chart.rankDefaulted?-1:chart.rank;
        frame.hasBga=!chart.bga.empty()||!chart.fallbackVideo.empty();
        const auto& summary = lr2ChartSummary(index);
        frame.hasLongNotes = summary.hasLongNotes;
        frame.song.numbers.noteCount = summary.judgementNotes;
        frame.song.numbers.minBpm = summary.minBpm;
        frame.song.numbers.maxBpm = summary.maxBpm;
        frame.bpmChange=frame.song.numbers.minBpm!=frame.song.numbers.maxBpm;
        if(const auto* record=playRecordForChart(index)) {
            const int notes=lr2RecordNotes(record,index);
            frame.clearLamp=lr2ClearLamp(record);
            frame.rank=lr2BarRank(record,notes);
            frame.song.numbers.maxCombo=static_cast<int>(record->maxCombo);
            frame.record=lr2SelectRecord(record,notes);
            frame.bestGauge=lr2BestGauge(record);
            // Lane option of the best clear; records from before the option count as OFF.
            frame.bestLaneOption=record->clearOptions?static_cast<int>(bms::laneOptionFromName(record->clearOptions->lane)):0;
        }
    }
    const auto now=Lr2SelectClock::now();
    const auto age=[now](Lr2SelectClock::time_point started) {
        return started==Lr2SelectClock::time_point{}?-1.:
            std::chrono::duration<double,std::milli>(now-started).count();
    };
    frame.selectionAge=std::max(0.,age(gBrowseSelectedAt));
    frame.fadeoutAge=age(gLr2SelectFadeAt);
    // Summed once per change of the records, not every frame.
    const auto& totals=gPlayRecordStore.totals();
    frame.totals={totals.playCount,totals.clearCount,totals.judgements,totals.playSeconds};
    frame.panel=gLr2SelectPanel;
    for(std::size_t panel=0;panel<frame.panelOn.size();++panel) {
        frame.panelOn[panel]=age(gLr2PanelOpened[panel]);
        frame.panelOff[panel]=age(gLr2PanelClosed[panel]);
    }
    constexpr std::array<int,6> keyCells{2,1,4,3,5,0}; // Same order as KeyModeFilter.
    frame.keyMode=keyCells[static_cast<std::size_t>(gKeyModeFilter)];
    frame.difficultyFilter=gDifficultyFilter;
    frame.laneOption=static_cast<int>(gLaneOption);
    frame.targetKind=static_cast<int>(gTargetKind);
    frame.targetRate=gDefaultTargetRate;
    frame.gaugeCell=lr2GaugeCell();
    for(std::size_t i=0;i<lr2VolumeChannels.size();++i)frame.volumes[i]=gVolumes[lr2VolumeChannels[i]]/100.f;
    frame.hiSpeed=gNoteSpeed;
    frame.judgeTiming=gTimingOffsets.judgeMs;
    frame.hiSpeedFix=static_cast<int>(gHiSpeedFix);
    frame.laneCover=gLaneCoverPercent;
    frame.autoJudge=gAutoJudgeMode;
    frame.screenMode=gScreenMode;
    frame.bgaEnabled=gBgaBrightness>0;
    frame.scoreGraph=lr2ScoreGraph();
    // A key released while another window had focus never sent its key-up.
    const bool foreground=GetForegroundWindow()==gWindow;
    for(std::size_t key=0;key<gLr2KeyDown.size();++key) {
        if(gLr2KeyDown[key]!=Lr2SelectClock::time_point{}&&(!foreground||!(GetAsyncKeyState(static_cast<int>(gLr2KeyVk[key]))&0x8000)))
            gLr2KeyDown[key]={};
        frame.keyOn[key]=age(gLr2KeyDown[key]);
    }
    const int count=ListView_GetItemCount(gList);
    const int selected=ListView_GetNextItem(gList,-1,LVNI_SELECTED);
    frame.canScroll=count>1;
    frame.scroll=lr2SelectScroll(selected,count);
    POINT cursor{};
    RECT client{};
    if(GetForegroundWindow()==gWindow&&GetCursorPos(&cursor)&&ScreenToClient(gWindow,&cursor)&&
       GetClientRect(gWindow,&client)&&PtInRect(&client,cursor)) {
        frame.mouseX=float(cursor.x);frame.mouseY=float(cursor.y);
    }
    if(count>1&&selected>=0)frame.listPosition=float(selected)/(count-1);
    if(count<=0||selected<0)return frame;
    const auto& groups=gSongSelection.groups();
    if(const int group=selectedGroupIndex();frame.songSelected&&group>=0&&group<static_cast<int>(groups.size())) {
        for(const auto chartIndex:groups[static_cast<std::size_t>(group)].chartIndices) {
            if(chartIndex>=gCharts.size())continue;
            const auto& sibling=gCharts[chartIndex];
            if(sibling.difficulty<1||sibling.difficulty>5)continue;
            const auto difficulty=static_cast<std::size_t>(sibling.difficulty-1);
            if(!frame.folderCharts[difficulty]++)frame.folderLevels[difficulty]=lr2ChartLevel(sibling);
        }
    }
    // Slots nearest the cursor claim their row first, so a list shorter than the bar column is drawn
    // once instead of the same rows cycling through every remaining bar.
    std::array<int,maxSelectBars> slots{};
    for(int slot=0;slot<maxSelectBars;++slot)slots[slot]=slot;
    std::stable_sort(slots.begin(),slots.end(),[center=scene.selectCenter()](int a,int b) {
        return std::abs(a-center)<std::abs(b-center);
    });
    std::vector<bool> shown(static_cast<std::size_t>(count));
    for(const int slot:slots) {
        const auto bar=scene.selectBar(slot,width,height);
        if(!bar)continue;
        const int row=wrapSongListRow(selected+slot-scene.selectCenter(),count);
        if(row<0||row>=count||shown[static_cast<std::size_t>(row)])continue;
        shown[static_cast<std::size_t>(row)]=true;
        LVITEMW item{};item.mask=LVIF_PARAM;item.iItem=row;
        if(!ListView_GetItem(gList,&item))continue;
        auto& visual=frame.rows[slot];
        visual.row=row;
        const auto group=static_cast<int>(item.lParam);
        if(group>=0&&group<static_cast<int>(groups.size())&&!groups[group].chartIndices.empty()) {
            const auto& charts=groups[group];
            const auto chartIndex=charts.chartIndices[std::min(charts.selectedVariant,charts.chartIndices.size()-1)];
            if(chartIndex>=gCharts.size())continue;
            const auto& chart=gCharts[chartIndex];
            visual.title=chart.title+(chart.subtitle.empty()?L"":L" "+chart.subtitle);
            visual.level=chart.playLevel;
            visual.difficulty=chart.difficulty;
            visual.levelValue=lr2ChartLevel(chart);
            const auto* record=playRecordForChart(chartIndex);
            visual.lamp=lr2ClearLamp(record);
            visual.rank=lr2BarRank(record,lr2RecordNotes(record,chartIndex));
        } else {
            wchar_t label[1024]{};
            ListView_GetItemText(gList,row,1,label,1024);
            visual.title=label;visual.body=1;
            if(slot==scene.selectCenter()&&!frame.songSelected) {
                frame.song.songText[0]=visual.title;
                frame.song.songText[2]=visual.title;
            }
        }
    }
    return frame;
}

bool presentLr2Select(int width,int height) {
    using namespace sevenmania::skin::lr2;
    if(gLr2SelectFile.empty())return false;
    // The owned settings window blocks input while the LR2 select scene stays visible behind it.
    if(gPlaying||gShowingResult||gLoading||gStartupSplash||gSongInfoVisible||
       !gList||!gBackBufferPixels||width<=0||height<=0||!gCompositionEnabled) {
        gLr2SelectGpuFailed=false;
        if(gLr2SelectActive) {
            gLr2SelectActive=false;
            resetLr2SelectControls();
            gLr2SelectScene->releaseDevice();
            // The LR2 decide screen draws on the same presenter; rebuilding it would only delay that screen.
            if(!lr2DecideShowing())gCompositionPresenter.reset();
        }
        return false;
    }
    if(gLr2SelectGpuFailed)return false;
    auto& started=gLr2SelectShownAt;
    if(started==Lr2SelectClock::time_point{})started=Lr2SelectClock::now();
    if(!gLr2SelectAttempted) {
        gLr2SelectAttempted=true;
        Options options;
        options.screen=ScreenType::Select;
        options.initialOptions=lr2SelectLoadOptions();
        gLr2SelectScene=loadLr2Screen(gLr2SelectFile,options,{});
    }
    if(!gLr2SelectScene)return false;
    if(!gLr2SelectActive)started=std::chrono::steady_clock::now();
    auto frame=captureLr2Select(*gLr2SelectScene,width,height);
    const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    gLr2SelectActive=presentLr2Screen(*gLr2SelectScene,elapsed,width,height,&frame);
    if(!gLr2SelectActive) {
        resetLr2SelectControls();
        gLr2SelectScene->releaseDevice();
        gLr2SelectGpuFailed=true;
        reportLr2Failure(ScreenType::Select,gLr2SelectFile,L"GPU 출력 실패");
        gCompositionPresenter.reset();
    } else {
        gLr2RuntimeErrors[static_cast<std::size_t>(ScreenType::Select)].clear();
    }
    return gLr2SelectActive;
}

void updateLr2Slider(const sevenmania::skin::lr2::SelectControl& control,POINT point) {
    const float value=control.sliderPosition(float(point.x),float(point.y));
    if(control.slider==1) {
        const int count=ListView_GetItemCount(gList);
        if(count<=1)return;
        const int row=static_cast<int>(std::lround(double(value)*(count-1)));
        if(row!=ListView_GetNextItem(gList,-1,LVNI_SELECTED))selectSongListRow(row,row,false);
    } else {
        const int channel=lr2VolumeChannels[control.slider-17];
        const int percent=static_cast<int>(std::lround(value*100.f));
        if(gVolumes[channel]==percent)return;
        gVolumes[channel]=percent;
        gAudio.setVolumes(gVolumes[0],gVolumes[1],gVolumes[2],gVolumes[3]);
        gLr2SliderVolumeChanged=true;
    }
    InvalidateRect(gWindow,nullptr,FALSE);
}

// An option panel button: the value moves through the settings controller, which applies and saves it
// like the settings screen. Off/on toggles remember the last "on" value for this run.
void pressLr2OptionButton(int id,bool increment) {
    static int laneCoverOn=0,bgaOn=0,autoJudgeOn=0;
    switch(id) {
        case 40: {
            // Same order as LR2 shows them, limited to the gauges this player has.
            constexpr std::array order{GrooveGaugeMode::Normal,GrooveGaugeMode::Hard,GrooveGaugeMode::Easy};
            const auto at=std::find(order.begin(),order.end(),gGrooveGaugeMode);
            const std::size_t index=at==order.end()?0:static_cast<std::size_t>(at-order.begin());
            postSetting(ID_GROOVE_GAUGE,static_cast<int>(order[(index+(increment?1:order.size()-1))%order.size()]));
            break;
        }
        case 57: postSetting(ID_SPEED,static_cast<int>(std::lround(gNoteSpeed*4))+(increment?1:-1));break;
        case 74: postSetting(ID_JUDGE_TIMING_OFFSET,gTimingOffsets.judgeMs+(increment?1:-1));break;
        case 55: {
            // Only the values the skin's picture shows (LR2 skins have five: OFF to MIN BPM, no CONSTANT).
            const int shown=gLr2SelectScene?gLr2SelectScene->buttonStates(55):0;
            const int count=shown>0?std::min(shown,bms::hiSpeedFixCount):bms::hiSpeedFixCount;
            const int current=static_cast<int>(gHiSpeedFix);
            const int next=current>=count?(increment?0:count-1):(current+(increment?1:count-1))%count;
            postSetting(ID_HI_SPEED_FIX,next);
            break;
        }
        case 46: case 50:
            if(gLaneCoverPercent>0)laneCoverOn=gLaneCoverPercent;
            postSetting(ID_LANE_COVER,gLaneCoverPercent>0?0:laneCoverOn>0?laneCoverOn:20);
            break;
        case 72:
            if(gBgaBrightness>0)bgaOn=gBgaBrightness;
            postSetting(ID_BGA_DARKNESS,gBgaBrightness>0?0:bgaOn>0?bgaOn:50);
            break;
        case 75:
            if(gAutoJudgeMode)autoJudgeOn=gAutoJudgeMode;
            postSetting(ID_AUTO_JUDGE_MODE,gAutoJudgeMode?0:autoJudgeOn?autoJudgeOn:static_cast<int>(bms::AutoJudgeAdjuster::Suggest));
            break;
        case 70: lr2ScoreGraph(!lr2ScoreGraph());break;
        case 10: cycleDifficultyFilter(increment);break;
        case 42:
            postSetting(ID_LANE_OPTION,(static_cast<int>(gLaneOption)+(increment?1:bms::laneOptionCount-1))%bms::laneOptionCount);
            break;
        case 77: gTargetKind=bms::nextTargetKind(gTargetKind,increment);saveDisplaySettings();break;
        case 76:
            gDefaultTargetRate=std::clamp(gDefaultTargetRate+(increment?1:-1),bms::minDefaultTargetRate,bms::maxDefaultTargetRate);
            saveDisplaySettings();
            break;
    }
}

// Select #STARTINPUT: keys, clicks and the wheel do nothing until the scene has been up that long.
// Swallows the message while locked; lr2SelectInput and the list never see it.
bool lr2SelectInputLocked(UINT message) {
    if(!gLr2SelectActive||!gLr2SelectScene||gLoading||gPlaying||gShowingResult||gSettingsWindow)return false;
    switch(message) {
    case WM_KEYDOWN: case WM_CHAR: case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: case WM_MOUSEWHEEL: break;
    default: return false;
    }
    const double age=std::chrono::duration<double,std::milli>(Lr2SelectClock::now()-gLr2SelectShownAt).count();
    return sevenmania::skin::lr2::sceneInputLocked(gLr2SelectScene->startInputMilliseconds(),age);
}

// Select MOUSECURSOR: over the client area the skin draws the cursor, so the system one is hidden.
bool lr2SelectSetCursor(LPARAM lParam) {
    if(LOWORD(lParam)!=HTCLIENT||!gLr2SelectActive||!gLr2SelectScene||!gLr2SelectScene->hasMouseCursor()||
       gLoading||gPlaying||gShowingResult||gSongInfoVisible||gSettingsWindow)return false;
    SetCursor(nullptr);
    return true;
}

// Play #STARTINPUT: while the LR2 play scene's lead-in is younger than it, the loading keys
// (Esc cancels, Enter skips the intro) are ignored.
bool lr2PlayInputLocked() {
    if(!gLr2PlayScene||gLr2GpuFailed||gLr2Play.leadInFailed)return false;
    return sevenmania::skin::lr2::sceneInputLocked(gLr2PlayScene->startInputMilliseconds(),gLr2Play.leadInAge());
}

void openSkinSelectScreen(); // SkinSelectScreen.inl, included later with the settings window
void openKeyConfigScreen(); // KeyConfigScreen.inl, likewise
// Buttons that step through values, which a right click steps backwards.
bool lr2SteppingButton(int id) {return id==10||id==11||id==40||id==42||id==55||id==57||id==74||id==76||id==77;}

bool lr2SelectInput(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if(gLr2SliderDrag&&(message==WM_CAPTURECHANGED||message==WM_CANCELMODE)) {
        finishLr2SliderDrag();
        return false;
    }
    if(!gLr2SelectActive||!gLr2SelectScene||gLoading||gPlaying||gShowingResult||gSongInfoVisible||gSettingsWindow)return false;
    // Lane keys only light the skin's key timers; the keys keep their usual meaning.
    if(message==WM_KEYDOWN||message==WM_KEYUP) {
        const int key=lr2SelectKey(wParam,lParam);
        if(key>=0) {
            auto& down=gLr2KeyDown[static_cast<std::size_t>(key)];
            if(message==WM_KEYUP)down={};
            else if(down==Lr2SelectClock::time_point{}) {
                down=Lr2SelectClock::now();
                gLr2KeyVk[static_cast<std::size_t>(key)]=wParam==VK_SHIFT?
                    MapVirtualKeyW((static_cast<UINT>(lParam)>>16)&0xff,MAPVK_VSC_TO_VK_EX):static_cast<UINT>(wParam);
            }
            InvalidateRect(gWindow,nullptr,FALSE);
        }
        return false;
    }
    if(message==WM_MOUSEWHEEL) {
        gSongListWheelRemainder+=GET_WHEEL_DELTA_WPARAM(wParam);
        const int notches=gSongListWheelRemainder/WHEEL_DELTA;
        gSongListWheelRemainder%=WHEEL_DELTA;
        if(notches)navigateSongList(notches>0?-1:1,std::abs(notches));
        return true;
    }
    // Right click turns a stepping button the other way (hi-speed down, the previous gauge, ...).
    if((message==WM_RBUTTONDOWN||message==WM_RBUTTONDBLCLK)&&!gLr2SliderDrag) {
        POINT point{static_cast<short>(LOWORD(lParam)),static_cast<short>(HIWORD(lParam))};
        if(window!=gWindow)MapWindowPoints(window,gWindow,&point,1);
        const auto control=gLr2SelectScene->selectControlAt(float(point.x),float(point.y));
        if(!control||!lr2SteppingButton(control->button))return false;
        SetFocus(gList);
        if(control->button==11)cycleKeyModeFilter(false);
        else pressLr2OptionButton(control->button,false);
        triggerSystemSound(bms::SystemSound::OptionChange);
        InvalidateRect(gWindow,nullptr,FALSE);
        return true;
    }
    const bool press=message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK;
    if(!press&&(!gLr2SliderDrag||(message!=WM_MOUSEMOVE&&message!=WM_LBUTTONUP)))return false;
    POINT point{static_cast<short>(LOWORD(lParam)),static_cast<short>(HIWORD(lParam))};
    if(window!=gWindow)MapWindowPoints(window,gWindow,&point,1);
    if(!press) {
        updateLr2Slider(*gLr2SliderDrag,point);
        if(message==WM_LBUTTONUP)finishLr2SliderDrag();
        return true;
    }
    if(const auto control=gLr2SelectScene->selectControlAt(float(point.x),float(point.y))) {
        SetFocus(gList);
        if(control->row>=0) {
            if(selectSongListRow(control->row,control->row)) {
                if(message==WM_LBUTTONDBLCLK)beginSelectedChart();
                else InvalidateRect(gWindow,nullptr,FALSE);
            }
        } else if(control->slider) {
            SetCapture(gWindow);
            gLr2SliderDrag=control;
            updateLr2Slider(*control,point);
            if(GetCapture()!=gWindow)finishLr2SliderDrag();
        } else if(message==WM_LBUTTONDOWN) {
            const int id=control->button;
            if(id>=1&&id<=6) {
                const auto now=Lr2SelectClock::now();
                const int old=gLr2SelectPanel;
                if(old) {
                    gLr2PanelOpened[old-1]={};
                    gLr2PanelClosed[old-1]=now;
                }
                gLr2SelectPanel=old==id?0:id;
                if(gLr2SelectPanel) {
                    gLr2PanelClosed[id-1]={};
                    gLr2PanelOpened[id-1]=now;
                }
                triggerSystemSound(gLr2SelectPanel?bms::SystemSound::OptionOpen:bms::SystemSound::OptionClose);
            } else if(id==11){cycleKeyModeFilter(control->increment(float(point.x),float(point.y)));triggerSystemSound(bms::SystemSound::OptionChange);}
            else if(id==12){toggleSongSort();triggerSystemSound(bms::SystemSound::OptionChange);}
            else if(id==15)beginSelectedChart();
            else if(id==16)beginSelectedChart(false,true); // Autoplay.
            else if(id==13||id==14) {
                // LR2/beatoraja: 13 is KEY CONFIG, 14 is SKIN SELECT.
                resetLr2SelectControls();
                if(id==13)openKeyConfigScreen();else openSkinSelectScreen();
                return true;
            } else {pressLr2OptionButton(id,id==57||control->increment(float(point.x),float(point.y)));triggerSystemSound(bms::SystemSound::OptionChange);}
            InvalidateRect(gWindow,nullptr,FALSE);
        }
        return true;
    }
    return window==gList;
}
