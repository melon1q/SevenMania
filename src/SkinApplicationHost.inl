// Included inside Main.cpp's UI owner namespace, with BMS_ENABLE_SKIN_BINDINGS.
constexpr int ID_SKIN_ANIMATION=1292, ID_SKIN_RETRY=1293;
void logSkinWarning(const std::wstring& text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string line(static_cast<std::size_t>(std::max(size, 0)), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), line.data(), size, nullptr, nullptr);
    std::ofstream log(gSettingsFile.parent_path() / L"skin-render.log", std::ios::app);
    log << line << '\n';
    OutputDebugStringA((line + "\n").c_str());
}
std::map<int, TurntableImage> gSkinHitEffects;
std::map<int, int> gSkinHitEffectFrames;
int gSkinHitEffect{};

bool selectSkinHitEffect(int number) {
    const auto found = gSkinHitEffects.find(number);
    if (found == gSkinHitEffects.end()) return false;
    const auto& image = found->second;
    if (!gFeedback.setSpriteSheet(image.width(), image.height(), image.pixels(), gSkinHitEffectFrames[number])) return false;
    gSkinHitEffect = number;
    return true;
}


void loadSkinHitEffects(const std::filesystem::path& root, const std::shared_ptr<const sevenmania::skin::Bytes>& packaged = {}) {
    gSkinHitEffects.clear();
    gSkinHitEffectFrames.clear();
    gSkinHitEffect = 0;
    if (!root.empty() && gGdiPlusToken) {
        std::vector<std::wstring> issues;
        for (const auto& [number,path] : skinHitEffectFiles(root, &issues)) {
            const auto name = L"HitEffects/" + path.filename().wstring();
            const int frames = skinHitEffectFrames(path);
            TurntableImage image;
            if (frames < 1 || frames > PlayFeedback::maxSpriteFrames) {
                issues.push_back(name + L" ignored: Frames must be 1-" + std::to_wstring(PlayFeedback::maxSpriteFrames) + L".");
            } else if (!loadRasterPngFile(path, image)) {
                issues.push_back(name + L" ignored: the PNG could not be decoded.");
            } else if (image.width() % frames != 0) {
                issues.push_back(name + L" ignored: the width (" + std::to_wstring(image.width()) +
                    L") must be a multiple of the frame count (" + std::to_wstring(frames) + L").");
            } else {
                gSkinHitEffects.emplace(number, std::move(image));
                gSkinHitEffectFrames[number] = frames;
            }
        }
        for (const auto& issue : issues) logSkinWarning(issue);
    }
    if (!gSkinHitEffects.empty()) {
        const int saved = static_cast<int>(GetPrivateProfileIntW(L"Skin", L"HitEffect", 0, gSettingsFile.c_str()));
        selectSkinHitEffect(gSkinHitEffects.contains(saved) ? saved : gSkinHitEffects.begin()->first);
    } else {
        TurntableImage image;
        if (packaged && gGdiPlusToken) {
            Microsoft::WRL::ComPtr<IStream> stream;
            stream.Attach(SHCreateMemStream(packaged->data(), static_cast<UINT>(packaged->size())));
            if (stream) {
                Gdiplus::Bitmap bitmap(stream.Get());
                loadRasterBitmap(bitmap, image, 0, 0);
            }
        }
        if (!gFeedback.setSpriteSheet(image.width(), image.height(), image.pixels()) &&
            !gFeedback.setSpriteSheet(gHitSplashSheet.width(), gHitSplashSheet.height(), gHitSplashSheet.pixels()))
            gFeedback.useDefaultSprites();
    }
}
sevenmania::skin::SkinApplication gSkinApplication;
sevenmania::skin::SkinDataProvider gSkinData;
void restartSongInformationEntrance(){gSkinApplication.enterScreen(sevenmania::skin::ScreenKind::SongInformation);}
BrowseBackdrop gSkinTitleBackdrop, gSkinBgaBackdrop, gSkinHybridBackdrop;
uint64_t gSkinTitleRevision=0, gSkinBgaRevision=0, gSkinHybridRevision=0;
int gSkinBoxWidth=0, gSkinBoxHeight=0;
std::filesystem::path gSkinSource;
std::wstring gSkinStatus=L"기본 화면 사용 중";
HWND gSkinNativeStatus{};
std::filesystem::path gSkinWanted;
bool gSkinRetryPending=false;
std::filesystem::path defaultSkinPath(){
    const auto skins=executableFolder()/L"Assets/Skins";
    const auto current=skins/L"DefaultSkin/DefaultReference.semaskin";
    std::error_code ec;
    return std::filesystem::is_regular_file(current,ec)?current:skins/L"DefaultReference.semaskin";
}
void skinVisibility(bool ready){if(!ready){for(HWND button:gPauseButtons)if(IsWindow(button))ShowWindow(button,SW_HIDE);for(HWND child:{gList,gFolder,gInfo,gStatus,gHiddenSongs,gResultNext})if(IsWindow(child))ShowWindow(child,SW_HIDE);}else{if(IsWindow(gList))ShowWindow(gList,!gPlaying&&!gShowingResult&&!gStartupSplash?SW_SHOWNA:SW_HIDE);updateHiddenSongsVisibility();syncNativeResultNext();for(HWND button:gPauseButtons)if(IsWindow(button))ShowWindow(button,gPlaying&&gPaused?SW_SHOWNA:SW_HIDE);}}
void unloadSkinSelection(){if(IsWindow(gWindow)){KillTimer(gWindow,ID_SKIN_RETRY);KillTimer(gWindow,ID_SKIN_ANIMATION);}gCompositionPresenter.setFrameOverlay(nullptr);gCompositionPresenter.setFrameForeground(nullptr);gSkinApplication.clear();gNoteImages.clear();gSkinData.clear();gSkinRetryPending=false;gSkinWanted.clear();}


