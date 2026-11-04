// Included inside Main.cpp's UI owner namespace, with BMS_ENABLE_SKIN_BINDINGS.
constexpr int ID_SKIN_LOAD=1290, ID_SKIN_CLEAR=1291, ID_SKIN_ANIMATION=1292, ID_SKIN_RETRY=1293;
sevenmania::skin::SkinApplication gSkinApplication;
sevenmania::skin::SkinDataProvider gSkinData;
BrowseBackdrop gSkinTitleBackdrop, gSkinBgaBackdrop;
uint64_t gSkinTitleRevision=0, gSkinBgaRevision=0;
int gSkinBoxWidth=0, gSkinBoxHeight=0;
std::filesystem::path gSkinSource;
std::wstring gSkinStatus=L"기본 화면 사용 중";
HWND gSkinNativeStatus{};
std::filesystem::path gSkinWanted;
bool gSkinRetryPending=false;
bool gSkinMirrorUi=false;
std::filesystem::path defaultSkinPath(){return executableFolder()/L"Assets/Skins/DefaultReference.semaskin";}
void skinVisibility(bool ready){if(!ready){for(HWND button:gPauseButtons)if(IsWindow(button))ShowWindow(button,SW_HIDE);for(HWND child:{gList,gFolder,gInfo,gStatus,gHiddenSongs,gResultNext})if(IsWindow(child))ShowWindow(child,SW_HIDE);}else{if(IsWindow(gList))ShowWindow(gList,!gPlaying&&!gShowingResult&&!gStartupSplash?SW_SHOWNA:SW_HIDE);updateHiddenSongsVisibility();if(IsWindow(gResultNext))ShowWindow(gResultNext,gShowingResult?SW_SHOWNA:SW_HIDE);for(HWND button:gPauseButtons)if(IsWindow(button))ShowWindow(button,gPlaying&&gPaused?SW_SHOWNA:SW_HIDE);}}
void unloadSkinSelection(){if(IsWindow(gWindow)){KillTimer(gWindow,ID_SKIN_RETRY);KillTimer(gWindow,ID_SKIN_ANIMATION);}gCompositionPresenter.setFrameOverlay(nullptr);gCompositionPresenter.setFrameForeground(nullptr);gSkinApplication.clear();gSkinData.clear();gSkinRetryPending=false;gSkinWanted.clear();}


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
        gSkinApplication.clear();gSkinRetryPending=true;skinVisibility(false);SetTimer(gWindow,ID_SKIN_RETRY,1000,nullptr);InvalidateRect(gWindow,nullptr,FALSE);
        gSkinStatus=L"스킨을 불러오지 못했습니다. 빈 화면에서 1초 간격으로 재시도합니다.\n"+skinWide(error->path)+L": "+skinWide(error->message);
        return false;
    }
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
    if(path&&!path->empty()&&std::filesystem::path(*path).is_absolute())applySkinFile(*path,false);
    else applySkinFile(defaultSkinPath(),false);
}
void retrySkinSelection(){if(gSkinRetryPending&&!gSkinWanted.empty()){auto path=gSkinWanted;applySkinFile(path,false);}}
void drawAppliedSkinDc(HDC dc);
bool drawSkinOnly(HDC dc){
    fillRect(dc,{0,0,gWindowWidth,gWindowHeight},RGB(0,0,0));
    if(gSkinRetryPending||!gSkinApplication.active())return false;
    drawAppliedSkinDc(dc);return true;
}
std::optional<std::filesystem::path> pickSkinFile(HWND owner) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if(FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))) {
        gSkinStatus=L"스킨 파일 선택창을 열지 못했습니다.";return {};
    }
    const COMDLG_FILTERSPEC filters[]{{L"SevenMania 스킨",L"*.semaskin"}};
    dialog->SetFileTypes(1,filters);dialog->SetDefaultExtension(L"semaskin");dialog->SetTitle(L"SevenMania 스킨 불러오기");
    dialog->SetOptions(FOS_FORCEFILESYSTEM|FOS_FILEMUSTEXIST|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR|FOS_DONTADDTORECENT);
    const auto shown=dialog->Show(owner);
    if(shown==HRESULT_FROM_WIN32(ERROR_CANCELLED))return {};
    if(FAILED(shown)){gSkinStatus=L"스킨 파일 선택창에 오류가 발생했습니다.";return {};}
    Microsoft::WRL::ComPtr<IShellItem> item;PWSTR name{};
    if(FAILED(dialog->GetResult(&item))||FAILED(item->GetDisplayName(SIGDN_FILESYSPATH,&name))) {
        gSkinStatus=L"선택한 스킨 경로를 읽지 못했습니다.";return {};
    }
    // Keep the shell allocation safe even if constructing a path throws.
    struct Name {PWSTR p;~Name(){CoTaskMemFree(p);}} cleanup{name};
    return std::filesystem::path(name);
}
using SkinPicker=std::function<std::optional<std::filesystem::path>(HWND)>;
SkinPicker gSkinPicker=pickSkinFile;
void runSkinSettingsAction(int action) {
    try {
        if(action==ID_SKIN_LOAD) {if(const auto path=gSkinPicker(gSettingsWindow))applySkinFile(*path);}
        else if(action==ID_SKIN_CLEAR)clearSkinSelection();
    }catch(...){gSkinStatus=L"스킨을 불러오지 못했습니다. 기존 선택을 유지합니다.";}
    if(IsWindow(gSkinNativeStatus))SetWindowTextW(gSkinNativeStatus,skinSummary().c_str());
    setBindingStatus(gSkinStatus);
}
void updateSkinJudgementAnchor() {
    if(!gPlaying || gActiveChartIndex<0 || gActiveChartIndex>=static_cast<int>(gCharts.size()))return;
    const auto& chart=gCharts[static_cast<size_t>(gActiveChartIndex)];
    const bool dp=chart.mode==bms::ChartMode::TenKey||chart.mode==bms::ChartMode::FourteenKey;
    const int lane=dp?std::max(28,LaneLayout::defaultKeyWidth*2/3):LaneLayout::defaultKeyWidth;
    const int scratch=dp?68:LaneLayout::defaultScratchWidth;
    const int width=bms::chartLayout(chart,0,lane,scratch).totalWidth();
    const int available=std::max(0,gWindowWidth-width),edge=std::min(64,available/2);
    const int left=gGearPosition==0?edge:gGearPosition==2?std::max(edge,available-edge):available/2;
    const int line=bms::optionJudgeLine(gChartOptions,gPlayhead,judgementLineY(gWindowHeight),gWindowHeight);
    gTimingHintScale=std::min(gWindowWidth/1920.0,gWindowHeight/1080.0);gTimingHintX=left+width/2.0;gTimingHintY=line+(-200+gJudgementTextYOffset-24)*gTimingHintScale;
    gSkinApplication.setJudgementAnchor(left+width/2.0,line,gWindowWidth,gWindowHeight);
    sevenmania::skin::SkinGearState gear;gear.judgementColor=gJudgementColor;gear.judgementScale=gJudgementPop.scale();gear.position=gGearPosition;gear.mirrorUi=gSkinMirrorUi;gear.active=chart.mode==bms::ChartMode::SevenKey||chart.mode==bms::ChartMode::Unknown;gear.centerX=left+width/2.0;gear.lineY=line;gear.gauge=gGrooveGauge.value();gear.speed=gNoteSpeed;gear.rotation=gScratchDisc.angle()*180/3.141592653589793;gear.pressed=primaryPressed();gSkinApplication.setGearState(gear);
}
void drawAppliedSkinDc(HDC dc) {
    if(gGpuFrame||gLoading||gStartupSplash||!gSkinApplication.active())return;
    gSkinApplication.releaseGpu();
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
        for(auto mode:{std::string_view("title"),std::string_view("bga")})if(gSkinApplication.hasBox(mode)) {
            auto& backdrop=mode=="title"?gSkinTitleBackdrop:gSkinBgaBackdrop;
            const auto& pixels=backdrop.render(chart,mode=="title"?2:1,{},gBackgroundBlur,gBackgroundOpacity,w,h,time);
            auto& revision=mode=="title"?gSkinTitleRevision:gSkinBgaRevision;
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
        gBrowseTextOverflow=gSkinApplication.scrolling()||gSkinApplication.hasBox("bga");
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
    if(!gGpuFrame||!gSkinApplication.active())return;
    gSkinApplication.setJudgementRainbow(gJudgementRainbow && std::chrono::steady_clock::now()<gJudgementUntil);
    updateSkinJudgementAnchor();
    gSkinApplication.setJudgementOffset(gJudgementTextYOffset);
    if(captureSevenManiaSkinData(gSkinData)&&gSkinApplication.prepareGpu(gCompositionPresenter.renderTarget(),gSkinData.snapshot()))
        {gCompositionPresenter.setFrameOverlay(drawAppliedSkinGpu);if(!gPaused)gCompositionPresenter.setFrameForeground(drawAppliedSkinForeground);}
}