std::wstring skinWide(const std::string& value) {
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    std::wstring result(static_cast<std::size_t>(count),L'\0');
    if(count)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),count);
    return result;
}
std::wstring skinSummary() {
    std::wstring summary=gSkinStatus;
    if(const auto* m=gSkinApplication.manifest()) {
        summary+=L"\n지원 화면: ";
        for(const auto kind:m->supportedScreens) {
            if(kind==sevenmania::skin::ScreenKind::Select)summary+=L"곡 선택  ";
            else if(kind==sevenmania::skin::ScreenKind::Play)summary+=L"플레이  ";
            else summary+=L"리절트  ";
        }
    }
    if(gSkinApplication.error())summary+=L"\n최근 스킨 오류: "+skinWide(gSkinApplication.error()->message);
    return summary;
}
bool saveSkinSelection() {
    // Existing INI files can be ANSI. Keep paths lossless across Windows code pages.
    const auto source=gSkinSource.wstring();
    if(source.empty())return WritePrivateProfileStringW(L"Skin",L"PackagePath",L"",gSettingsFile.c_str())!=0;
    const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,source.data(),static_cast<int>(source.size()),nullptr,0,nullptr,nullptr);
    if(!size||size>16380)return false;
    std::string utf8(static_cast<std::size_t>(size),'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,source.data(),static_cast<int>(source.size()),utf8.data(),size,nullptr,nullptr);
    constexpr wchar_t hex[]=L"0123456789abcdef";std::wstring encoded=L"utf8:";encoded.reserve(5+utf8.size()*2);
    for(unsigned char c:utf8){encoded.push_back(hex[c>>4]);encoded.push_back(hex[c&15]);}
    return WritePrivateProfileStringW(L"Skin",L"PackagePath",encoded.c_str(),gSettingsFile.c_str())!=0;
}
std::optional<std::wstring> readSkinSelection() {
    std::wstring path(32768,L'\0');
    const auto count=GetPrivateProfileStringW(L"Skin",L"PackagePath",L"",path.data(),static_cast<DWORD>(path.size()),gSettingsFile.c_str());
    if(count>=path.size()-1)return {};
    path.resize(count);if(!path.starts_with(L"utf8:"))return path; // Legacy/manual absolute paths.
    if((path.size()-5)%2)return {};
    const auto digit=[](wchar_t c)->int {return c>=L'0'&&c<=L'9'?c-L'0':c>=L'a'&&c<=L'f'?c-L'a'+10:-1;};
    std::string bytes;bytes.reserve((path.size()-5)/2);
    for(std::size_t i=5;i<path.size();i+=2){const int hi=digit(path[i]),lo=digit(path[i+1]);if(hi<0||lo<0)return {};bytes.push_back(static_cast<char>((hi<<4)|lo));}
    if(bytes.find('\0')!=std::string::npos)return {};
    const auto decoded=skinWide(bytes);if(!bytes.empty()&&decoded.empty())return {};return decoded;
}
bool applySkinFile(const std::filesystem::path& file,bool persist=true) {
    std::error_code ec;const auto absolute=std::filesystem::absolute(file,ec);
    if(ec){gSkinStatus=L"스킨 경로를 읽을 수 없습니다. 기존 선택을 유지합니다.";return false;}
    gSkinWanted=absolute;
    auto error=gSkinApplication.setHudModes(gSkinJudgementMode,gSkinComboMode);
    if(!error)error=gSkinApplication.load(absolute,std::max(1,gWindowWidth),std::max(1,gWindowHeight));
    if(error) {
        static std::string lastLoggedError;const auto message=error->path+": "+error->message;
        if(message!=lastLoggedError){std::ofstream log(gSettingsFile.parent_path()/L"skin-render.log",std::ios::app);log<<message<<'\n';OutputDebugStringA(message.c_str());lastLoggedError=message;}
        gSkinApplication.clear();gNoteImages.clear();loadSkinHitEffects({});gSkinRetryPending=true;skinVisibility(false);SetTimer(gWindow,ID_SKIN_RETRY,1000,nullptr);InvalidateRect(gWindow,nullptr,FALSE);
        gSkinStatus=L"스킨을 불러오지 못했습니다. 빈 화면에서 1초 간격으로 재시도합니다.\n"+skinWide(error->path)+L": "+skinWide(error->message);
        return false;
    }
    if (gGdiPlusToken) {
        wchar_t sheet[1024]{};
        GetPrivateProfileStringW(L"Skin", L"NoteGroup", L"", sheet, 1024, gSettingsFile.c_str());
        const bool bundledSkin = std::filesystem::equivalent(absolute, defaultSkinPath(), ec);
        for (const auto& warning : gNoteImages.load(absolute, skinSettingDecode(sheet), bundledSkin)) logSkinWarning(warning);
    }
    for (const auto& warning : gSkinApplication.warnings()) logSkinWarning(warning);
    loadSkinHitEffects(absolute.parent_path(), gSkinApplication.hitSplash());
    gSkinRetryPending=false;gSkinTitleRevision=0;gSkinBgaRevision=0;gSkinBoxWidth=0;gSkinBoxHeight=0;KillTimer(gWindow,ID_SKIN_RETRY);skinVisibility(true);
    gSkinSource=absolute;gSkinData.clear();KillTimer(gWindow,ID_SKIN_ANIMATION);if(gSkinApplication.animating())SetTimer(gWindow,ID_SKIN_ANIMATION,16,nullptr);
    gSkinStatus=L"적용 중: "+skinWide(gSkinApplication.manifest()->name);
    if(!gSkinApplication.diagnostics().empty()){
        std::ofstream log(gSettingsFile.parent_path()/L"skin-render.log",std::ios::app);
        for(const auto& warning:gSkinApplication.diagnostics())log<<warning.element<<" / "<<warning.binding<<": "<<warning.message<<'\n';
        gSkinStatus+=L" (리소스 경고: skin-render.log 확인)";
    }
    if(persist&&!saveSkinSelection())gSkinStatus+=L" (설정을 저장하지 못했습니다.)";
    InvalidateRect(gWindow,nullptr,FALSE);return true;
}
void clearSkinSelection(bool persist=true) {
    unloadSkinSelection();gSkinSource.clear();if(persist)saveSkinSelection();
    applySkinFile(defaultSkinPath(),false);
}
void restoreSkinSelection() {
    unloadSkinSelection();gSkinSource.clear();const auto path=readSkinSelection();
    if(path&&!path->empty()&&std::filesystem::path(*path).is_absolute()){
        auto selected=std::filesystem::path(*path);std::error_code ec;
        if(!std::filesystem::exists(selected,ec)&&selected.filename()==L"DefaultReference.semaskin"&&selected.parent_path().filename()==L"Skins")selected=selected.parent_path()/L"DefaultSkin"/selected.filename();
        applySkinFile(selected,false);
    }
    else applySkinFile(defaultSkinPath(),false);
}
void retrySkinSelection(){if(gSkinRetryPending&&!gSkinWanted.empty()){auto path=gSkinWanted;applySkinFile(path,false);}}
void drawAppliedSkinDc(HDC dc);
bool drawSkinOnly(HDC dc){
    fillRect(dc,{0,0,gWindowWidth,gWindowHeight},RGB(0,0,0));
    if(gSkinRetryPending||!gSkinApplication.active())return false;
    drawAppliedSkinDc(dc);return true;
}
// Key names the skin's gear-key-guide elements show; rebuilt only when the mode or a binding changes.
std::shared_ptr<const std::array<std::string,16>> skinKeyLabels(bms::ChartMode mode) {
    static std::shared_ptr<const std::array<std::string,16>> labels;
    static std::array<UINT,16> codes{};
    static bms::ChartMode cachedMode=bms::ChartMode::Other;
    std::array<UINT,16> current{};
    for(int lane=0;lane<16;++lane)current[static_cast<size_t>(lane)]=laneKey(lane);
    if(labels&&cachedMode==mode&&current==codes)return labels;
    auto next=std::make_shared<std::array<std::string,16>>();
    for(int lane=0;lane<16;++lane){
        if(!current[static_cast<size_t>(lane)])continue;
        const auto text=keyName(current[static_cast<size_t>(lane)]);
        const int length=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
        if(length<=0)continue;
        (*next)[static_cast<size_t>(lane)].resize(static_cast<size_t>(length));
        WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),(*next)[static_cast<size_t>(lane)].data(),length,nullptr,nullptr);
    }
    codes=current;cachedMode=mode;labels=std::move(next);
    return labels;
}
// Skin elements bound to play.keyGuide are shown while the song loads and until playback starts.
bool skinKeyGuideShown() {return gPlaybackLoad.preparing()||!gPlaying||gPlayhead<0;}
void updateSkinJudgementAnchor() {
    if((!gPlaying&&!gPlaybackLoad.preparing()) || gActiveChartIndex<0 || gActiveChartIndex>=static_cast<int>(gCharts.size()))return;
    const auto& chart=gCharts[static_cast<size_t>(gActiveChartIndex)];
    const auto widths=bms::chartDeckWidths(chart,gUiScale),designWidths=bms::chartDeckWidths(chart);
    const int lane=widths.key,scratch=widths.scratch;
    const int width=bms::chartLayout(chart,0,lane,scratch).totalWidth();
    // The skin gear is authored for the FHD deck; scale it to the deck the engine actually draws.
    const int designWidth=bms::chartLayout(chart,0,designWidths.key,designWidths.scratch).totalWidth();
    const int available=std::max(0,gWindowWidth-width),edge=std::min(ui(64),available/2);
    const int left=gGearPosition==0?edge:gGearPosition==2?std::max(edge,available-edge):available/2;
    const int line=bms::optionJudgeLine(gChartOptions,gPlayhead,judgementLineY(gWindowHeight),gWindowHeight);
    gTimingHintScale=gUiScale;gTimingHintX=left+width/2.0;gTimingHintY=line+(-200+gJudgementTextYOffset-24)*gTimingHintScale;
    gSkinApplication.setJudgementAnchor(left+width/2.0,line,gWindowWidth,gWindowHeight);
    sevenmania::skin::SkinGearState gear;gear.judgementColor=gJudgementColor;gear.judgementScale=gJudgementPop.scale();gear.position=gGearPosition;gear.mirrorUi=gSkinMirrorUi;gear.mode=bms::gearModeIndex(chart.mode);gear.active=gear.mode==0||gSkinApplication.supportsGearMode(gear.mode);gear.suppressNativeGear=!gear.active;gear.centerX=left+width/2.0;gear.lineY=line;gear.scale=designWidth>0?double(width)/designWidth:gUiScale;gear.gauge=gGrooveGauge.value();gear.speed=gNoteSpeed;gear.rotation=gScratchDisc.angle()*180/3.141592653589793;gear.pressed=gPressed;gear.keyLabels=skinKeyLabels(chart.mode);gear.keyGuide=skinKeyGuideShown();gSkinApplication.setGearState(gear);
}
void drawAppliedSkinDc(HDC dc) {
    if(gPlaybackLoad.preparing()){
        if(captureSevenManiaSkinData(gSkinData))gSkinApplication.drawDc(dc,gWindowWidth,gWindowHeight,gSkinData.snapshot());
        return; // Keep prepared GPU resources; do not run browse-media decoders here.
    }
    if(gGpuFrame||gLoading||gStartupSplash||!gSkinApplication.active())return;
    gSkinApplication.releaseGpu();
    gNoteImages.releaseGpu();
    if(!captureSevenManiaSkinData(gSkinData))return;
    const bool boxResized=gSkinBoxWidth!=gWindowWidth||gSkinBoxHeight!=gWindowHeight;
    if(!gPlaying&&!gShowingResult) {
        const int index=selectedIndex();
        const auto* chart=index>=0&&index<static_cast<int>(gCharts.size())?&gCharts[static_cast<size_t>(index)]:nullptr;
        double time=std::chrono::duration<double>(std::chrono::steady_clock::now()-gBrowseSelectedAt).count();
        if(chart){const double start=chart->previewStartSeconds<chart->durationSeconds?chart->previewStartSeconds:0;const double length=std::max(1.0,std::min(chart->previewDurationSeconds,chart->durationSeconds-start));time=start+std::fmod(std::max(0.0,time),length);}
        time=gSongPreview.chartTime(time);
        const int w=std::clamp(gWindowWidth,1,1920),h=std::clamp(gWindowHeight,1,1080);
        if(gSkinBoxWidth!=gWindowWidth||gSkinBoxHeight!=gWindowHeight){gSkinTitleRevision=0;gSkinBgaRevision=0;gSkinBoxWidth=gWindowWidth;gSkinBoxHeight=gWindowHeight;}
        for(auto mode:{std::string_view("title"),std::string_view("bga"),std::string_view("hybrid")})if(gSkinApplication.hasBox(mode)) {
            auto& backdrop=mode=="title"?gSkinTitleBackdrop:mode=="hybrid"?gSkinHybridBackdrop:gSkinBgaBackdrop;
            if((mode=="bga"||(mode=="hybrid"&&gBrowseBackgroundMode==1))&&std::chrono::steady_clock::now()-gBrowseSelectedAt<std::chrono::milliseconds(500)){
                backdrop.reset();const std::uint32_t gray=0xff404040;gSkinApplication.updateBox(mode,&gray,1,1);
                if(mode=="bga")gSkinBgaRevision=0;else gSkinHybridRevision=0;
                continue;
            }
            const auto& pixels=backdrop.render(chart,mode=="hybrid"?gBrowseBackgroundMode:mode=="title"?2:1,mode=="hybrid"&&!gCustomBackgroundName.empty()?executableFolder()/L"Assets"/gCustomBackgroundName:std::filesystem::path{},gBackgroundBlur,gBackgroundOpacity,w,h,time);
            auto& revision=mode=="title"?gSkinTitleRevision:mode=="hybrid"?gSkinHybridRevision:gSkinBgaRevision;
            if(revision!=backdrop.revision()){
                if(!pixels.empty())gSkinApplication.updateBox(mode,pixels.data(),w,h);
                else {const std::uint32_t gray=0xff404040;gSkinApplication.updateBox(mode,&gray,1,1);}
                revision=backdrop.revision();
            }
        }
    }
    gSkinApplication.setJudgementRainbow(gJudgementRainbow && std::chrono::steady_clock::now()<gJudgementUntil);
    updateSkinJudgementAnchor();
    gSkinApplication.setJudgementOffset(gJudgementTextYOffset);
    gSkinApplication.drawDc(dc,gWindowWidth,gWindowHeight,gSkinData.snapshot());
    if(!gPlaying&&!gShowingResult){
        gBrowseTextOverflow=gSkinApplication.scrolling()||(gSkinApplication.hasBox("bga")||gSkinApplication.hasBox("hybrid"));
        if(boxResized){gSkinTitleRevision=0;gSkinBgaRevision=0;InvalidateRect(gWindow,nullptr,FALSE);}
    }
}
void drawAppliedSkinGpu(ID2D1RenderTarget*,int width,int height) {
    gSkinApplication.drawGpu(float(width),float(height));
    // Pause commands are queued after this background pass; keep judgement below them.
    if(gPaused)gSkinApplication.drawGpuForeground(float(width),float(height));
}
void drawAppliedSkinForeground(ID2D1RenderTarget* target,int width,int height) {
    gSkinApplication.drawGpuForeground(float(width),float(height));
    static Microsoft::WRL::ComPtr<ID2D1RenderTarget> owner;
    static Microsoft::WRL::ComPtr<ID2D1Bitmap> fast,slow;
    if(owner.Get()!=target){owner=target;fast.Reset();slow.Reset();
        const auto load=[&](const TurntableImage& image,Microsoft::WRL::ComPtr<ID2D1Bitmap>& bitmap){if(image.loaded())target->CreateBitmap(D2D1::SizeU(image.width(),image.height()),image.pixels(),image.width()*4,D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),&bitmap);};
        load(gFastHintImage,fast);load(gSlowHintImage,slow);
    }
    if(!gPaused&&gTimingHint&&std::chrono::steady_clock::now()<gJudgementUntil){auto* bitmap=gTimingHint<0?fast.Get():slow.Get();if(bitmap){const float w=float(90*gTimingHintScale),h=float(32*gTimingHintScale);target->DrawBitmap(bitmap,D2D1::RectF(float(gTimingHintX)-w/2,float(gTimingHintY)-h/2,float(gTimingHintX)+w/2,float(gTimingHintY)+h/2));}}
}
void prepareAppliedSkinGpu() {
    HUD_SCOPE(SkinHostPrepare);
    gCompositionPresenter.setFrameOverlay(nullptr);gCompositionPresenter.setFrameForeground(nullptr);
    if(gGpuFrame&&gPlaying&&gLr2PlayScene&&!gLr2GpuFailed&&gLr2PlayScene->playLanes(gWindowWidth,gWindowHeight)) {
        gNoteImages.prepareGpu(gCompositionPresenter.renderTarget());
        return;
    }
    if(!gGpuFrame||!gSkinApplication.active())return;
    if (!hybridRendererEnabled) gNoteImages.prepareGpu(gCompositionPresenter.renderTarget());
    gSkinApplication.setJudgementRainbow(gJudgementRainbow && std::chrono::steady_clock::now()<gJudgementUntil);
    updateSkinJudgementAnchor();
    gSkinApplication.setJudgementOffset(gJudgementTextYOffset);
    if(captureSevenManiaSkinData(gSkinData)&&gSkinApplication.prepareGpu(gCompositionPresenter.renderTarget(),gSkinData.snapshot()))
        {gCompositionPresenter.setFrameOverlay(drawAppliedSkinGpu);if(!gPaused)gCompositionPresenter.setFrameForeground(drawAppliedSkinForeground);}
}
