#include "AudioEngine.h"
#include "PlaybackLoadState.h"
#include "PlaybackLoadJob.h"
#include <psapi.h>
#include "ControllerDialog.h"
#ifdef BMS_ENABLE_SKIN_BINDINGS
#include "skin/SevenManiaSkinData.h"
#include "skin/SkinApplication.h"
void emitSevenManiaSkinEvent(std::string_view event);
bool skinOwnsHud();
bool skinOwnsJudgement();
int selectSkinShortcutAt(int x,int y);
bool skinNeedsJudgementText();
void prepareSkinForPlayback();
bool preparingSkinAnimated();
bool drawPreparingSkin(HDC);
bool skinCoversLegacySlots(int left,int top,int right,int bottom);
#endif
#include "BmsParser.h"
#include "GameplayModePolicy.h"
#include "KeyProfiles.h"
#include "SongCatalog.h"
#include "ChartRules.h"
#include "ChartOptions.h"
#include "NoteEffects.h"
#include "ScrollTimeline.h"
#include "BgaTimeline.h"
#include "ChartOrdering.h"
#include "SongLibrary.h"
#include "SharedStartupSettings.h"
#include "SongImportDialog.h"
#include "SongSelection.h"
#include "SongFolderNavigation.h"
#include "SongMetadataCache.h"
#include "SongIndexWorker.h"
bms::SongIndexWorker gSongIndexWorker;
bms::SongLibrarySources gLibrarySources;
size_t gLibrarySourceSelection{};
constexpr UINT WM_LIBRARY_ACTION=WM_APP+96;
constexpr UINT WM_LIBRARY_PROGRESS=WM_APP+97;
std::wstring gLibraryIndexStatus=L"대기 중";
constexpr UINT WM_LIBRARY_INDEX=WM_APP+95;
bool gForceLibraryRescan{};
bms::ui::SongFolderNavigation gSongFolders;
#include "RmlSongListRenderer.h"
#include "RmlSettingsHost.h"
#include "SettingsController.h"
#include "TextMarquee.h"
#include "FramePacer.h"
#include "ResultFade.h"
#include "FrameStatistics.h"
#include "LongNoteHold.h"
#include "Judgement.h"
#include "AccuracyLimit.h"
#include "PerformanceTrace.h"
#include "HudFrameProbe.h"
#include "ExperimentProfile.h"
#include "BgaRaster.h"
#include "AsyncBgaRaster.h"
#include "VideoBga.h"
#include "AsyncVideoBga.h"
#include "BgaVideoLayers.h"
#include "VideoComposite.h"
#include "BgaSwitchState.h"
#include "BrowseBackdrop.h"
#include "SongPreview.h"
#include "UserBackground.h"
#include "PlayFeedback.h"
#include "TextPopAnimation.h"
#include "TextRasterCache.h"
#include "HudTextStyle.h"
#include "TextMask.h"
#include "GrooveGauge.h"
#include "ResultScore.h"
#include "PracticeMode.h"
#include "PracticePreview.h"
#include "KeyBeams.h"
#include "KeyDeck.h"
#include "TurntableImage.h"
#include "CompositionPresenter.h"
#include "HybridFrame.h"
#include "SystemResources.h"
#include <thread>
#include <tuple>
#include <atomic>

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <windows.h>
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")
#include <gdiplus.h>
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <mfapi.h>
#include <dwmapi.h>
#include <fcntl.h>
#include <io.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwmapi.lib")

#pragma comment(lib, "comctl32.lib")

namespace {

constexpr int ID_OPEN = 1001;
constexpr wchar_t BROWSE_SHORTCUT_HINT[] = L"[Enter] 플레이   [Shift + Enter] 연습모드   [Space] 설정   [F2] 곡 폴더 열기   [F5] 다시 검색";
constexpr int IDI_APP_ICON = 1;
constexpr int ID_PLAY = 1003;
constexpr int ID_RESCAN = 1004;
constexpr int ID_LIST = 1005;
constexpr int ID_FOLDER = 1006;
constexpr int ID_STATUS = 1007;
constexpr int ID_INFO = 1008;
constexpr int ID_HELP = 1009;
constexpr int ID_BIND = 1010;
constexpr int ID_SETTINGS = 1011;
constexpr int ID_FPS = 1012;
constexpr int ID_SPEED = 1013;
constexpr int ID_CLOSE_SETTINGS = 1014;
constexpr int ID_RESET_KEYS = 1015;
constexpr int ID_SETTINGS_STATUS = 1016;
constexpr int ID_SETTINGS_KEYS = 1017;
constexpr int ID_JUDGE_POSITION = 1018;
constexpr int ID_LANE_TRANSPARENCY = 1020;
constexpr int ID_BGA_DARKNESS = 1021;
constexpr int ID_LANE_COVER = 1022;
constexpr int ID_SCREEN_MODE = 1023;
constexpr int ID_GEAR_POSITION = 1024;
constexpr int ID_COMPOSITION = 1025;
constexpr int ID_DIRECT_COMPOSITION = 1273;
constexpr int ID_JUDGEMENT_PROFILE = 1026;
constexpr int ID_NOTE_HEIGHT = 1027;
constexpr int ID_HIDDEN_SONGS = 1028;
constexpr int ID_GROOVE_GAUGE = 1029;
constexpr int ID_SOUND_BUFFER = 1034;
constexpr int ID_AUDIO_OUTPUT_MODE = 1053;
constexpr int ID_BROWSE_BACKGROUND = 1035;
constexpr int ID_CUSTOM_BACKGROUND = 1036;
constexpr int ID_BACKGROUND_BLUR = 1037;
constexpr int ID_BACKGROUND_OPACITY = 1038;
constexpr int ID_SONG_PREVIEW = 1039;
constexpr int ID_PREVIEW_VOLUME = 1040;
constexpr int ID_CPU_LIMIT = 1041;
constexpr int ID_MEMORY_LIMIT = 1042;
constexpr int ID_GPU_LIMIT = 1043;
constexpr int ID_NOTE_SUBPIXEL = 1270;
constexpr int ID_NOTE_ANTIALIAS = 1271;
constexpr int ID_LOW_LATENCY = 1272;
constexpr int ID_RESOURCE_USAGE = 1044;
constexpr int ID_COMBO_TEXT_Y = 1045;
constexpr int ID_JUDGEMENT_TEXT_Y = 1046;
constexpr int ID_CPU_PERFORMANCE = 1047;
constexpr int ID_PRACTICE_PLAY = 1048;
constexpr int ID_CATEGORY_BASE = 1060;
constexpr int ID_VOLUME_BASE = 1030;
constexpr UINT WM_PLAY_CHART_READY=WM_APP+48;
constexpr UINT WM_LOAD_FINISHED = WM_APP + 20;
constexpr UINT WM_LOAD_PROGRESS = WM_APP + 21;
constexpr UINT WM_APPLY_SETTINGS = WM_APP + 42;
constexpr UINT WM_RML_SETTINGS_NAVIGATE = WM_APP + 43;
constexpr UINT WM_RML_SETTINGS_FALLBACK = WM_APP + 44;
constexpr UINT WM_RML_SETTINGS_PICK_IMAGE = WM_APP + 45;
constexpr UINT WM_RML_SETTINGS_ACTION = WM_APP + 46;
constexpr UINT WM_ACCURACY_RESTART=WM_APP+47;
constexpr int ID_COLOR_BASE = 1100;
constexpr int ID_SEPARATOR_BASE = 1200;
constexpr int ID_SEPARATOR_OPACITY=1096;
int gSeparatorOpacity=100;
constexpr int ID_RESULT_NEXT = 1250;
constexpr int ID_PAUSE_RESUME = 1251;
constexpr int ID_PAUSE_RESTART = 1252;
constexpr int ID_PAUSE_QUIT = 1253;
constexpr int ID_PAUSE_PRACTICE = 1254;
constexpr int ID_SUMMARY_FPS = 1260;
constexpr int ID_SUMMARY_BGA = 1261;
constexpr int ID_SUMMARY_SPEED = 1262;
constexpr int ID_SUMMARY_JUDGEMENT = 1263;
constexpr int ID_SUMMARY_SCREEN_MODE = 1264;
constexpr int ID_SUMMARY_GAUGE = 1265;
constexpr int ID_TIMING_HINT=1052;
constexpr int ID_INTRO_ENABLE=1097,ID_INTRO_DURATION=1098;
constexpr int ID_PREVIEW_START=1099;
constexpr int ID_ACCURACY_LIMIT=1049,ID_SUMMARY_ACCURACY_LIMIT=1266;
constexpr int ID_RESOLUTION = 1048;
constexpr int JUDGE_OFFSET_MIN = -4096;
constexpr int JUDGE_OFFSET_MAX = 4096;
constexpr int HUD_TEXT_Y_MIN = -256;
constexpr int HUD_TEXT_Y_MAX = 256;
constexpr int PLAYFIELD_TOP = 0;

struct PaletteColor { const wchar_t* name; COLORREF color; };
struct DisplayResolution { int width{}; int height{}; };
constexpr std::array<PaletteColor, 10> gPalette{{
    {L"노랑", RGB(251, 191, 36)}, {L"하늘색", RGB(56, 189, 248)},
    {L"파랑", RGB(96, 165, 250)}, {L"초록", RGB(74, 222, 128)},
    {L"빨강", RGB(248, 113, 113)}, {L"분홍", RGB(244, 114, 182)},
    {L"보라", RGB(167, 139, 250)}, {L"흰색", RGB(241, 245, 249)},
    {L"주황", RGB(251, 146, 60)}, {L"민트", RGB(45, 212, 191)}
}};
constexpr std::array<UINT, 8> DEFAULT_KEYS{ VK_LSHIFT, 'S', 'D', 'F', VK_SPACE, 'J', 'K', 'L' };

HWND gWindow{};
HWND gList{}, gFolder{}, gStatus{}, gInfo{};
HWND gHiddenSongs{};
HIMAGELIST gSongRowImages{};
bool gShowHiddenCharts{};
bool gHasPartialLoadCharts{};
std::wstring gPartialLoadSummary;
std::array<int, 2> gBrowseSelections{ -1, -1 };
HWND gHelp{}, gBind{};
HWND gPracticeWindow{}, gPracticeNext{}, gPracticeBack{};
HFONT gPracticeHudFont{};
PracticeMode gPractice;
PracticePreview gPracticePreview;
int gPracticeStage{}, gPracticeBoundary{}, gPracticeNotesChartIndex = -1;
std::vector<bms::Note> gPracticeOriginalNotes;
double gPracticeOriginalDuration{};
HWND gResultNext{};
std::array<HWND, 4> gPauseButtons{};
std::wstring gPracticePresetSection;
size_t gPauseSelection{};
HWND gSettingsWindow{}, gSettingsStatus{}, gSettingsKeys{};
HWND gSettingsPreviewOverlay{};
HWND gFpsChoice{}, gSpeedChoice{}, gJudgementChoice{}, gNoteHeightChoice{}, gGrooveGaugeChoice{};
HWND gTimingHintChoice{};
HWND gAccuracyLimitChoice{},gSummaryAccuracyLimit{};
HWND gSummaryFps{}, gSummaryBga{}, gSummarySpeed{}, gSummaryJudgement{}, gSummaryScreenMode{}, gSummaryGauge{};
HWND gSoundBufferChoice{}, gAudioOutputModeChoice{};
HWND gBrowseBackgroundChoice{}, gCustomBackgroundLabel{}, gBackgroundBlurSlider{}, gBackgroundBlurValue{};
HWND gBackgroundOpacitySlider{}, gBackgroundOpacityValue{}, gSongPreviewChoice{}, gPreviewVolumeSlider{}, gPreviewVolumeValue{};
HWND gCpuLimitSlider{}, gCpuLimitValue{}, gMemoryLimitSlider{}, gMemoryLimitValue{}, gGpuLimitSlider{}, gGpuLimitValue{}, gResourceUsageChoice{}, gCpuPerformanceChoice{};
HWND gComboTextYSlider{}, gComboTextYValue{}, gJudgementTextYSlider{}, gJudgementTextYValue{};
int gBrowseBackgroundMode{2}, gBackgroundBlur = 20, gBackgroundOpacity = 10, gPreviewVolume = 50;
int gCpuLimitPercent = 100, gMemoryLimitPercent = 100, gGpuLimitPercent = 100;
bool gSongPreviewEnabled{true};
bool gShowResourceUsage{};
bool gCpuPerformanceMode{};
int gDefaultThreadPriority = THREAD_PRIORITY_NORMAL;
std::wstring gCustomBackgroundName, gBrowseMediaKey, gBrowseAudioKey, gBrowseAudioError;
std::wstring gMemoryLimitError;
BrowseBackdrop gBrowseBackdrop;
#include "SelectionAudio.h"
SongPreview gSongPreview;
SelectionAudio gSelectionAudio;
std::chrono::steady_clock::time_point gBrowseSelectedAt{};
std::chrono::steady_clock::time_point gBrowseTextStartedAt = std::chrono::steady_clock::now();
std::wstring gBrowseTextKey;
bool gBrowseTextOverflow{};
struct BrowseDifficultyCarousel {
    std::wstring groupKey;
    double from{};
    double target{};
    std::chrono::steady_clock::time_point started{};
    bool animating{};

    double position(std::chrono::steady_clock::time_point now) const {
        if (!animating) return target;
        constexpr double durationSeconds = 0.160;
        const double t = std::clamp(std::chrono::duration<double>(now - started).count() / durationSeconds, 0.0, 1.0);
        const double eased = 1.0 - std::pow(1.0 - t, 3.0);
        return from + (target - from) * eased;
    }

    bool active(std::chrono::steady_clock::time_point now) {
        if (!animating) return false;
        if (now - started >= std::chrono::milliseconds(160)) {
            from = target;
            animating = false;
            return false;
        }
        return true;
    }
};
BrowseDifficultyCarousel gBrowseDifficultyCarousel;
bool gStartupSplash{};
int gStartupProgress{};
std::unique_ptr<Gdiplus::Bitmap> gStartupSplashImage;
constexpr std::array<int, 6> SOUND_BUFFER_OPTIONS{32, 64, 128, 256, 512, 1024};
int gSoundBufferFrames = 256, gAudioOutputMode{};
HWND gJudgePosition{}, gJudgeValue{};
HWND gLaneTransparencySlider{}, gLaneTransparencyValue{};
HWND gBgaDarknessChoice{};
HWND gLaneCoverSlider{}, gLaneCoverValue{};
HWND gScreenModeChoice{};
HWND gResolutionChoice{};
std::wstring gMonitorDevice;
int gMonitorWidth{}, gMonitorHeight{};
int gResolutionWidth{}, gResolutionHeight{};
bool gResolutionConfigured{};
std::vector<DisplayResolution> gResolutionOptions;
HWND gGearChoice{}, gCompositionChoice{}, gDirectCompositionChoice{};
HWND gSubpixelChoice{}, gAntialiasChoice{}, gLowLatencyChoice{};
bool gNoteSubpixel = true, gNoteAntialias = true;
bool gReflexRequested = false;
bool gLatencyInputPump = false;
std::array<HWND, 7> gCategoryButtons{};
std::array<HWND, 4> gVolumeSliders{}, gVolumeValues{};
std::unordered_map<HWND, int> gSettingsControlCategory;
std::unordered_map<HWND, RECT> gSettingsControlRects;
HWND gSettingsBody{};
int gSettingsCategory{}, gCreatingSettingsCategory{};
std::array<int, 4> gVolumes{100, 100, 100, 100};
int gGearPosition = 1;
bool gCompositionEnabled{};
bool gDirectCompositionEnabled = true;
CompositionPresenter gCompositionPresenter;
SystemResources gSystemResources;
bool gSongInfoVisible{};std::wstring gSongInfoText;
#ifdef BMS_ENABLE_SKIN_BINDINGS
sevenmania::skin::SkinDataProvider gSongInformationData;
#endif
bool gGpuFrame{}, gGpuNotePass{}, gGpuUnavailable{};
int gGpuFrameAccumulator = 100;
std::thread gLoadWorker;
std::atomic_bool gLoadCancelled{};
bool gLoading{};
struct LoadResult {
    std::vector<bms::Chart> charts; bms::Chart chart; std::wstring error;
    std::vector<std::filesystem::path> skippedFiles;
    std::vector<std::wstring> importWarnings;
    std::filesystem::path folder;
    size_t copiedFiles{}, unchangedFiles{};
    double scanSeconds{};
    int index = -1; bool playback{},cachedIndex{};
};
LoadResult gLoadResult;
int gScreenMode{}, gAppliedScreenMode{}, gSettingsScroll{};
constexpr int SETTINGS_CONTENT_HEIGHT = 1570;
WINDOWPLACEMENT gWindowedPlacement{sizeof(WINDOWPLACEMENT)};
std::wstring gFullscreenDevice;
int gBgaBrightness = 50;
std::array<HWND, 8> gColorChoices{};
std::array<HWND, 7> gSeparatorButtons{};
std::array<bool, 7> gLaneSeparators{ true, false, false, false, false, false, false };
KeyBeams gKeyBeams;
ScratchDisc gScratchDisc;
TurntableImage gTurntableImage;
TurntableImage gBlackKeyImage;
TurntableImage gWhiteKeyImage;
TurntableImage gHudSlotImage;
TurntableImage gHiSpeedSlotImage, gGaugeSlotImage;
TurntableImage gFastHintImage,gSlowHintImage;
int gTimingHint=0;double gTimingHintX=0,gTimingHintY=0,gTimingHintScale=1;
HFONT gGaugeModeFont{};
TurntableImage gHitSplashSheet;
int gTargetFps = 240;
int gJudgeLineOffset{};
int gComboTextYOffset{}, gJudgementTextYOffset=0;
int gLaneTransparency = 34;
int gBgaDarkness = 50;
int gLaneCoverPercent{};
int gAuthoredCoverPercent{};
PlayFeedback gFeedback;
TextPopAnimation gComboPop, gJudgementPop;
TextRasterCache gTextRaster, gHudTextRaster, gHudMaskRaster;
EmbeddedHudFont gEmbeddedHudFont;
TextRasterCache gResourceTextRaster;
GrooveGauge gGrooveGauge;
bool gResultGaugeCleared = true;
bool gResultGaugeFailed{};
std::array<int, 8> gLaneColors{ 4, 7, 2, 7, 2, 7, 2, 7 };
FramePacer gFramePacer;
bool gOutputWakeReady{};
HFONT gFont{}, gSmallFont{}, gHudBoldFont{}, gListFont{}, gListSelectedFont{}, gListLevelFont{}, gListSelectedLevelFont{};
HFONT gBrowseTitleFont{}, gBrowseLevelFont{}, gBrowseMetadataFont{}, gBrowseRankFont{};
HFONT gBrowseSongTitleFont{}, gBrowseSongInfoFont{};
HFONT gComboFont{}, gJudgementFont{};
HFONT gResultRankFont{}, gResultScoreFont{};
HFONT gResultTitleFont{}, gResultDetailsFont{}, gResultNextFont{};
HBRUSH gControlBrush{};
HDC gBackBufferDc{};
HBITMAP gBackBufferBitmap{};
HGDIOBJ gBackBufferOriginalBitmap{};
void* gBackBufferPixels{};
int gBackBufferWidth{}, gBackBufferHeight{};
HDC gSettingsPreviewDc{};
HBITMAP gSettingsPreviewBitmap{};
HGDIOBJ gSettingsPreviewOriginalBitmap{};
void* gSettingsPreviewPixels{};
int gSettingsPreviewWidth{}, gSettingsPreviewHeight{};
bool gSettingsPreviewShowing{};
HDC gResultBackdropDc{};
HBITMAP gResultBackdropBitmap{};
HGDIOBJ gResultBackdropOriginalBitmap{};
void* gResultBackdropPixels{};
int gResultBackdropWidth{}, gResultBackdropHeight{};
HDC gBgaBackplateDc{};
HBITMAP gBgaBackplateBitmap{};
HGDIOBJ gBgaBackplateOriginalBitmap{};
void* gBgaBackplatePixels{};
std::shared_ptr<const uint32_t> gDirectBgaFrame, gDirectBgaOverlay;
int gBgaBackplateWidth{}, gBgaBackplateHeight{};
bool gBgaBackplateDirty{ true };
bool gBgaBackplateMissState{};
int gBgaBackplateFieldX{}, gBgaBackplateLaneWidth{}, gBgaBackplateTop{}, gBgaBackplateJudgeY{};
int gBgaBackplateScratchWidth{};
int gBgaBackplateDarkness = -1, gBgaBackplateTransparency = -1;
int gBgaBackplateBrightness = -1;
std::unordered_map<COLORREF, HBRUSH> gBrushCache;
AsyncBgaRaster gBgaRaster;
AsyncVideoBga gVideoBga;
BgaVideoLayers gBgaVideos;
double gPoorBgaStart=-1;
BgaSwitchState gBgaSwitch;
bms::ForcedOptions gChartOptions;
uint64_t gBgaBackplateVideoGeneration{};
std::array<std::wstring, 4> gBgaActiveImages;
std::array<std::wstring, 4> gBgaBackplateImages;
size_t gBgaEventIndex{};
double gBgaVideoStartSeconds{};
double gMissBgaUntil{};
ULONG_PTR gGdiPlusToken{};
std::vector<bms::Chart> gCharts;
bms::ui::SongSelectionModel gSongSelection;
bms::ui::RmlSongListRenderer gRmlSongList;
bms::ui::RmlSettingsHost gRmlSettingsHost;
bms::ui::RmlSettingsHost gRmlSettingsPage;
bool gRmlSettingsPageEnabled = bms::ui::RmlSettingsHost::available();
bool gRmlSettingsPageActive{}, gRmlSettingsPageFailed{}, gNativeSettingsFallback{};
std::filesystem::path gRmlSettingsAssetRoot;
struct RecordJudgement {std::wstring spec=L"LEGACY";int rank=-1;};
struct PlayRecord {
    bool played{};
    bool cleared{};
    unsigned maxCombo{};
    int bestAccuracyScore{};
    RecordJudgement scoreJudgement,comboJudgement,clearJudgement;
};
std::unordered_map<std::wstring, PlayRecord> gPlayRecords;
std::vector<std::wstring> gChartRecordKeys;
std::filesystem::path gPlayRecordsFile;
std::filesystem::path gCurrentFolder;
std::filesystem::path gSettingsFile;
constexpr UINT_PTR ID_SONG_LIST_ANIMATION = 0xB501;
constexpr UINT_PTR ID_PAUSE_PRESENT_RETRY = 0xB502;
constexpr auto songListRepeatInitialDelay = std::chrono::milliseconds(220);
constexpr auto songListRepeatInterval = std::chrono::milliseconds(50);
constexpr double songListSpringOmega = 75.0;
constexpr int songListFallbackRowHeight = 58;
struct SongListScrollSpring {
    double position{}, target{}, velocity{};
    std::chrono::steady_clock::time_point lastUpdate{};
    bool animating{};

    void snap(double value, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
        position = target = value; velocity = 0.0; lastUpdate = now; animating = false;
    }
    bool advance(std::chrono::steady_clock::time_point now) {
        if (!animating) return false;
        if (lastUpdate == std::chrono::steady_clock::time_point{}) { lastUpdate = now; return true; }
        const double dt = std::max(0.0, std::chrono::duration<double>(now - lastUpdate).count());
        lastUpdate = now;
        if (dt <= 0.0) return true;
        if (dt >= 1.0) { snap(target, now); return false; }
        const double error = position - target;
        const double c = velocity + songListSpringOmega * error;
        const double decay = std::exp(-songListSpringOmega * dt);
        position = target + (error + c * dt) * decay;
        velocity = (velocity - songListSpringOmega * c * dt) * decay;
        const double nextError = position - target;
        if ((error > 0.0 && nextError <= 0.0) || (error < 0.0 && nextError >= 0.0)) {
            snap(target, now);
            return false;
        }
        if (std::abs(position - target) < 0.001 && std::abs(velocity) < 0.05) snap(target, now);
        return animating;
    }
    void retarget(double next, std::chrono::steady_clock::time_point now) {
        advance(now); target = next;
        if (!animating && std::abs(position - target) < 0.001) { snap(next, now); return; }
        if (lastUpdate == std::chrono::steady_clock::time_point{}) lastUpdate = now;
        animating = true;
    }
    void rebaseCycles(int rowCount) {
        if (rowCount <= 0 || !std::isfinite(position) || !std::isfinite(target)) return;
        const double cycleOffset = std::floor(position / static_cast<double>(rowCount)) * rowCount;
        if (cycleOffset == 0.0) return;
        position -= cycleOffset;
        target -= cycleOffset;
    }
};
SongListScrollSpring gSongListScroll;
UINT gSongListHeldKey{};
std::chrono::steady_clock::time_point gSongListNextRepeat{};
int gSongListWheelRemainder{};
constexpr std::int64_t songListInvalidRenderWindow = std::numeric_limits<std::int64_t>::min();
std::int64_t gSongListRenderWindowStart{songListInvalidRenderWindow};
int gSongListRenderWindowSize{};
void scheduleSongListAnimationTimer();
void clearSongListHeldKey();
AudioEngine gAudio;
bool gPlaying = false;
bool gPaused{};
bool gShowingResult = false;
ResultFade gResultFade;
constexpr UINT_PTR ID_RESULT_FADE = 0xB503;
std::vector<uint32_t> gResultFadePixels;
int gResultFadeWidth{}, gResultFadeHeight{};

void resetResultFade() {
    gResultFade.reset();
    std::vector<uint32_t>().swap(gResultFadePixels);
    if (gWindow) KillTimer(gWindow, ID_RESULT_FADE);
}

void drawResultFade(HDC dc) {
    if (!gResultFade.active()) return;
    const auto now = ResultFade::Clock::now();
    RECT client{}; GetClientRect(gWindow, &client);
    if (gResultFade.outgoing(now)) {
        if (!gResultFadePixels.empty()) {
            BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = gResultFadeWidth; info.bmiHeader.biHeight = -gResultFadeHeight;
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
            StretchDIBits(dc, 0, 0, client.right, client.bottom, 0, 0, gResultFadeWidth, gResultFadeHeight,
                gResultFadePixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
        } else FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    }
    Gdiplus::Graphics graphics(dc);
    const auto alpha = static_cast<BYTE>(std::lround(255 * gResultFade.darkness(now)));
    Gdiplus::SolidBrush overlay(Gdiplus::Color(alpha, 0, 0, 0));
    graphics.FillRectangle(&overlay, 0, 0, client.right, client.bottom);
}

ResultScore gResultScore;
bool gCapturingBindings = false;
bool gCaptureAwaitRelease = false;
bool gTimerResolutionRequested = false;
int gActiveChartIndex = -1;
double gPlayhead = 0;
bms::PlaybackLoadState gPlaybackLoad;
PlaybackLoadJob gPlaybackJob;
#include "SongIntroOverlay.h"
SongIntroOverlay gSongIntro;int gIntroEnabled=1,gIntroDuration=1100;
#include "PlayPreview.h"
PlayPreview gPlayPreview;
int gPreviewStartMode{};
std::chrono::steady_clock::time_point gPlaybackLoadStarted{};
std::chrono::steady_clock::time_point gTitleFadeOutStarted{};
constexpr auto titleFadeDuration = std::chrono::milliseconds(250);
double gNoteSpeed = 1.0;
int gNoteHeight = 12;
JudgementProfile gJudgementProfile = JudgementProfile::Normal;
GrooveGaugeMode gGrooveGaugeMode = GrooveGaugeMode::Normal;
int gTimingHintCondition=0;
AccuracyLimit gAccuracyLimit=AccuracyLimit::None;
bool gAccuracyRestartPending=false;
std::uint64_t gPlaybackGeneration=0;
void clearAccuracyRestart(){gAccuracyRestartPending=false;++gPlaybackGeneration;}
void requestAccuracyRestart(TimingJudgement grade){
    if(!gPlaying||gPaused||gLoading||gAccuracyRestartPending||!belowAccuracyLimit(gAccuracyLimit,grade))return;
    gAccuracyRestartPending=true;
    if(!PostMessageW(gWindow,WM_ACCURACY_RESTART,static_cast<WPARAM>(gPlaybackGeneration),0))gAccuracyRestartPending=false;
}
const wchar_t* activeGaugeModeText() {
    switch(gGrooveGaugeMode){case GrooveGaugeMode::Easy:return L"EASY";case GrooveGaugeMode::Hard:return L"HARD";default:return L"NORMAL";}
}
std::array<UINT, 8> gLaneKeys = DEFAULT_KEYS;
std::array<UINT, 16> gPendingLaneKeys{};
bms::KeyProfiles gKeyProfiles;int gBindingMode=1;
int gCaptureLane{};
size_t gMissScanIndex{};
std::vector<unsigned char> gHit;
std::vector<size_t> gLingeringLongNotes;
std::array<std::vector<size_t>, 16> gLaneNoteIndices;
std::vector<bool> gMineHit;
std::array<bool, 16> gPressed{};
std::array<bool,16> gKeyboardPressed{};
std::array<bool,8> gControllerPressed{};
bms::controller::Device gController;
bool gControllerConfiguring=false;
constexpr int ID_CONTROLLER_SETTINGS=1294;
constexpr UINT_PTR ID_CONTROLLER_POLL=0xB510;
std::array<LongNoteHold, 16> gHolds{};
std::wstring gJudgementText;
bool gJudgementRainbow = false;
int gSkinJudgementGrade=-1;
std::chrono::steady_clock::time_point gSkinJudgementAt{};
int gSkinJudgementMode=-1,gSkinComboMode=-1;
constexpr int ID_SKIN_JUDGEMENT_MODE=1050,ID_SKIN_COMBO_MODE=1051;
HWND gSkinJudgementChoice{},gSkinComboChoice{};
COLORREF gJudgementColor{ RGB(125, 211, 252) };
std::chrono::steady_clock::time_point gJudgementUntil{};
std::chrono::steady_clock::time_point gFpsEpoch{};
unsigned gFpsFrameCount{};
unsigned gMeasuredFps{};
FrameStatistics gFrameStatistics;
int gFrameStatisticsView=-1;
int gWindowWidth = 1240;
int gWindowHeight = 800;

int judgementLineY(int height) {
    const int nominal = static_cast<int>(std::lround(height * 2.0 / 3.0));
    const int upper = 40;
    const int lower = std::max(upper, height - 112);
    return std::clamp(nominal + gJudgeLineOffset, upper, lower);
}

bool keyGuideVisible(double playhead) {
    return playhead >= 0.0 && playhead < 2.0;
}

std::wstring number(double value, int precision = 1) {
    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%.*f", precision, value);
    return buffer;
}

void setText(HWND control, const std::wstring& text) {
    if (!control) return;
    SetWindowTextW(control, text.c_str());
    if (gWindow && (control == gFolder || control == gInfo || control == gStatus)) {
        RECT bounds{};
        if (GetWindowRect(control, &bounds)) {
            POINT origin{bounds.left, bounds.top};
            ScreenToClient(gWindow, &origin);
            OffsetRect(&bounds, origin.x - bounds.left, origin.y - bounds.top);
            InvalidateRect(gWindow, &bounds, FALSE);
        }
    }
}

void setStatus(const std::wstring& text) {
    setText(gStatus, text);
    if (gStartupSplash && gWindow) InvalidateRect(gWindow, nullptr, FALSE);
}
void updateSelection();
void updateBrowseMedia();
void stopBrowseMedia();
std::filesystem::path executableFolder();
void openSettings();
void openFullSettings();
void restartSongInformationEntrance();
RECT playBgaViewport();
void layoutControls();
void saveDisplaySettings();
void beginSelectedChart(bool practice = false);
void beginSongEntry(const bms::Chart&);
bool songEntryActive();
void drawSongEntry(HDC);

void drawPreviewNotes(HDC);
bool previewKey(WPARAM);
void beginPlaybackLoadWorker(const std::filesystem::path&,int,int,PracticeMode);
void openPracticeSetup();

std::wstring practiceTime(double seconds) {
    const int tenths = static_cast<int>(std::lround(std::max(0.0, seconds) * 10));
    const int minutes = tenths / 600, wholeSeconds = tenths / 10 % 60;
    return (minutes < 10 ? L"0" : L"") + std::to_wstring(minutes) + L":" +
        (wholeSeconds < 10 ? L"0" : L"") + std::to_wstring(wholeSeconds) + L"." + std::to_wstring(tenths % 10);
}

double playbackTimingScale() {return (gPractice.enabled?gPractice.rate():1.0)*gChartOptions.rate;}

void restorePracticeNotes() {
    if (gPracticeNotesChartIndex >= 0 && gPracticeNotesChartIndex < static_cast<int>(gCharts.size())) {
        auto& chart = gCharts[static_cast<size_t>(gPracticeNotesChartIndex)];
        chart.notes = std::move(gPracticeOriginalNotes);
        chart.durationSeconds = gPracticeOriginalDuration;
    }
    gPracticeOriginalNotes.clear();
    gPracticeNotesChartIndex = -1;
    gPractice.enabled = false;
}

void appendPartialLoadReason(bms::Chart& chart, const std::wstring& reason) {
    if (reason.empty() || chart.partialLoadReason.find(reason) != std::wstring::npos) return;
    if (!chart.partialLoadReason.empty()) chart.partialLoadReason += L" · ";
    chart.partialLoadReason += reason;
}

void rebuildPartialLoadSummary() {
    std::vector<std::wstring> reasons;
    gHasPartialLoadCharts = false;
    for (const auto& chart : gCharts) {
        if (!chart.playable || !chart.partialLoad) continue;
        gHasPartialLoadCharts = true;
        const auto reason = chart.partialLoadReason.empty() ? L"일부 곡 파일을 읽지 못했습니다." : chart.partialLoadReason;
        if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end()) reasons.push_back(reason);
    }
    gPartialLoadSummary.clear();
    const size_t shown = std::min<size_t>(2, reasons.size());
    for (size_t i = 0; i < shown; ++i) {
        if (!gPartialLoadSummary.empty()) gPartialLoadSummary += L" / ";
        gPartialLoadSummary += reasons[i];
    }
    if (reasons.size() > shown) gPartialLoadSummary += L" / 외 " + std::to_wstring(reasons.size() - shown) + L"개 원인";
}

void syncRmlSettingsState(const std::wstring& status);
void setBindingStatus(const std::wstring& text) {
    setText(gSettingsWindow ? gSettingsStatus : gStatus, text);
    if (gRmlSettingsPageActive) syncRmlSettingsState(text);
}

std::wstring keyName(UINT key) {
    if (key == VK_LSHIFT) return L"L-SHIFT";
    if (key == VK_RSHIFT) return L"R-SHIFT";
    if (key == VK_LCONTROL) return L"L-CTRL";
    if (key == VK_RCONTROL) return L"R-CTRL";
    if (key == VK_LMENU) return L"L-ALT";
    if (key == VK_RMENU) return L"R-ALT";
    if (key == VK_SPACE) return L"SPACE";
    if (key >= 'A' && key <= 'Z') return std::wstring(1, static_cast<wchar_t>(key));
    if (key >= '0' && key <= '9') return std::wstring(1, static_cast<wchar_t>(key));
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) return L"NUM " + std::to_wstring(key - VK_NUMPAD0);
    if (key >= VK_F1 && key <= VK_F24) return L"F" + std::to_wstring(key - VK_F1 + 1);
    wchar_t name[80]{};
    UINT scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC_EX);
    LONG keyData = static_cast<LONG>(scan << 16);
    if ((scan & 0xff00) == 0xe000) keyData |= (1L << 24);
    if (GetKeyNameTextW(keyData, name, static_cast<int>(std::size(name))) > 0) return name;
    return L"VK " + std::to_wstring(key);
}

std::wstring keySummary() {
    std::wstring text = L"SCR " + keyName(gLaneKeys[0]);
    for (int lane = 1; lane < 8; ++lane) text += L"  " + keyName(gLaneKeys[static_cast<size_t>(lane)]);
    return text;
}

UINT normalizedKey(WPARAM key, LPARAM detail) {
    if (key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU) {
        UINT scan = static_cast<UINT>((detail >> 16) & 0xff);
        if ((detail & (1LL << 24)) != 0) scan |= 0xe000;
        const UINT mapped = MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX);
        if (mapped != 0) return mapped;
    }
    return static_cast<UINT>(key);
}

bool reservedBinding(UINT key) {
    return key == 0 || key == VK_ESCAPE || key == VK_RETURN || key == VK_PRIOR || key == VK_NEXT || key == VK_F1 || key == VK_F2 || key == VK_F5 ||
        key == VK_LWIN || key == VK_RWIN || key == VK_APPS || key == VK_TAB || key == VK_LMENU || key == VK_RMENU;
}

void loadKeyBindings() {
    if (gSettingsFile.empty()) return;
    constexpr std::array<const wchar_t*, 8> names{ L"Scratch", L"Key1", L"Key2", L"Key3", L"Key4", L"Key5", L"Key6", L"Key7" };
    std::array<UINT, 8> loaded{};
    for (size_t i = 0; i < loaded.size(); ++i) {
        const UINT value = GetPrivateProfileIntW(L"KeyBindings", names[i], gLaneKeys[i], gSettingsFile.c_str());
        if (value > 0xff || reservedBinding(value) || std::find(loaded.begin(), loaded.begin() + static_cast<ptrdiff_t>(i), value) != loaded.begin() + static_cast<ptrdiff_t>(i)) return;
        loaded[i] = value;
    }
    gLaneKeys = loaded;
    std::copy(gLaneKeys.begin(),gLaneKeys.end(),gKeyProfiles.keys[1].begin());gKeyProfiles.load(gSettingsFile);
    std::copy_n(gKeyProfiles.keys[1].begin(),8,gLaneKeys.begin());
}

void saveKeyBindings() {
    if (gSettingsFile.empty()) return;
    constexpr std::array<const wchar_t*, 8> names{ L"Scratch", L"Key1", L"Key2", L"Key3", L"Key4", L"Key5", L"Key6", L"Key7" };
    for (size_t i = 0; i < gLaneKeys.size(); ++i) {
        const std::wstring value = std::to_wstring(gLaneKeys[i]);
        WritePrivateProfileStringW(L"KeyBindings", names[i], value.c_str(), gSettingsFile.c_str());
    }
}

bool applyLaneKeys(const std::array<UINT, 8>& keys) {
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i] > 255 || reservedBinding(keys[i]) || std::find(keys.begin(), keys.begin()+i, keys[i]) != keys.begin()+i) return false;
    gLaneKeys = keys; saveKeyBindings();std::copy(keys.begin(),keys.end(),gKeyProfiles.keys[1].begin());gKeyProfiles.save(gSettingsFile,1);return true;
}

void loadDisplaySettings() {
    if (gSettingsFile.empty()) return;
    const auto startup=sevenmania::readStartupSettings(gSettingsFile);
    gBrowseBackgroundMode = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Browse", L"Background", 2, gSettingsFile.c_str())), 0, 3);
    gBackgroundBlur = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Browse", L"Blur", 20, gSettingsFile.c_str())), 0, 100);
    gBackgroundOpacity = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Browse", L"Opacity", 10, gSettingsFile.c_str())), 0, 100);
    gSongPreviewEnabled = GetPrivateProfileIntW(L"Sound", L"SongPreview", 1, gSettingsFile.c_str()) != 0;
    gPreviewVolume = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Sound", L"PreviewVolume", 50, gSettingsFile.c_str())), 0, 100);
    gAudioOutputMode = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Sound", L"OutputMode", 0, gSettingsFile.c_str())), 0, 1);
    gAudio.setWasapiExclusiveEnabled(gAudioOutputMode == 1);
    gCpuLimitPercent = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"System", L"CpuLimitPercent", 100, gSettingsFile.c_str())), 10, 100);
    gMemoryLimitPercent = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"System", L"MemoryLimitPercent", 100, gSettingsFile.c_str())), 10, 100);
    gGpuLimitPercent = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"System", L"GpuLimitPercent", 100, gSettingsFile.c_str())), 10, 100);
    gShowResourceUsage = GetPrivateProfileIntW(L"System", L"ShowUsage", 0, gSettingsFile.c_str()) != 0;
    gCpuPerformanceMode = GetPrivateProfileIntW(L"System", L"CpuPerformanceMode", 0, gSettingsFile.c_str()) != 0;
    gMemoryLimitError.clear();
    gSystemResources.setMemoryLimitPercent(gMemoryLimitPercent, gMemoryLimitError);
    std::array<wchar_t, 1024> backgroundName{};
    GetPrivateProfileStringW(L"Browse", L"CustomBackground", L"", backgroundName.data(), static_cast<DWORD>(backgroundName.size()), gSettingsFile.c_str());
    gCustomBackgroundName = std::filesystem::path(backgroundName.data()).filename().wstring();
    gGearPosition = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Display", L"GearPosition", 1, gSettingsFile.c_str())), 0, 2);
    const UINT legacyComposition = GetPrivateProfileIntW(L"Display", L"DirectComposition", 1, gSettingsFile.c_str());
    gDirectCompositionEnabled = legacyComposition != 0;
    gCompositionEnabled = GetPrivateProfileIntW(L"Display", L"GpuRendering", legacyComposition, gSettingsFile.c_str()) != 0;
    gNoteSubpixel = GetPrivateProfileIntW(L"Display", L"NoteSubpixel", 1, gSettingsFile.c_str()) != 0;
    gReflexRequested = GetPrivateProfileIntW(L"Display", L"NvidiaReflex", 0, gSettingsFile.c_str()) != 0;
    gNoteAntialias = GetPrivateProfileIntW(L"Display", L"NoteAntialias", 1, gSettingsFile.c_str()) != 0;
    constexpr std::array<const wchar_t*, 4> volumeKeys{L"MasterVolume", L"MusicVolume", L"KeyVolume", L"SfxVolume"};
    for (size_t i = 0; i < volumeKeys.size(); ++i)
        gVolumes[i] = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Sound", volumeKeys[i], 100, gSettingsFile.c_str())), 0, 100);
    gAudio.setVolumes(gVolumes[0], gVolumes[1], gVolumes[2], gVolumes[3]);
    const int fps = startup.fps;
    if (fps==vsyncFps||(fps>=0&&fps<=1500)) gTargetFps = fps;
    gJudgeLineOffset = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Display", L"JudgeLineOffset", 0, gSettingsFile.c_str())), JUDGE_OFFSET_MIN, JUDGE_OFFSET_MAX);
    gLaneTransparency = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Display", L"LaneTransparency", 34, gSettingsFile.c_str())), 0, 100);
    const int brightness = startup.brightness;
    gBgaBrightness = (brightness + 5) / 10 * 10; gBgaDarkness = 100 - gBgaBrightness;
    gScreenMode = startup.mode;
    gLaneCoverPercent = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Display", L"LaneCover", 0, gSettingsFile.c_str())), 0, 100);
    gSeparatorOpacity=std::clamp(int(GetPrivateProfileIntW(L"Display",L"SeparatorOpacity",100,gSettingsFile.c_str())),0,100);
    for (size_t i = 0; i < gLaneSeparators.size(); ++i) {
        const std::wstring key = L"Separator" + std::to_wstring(i);
        gLaneSeparators[i] = GetPrivateProfileIntW(L"Display", key.c_str(), i == 0 ? 1 : 0, gSettingsFile.c_str()) != 0;
    }
    // The persisted value is quarter-speed steps. Clamp old or edited config files
    // to the supported 0.50x–10.00x range.
    const UINT speed = GetPrivateProfileIntW(L"Display", L"SpeedSteps", 4, gSettingsFile.c_str());
    gNoteSpeed = std::clamp(static_cast<double>(speed) / 4.0, 0.5, 10.0);
    gJudgementProfile = JudgementProfile::Normal;
    gGrooveGaugeMode = static_cast<GrooveGaugeMode>(std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Gameplay", L"GrooveGaugeMode", 0, gSettingsFile.c_str())), 0, 2));
    gIntroEnabled=GetPrivateProfileIntW(L"Gameplay",L"SongIntro",1,gSettingsFile.c_str())!=0;
    gIntroDuration=std::clamp(int(GetPrivateProfileIntW(L"Gameplay",L"SongIntroMs",1100,gSettingsFile.c_str())),800,1500);
    gPreviewStartMode=std::clamp(int(GetPrivateProfileIntW(L"Gameplay",L"PreviewStart",0,gSettingsFile.c_str())),0,2);
    gTimingHintCondition=std::clamp(int(GetPrivateProfileIntW(L"Gameplay",L"TimingHintCondition",0,gSettingsFile.c_str())),0,3);
    gAccuracyLimit=static_cast<AccuracyLimit>(std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Gameplay",L"AccuracyLimit",0,gSettingsFile.c_str())),0,4));
    gSkinJudgementMode=1;
    gSkinComboMode=1;
    gComboTextYOffset = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Gameplay", L"ComboTextYOffset", 0, gSettingsFile.c_str())), HUD_TEXT_Y_MIN, HUD_TEXT_Y_MAX);
    gJudgementTextYOffset = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Gameplay", L"JudgementTextYOffset", 0, gSettingsFile.c_str())), HUD_TEXT_Y_MIN, HUD_TEXT_Y_MAX);
    const int bufferFrames = startup.buffer;
    gSoundBufferFrames = std::find(SOUND_BUFFER_OPTIONS.begin(), SOUND_BUFFER_OPTIONS.end(), bufferFrames) != SOUND_BUFFER_OPTIONS.end() ? bufferFrames : 256;
    gAudio.setBufferFrames(static_cast<uint32_t>(gSoundBufferFrames));
    gNoteHeight = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Gameplay", L"NoteHeight", 12, gSettingsFile.c_str())), 4, 32);
    gNoteHeight = std::clamp(((gNoteHeight + 1) / 2) * 2, 4, 32);
    for (size_t lane = 0; lane < gLaneColors.size(); ++lane) {
        const std::wstring key = L"Lane" + std::to_wstring(lane);
        const UINT value = GetPrivateProfileIntW(L"NoteColors", key.c_str(), gLaneColors[lane%8], gSettingsFile.c_str());
        if (value < gPalette.size()) gLaneColors[lane%8] = static_cast<int>(value);
    }
    const int resolutionWidth = startup.width;
    const int resolutionHeight = startup.height;
    gResolutionConfigured = resolutionWidth > 0 && resolutionHeight > 0;
    gResolutionWidth = gResolutionConfigured ? resolutionWidth : 0;
    gResolutionHeight = gResolutionConfigured ? resolutionHeight : 0;
}

void saveDisplaySettings() {
    if (gSettingsFile.empty()) return;
    WritePrivateProfileStringW(L"Browse", L"Background", std::to_wstring(gBrowseBackgroundMode).c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Browse", L"Blur", std::to_wstring(gBackgroundBlur).c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Browse", L"Opacity", std::to_wstring(gBackgroundOpacity).c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Browse", L"CustomBackground", gCustomBackgroundName.c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Sound", L"SongPreview", gSongPreviewEnabled ? L"1" : L"0", gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Sound", L"PreviewVolume", std::to_wstring(gPreviewVolume).c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Sound", L"BufferFrames", std::to_wstring(gSoundBufferFrames).c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Sound", L"OutputMode", std::to_wstring(gAudioOutputMode).c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"System", L"CpuLimitPercent", std::to_wstring(gCpuLimitPercent).c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"System", L"MemoryLimitPercent", std::to_wstring(gMemoryLimitPercent).c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"System", L"GpuLimitPercent", std::to_wstring(gGpuLimitPercent).c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"System", L"ShowUsage", gShowResourceUsage ? L"1" : L"0", gSettingsFile.c_str());
    WritePrivateProfileStringW(L"System", L"CpuPerformanceMode", gCpuPerformanceMode ? L"1" : L"0", gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Display", L"GearPosition", std::to_wstring(gGearPosition).c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Display", L"DirectComposition", gDirectCompositionEnabled ? L"1" : L"0", gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Display", L"GpuRendering", gCompositionEnabled ? L"1" : L"0", gSettingsFile.c_str());
    constexpr std::array<const wchar_t*, 4> volumeKeys{L"MasterVolume", L"MusicVolume", L"KeyVolume", L"SfxVolume"};
    for (size_t i = 0; i < volumeKeys.size(); ++i)
        WritePrivateProfileStringW(L"Sound", volumeKeys[i], std::to_wstring(gVolumes[i]).c_str(), gSettingsFile.c_str());
    const std::wstring fps = std::to_wstring(gTargetFps);
    const std::wstring judgeOffset = std::to_wstring(gJudgeLineOffset);
    const std::wstring speed = std::to_wstring(static_cast<int>(std::lround(gNoteSpeed * 4)));
    const std::wstring judgementProfile = std::to_wstring(static_cast<int>(gJudgementProfile));
    const std::wstring grooveGaugeMode = std::to_wstring(static_cast<int>(gGrooveGaugeMode));
    const std::wstring comboTextYOffset = std::to_wstring(gComboTextYOffset);
    const std::wstring judgementTextYOffset = std::to_wstring(gJudgementTextYOffset);
    const std::wstring noteHeight = std::to_wstring(gNoteHeight);
    WritePrivateProfileStringW(L"Display", L"FPS", fps.c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Display", L"NoteSubpixel", gNoteSubpixel ? L"1" : L"0", gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Display", L"NvidiaReflex", gReflexRequested ? L"1" : L"0", gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Display", L"NoteAntialias", gNoteAntialias ? L"1" : L"0", gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Display", L"JudgeLineOffset", judgeOffset.c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Display", L"SpeedSteps", speed.c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Gameplay", L"JudgementProfile", judgementProfile.c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Gameplay", L"GrooveGaugeMode", grooveGaugeMode.c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Display",L"SeparatorOpacity",std::to_wstring(gSeparatorOpacity).c_str(),gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Gameplay",L"SongIntro",std::to_wstring(gIntroEnabled).c_str(),gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Gameplay",L"SongIntroMs",std::to_wstring(gIntroDuration).c_str(),gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Gameplay",L"PreviewStart",std::to_wstring(gPreviewStartMode).c_str(),gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Gameplay",L"TimingHintCondition",std::to_wstring(gTimingHintCondition).c_str(),gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Gameplay",L"AccuracyLimit",std::to_wstring(static_cast<int>(gAccuracyLimit)).c_str(),gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Skin",L"JudgementMode",std::to_wstring(gSkinJudgementMode).c_str(),gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Skin",L"ComboMode",std::to_wstring(gSkinComboMode).c_str(),gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Gameplay", L"ComboTextYOffset", comboTextYOffset.c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Gameplay", L"JudgementTextYOffset", judgementTextYOffset.c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Gameplay", L"NoteHeight", noteHeight.c_str(), gSettingsFile.c_str());
    const std::wstring transparency = std::to_wstring(gLaneTransparency);
    WritePrivateProfileStringW(L"Display", L"LaneTransparency", transparency.c_str(), gSettingsFile.c_str());
    const std::wstring darkness = std::to_wstring(gBgaDarkness);
    WritePrivateProfileStringW(L"Display", L"BGADarkness", darkness.c_str(), gSettingsFile.c_str());
    const std::wstring brightness = std::to_wstring(gBgaBrightness), screenMode = std::to_wstring(gScreenMode);
    WritePrivateProfileStringW(L"Display", L"Brightness", brightness.c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Display", L"ScreenMode", screenMode.c_str(), gSettingsFile.c_str());
    const std::wstring resolutionWidth = gResolutionConfigured ? std::to_wstring(gResolutionWidth) : L"0";
    const std::wstring resolutionHeight = gResolutionConfigured ? std::to_wstring(gResolutionHeight) : L"0";
    WritePrivateProfileStringW(L"Display", L"ResolutionWidth", resolutionWidth.c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Display", L"ResolutionHeight", resolutionHeight.c_str(), gSettingsFile.c_str());
    const std::wstring cover = std::to_wstring(gLaneCoverPercent);
    WritePrivateProfileStringW(L"Display", L"LaneCover", cover.c_str(), gSettingsFile.c_str());
    for (size_t i = 0; i < gLaneSeparators.size(); ++i) {
        const std::wstring key = L"Separator" + std::to_wstring(i);
        WritePrivateProfileStringW(L"Display", key.c_str(), gLaneSeparators[i] ? L"1" : L"0", gSettingsFile.c_str());
    }
    for (size_t lane = 0; lane < gLaneColors.size(); ++lane) {
        const std::wstring key = L"Lane" + std::to_wstring(lane);
        const std::wstring value = std::to_wstring(gLaneColors[lane%8]);
        WritePrivateProfileStringW(L"NoteColors", key.c_str(), value.c_str(), gSettingsFile.c_str());
    }
}

bool applyCpuPerformanceMode() {
    const int priority = gCpuPerformanceMode ? THREAD_PRIORITY_ABOVE_NORMAL : gDefaultThreadPriority;
    return SetThreadPriority(GetCurrentThread(), priority) != FALSE;
}

void updateBindingCaptureStatus() {
    constexpr std::array<const wchar_t*, 8> laneNames{ L"스크래치", L"1번 건반", L"2번 건반", L"3번 건반", L"4번 건반", L"5번 건반", L"6번 건반", L"7번 건반" };
    setBindingStatus(L"키 배치 설정: " + std::to_wstring(gCaptureLane+1)+L"번 입력" + L" 키를 누르세요 (Esc 취소)");
}

void beginKeyBindingCapture() {
    gPendingLaneKeys.fill(0);
    gCaptureLane = 0;
    gCaptureAwaitRelease = false;
    gCapturingBindings = true;
    updateBindingCaptureStatus();
    SetFocus(gSettingsWindow ? gSettingsWindow : gWindow);
}

void cancelKeyBindingCapture() {
    gCapturingBindings = false;
    gCaptureAwaitRelease = false;
    setBindingStatus(L"키 배치 변경을 취소했습니다. 기존 배치를 유지합니다.");
}

void acceptBindingKey(UINT key) {
    if (reservedBinding(key)) {
        setBindingStatus(L"Enter, Esc, F1/F2/F5, PageUp/PageDown, Tab, Windows 키는 지정할 수 없습니다. 다른 키를 누르세요.");
        return;
    }
    if (std::find(gPendingLaneKeys.begin(), gPendingLaneKeys.begin() + gCaptureLane, key) != gPendingLaneKeys.begin() + gCaptureLane) {
        setBindingStatus(L"이미 다른 레인에 지정한 키입니다. 각 레인은 서로 다른 키를 사용해야 합니다.");
        return;
    }
    gPendingLaneKeys[static_cast<size_t>(gCaptureLane)] = key;
    gCaptureAwaitRelease = true;
    ++gCaptureLane;
    if (gCaptureLane < bms::KeyProfiles::counts[gBindingMode]) {
        updateBindingCaptureStatus();
        return;
    }
    if(gBindingMode==1){std::array<UINT,8> seven;std::copy_n(gPendingLaneKeys.begin(),8,seven.begin());if(!applyLaneKeys(seven)){cancelKeyBindingCapture();return;}}
    gKeyProfiles.keys[gBindingMode]=gPendingLaneKeys;gKeyProfiles.save(gSettingsFile,gBindingMode);
    gCapturingBindings = false;
    gCaptureAwaitRelease = false;
    setBindingStatus(L"키 배치를 저장했습니다.");
    setText(gSettingsKeys, keySummary());
    updateSelection();
}

// Run before RmlUi/IsDialogMessage so physical binding keys never become UI navigation.
bool handleSettingsCaptureMessage(UINT message, WPARAM key, LPARAM data) {
    if (!gSettingsWindow || !gCapturingBindings) return false;
    if (message == WM_KEYDOWN) {
        if (key == VK_ESCAPE) cancelKeyBindingCapture();
        else if (!gCaptureAwaitRelease && (data & (1LL << 30)) == 0) acceptBindingKey(normalizedKey(key,data));
        return true;
    }
    if (message == WM_KEYUP) { gCaptureAwaitRelease=false; return true; }
    if (message == WM_SYSKEYDOWN) { setBindingStatus(L"Alt 키와 Alt 조합은 레인에 지정할 수 없습니다."); return true; }
    return message==WM_SYSKEYUP || message==WM_CHAR || message==WM_DEADCHAR;
}

void setJudgement(const std::wstring& text, COLORREF color) {
    gTimingHint=0;
    gSkinJudgementGrade=text==L"MINE"?5:-1;gSkinJudgementAt=std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const bool alreadyVisible = !gJudgementText.empty() && now < gJudgementUntil;
    gJudgementText = text;
    gJudgementRainbow = false;
    gJudgementColor = color;
    if (alreadyVisible) gJudgementPop.reset();
    else gJudgementPop.start(now);
    gJudgementUntil = now + std::chrono::milliseconds(850);
#ifdef BMS_ENABLE_SKIN_BINDINGS
    emitSevenManiaSkinEvent("judgement");
#endif
    const bool poor = text.find(L"POOR") != std::wstring::npos || text.find(L"MISS") != std::wstring::npos;
    const bool bad = text.find(L"BAD") != std::wstring::npos;
    if (poor || bad) gFeedback.miss();
    if (poor) {gMissBgaUntil=gPlayhead+2.5;gPoorBgaStart=gPlayhead;}
}

void finishPlayback();
void captureBrowseBackdrop();
void startUiEffects() { gAudio.setWasapiExclusiveEnabled(false); std::wstring ignored; gAudio.startEffectsOnly(ignored); }

JudgementWindows currentJudgementWindows() {
    if(gActiveChartIndex<0 || gActiveChartIndex>=static_cast<int>(gCharts.size())) return judgementWindows(gJudgementProfile);
    return bms::chartJudgementWindows(gCharts[gActiveChartIndex],gPlayhead,gJudgementProfile);
}
std::array<bool,8> primaryPressed() { std::array<bool,8> keys{};std::copy_n(gPressed.begin(),8,keys.begin());return keys; }
void resetMines() {
    gMineHit.clear();
    if(gActiveChartIndex>=0 && gActiveChartIndex<static_cast<int>(gCharts.size()))
        gMineHit.resize(gCharts[gActiveChartIndex].mines.size());
}
void showTimingJudgement(TimingJudgement judgement, int scoreUnits = 2) {
    BMS_PERF_SCOPE(judgeLogicMs);BMS_PERF_COUNT(judgements);BMS_PERF_EVENT(3,static_cast<int>(judgement),gPlayhead);
    if (gShowingResult||gAccuracyRestartPending) return;
    auto gaugeJudgement=judgement;
    if((judgement==TimingJudgement::Good && (gChartOptions.crazy.contains(L"good2bad") || gChartOptions.crazy.contains(L"great2bad"))) ||
       (judgement==TimingJudgement::Great && gChartOptions.crazy.contains(L"great2bad")))gaugeJudgement=TimingJudgement::Bad;
    if(scoreUnits>0) {gGrooveGauge.apply(gaugeJudgement);gResultScore.apply(judgement,scoreUnits);}
    switch (judgement) {
    case TimingJudgement::PGreat: setJudgement(L"GREAT", RGB(255, 214, 20)); gJudgementRainbow = true; break;
    case TimingJudgement::Great: setJudgement(L"GREAT", RGB(74, 222, 128)); break;
    case TimingJudgement::Good: setJudgement(L"GOOD", RGB(56, 189, 248)); break;
    case TimingJudgement::Bad: setJudgement(L"BAD", RGB(251, 146, 60)); break;
    case TimingJudgement::Poor: setJudgement(L"POOR", RGB(248, 113, 113)); break;
    }
    gSkinJudgementGrade=static_cast<int>(judgement);
    requestAccuracyRestart(judgement);
    if (gPlaying && !gAccuracyRestartPending && !gPractice.enabled && gGrooveGauge.depleted()) finishPlayback();
}

void showHoldJudgement(HoldResult result, int scoreUnits=1) {
    switch (result) {
    case HoldResult::PGreat: showTimingJudgement(TimingJudgement::PGreat, scoreUnits); break;
    case HoldResult::Poor: showTimingJudgement(TimingJudgement::Poor, scoreUnits); break;
    case HoldResult::Bad: showTimingJudgement(TimingJudgement::Bad, scoreUnits); break;
    case HoldResult::None: break;
    }
}

void resolveHold(size_t lane,HoldResult result) {
    if(result==HoldResult::None || gActiveChartIndex<0)return;
    const auto& notes=gCharts[gActiveChartIndex].notes;const auto index=gHolds[lane].noteIndex();
    if(index>=notes.size())return;const auto& note=notes[index];
    if(result!=HoldResult::PGreat && note.longNote())gLingeringLongNotes.push_back(index);
    if(result==HoldResult::PGreat && !note.endSampleId.empty())gAudio.trigger(note.endSampleId);
    if(note.mode==bms::LongNoteMode::LN)showTimingJudgement(result==HoldResult::PGreat?gHolds[lane].headJudgement():TimingJudgement::Poor,2);
    else if(note.mode==bms::LongNoteMode::CN && result==HoldResult::PGreat) {
        // HoldResult marks an accepted release; its exact grade must still use
        // the authored rank and the player's real release offset.
        const auto grade=bms::judgeChartOffset(gCharts[gActiveChartIndex],gPlayhead,
            (gPlayhead-note.timeSeconds-note.lengthSeconds)/playbackTimingScale(),gJudgementProfile);
        if(grade<=TimingJudgement::Good)gFeedback.addCombo(1);
        showTimingJudgement(grade,2);
    } else {if(result==HoldResult::PGreat)gFeedback.addCombo(1);showHoldJudgement(result,2);}
}
void showHoldCombo(unsigned ticks) {
    if (!ticks) return;
    gFeedback.addCombo(ticks);
    // Body ticks keep the combined HUD alive; scoring and gauge changes belong
    // only to the existing head/release judgements.
    setJudgement(L"GREAT", RGB(255, 214, 20));
    gJudgementRainbow = true;gSkinJudgementGrade=0;
}

int bgaLayerIndex(bms::BgaLayer layer) {
    switch (layer) {
    case bms::BgaLayer::Base: return 0;
    case bms::BgaLayer::Poor: return 1;
    case bms::BgaLayer::Layer: return 2;
    case bms::BgaLayer::Layer2: return 3;
    }
    return 0;
}

void updateBgaState(const bms::Chart& chart, double timeSeconds) {
    while (gBgaEventIndex < chart.bga.size() && chart.bga[gBgaEventIndex].timeSeconds <= timeSeconds) {
        const auto& event = chart.bga[gBgaEventIndex++];
        auto& active = gBgaActiveImages[static_cast<size_t>(bgaLayerIndex(event.layer))];
        if (active != event.imageId) {
            active = event.imageId;
        }
    }
}

std::filesystem::path executableFolder() {
    std::wstring buffer(32768, L'\0');
    DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(size);
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path loadLastLibraryFolder() {
    if (gSettingsFile.empty()) return {};
    std::wstring value(32768, L'\0');
    const DWORD length = GetPrivateProfileStringW(L"Library", L"LastFolder", L"", value.data(), static_cast<DWORD>(value.size()), gSettingsFile.c_str());
    if (length == 0 || length >= value.size()) return {};
    value.resize(length);
    return std::filesystem::path(value);
}

void initializeSettingsPath() {
    std::wstring overridePath(32768, L'\0');
    const DWORD overrideLength = GetEnvironmentVariableW(L"BMS_PLAYER_CONFIG_PATH", overridePath.data(), static_cast<DWORD>(overridePath.size()));
    if (overrideLength > 0 && overrideLength < overridePath.size()) {
        overridePath.resize(overrideLength);
        std::filesystem::path file(overridePath);
        if (file.is_absolute()) { gSettingsFile = file; return; }
    }
#ifdef BMS_FRAME_DIAGNOSTICS
    gSettingsFile=executableFolder()/L"BmsPlayer-Test.ini";return;
#endif
#ifdef BMS_PERF_DIAGNOSTICS
    gSettingsFile = executableFolder() / L"BmsPlayer-profile.ini";
    return;
#endif
    try { gSettingsFile=sevenmania::settingsPath(); } catch(...) { gSettingsFile.clear(); }
}

std::wstring normalizedRecordPath(const std::filesystem::path& path) {
    if (path.empty()) return {};
    std::error_code ec;
    auto normalized = std::filesystem::absolute(path, ec);
    if (ec) normalized = path;
    std::wstring value = normalized.lexically_normal().wstring();
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}

std::wstring recordSectionForPath(const std::wstring& path) {
    uint64_t hash = 14695981039346656037ull;
    for (wchar_t ch : path) {
        const uint32_t value = static_cast<uint32_t>(ch);
        for (size_t byte = 0; byte < sizeof(wchar_t); ++byte) {
            hash ^= static_cast<uint8_t>(value >> (byte * 8));
            hash *= 1099511628211ull;
        }
    }
    return L"Play_" + std::to_wstring(hash);
}

void loadPlayRecords() {
    gPlayRecords.clear();
    if (gPlayRecordsFile.empty()) return;
    DWORD capacity = 4096;
    std::vector<wchar_t> sections;
    DWORD length = 0;
    for (;;) {
        sections.assign(capacity, L'\0');
        length = GetPrivateProfileSectionNamesW(sections.data(), capacity, gPlayRecordsFile.c_str());
        if (length < capacity - 2 || capacity >= 1u << 20) break;
        capacity *= 2;
    }
    for (DWORD offset = 0; offset < length;) {
        const std::wstring section(sections.data() + offset);
        offset += static_cast<DWORD>(section.size()) + 1;
        if (!section.starts_with(L"Play_")) continue;
        std::vector<wchar_t> storedPath(32768, L'\0');
        const DWORD pathLength = GetPrivateProfileStringW(section.c_str(), L"Path", L"", storedPath.data(),
            static_cast<DWORD>(storedPath.size()), gPlayRecordsFile.c_str());
        if (pathLength == 0 || pathLength >= storedPath.size()) continue;
        std::wstring path(storedPath.data(), pathLength);
        const auto key = normalizedRecordPath(path);
        if (key.empty() || recordSectionForPath(key) != section) continue;
        PlayRecord record;
        record.played = GetPrivateProfileIntW(section.c_str(), L"Played", 0, gPlayRecordsFile.c_str()) != 0;
        record.cleared = GetPrivateProfileIntW(section.c_str(), L"Cleared", 0, gPlayRecordsFile.c_str()) != 0;
        record.maxCombo = static_cast<unsigned>(std::max(0, static_cast<int>(GetPrivateProfileIntW(section.c_str(), L"MaxCombo", 0, gPlayRecordsFile.c_str()))));
        record.bestAccuracyScore = std::clamp(static_cast<int>(GetPrivateProfileIntW(section.c_str(), L"AccuracyScore", 0, gPlayRecordsFile.c_str())), 0, 1'000'000);
        for(auto pair:{std::pair{L"Score",&record.scoreJudgement},std::pair{L"Combo",&record.comboJudgement},std::pair{L"Clear",&record.clearJudgement}}) {
            wchar_t value[128]{};const std::wstring prefix=pair.first;
            GetPrivateProfileStringW(section.c_str(),(prefix+L"JudgeSpec").c_str(),L"LEGACY",value,128,gPlayRecordsFile.c_str());
            pair.second->spec=value;
            pair.second->rank=static_cast<int>(GetPrivateProfileIntW(section.c_str(),(prefix+L"JudgeRank").c_str(),-1,gPlayRecordsFile.c_str()));
        }
        gPlayRecords[key] = record;
    }
}

void rebuildChartRecordKeys() {
    gChartRecordKeys.resize(gCharts.size());
    for (size_t i = 0; i < gCharts.size(); ++i) gChartRecordKeys[i] = normalizedRecordPath(gCharts[i].path);
}

const PlayRecord* playRecordForChart(size_t chartIndex) {
    if (chartIndex >= gChartRecordKeys.size()) return nullptr;
    const auto found = gPlayRecords.find(gChartRecordKeys[chartIndex]);
    return found == gPlayRecords.end() || !found->second.played ? nullptr : &found->second;
}

void savePlayRecord(const std::filesystem::path& chartPath, bool cleared, unsigned maxCombo, int accuracyScore, const std::wstring& spec=L"LEGACY", int rank=-1) {
    const auto key = normalizedRecordPath(chartPath);
    if (key.empty() || gPlayRecordsFile.empty()) return;
    auto& record = gPlayRecords[key];
    const RecordJudgement provenance{spec,rank};
    if(!record.played || accuracyScore>record.bestAccuracyScore)record.scoreJudgement=provenance;
    if(!record.played || maxCombo>record.maxCombo)record.comboJudgement=provenance;
    if(!record.played || (cleared&&!record.cleared))record.clearJudgement=provenance;
    record.played = true;
    record.cleared = record.cleared || cleared;
    record.maxCombo = std::max(record.maxCombo, maxCombo);
    record.bestAccuracyScore = std::max(record.bestAccuracyScore, std::clamp(accuracyScore, 0, 1'000'000));
    const auto section = recordSectionForPath(key);
    WritePrivateProfileStringW(section.c_str(), L"Path", key.c_str(), gPlayRecordsFile.c_str());
    WritePrivateProfileStringW(section.c_str(), L"Played", L"1", gPlayRecordsFile.c_str());
    WritePrivateProfileStringW(section.c_str(), L"Cleared", record.cleared ? L"1" : L"0", gPlayRecordsFile.c_str());
    WritePrivateProfileStringW(section.c_str(), L"MaxCombo", std::to_wstring(record.maxCombo).c_str(), gPlayRecordsFile.c_str());
    WritePrivateProfileStringW(section.c_str(), L"AccuracyScore", std::to_wstring(record.bestAccuracyScore).c_str(), gPlayRecordsFile.c_str());
    for(auto pair:{std::pair{L"Score",&record.scoreJudgement},std::pair{L"Combo",&record.comboJudgement},std::pair{L"Clear",&record.clearJudgement}}) {
        const std::wstring prefix=pair.first;
        WritePrivateProfileStringW(section.c_str(),(prefix+L"JudgeSpec").c_str(),pair.second->spec.c_str(),gPlayRecordsFile.c_str());
        WritePrivateProfileStringW(section.c_str(),(prefix+L"JudgeRank").c_str(),std::to_wstring(pair.second->rank).c_str(),gPlayRecordsFile.c_str());
    }
    if (gList) InvalidateRect(gList, nullptr, FALSE);
}

std::wstring recordAccuracyText(int score) {
    const int hundredths = (std::clamp(score, 0, 1'000'000) + 50) / 100;
    const int fraction = hundredths % 100;
    return L"정확도  " + std::to_wstring(hundredths / 100) + L"." + (fraction < 10 ? L"0" : L"") +
        std::to_wstring(fraction) + L"%";
}

int selectedGroupIndex() {
    return gSongSelection.selectedGroupIndex();
}

bool selectSongGroupFromListRow(int row) {
    if (!gList || row < 0) return false;
    LVITEMW item{};
    item.mask = LVIF_PARAM;
    item.iItem = row;
    if(!ListView_GetItem(gList,&item))return false;
    if(item.lParam<0){gSongSelection.clearSelection();return true;}
    return gSongSelection.selectGroup(static_cast<int>(item.lParam));
}

int selectedIndex() {
    return gSongSelection.selectedChartIndex();
}

void saveLibraryNavigation(){const int i=selectedIndex();gSongFolders.save(i>=0&&i<int(gCharts.size())?gCharts[size_t(i)].path:std::filesystem::path{});}
int restoreLibraryChart(const std::filesystem::path& selected){
    const auto& groups=gSongSelection.groups();for(size_t i=0;i<groups.size();++i)for(size_t v=0;v<groups[i].chartIndices.size();++v)if(gCharts[groups[i].chartIndices[v]].path==selected){gSongSelection.selectGroup(int(i));gSongSelection.cycleSelectedVariant(int(v)-int(groups[i].selectedVariant));return int(i);}return -1;
}
void insertSongRow(size_t index);
void refreshSongRow(size_t index);
bool cycleSelectedVariant(int delta);

void stopBrowseMedia() {
    gSongPreview.reset();
    gBrowseBackdrop.reset();
    gBrowseMediaKey.clear();
    gBrowseAudioKey.clear();
    gBrowseAudioError.clear();
}

void updateBrowseMedia() {
    if (gPlaying || gShowingResult || gLoading || gPracticeWindow) { if (!gBrowseMediaKey.empty()) stopBrowseMedia(); return; }
    const int groupIndex = selectedGroupIndex();
    const auto& groups = gSongSelection.groups();
    const int index = groupIndex >= 0 && groupIndex < static_cast<int>(groups.size()) ?
        static_cast<int>(groups[static_cast<size_t>(groupIndex)].previewChartIndex) : selectedIndex();
    gSongPreview.pauseWarmup(false);
    const auto* chart = index >= 0 && index < static_cast<int>(gCharts.size()) ? &gCharts[static_cast<size_t>(index)] : nullptr;
    const std::wstring key = (chart ? chart->path.wstring() : L"") + L"|" + std::to_wstring(gBrowseBackgroundMode) +
        L"|" + (gSongPreviewEnabled ? L"1" : L"0");
    if (key != gBrowseMediaKey) {
        if (!gBrowseAudioError.empty()) setStatus(L"");
        stopBrowseMedia(); gBrowseMediaKey = key;
        gBrowseSelectedAt = std::chrono::steady_clock::now();
    }
}

void tickBrowseMedia() {
    if(!gPracticeWindow&&!gSettingsWindow&&!gPlaying&&!gLoading&&gSongIndexWorker.hasPending())PostMessageW(gWindow,WM_LIBRARY_INDEX,0,0);
    if (gPracticeWindow) {gSelectionAudio.stopMusic();return;}
    updateBrowseMedia();
    // Debounce rapid navigation; do not decode all the songs passed by the cursor.
    const int groupIndex = selectedGroupIndex();
    const auto& groups = gSongSelection.groups();
    const int index = groupIndex >= 0 && groupIndex < static_cast<int>(groups.size()) ?
        static_cast<int>(groups[static_cast<size_t>(groupIndex)].previewChartIndex) : selectedIndex();
    if (gSongPreviewEnabled && gBrowseAudioKey.empty() && index >= 0 && index < static_cast<int>(gCharts.size()) &&
        std::chrono::steady_clock::now() - gBrowseSelectedAt >= std::chrono::milliseconds(500)) {
        gSongPreview.select(gCharts[static_cast<size_t>(index)]); gBrowseAudioKey = gBrowseMediaKey;
    }
    gSongPreview.tick(gVolumes[0], gPreviewVolume, gSoundBufferFrames, SongPreview::fadeDurationMs);
    if(!gStartupSplash)gSelectionAudio.tickMusic(gVolumes[0],gVolumes[1],gSongPreview.playing(),gSoundBufferFrames);
    const auto error = gSongPreview.lastError();
    if (!error.empty() && error != gBrowseAudioError) {
        gBrowseAudioError = error; setStatus(L"미리보기: " + error);
    }
}

void chooseCustomBackground(HWND owner) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return;
    const COMDLG_FILTERSPEC filters[]{{L"이미지 파일", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif"}};
    dialog->SetFileTypes(1, filters);
    dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"곡 선택 배경 이미지");
    if (FAILED(dialog->Show(owner))) return;
    Microsoft::WRL::ComPtr<IShellItem> item;
    PWSTR name{};
    if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) return;
    const std::filesystem::path source(name); CoTaskMemFree(name);
    std::wstring message;
    if (!copyUserBackgroundImage(source, executableFolder() / L"Assets", gCustomBackgroundName, message)) {
        MessageBoxW(owner, message.c_str(), L"배경 이미지", MB_ICONERROR); return;
    }
    gBrowseBackgroundMode = 3;
    if (!gRmlSettingsPageActive) {
        SendMessageW(gBrowseBackgroundChoice, CB_SETCURSEL, 3, 0);
        setText(gCustomBackgroundLabel, gCustomBackgroundName);
    }
    gBrowseBackdrop.reset(); updateBrowseMedia(); saveDisplaySettings();
    InvalidateRect(gWindow, nullptr, FALSE);
}

void updateHiddenSongsVisibility() {
    if (!gHiddenSongs) return;
    const bool hasHidden = std::any_of(gCharts.begin(), gCharts.end(), [](const auto& chart) { return !chart.playable; });
    ShowWindow(gHiddenSongs, hasHidden && !gStartupSplash && !gPlaying && !gShowingResult ? SW_SHOWNA : SW_HIDE);
    EnableWindow(gHiddenSongs, hasHidden && !gLoading);
}

void finishStartupSplash() {
    if (!gStartupSplash) return;
    gStartupSplash = false;
    gStartupSplashImage.reset();
    ShowWindow(gList, SW_SHOWNA);
    updateHiddenSongsVisibility();
    InvalidateRect(gWindow, nullptr, FALSE);
    SetFocus(gList);
}

void updateSelection() {
    updateHiddenSongsVisibility();
    const int scrollIndex = selectedIndex();
    const auto scrollKey = scrollIndex >= 0 && scrollIndex < static_cast<int>(gCharts.size()) ?
        gCharts[static_cast<size_t>(scrollIndex)].path.wstring() : std::wstring{};
    if (scrollKey != gBrowseTextKey) {
        gBrowseTextKey = scrollKey;
        gBrowseTextStartedAt = std::chrono::steady_clock::now();
    }
    {
        const auto now = std::chrono::steady_clock::now();
        const int groupIndex = selectedGroupIndex();
        const auto& groups = gSongSelection.groups();
        if (groupIndex >= 0 && groupIndex < static_cast<int>(groups.size())) {
            const auto& group = groups[static_cast<size_t>(groupIndex)];
            std::wstring key = group.directory;
            key.push_back(L'\0'); key += group.title;
            key.push_back(L'\0'); key += std::to_wstring(static_cast<int>(group.mode));
            const double nextTarget = static_cast<double>(std::min(group.selectedVariant,
                group.chartIndices.empty() ? size_t{} : group.chartIndices.size() - 1));
            if (key != gBrowseDifficultyCarousel.groupKey) {
                gBrowseDifficultyCarousel.groupKey = std::move(key);
                gBrowseDifficultyCarousel.from = gBrowseDifficultyCarousel.target = nextTarget;
                gBrowseDifficultyCarousel.animating = false;
            } else if (nextTarget != gBrowseDifficultyCarousel.target) {
                // Retarget from the visible interpolated position so rapid input stays continuous.
                gBrowseDifficultyCarousel.from = gBrowseDifficultyCarousel.position(now);
                gBrowseDifficultyCarousel.target = nextTarget;
                gBrowseDifficultyCarousel.started = now;
                gBrowseDifficultyCarousel.animating = group.chartIndices.size() > 1;
            }
        } else {
            gBrowseDifficultyCarousel.groupKey.clear();
            gBrowseDifficultyCarousel.from = gBrowseDifficultyCarousel.target = 0.0;
            gBrowseDifficultyCarousel.animating = false;
        }
        if (gBrowseDifficultyCarousel.active(now)) scheduleSongListAnimationTimer();
    }
    updateBrowseMedia();
    const int index = selectedIndex();
    if (index < 0 || index >= static_cast<int>(gCharts.size())) {
        setText(gInfo, !gLoading && !gCharts.empty() && !gShowHiddenCharts &&
            !std::any_of(gCharts.begin(), gCharts.end(), [](const auto& chart) { return chart.playable; }) ?
            L"실행 가능한 채보가 없습니다. 숨겨진 파일 보기에서 차단 이유를 확인하세요." :
            L"곡을 선택한 뒤 ← / → 키로 같은 곡의 난이도를 고르고 Enter로 플레이하세요.");
        InvalidateRect(gWindow, nullptr, FALSE);
        return;
    }
    setText(gInfo, L"");
    InvalidateRect(gList, nullptr, FALSE);
    InvalidateRect(gWindow, nullptr, FALSE);
}

void showSelectedChartErrors() {
    const int index = selectedIndex();
    if (index < 0 || index >= static_cast<int>(gCharts.size())) return;
    const auto& chart = gCharts[static_cast<size_t>(index)];
    std::wstring details;
    auto append = [&](const std::wstring& text) {
        if (text.empty()) return;
        if (!details.empty()) details += L"\r\n\r\n";
        details += text;
    };
    append(chart.comment);
    append(chart.blockReason);
    append(chart.partialLoadReason);
    if (!chart.missingSampleFiles.empty()) {
        std::wstring missing = L"누락된 음원 파일 (" + std::to_wstring(chart.missingSampleFiles.size()) + L"개)";
        for (const auto& path : chart.missingSampleFiles) missing += L"\r\n" + path.wstring();
        append(missing);
    }
    if (details.empty()) details = L"확인된 오류가 없습니다.";
    const auto title = L"곡 오류 정보 — " + chart.title;
    MessageBoxW(gWindow, details.c_str(), title.c_str(), MB_OK | (chart.playable ? MB_ICONINFORMATION : MB_ICONWARNING));
}

void setSongRowText(size_t index, int rowIndex) {
    const auto& groups = gSongSelection.groups();
    if (index >= groups.size()) return;
    const auto& group = groups[index];
    if (group.chartIndices.empty()) return;
    const size_t variant = std::min(group.selectedVariant, group.chartIndices.size() - 1);
    const auto& chart = gCharts[group.chartIndices[variant]];
    std::wstring title = chart.title;
    if (!chart.subtitle.empty()) title += L"  " + chart.subtitle;
    std::wstring level = chart.playLevel.empty() ? L"-" : chart.playLevel;
    ListView_SetItemText(gList, rowIndex, 0, level.data());
    ListView_SetItemText(gList, rowIndex, 1, title.data());
}

void insertSongRow(size_t index) {
    LVITEMW row{};
    row.mask = LVIF_TEXT | LVIF_PARAM;
    row.iItem = ListView_GetItemCount(gList);
    row.pszText = const_cast<LPWSTR>(L"");
    row.lParam = static_cast<LPARAM>(index);
    const int inserted = ListView_InsertItem(gList, &row);
    if (inserted >= 0) setSongRowText(index, inserted);
}

void refreshSongRow(size_t index) {
    LVFINDINFOW find{}; find.flags = LVFI_PARAM; find.lParam = static_cast<LPARAM>(index);
    const int row = ListView_FindItem(gList, -1, &find);
    if (index < gSongSelection.groups().size() && row >= 0) setSongRowText(index, row);
}

bms::ui::SongEntryKind navigationKind(int id);
void rebuildSongRows(int preferred = -1) {
    clearSongListHeldKey();
    if (gWindow) KillTimer(gWindow, ID_SONG_LIST_ANIMATION);
    gSongListScroll.snap(0.0);
    gSongListRenderWindowStart = songListInvalidRenderWindow;
    gSongListRenderWindowSize = 0;
    SendMessageW(gList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(gList);
    if(gLibrarySources.configured())gSongFolders.configure(gLibrarySources.navigationRoots(),gLibrarySources.originalFolders);else gSongFolders.root(gCurrentFolder);gSongFolders.build(gCharts);
    if(gLibrarySources.configured())setText(gFolder,gLibrarySources.roots.empty()?L"곡 폴더: (없음)":gSongFolders.isRoot()?L"곡 폴더: ROOT":L"곡 폴더: "+gSongFolders.location().wstring());
    const auto folderRow=[&](int id,const std::wstring& title){LVITEMW item{};item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=ListView_GetItemCount(gList);item.pszText=const_cast<wchar_t*>(bms::ui::entryTypeLabel(navigationKind(id)));item.lParam=id;const int row=ListView_InsertItem(gList,&item);ListView_SetItemText(gList,row,1,const_cast<wchar_t*>(title.c_str()));};
    if(gLibrarySources.configured()?!gLibrarySources.roots.empty():!gCurrentFolder.empty()){if(!gSongFolders.isRoot())folderRow(-1,L"UP");else folderRow(-2,L"ALL SONGS");}
    for(size_t i=0;i<gSongFolders.folders.size();++i)folderRow(-3-int(i),gSongFolders.folders[i].filename().wstring());
    int selectedRow = -1;
    size_t hiddenCount = 0;
    for (const auto& chart : gCharts) if (!chart.playable) ++hiddenCount;
    const auto& groups = gSongSelection.groups();
    for (size_t i = 0; i < groups.size(); ++i) {
        const auto& group = groups[i];
        if (group.chartIndices.empty() || !gSongFolders.accepts(gCharts[group.chartIndices.front()].path) || gCharts[group.chartIndices.front()].playable == gShowHiddenCharts) continue;
        if (static_cast<int>(i) == preferred) selectedRow = ListView_GetItemCount(gList);
        insertSongRow(i);
    }
    if (selectedRow < 0 && ListView_GetItemCount(gList) > 0) selectedRow = std::clamp(gSongFolders.cursor(),0,ListView_GetItemCount(gList)-1);
    if (selectedRow >= 0) {
        ListView_SetItemState(gList, selectedRow, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(gList, selectedRow, FALSE);
        if (!selectSongGroupFromListRow(selectedRow)) gSongSelection.clearSelection();
    } else {
        gSongSelection.clearSelection();
    }
    gSongListScroll.snap(static_cast<double>(std::max(0, selectedRow)));
    SendMessageW(gList, WM_SETREDRAW, TRUE, 0);
    setText(gHiddenSongs, gShowHiddenCharts ? L"곡 목록으로 돌아가기" : L"숨겨진 파일 보기 (" + std::to_wstring(hiddenCount) + L")");
    EnableWindow(gHiddenSongs, !gLoading && (gShowHiddenCharts || hiddenCount != 0));
    updateSelection();
    SetFocus(gList);
    InvalidateRect(gList, nullptr, TRUE);
    InvalidateRect(gWindow, nullptr, FALSE);
}

bool navigateSongFolder(bool back=false) {
    if(gPlaying||gShowingResult||gLoading)return false;
    const int row=ListView_GetNextItem(gList,-1,LVNI_SELECTED);
    LVITEMW item{};item.mask=LVIF_PARAM;item.iItem=row;
    if(!back&&(row<0||!ListView_GetItem(gList,&item)||item.lParam>=0))return false;
    gSongFolders.remember(std::max(0,row));
    if(back){if(!gSongFolders.back())return false;}else gSongFolders.enter(int(item.lParam));
    rebuildSongRows();saveLibraryNavigation();return true;
}

UINT songListAnimationIntervalMs() {
    int refreshHz = 60;
    if (gWindow) {
        const HMONITOR monitor = MonitorFromWindow(gWindow, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEXW info{}; info.cbSize = sizeof(info);
        DEVMODEW mode{}; mode.dmSize = sizeof(mode);
        if (monitor && GetMonitorInfoW(monitor, &info) &&
            EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1)
            refreshHz = static_cast<int>(mode.dmDisplayFrequency);
    }
    return static_cast<UINT>(std::clamp(static_cast<int>(std::lround(1000.0 / refreshHz)), 4, 16));
}

void scheduleSongListAnimationTimer() {
    if (!gWindow || gPlaying || gShowingResult || gLoading) return;
    const auto now = std::chrono::steady_clock::now();
    if (gSongListScroll.animating || gSongListHeldKey || gBrowseDifficultyCarousel.active(now))
        SetTimer(gWindow, ID_SONG_LIST_ANIMATION, songListAnimationIntervalMs(), nullptr);
    else KillTimer(gWindow, ID_SONG_LIST_ANIMATION);
}

void clearSongListHeldKey() {
    gSongListHeldKey = 0;
    gSongListNextRepeat = {};
}

int songListRowHeight() {
    if (gList && ListView_GetItemCount(gList) > 0) {
        RECT row{};
        if (ListView_GetItemRect(gList, 0, &row, LVIR_BOUNDS) && row.bottom > row.top)
            return row.bottom - row.top;
    }
    return songListFallbackRowHeight;
}

int wrapSongListRow(std::int64_t virtualRow, int rowCount) {
    if (rowCount <= 0) return -1;
    const auto count = static_cast<std::int64_t>(rowCount);
    const auto remainder = virtualRow % count;
    return static_cast<int>(remainder < 0 ? remainder + count : remainder);
}

struct SongListVisualHit {
    int row{-1};
    std::int64_t virtualRow{};
    explicit operator bool() const noexcept { return row >= 0; }
};

SongListVisualHit songListHitTestAtVisualY(int y) {
    if (!gList) return {};
    RECT viewport{};
    if (!GetClientRect(gList, &viewport) || y < viewport.top || y >= viewport.bottom) return {};
    const int count = ListView_GetItemCount(gList);
    const int rowHeight = songListRowHeight();
    if (count <= 0 || rowHeight <= 0) return {};
    const double centerY = static_cast<double>(viewport.top + viewport.bottom) * 0.5;
    const double index = gSongListScroll.position + (static_cast<double>(y) - centerY) / rowHeight;
    const auto virtualRow = static_cast<std::int64_t>(std::llround(index));
    return {wrapSongListRow(virtualRow, count), virtualRow};
}

void beginSongListScroll(std::int64_t virtualRow) {
    const int count = gList ? ListView_GetItemCount(gList) : 0;
    if (count <= 0) return;
    if (!gRmlSongList.enabled()) {
        ListView_EnsureVisible(gList, wrapSongListRow(virtualRow, count), FALSE);
        return;
    }
    gSongListScroll.retarget(static_cast<double>(virtualRow), std::chrono::steady_clock::now());
    scheduleSongListAnimationTimer();
}

bool selectSongListRow(int row, std::int64_t virtualRow, bool playClick = true) {
    if (!gList || row < 0 || row >= ListView_GetItemCount(gList)) return false;
    const int current = ListView_GetNextItem(gList, -1, LVNI_SELECTED);
    if (current != row) {
        ListView_SetItemState(gList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemState(gList, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    if (playClick) gAudio.triggerEffect(L"click");
    else if(current!=row)gAudio.triggerEffect(L"songlistscroll");
    beginSongListScroll(virtualRow);
    return true;
}

bool navigateSongList(int direction, int steps = 1) {
    if (!gList || steps <= 0) return false;
    const int count = ListView_GetItemCount(gList);
    if (count <= 0) return false;
    const int current = ListView_GetNextItem(gList, -1, LVNI_SELECTED);
    const double base = current < 0 ? (direction > 0 ? -1.0 : 0.0) :
        gRmlSongList.enabled() ? gSongListScroll.target : static_cast<double>(current);
    const double nextVisualTarget = base + static_cast<double>(direction) * steps;
    const auto virtualRow = static_cast<std::int64_t>(std::llround(nextVisualTarget));
    const int next = wrapSongListRow(virtualRow, count);
    return selectSongListRow(next, virtualRow, false);
}

void tickSongListAnimations() {
    if (!gWindow || !gList) return;
    if (gPlaying || gShowingResult || gLoading) {
        clearSongListHeldKey();
        gSongListScroll.animating = false;
        KillTimer(gWindow, ID_SONG_LIST_ANIMATION);
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const bool carouselActive = gBrowseDifficultyCarousel.active(now);
    bool selectionChanged = false;
    if (gSongListHeldKey && now >= gSongListNextRepeat) {
        const auto late = std::chrono::duration_cast<std::chrono::milliseconds>(now - gSongListNextRepeat);
        const int dueSteps = std::clamp(1 + static_cast<int>(late.count() / songListRepeatInterval.count()), 1, 16);
        selectionChanged = navigateSongList(gSongListHeldKey == VK_DOWN ? 1 : -1, dueSteps);
        if (selectionChanged) gSongListNextRepeat = now + songListRepeatInterval;
        else clearSongListHeldKey();
    }
    const double previousPosition = gSongListScroll.position;
    const bool scrollActive = gSongListScroll.advance(now);
    gSongListScroll.rebaseCycles(ListView_GetItemCount(gList));
    const bool positionChanged = std::abs(gSongListScroll.position - previousPosition) >= 0.0001;
    if (positionChanged || selectionChanged) {
        InvalidateRect(gList, nullptr, FALSE);
        UpdateWindow(gList);
    }
    if (carouselActive) { InvalidateRect(gWindow, nullptr, FALSE); UpdateWindow(gWindow); }
    if (scrollActive || carouselActive || gSongListHeldKey) scheduleSongListAnimationTimer();
    else KillTimer(gWindow, ID_SONG_LIST_ANIMATION);
}

bool cycleSelectedVariant(int delta) {
    const int groupIndex = selectedGroupIndex();
    if (!gSongSelection.cycleSelectedVariant(delta)) return false;
    gAudio.triggerEffect(L"levelchange");
    refreshSongRow(static_cast<size_t>(groupIndex));
    updateSelection();
    InvalidateRect(gList, nullptr, FALSE);
    return true;
}

void insertLoadingRow() {
    LVITEMW row{};
    row.mask = LVIF_TEXT | LVIF_PARAM;
    row.iItem = 0;
    row.pszText = const_cast<LPWSTR>(L"Loading...");
    row.lParam = static_cast<LPARAM>(-1);
    ListView_InsertItem(gList, &row);
}

void toggleSongSort() {
    gSongSelection.toggleSort();
    const int preferred = gSongSelection.selectedGroupIndex();
    gBrowseSelections.fill(-1);
    gBrowseSelections[gShowHiddenCharts ? 1 : 0] = preferred;
    rebuildSongRows(preferred);
    setStatus(gSongSelection.sortDescending() ? L"곡 정렬: Z-A" : L"곡 정렬: A-Z");
}

void buildSongGroups() {
    gSongSelection.setCollectVariants(gLibrarySources.collectVariants);
    gSongSelection.setIndividualCharts(gLibrarySources.individualCharts);
    gSongSelection.rebuild(gCharts);
}

bms::ui::SongEntryKind navigationKind(int id) {
    if(id<=-3&&size_t(-id-3)<gSongFolders.folders.size()&&gSongFolders.isLibraryFolder(gSongFolders.folders[size_t(-id-3)]))return bms::ui::SongEntryKind::LibraryRoot;
    return bms::ui::navigationEntryKind(id);
}
void startLibraryValidation(const std::filesystem::path& target={}) {
    gLibraryIndexStatus=L"라이브러리 확인 중...";setStatus(gLibraryIndexStatus);
    std::vector<bms::Chart> previous;
    if(!target.empty()){previous.reserve(gCharts.size());for(const auto& chart:gCharts)previous.push_back(bms::SongMetadataCache::metadata(chart));}
    gSongIndexWorker.start(gLibrarySources,[window=gWindow]{PostMessageW(window,WM_LIBRARY_INDEX,0,0);},
        [window=gWindow,last=std::chrono::steady_clock::time_point{}](size_t done,size_t total)mutable{
            const auto now=std::chrono::steady_clock::now();if(done!=total&&done!=0&&now-last<std::chrono::milliseconds(100))return;last=now;
            PostMessageW(window,WM_LIBRARY_PROGRESS,total?WPARAM(done*100/total):100,0);
        },target,std::move(previous),target.empty()?std::vector<std::filesystem::path>{}:gSongFolders.knownFolders);
}
void applyLibraryView() {
    const int index=selectedIndex();const auto selected=index>=0&&index<int(gCharts.size())?gCharts[size_t(index)].path:std::filesystem::path{};
    gLibrarySources.save();buildSongGroups();
    gSongFolders.configure(gLibrarySources.navigationRoots(),gLibrarySources.originalFolders);
    // A flattened directory need not be a destination in smart mode. Return to
    // the selected chart's new display parent when changing topology.
    gSongFolders.build(gCharts);
    if(!selected.empty())gSongFolders.restore(gSongFolders.displayParent(selected),gCharts);
    rebuildSongRows(restoreLibraryChart(selected));saveLibraryNavigation();
}
bool registerLibraryPaths(const std::vector<std::filesystem::path>& paths) {
    std::vector<std::filesystem::path> added;
    for(auto path:paths){std::error_code ec;
        if(!std::filesystem::is_directory(path,ec)){
            const auto ext=bms::toLower(path.extension().wstring());
            if(!std::filesystem::is_regular_file(path,ec)||(ext!=L".bms"&&ext!=L".bme"&&ext!=L".bml"&&ext!=L".pms"&&ext!=L".bmson"&&ext!=L".mbm")){setStatus(L"지원하는 BMS 폴더 또는 채보 파일이 아닙니다.");continue;}
            path=path.parent_path(); // Register shared media in place, never copy.
        }
        if(gLibrarySources.add(path))added.push_back(gLibrarySources.roots.back());
    }
    if(!added.empty()){gLibrarySources.save();gSongFolders.configure(gLibrarySources.navigationRoots(),gLibrarySources.originalFolders);startLibraryValidation(added.size()==1?added.front():std::filesystem::path{});}
    else setStatus(L"이미 등록된 경로이거나 지원하지 않는 항목입니다.");
    return !added.empty();
}

void scanFolders(const std::vector<std::filesystem::path>& sources) {
    if (gLoading) return;
    for(const auto& source:sources)gLibrarySources.add(source);
    gLibrarySources.save();
    gSongPreview.clearCache();
    gSongPreview.pauseWarmup(true);
    saveLibraryNavigation();gSongIndexWorker.cancel();
    if (gLoadWorker.joinable()) gLoadWorker.join();
    gAudio.stop();
    gPlaying = false;
    const auto libraryRoot = gLibrarySources.roots.empty()?std::filesystem::path{}:gLibrarySources.roots.front();
    gCurrentFolder = libraryRoot;
    gHasPartialLoadCharts = false;
    gPartialLoadSummary.clear();
    setText(gFolder, L"곡 폴더: " + (libraryRoot.empty()?std::wstring(L"(없음)"):libraryRoot.wstring()));
    if (!gSettingsFile.empty()) WritePrivateProfileStringW(L"Library", L"LastFolder", libraryRoot.c_str(), gSettingsFile.c_str());
    gSongSelection.clear();
    ListView_DeleteAllItems(gList);
    gCharts.clear();
    gChartRecordKeys.clear();
    updateHiddenSongsVisibility();
    gShowHiddenCharts = false;
    gBrowseSelections.fill(-1);
    EnableWindow(gHiddenSongs, FALSE);
    insertLoadingRow();
    setStatus(L"곡 라이브러리를 불러오고 있습니다...");
    UpdateWindow(gWindow);

    gLoading = true; gLoadCancelled = false;
    gLoadResult = {};
    const bool force=gForceLibraryRescan;gForceLibraryRescan=false;
    const auto librarySources=gLibrarySources;
    gLoadWorker = std::thread([librarySources, libraryRoot,force] {
    auto& charts = gLoadResult.charts;
    try {
    const auto progress = [](WPARAM stage) {
        return [stage, last = std::chrono::steady_clock::time_point{}](size_t completed, size_t total) mutable {
            const auto now = std::chrono::steady_clock::now();
            if (completed == 0 || completed == total || now - last >= std::chrono::milliseconds(100)) {
                PostMessageW(gWindow, WM_LOAD_PROGRESS, stage, total ? static_cast<LPARAM>(completed * 100 / total) : 100);
                last = now;
            }
        };
    };
    gLoadResult.folder = libraryRoot;
    const auto start = std::chrono::steady_clock::now();
    if(!force)charts=bms::snapshotSongLibraries(librarySources,&gLoadCancelled);
    gLoadResult.cachedIndex=!charts.empty();
    if(charts.empty()){
        auto update=bms::scanSongLibraries(librarySources,&gLoadCancelled,progress(2));
        charts=std::move(update.charts);gLoadResult.importWarnings=std::move(update.warnings);
    }
    const auto pathIsWithin = [](const std::filesystem::path& parent, const std::filesystem::path& child) {
        const auto normalizedParent = std::filesystem::absolute(parent).lexically_normal();
        const auto normalizedChild = std::filesystem::absolute(child).lexically_normal();
        auto parentPart = normalizedParent.begin();
        auto childPart = normalizedChild.begin();
        for (; parentPart != normalizedParent.end(); ++parentPart, ++childPart)
            if (childPart == normalizedChild.end() || _wcsicmp(parentPart->c_str(), childPart->c_str()) != 0) return false;
        return true;
    };
    for (auto& chart : charts) {
        if (!chart.playable) continue;
        for (const auto& skipped : gLoadResult.skippedFiles) {
            if (!pathIsWithin(chart.path.parent_path(), skipped)) continue;
            chart.partialLoad = true;
            appendPartialLoadReason(chart, L"복사하지 못한 파일: " + skipped.filename().wstring());
        }
        if (chart.partialLoad && chart.partialLoadReason.empty())
            chart.partialLoadReason = L"일부 채보 데이터를 읽지 못했습니다.";
    }
    std::stable_sort(charts.begin(), charts.end(), [&](const bms::Chart& a, const bms::Chart& b) {
        const int levelOrder = bms::comparePlayLevelAscending(a, b);
        if (levelOrder != 0) return levelOrder < 0;

        // Keep equal-level and unranked charts deterministic without inventing levels.
        const int groupA = bms::ui::songNameGroup(a.title), groupB = bms::ui::songNameGroup(b.title);
        if (groupA != groupB) return groupA < groupB;
        const int titleOrder = bms::ui::compareTitleText(a.title, b.title, groupA);
        if (titleOrder != 0) return titleOrder < 0;
        return bms::ui::compareTitleText(a.path.wstring(), b.path.wstring(), 2) < 0;
    });
    gLoadResult.scanSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    } catch (const bms::LibraryError& error) { gLoadResult.error = error.message; }
    catch (const std::filesystem::filesystem_error& error) { gLoadResult.error = L"곡 파일을 복사하거나 읽지 못했습니다: " + error.path1().wstring(); }
    catch (...) { gLoadResult.error = L"곡 폴더를 읽지 못했습니다. 폴더 경로를 확인하세요."; }
    PostMessageW(gWindow, WM_LOAD_FINISHED, 0, 0);
    });
}

void scanFolder(const std::filesystem::path& folder) {
    scanFolders({folder});
}

void releaseBgaBackplate() {
    if (gBgaBackplateDc && gBgaBackplateOriginalBitmap) SelectObject(gBgaBackplateDc, gBgaBackplateOriginalBitmap);
    if (gBgaBackplateBitmap) DeleteObject(gBgaBackplateBitmap);
    if (gBgaBackplateDc) DeleteDC(gBgaBackplateDc);
    gBgaBackplateDc = nullptr; gBgaBackplateBitmap = nullptr; gBgaBackplateOriginalBitmap = nullptr;
    gDirectBgaFrame.reset(); gDirectBgaOverlay.reset();
    gBgaBackplatePixels = nullptr; gBgaBackplateWidth = gBgaBackplateHeight = 0;
    gBgaBackplateDirty = true;
}

void focusBrowseInput(bool activateWindow = false) {
    if (!gWindow || !IsWindow(gWindow) || gPlaying || gShowingResult ||
        (gLoading && gLoadResult.playback) || gSettingsWindow || gPracticeWindow) return;
    if (activateWindow) {
        SetForegroundWindow(gWindow);
        SetActiveWindow(gWindow);
    }
    HWND target = gList && IsWindowVisible(gList) && IsWindowEnabled(gList) ? gList : gWindow;
    SetFocus(target);
}

void showBrowseView() {
    if (gLoading) { if(gLoadResult.playback)gPlaybackLoad.cancel();gLoadCancelled = true; setStatus(L"불러오기를 취소하고 있습니다..."); return; }
    gSongIntro.reset();
    gPlaying = false;
    gPaused = false;
    gGpuFrame = false;
    gCompositionPresenter.reset();
    gFeedback.reset();
    gGrooveGauge.reset();
    gKeyBeams.reset();
    if(gActiveChartIndex>=0 && gActiveChartIndex<static_cast<int>(gCharts.size())) {
        const auto layout=bms::chartLayout(gCharts[gActiveChartIndex],0,68,104);
        gKeyBeams.configure(layout);gFeedback.configure(layout);
    }
    gScratchDisc.reset();
    if(gPlaybackLoad.phase()==bms::PlaybackLoadPhase::Playing)gPlaybackLoad.reset();
    else if(gPlaybackLoad.preparing())gPlaybackLoad.cancel();
    gTitleFadeOutStarted = {};
    resetResultFade();
    gShowingResult = false;
    clearSongListHeldKey();
    gSongListScroll.snap(static_cast<double>(std::max(0, ListView_GetNextItem(gList, -1, LVNI_SELECTED))));
    gSongListRenderWindowStart = songListInvalidRenderWindow;
    gSongListRenderWindowSize = 0;
    if (gWindow) KillTimer(gWindow, ID_SONG_LIST_ANIMATION);
    restorePracticeNotes();
    gCapturingBindings = false;
    gCaptureAwaitRelease = false;
    gAudio.releaseSong();
    gBgaRaster.reset();
    gTextRaster.clear(); gHudTextRaster.clear(); gHudMaskRaster.clear();
    releaseBgaBackplate();
    startUiEffects();
    gPressed.fill(false);gKeyboardPressed.fill(false);gControllerPressed.fill(false);gController.mapper.reset();clearAccuracyRestart();
    gHolds.fill(LongNoteHold{});
    gLingeringLongNotes.clear();
    resetMines();
    gBgaEventIndex = 0;
    gBgaActiveImages.fill(L"");gBgaActiveImages[1]=L"00";
    gVideoBga.reset();
    gBgaVideos.reset();gBgaSwitch.reset();
    gMissBgaUntil = 0;
    ShowWindow(gList, SW_SHOW);
    // These STATIC controls only retain their text. drawBrowseScreen paints
    // the labels directly so child controls cannot erase the animated page.
    ShowWindow(gFolder, SW_HIDE);
    ShowWindow(gStatus, SW_HIDE);
    ShowWindow(gInfo, SW_HIDE);
    ShowWindow(gHiddenSongs, SW_SHOW);
    ShowWindow(gResultNext, SW_HIDE);
    for (HWND button : gPauseButtons) if (button) ShowWindow(button, SW_HIDE);
    setStatus(L"재생을 중단했습니다. 곡을 골라 다시 시작할 수 있습니다.");
    if (gList && ListView_GetItemCount(gList) > 0)
        ListView_RedrawItems(gList, 0, ListView_GetItemCount(gList) - 1);
    updateSelection();
    focusBrowseInput();
    if(gLibrarySources.configured())startLibraryValidation();
    RedrawWindow(gWindow, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void beginSelectedChart(bool practice) {
    if(selectedIndex()>=0)gSongIndexWorker.cancel();
    if(navigateSongFolder())return;
    if (gSettingsWindow || gLoading) return;
    const int index = selectedIndex();
    if (index < 0 || index >= static_cast<int>(gCharts.size())) return;
    if(!bms::gameplayModeAllowed(gCharts[index])){setStatus(bms::gameplayModeBlockReason(gCharts[index]));return;}
    clearSongListHeldKey();
    gSongListScroll.animating = false;
    if (gWindow) KillTimer(gWindow, ID_SONG_LIST_ANIMATION);
    const auto path = gCharts[static_cast<size_t>(index)].path;
    std::error_code sourceError;
    if(!std::filesystem::is_regular_file(path,sourceError)){setStatus(L"채보가 이동되었거나 라이브러리가 연결되지 않았습니다. F5로 새로고침하세요.");return;}
    if (!gCharts[static_cast<size_t>(index)].playable) { setStatus(gCharts[static_cast<size_t>(index)].blockReason); return; }
    gPractice.enabled = practice;
    gSelectionAudio.decide(gVolumes[0],gVolumes[3],gSoundBufferFrames);
    captureBrowseBackdrop();
    gGpuUnavailable = false;
    stopBrowseMedia();
    gSongPreview.pauseWarmup(true);
    gSongPreview.clearCache();
    gBrowseBackdrop.reset();
    if (gLoadWorker.joinable()) gLoadWorker.join();
    gVideoBga.reset();
    gBgaVideos.reset();gBgaSwitch.reset();
    gLoadResult = {}; gLoadResult.playback = true; gLoadResult.index = index;
    gLoading = true; gLoadCancelled = false;
    gPlaybackLoad.begin();
    gPlaybackLoadStarted = std::chrono::steady_clock::now();
    gTitleFadeOutStarted = {};
    for (HWND control : {gList, gFolder, gStatus, gInfo, gHiddenSongs})
        if (control) ShowWindow(control, SW_HIDE);
    setStatus(L"음원을 미리 디코딩하고 있습니다. 파일 수에 따라 잠시 걸릴 수 있습니다.");
    UpdateWindow(gWindow);
    const int videoWidth = gWindowWidth, videoHeight = gWindowHeight;
    const auto practiceSettings = gPractice;
    beginPlaybackLoadWorker(path,videoWidth,videoHeight,practiceSettings);
    beginSongEntry(gCharts[index]);
    InvalidateRect(gWindow,nullptr,FALSE);UpdateWindow(gWindow);
}

#include "PlaybackLoadWorker.inl"

#include "PlaybackLoading.inl"
#include "PlaybackLoadRuntime.inl"

void finishPlayback() {
    if (!gPlaying) return; // Finalize scores and capture the frame only once.
    resetResultFade();
    gResultFadeWidth = gBackBufferWidth; gResultFadeHeight = gBackBufferHeight;
    if (gBackBufferWidth > 0 && gBackBufferHeight > 0) {
        GdiFlush();
        if (!gCompositionEnabled || !gCompositionPresenter.captureLastFrame(gResultFadePixels, gBackBufferWidth, gBackBufferHeight)) {
            if (gBackBufferPixels) {
                const auto* pixels = static_cast<const uint32_t*>(gBackBufferPixels);
                gResultFadePixels.assign(pixels, pixels + static_cast<size_t>(gBackBufferWidth) * gBackBufferHeight);
            } else gResultFadePixels.clear();
        }
    }
    // Cover any synchronous repaint caused by tearing down the GPU child window.
    gResultFade.start();
    gPlaying = false;
    gPlaybackLoad.reset();
    gPaused = false;
    gShowingResult = true;
    gResultGaugeCleared = gGrooveGauge.clearConditionMet();
    gResultGaugeFailed = gGrooveGauge.depleted();
    if (!gPractice.enabled && gActiveChartIndex >= 0 && gActiveChartIndex < static_cast<int>(gCharts.size())) {
        savePlayRecord(gCharts[static_cast<size_t>(gActiveChartIndex)].path, gResultGaugeCleared,
            gFeedback.maxCombo(), gResultScore.value(), gCharts[gActiveChartIndex].judgementSpec,gCharts[gActiveChartIndex].rank);
    }
    gAudio.releaseSong();
    gBgaRaster.reset();
    gCompositionPresenter.reset();
    releaseBgaBackplate();
    startUiEffects();
    gVideoBga.reset();
    gBgaVideos.reset();gBgaSwitch.reset();
    gPressed.fill(false);gKeyboardPressed.fill(false);gControllerPressed.fill(false);gController.mapper.reset();clearAccuracyRestart();
    gHolds.fill(LongNoteHold{});
    gLingeringLongNotes.clear();
    resetMines();
    ShowWindow(gResultNext, SW_HIDE);
    for (HWND button : gPauseButtons) if (button) ShowWindow(button, SW_HIDE);
    gResultFade.start();
    SetTimer(gWindow, ID_RESULT_FADE, 4, nullptr);
    SetFocus(gWindow);
    InvalidateRect(gWindow, nullptr, TRUE);
}

void pausePlayback(bool focusMenu = true) {
    HUD_SCOPE(PauseTransition);
    if (!gPlaying || gPaused || !gAudio.pause()) return;
    gPlayhead = gAudio.positionSeconds();
    gPaused = true;
    gCompositionPresenter.lowLatency().endFrame();
    gCompositionPresenter.lowLatency().request(latency::Mode::Off);
    gPauseSelection = 0;
    layoutControls();
    for (HWND button : gPauseButtons) if (button) ShowWindow(button, SW_SHOW);
    if (focusMenu) SetFocus(gPauseButtons[0] ? gPauseButtons[0] : gWindow);
    InvalidateRect(gWindow, nullptr, TRUE);
}

void resumePlayback() {
    HUD_SCOPE(ResumeTransition);
    gFrameStatistics.suspend();
    if (!gPaused) return;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    prepareSkinForPlayback();
#endif
    if (!gAudio.resume()) {
        showBrowseView();
        return;
    }
    gPaused = false;
    for (HWND button : gPauseButtons) if (button) ShowWindow(button, SW_HIDE);
    SetFocus(gWindow);
    gFramePacer.reset(gTargetFps);
    InvalidateRect(gWindow, nullptr, TRUE);
}

void restartPlayback() {
    gFrameStatistics.reset();
    if (!gPlaying || gLoading || gActiveChartIndex < 0 || gActiveChartIndex >= static_cast<int>(gCharts.size())) return;
    if(gAudio.isRunning()&&!gAudio.isPaused()&&!gAudio.pause())gAudio.stop();
    const auto& chart = gCharts[static_cast<size_t>(gActiveChartIndex)];
    gPaused = false;
    gPlayhead = gPractice.enabled ? gPractice.startSeconds : 0;
    gHit.assign(chart.notes.size(), false);
    gLingeringLongNotes.clear();
    gResultScore.reset(static_cast<int>(bms::judgementNoteCount(chart)));
    gFeedback.reset();
    gGrooveGauge.configure(bms::judgementNoteCount(chart),gGrooveGaugeMode,bms::chartTotal(chart,bms::judgementNoteCount(chart)));
    if(gChartOptions.gaugeAuthored)gGrooveGauge.configureAuthored(gChartOptions.gameLevel,gChartOptions.extra);
    gKeyBeams.reset();
    if(gActiveChartIndex>=0 && gActiveChartIndex<static_cast<int>(gCharts.size())) {
        const auto layout=bms::chartLayout(gCharts[gActiveChartIndex],0,68,104);
        gKeyBeams.configure(layout);gFeedback.configure(layout);
    }
    gScratchDisc.reset();
    gPressed.fill(false);gKeyboardPressed.fill(false);gControllerPressed.fill(false);gController.mapper.reset();clearAccuracyRestart();
    gHolds.fill(LongNoteHold{});
    resetMines();
    gMissScanIndex = 0;
    gBgaEventIndex = 0;
    gBgaActiveImages.fill(L"");gBgaActiveImages[1]=L"00";
    gMissBgaUntil = 0;
    gJudgementText.clear();
    gJudgementUntil = {};
    gComboPop.reset();
    gJudgementPop.reset();
    gTextRaster.clear(); gHudTextRaster.clear(); gHudMaskRaster.clear();
    gBgaRaster.start(chart, gWindowWidth, gWindowHeight, gPlayhead);
    if (!chart.fallbackVideo.empty()) {
        gVideoBga.request(std::max(0.0, gPlayhead - gBgaVideoStartSeconds), gWindowWidth, gWindowHeight);
        gVideoBga.waitUntilPrepared();
    }
    releaseBgaBackplate();
    updateBgaState(chart, gPlayhead);
    std::wstring error;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    prepareSkinForPlayback();
#endif
    if (!(gPractice.enabled ? gAudio.startAt(gPractice.startSeconds,error,gPractice.rate()*gChartOptions.rate) : gAudio.startAt(0,error,gChartOptions.rate))) {
        showBrowseView();
        MessageBoxW(gWindow, error.c_str(), L"곡을 다시 시작하지 못했습니다", MB_ICONERROR | MB_OK);
        return;
    }
    for (HWND button : gPauseButtons) if (button) ShowWindow(button, SW_HIDE);
    SetFocus(gWindow);
    gFramePacer.reset(gTargetFps);
    InvalidateRect(gWindow, nullptr, TRUE);
}

void adjustPausedSetting(WPARAM key) {
    if (key == VK_F1 || key == VK_F2) {
        gNoteSpeed = std::clamp(gNoteSpeed + (key == VK_F2 ? 0.25 : -0.25), 0.5, 10.0);
        saveDisplaySettings();
    } else if (key == VK_PRIOR || key == VK_NEXT) {
        gLaneCoverPercent = std::clamp(gLaneCoverPercent + (key == VK_PRIOR ? 5 : -5), 0, 100);
        saveDisplaySettings();
    }
    InvalidateRect(gWindow, nullptr, FALSE);
}

void handlePausedKey(WPARAM key, bool repeated) {
    if (key == VK_ESCAPE) {
        if (!repeated) resumePlayback();
    } else if (key == VK_RETURN) {
        if (!repeated) {
            if (gPauseSelection < gPauseButtons.size() && gPauseButtons[gPauseSelection])
                SendMessageW(gPauseButtons[gPauseSelection], BM_CLICK, 0, 0);
            else resumePlayback();
        }
    } else if (key == VK_LEFT || key == VK_UP || key == VK_RIGHT || key == VK_DOWN) {
        if (!repeated && !gPauseButtons.empty()) {
            const int direction = (key == VK_LEFT || key == VK_UP) ? -1 : 1;
            const int count = static_cast<int>(gPauseButtons.size());
            gPauseSelection = static_cast<size_t>((static_cast<int>(gPauseSelection) + direction + count) % count);
            if (gPauseButtons[gPauseSelection]) SetFocus(gPauseButtons[gPauseSelection]);
            for (HWND button : gPauseButtons) if (button) InvalidateRect(button, nullptr, FALSE);
            InvalidateRect(gWindow, nullptr, FALSE);
            UpdateWindow(gWindow);
        }
    } else if (key == 'R' || key == VK_F5) {
        if (!repeated) restartPlayback();
    } else if (key == 'Q') {
        if (!repeated) showBrowseView();
    } else if (key == VK_F1 || key == VK_F2 || key == VK_PRIOR || key == VK_NEXT) {
        adjustPausedSetting(key);
    }
}

UINT laneKey(int lane) {
    if(gActiveChartIndex>=0 && gActiveChartIndex<static_cast<int>(gCharts.size()) && gCharts[gActiveChartIndex].mode==bms::ChartMode::PopnNine) {
        constexpr UINT popn[]{'Z','S','X','D','C','F','V','G','B'};return lane<9?popn[lane]:0;
    }
    constexpr UINT second[]{VK_RSHIFT,'Q','W','E','R','T','Y','U'};
    return lane<8?gLaneKeys[lane]:second[lane-8];
}
int keyLane(WPARAM key, LPARAM detail) {
    if(key==VK_SHIFT)key=MapVirtualKeyW((static_cast<UINT>(detail)>>16)&0xff,MAPVK_VSC_TO_VK_EX);
    int count=8;if(gActiveChartIndex>=0 && gActiveChartIndex<static_cast<int>(gCharts.size()))count=bms::chartLanes(gCharts[gActiveChartIndex]);
    for(int lane=0;lane<count;++lane)if(laneKey(lane)==key)return lane;
    return -1;
}

void pressLane(int lane) {
    HUD_SCOPE(InputDown);
    if (!gPlaying || gAccuracyRestartPending || lane < 0 || lane > 15 || gPressed[static_cast<size_t>(lane)]) return;
    BMS_PERF_SCOPE(inputMs);BMS_PERF_COUNT(keyDown);
    gPlayhead = gAudio.positionSeconds();BMS_PERF_EVENT(1,lane,gPlayhead);
    const auto& chart = gCharts[static_cast<size_t>(gActiveChartIndex)];
    updateBgaState(chart, gPlayhead);
    gPressed[static_cast<size_t>(lane)] = true;
    gBgaSwitch.press(chart,lane,gPlayhead);
    if (lane == 0) gScratchDisc.update(gPlayhead, true);
    gKeyBeams.setPressed(lane, true, gPlayhead);
    if(gHolds[lane].active() && chart.notes[gHolds[lane].noteIndex()].mode==bms::LongNoteMode::HCN) return;
    const auto& notes=chart.notes;
    const int nearest=bms::nearestLaneNote(chart,gHit,lane,gPlayhead,playbackTimingScale(),gJudgementProfile,gPractice.enabled?gPractice.startSeconds:0);
    if (nearest >= 0) {
        gFeedback.hit(lane, gPlayhead);
        gComboPop.start();
        gHit[static_cast<size_t>(nearest)] = true;
        const auto& note = notes[static_cast<size_t>(nearest)];
        const auto grade=*bms::judgeLaneOffset(chart,gPlayhead,lane,(gPlayhead-note.timeSeconds)/playbackTimingScale(),gJudgementProfile);
        if (!note.sampleId.empty()) gAudio.trigger(note.sampleId);
        if (note.longNote()) {
            gHolds[static_cast<size_t>(lane)].start(static_cast<size_t>(nearest),note.timeSeconds+note.lengthSeconds,gPlayhead,note.mode,grade);
        }
        showTimingJudgement(grade,note.longNote() && note.mode==bms::LongNoteMode::LN?0:2);
        const double signedOffset=gPlayhead-note.timeSeconds;
        gTimingHint=gTimingHintCondition==3 ? 0 : static_cast<int>(grade)<=gTimingHintCondition ? 0 : signedOffset < -0.000001 ? -1 : signedOffset > 0.000001 ? 1 : 0;
    } else {
        const auto& indices = gLaneNoteIndices[static_cast<size_t>(lane)];
        if (!indices.empty()) {
            const auto next = std::upper_bound(indices.begin(), indices.end(), gPlayhead,
                [&](double time, size_t noteIndex) { return time < notes[noteIndex].timeSeconds; });
            const size_t noteIndex = next == indices.begin() ? indices.front() : *std::prev(next);
            const auto hidden=bms::hiddenAt(chart,lane,gPlayhead);
            if(hidden && (notes[noteIndex].timeSeconds>gPlayhead || hidden->timeSeconds>=notes[noteIndex].timeSeconds)) gAudio.trigger(hidden->sampleId);
            else if (!notes[noteIndex].sampleId.empty()) gAudio.trigger(notes[noteIndex].sampleId);
        } else if(const auto hidden=bms::hiddenAt(chart,lane,gPlayhead)) gAudio.trigger(hidden->sampleId);
    }
    InvalidateRect(gWindow, nullptr, FALSE);
}

void releaseLaneInput(int lane) {
    HUD_SCOPE(InputUp);
    if(lane<0||lane>=16||!gPressed[lane])return;
    if(gPlaying&&!gPaused&&gActiveChartIndex>=0){
        BMS_PERF_SCOPE(inputMs);BMS_PERF_COUNT(keyUp);
        gPlayhead=gAudio.positionSeconds();BMS_PERF_EVENT(2,lane,gPlayhead);
        showHoldCombo(gHolds[lane].takeComboTicks(gPlayhead));
        resolveHold(lane,gHolds[lane].release(gPlayhead,currentJudgementWindows().goodMs/1000.0*playbackTimingScale()));
    }
    gPressed[lane]=false;
    if(lane==0)gScratchDisc.update(gPlayhead,false);
    gKeyBeams.setPressed(lane,false,gPlayhead);
    InvalidateRect(gWindow,nullptr,FALSE);
}
void pollControllerInput() {
    if(gControllerConfiguring)return;
    const bool wasEnabled=gController.config.enabled;
    const bool active=GetForegroundWindow()==gWindow&&!gSettingsWindow&&(!gLoading||gPlaybackLoad.preparing())&&!gCapturingBindings;
    gController.poll(active,[&](const bms::controller::State& state){
        if(active&&gPlaybackLoad.preparing()){
            constexpr WPARAM keys[]{VK_LEFT,VK_F1,VK_F2,VK_NEXT,VK_PRIOR,VK_UP,VK_DOWN,VK_RIGHT};
            for(int lane=0;lane<8;++lane){if(state.lanes[lane]&&!gPlayPreview.held[lane])previewKey(keys[lane]);gPlayPreview.held[lane]=state.lanes[lane];}
            if(std::any_of(state.lanes.begin(),state.lanes.end(),[](bool held){return held;}))gPlayPreview.touched();
            if(state.menuPressed)previewKey(VK_RETURN);return;
        }
        const bool playing=active&&gPlaying&&!gPaused;
        for(int lane=0;lane<8;++lane){
            const bool previous=gControllerPressed[lane],next=playing&&state.lanes[lane];
            gControllerPressed[lane]=next;
            if(next&&!previous)pressLane(lane);
            else if(previous&&!next&&!gKeyboardPressed[lane])releaseLaneInput(lane);
            if(lane==0&&next&&previous&&state.scratchPulse&&!gKeyboardPressed[0]&&!gHolds[0].active()){
                releaseLaneInput(0);pressLane(0);
            }
        }
        if(state.menuPressed&&active&&gPlaying){if(gPaused)resumePlayback();else pausePlayback();}
    });
    if(wasEnabled&&!gController.config.enabled){
        WritePrivateProfileStringW(L"Controller",L"Enabled",L"0",gSettingsFile.c_str());
        SetTimer(gWindow,ID_CONTROLLER_POLL,1000,nullptr);
    }
}
void openControllerSettings() {
    if(gCapturingBindings)cancelKeyBindingCapture();
    gControllerConfiguring=true;
    for(int lane=0;lane<8;++lane){gControllerPressed[lane]=false;if(!gKeyboardPressed[lane])releaseLaneInput(lane);}
    bms::controller::SettingsDialog dialog(gController,gSettingsWindow?gSettingsWindow:gWindow,gSettingsFile.wstring());
    dialog.show(gWindow);
    SetTimer(gWindow,ID_CONTROLLER_POLL,gController.config.enabled?4:1000,nullptr);
    gControllerConfiguring=false;
}

void updatePlaybackFrame() {
    pollControllerInput();
    BMS_TRACE_ZONE("Playback update");
    BMS_TRACE_FRAME();
    if (!gPlaying||gAccuracyRestartPending) return;
    BMS_PERF_BEGIN();HUD_FRAME_BEGIN();
    { BMS_PERF_SCOPE(positionMs);HUD_SCOPE(AudioPosition); gPlayhead = gAudio.positionSeconds(); }
    BMS_PERF_SET(rawPositionSeconds, gAudio.lastRawPositionSeconds());
    const int index = gActiveChartIndex;
    const double endSeconds = gPractice.enabled ? gPractice.endSeconds : gAudio.durationSeconds();
    if (index < 0 || index >= static_cast<int>(gCharts.size()) || gPlayhead >= endSeconds + 0.25 * (gPractice.enabled ? gPractice.rate() : 1.0)) {
        finishPlayback();
        return;
    }
    const auto& notes = gCharts[static_cast<size_t>(index)].notes;
    const auto& chart = gCharts[static_cast<size_t>(index)];
    const double bgaTime = gPlayhead - gBgaVideoStartSeconds;
    { BMS_PERF_SCOPE(videoMs);HUD_SCOPE(VideoUpdate); if (bgaTime >= 0 && (!hybridRendererEnabled || gBgaBrightness > 0)) gVideoBga.update(bgaTime); }
    {HUD_SCOPE(BgaStateUpdate);updateBgaState(chart, gPlayhead);}
    {HUD_SCOPE(GameLogic);
    for (size_t lane = 0; lane < gHolds.size(); ++lane) {
        const unsigned ticks=gHolds[lane].takeGaugeTicks(gPlayhead);
        for(unsigned tick=0;tick<ticks;++tick)gGrooveGauge.holdTick(gPressed[lane]);
        if(gGrooveGauge.depleted() && !gPractice.enabled){finishPlayback();return;}
        const HoldResult result=gHolds[lane].update(gPlayhead,gPressed[lane],currentJudgementWindows().goodMs/1000.0*playbackTimingScale());
        resolveHold(lane,result);
        if (!gPlaying||gAccuracyRestartPending) return;
        if (gPressed[lane]) showHoldCombo(gHolds[lane].takeComboTicks(gPlayhead));
    }
    if(gMineHit.size()!=chart.mines.size())gMineHit.assign(chart.mines.size(),false);
    for(size_t mine=0;mine<chart.mines.size();++mine) {
        const auto& n=chart.mines[mine];if(gMineHit[mine] || n.timeSeconds>gPlayhead)continue;
        gMineHit[mine]=true;
        if(n.timeSeconds<(gPractice.enabled?gPractice.startSeconds:0))continue;
        if(n.lane>=0 && n.lane<16 && gPressed[n.lane]) {
            gGrooveGauge.damage(n.damage);gAudio.trigger(L"00");setJudgement(L"MINE",RGB(255,64,64));
            if(gGrooveGauge.depleted() && !gPractice.enabled){finishPlayback();return;}
        }
    }
    // A scratch may have a longer legacy deadline than a following key note.
    // Expire each eligible note independently; advance only over resolved heads.
    const double legacySlow=currentJudgementWindows().badMs;
    const double minimumSlow=bms::usesOfficialJudgement(chart)?std::min(legacySlow,double(bms::officialJudgeWindow(chart.rank).badSlow)):legacySlow;
    for(size_t i=gMissScanIndex;i<notes.size() && notes[i].timeSeconds<gPlayhead-minimumSlow/1000*playbackTimingScale();++i) {
        if(gPractice.enabled&&notes[i].timeSeconds<gPractice.startSeconds){gHit[i]=true;continue;}
        if(!gHit[i] && bms::laneNoteExpired(chart,notes[i],gPlayhead,playbackTimingScale(),gJudgementProfile)) {
            gHit[i]=true;
            if(notes[i].longNote())gLingeringLongNotes.push_back(i);
            showTimingJudgement(TimingJudgement::Poor);
            if(!gPlaying||gAccuracyRestartPending)return;
        }
    }
    while(gMissScanIndex<notes.size() && gHit[gMissScanIndex])++gMissScanIndex;
    InvalidateRect(gWindow, nullptr, FALSE);
    }
    {HUD_SCOPE(PaintDispatch);UpdateWindow(gWindow);} // Present this frame now, independent of WM_TIMER/paint queue cadence.
    BMS_PERF_FINISH(gPlayhead);HUD_FRAME_END(gPlayhead);
}

void drawText(HDC dc, const std::wstring& text, int x, int y, COLORREF color, HFONT font = nullptr) {
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(gPlaying&&!gPaused&&skinOwnsHud())return ;
#endif
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    TextOutW(dc, x, y, text.c_str(), static_cast<int>(text.size()));
    if (old) SelectObject(dc, old);
}

void drawShadowedCenteredText(HDC dc, const std::wstring& text, RECT rect, HFONT font, COLORREF color, bool addBlackShadow = true) {
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ oldFont = font ? SelectObject(dc, font) : nullptr;
    const COLORREF outlineColor = RGB(GetRValue(color) / 3, GetGValue(color) / 3, GetBValue(color) / 3);
    constexpr std::array<POINT, 24> outlineOffsets{{
        {-2, 0}, {2, 0}, {0, -2}, {0, 2}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1},
        {-4, 0}, {4, 0}, {0, -4}, {0, 4}, {-3, -3}, {3, -3}, {-3, 3}, {3, 3},
        {-6, 0}, {6, 0}, {0, -6}, {0, 6}, {-5, -5}, {5, -5}, {-5, 5}, {5, 5}
    }};
    for (const POINT offset : outlineOffsets) {
        RECT outline = rect;
        OffsetRect(&outline, offset.x, offset.y);
        SetTextColor(dc, outlineColor);
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &outline, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    if (addBlackShadow) {
        RECT shadow = rect;
        OffsetRect(&shadow, 3, 4);
        SetTextColor(dc, RGB(0, 0, 0));
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &shadow, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (oldFont) SelectObject(dc, oldFont);
}

void drawTextBody(HDC dc, const std::wstring& text, RECT rect, HFONT font, COLORREF color) {
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(gPlaying&&!gPaused&&skinOwnsHud())return ;
#endif
    const auto old = font ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (old) SelectObject(dc, old);
}

void drawLeftTextBody(HDC dc, const std::wstring& text, RECT rect, HFONT font, COLORREF color) {
    const auto old = font ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (old) SelectObject(dc, old);
}

void drawResourceUsageLine(HDC dc, const std::wstring& text, RECT bounds, HFONT font, COLORREF color) {
    const auto oldFont = font ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    constexpr std::array<POINT, 4> outline{{{-1, 0}, {1, 0}, {0, -1}, {0, 1}}};
    SetTextColor(dc, RGB(0, 0, 0));
    for (const POINT offset : outline) {
        RECT shifted = bounds;
        OffsetRect(&shifted, offset.x, offset.y);
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &shifted,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &bounds,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (oldFont) SelectObject(dc, oldFont);
}

std::wstring memoryUsageLabel(const SystemResourceSnapshot& values) {
    constexpr uint64_t megabyte = 1024ull * 1024ull;
    constexpr uint64_t gigabyte = 1024ull * megabyte;
    const bool useGigabytes = values.workingSetBytes >= gigabyte;
    const double amount = static_cast<double>(values.workingSetBytes) /
        static_cast<double>(useGigabytes ? gigabyte : megabyte);
    const auto unit = useGigabytes ? L"GB" : L"MB";
    return L"Memory = " + number(amount, useGigabytes ? 1 : 0) + unit + L" (" +
        std::to_wstring(static_cast<int>(std::lround(values.memoryPercent))) + L"%)";
}

void recordResourceFrame(double now) {
    bool enabled=gShowResourceUsage;
#ifdef BMS_FRAME_DIAGNOSTICS
    enabled=true;
#endif
    if(!enabled){gFrameStatistics.reset();gFrameStatisticsView=-1;return;}
    if(gPaused||gLoading||gStartupSplash){gFrameStatistics.suspend();return;}
    const int view=gPlaying?1:gShowingResult?2:0;
    if(view!=gFrameStatisticsView){gFrameStatistics.reset();gFrameStatisticsView=view;}
    const bool alarm=gFrameStatistics.record(now);
#ifdef BMS_FRAME_DIAGNOSTICS
    if(alarm&&gPlaying)gAudio.triggerEffect(L"fps-drop");
#else
    (void)alarm;
#endif
}
std::array<std::wstring,6> resourceUsageLines(const SystemResourceSnapshot& values) {
    const auto fps=[](const wchar_t* label,double value){return std::wstring(label)+(value>0?std::to_wstring(std::lround(value)):L"--");};
    return {
        L"CPU = "+std::to_wstring(values.cpuPercent)+L"%",
        memoryUsageLabel(values),
        gCompositionEnabled?L"GPU = "+(values.gpuAvailable?std::to_wstring(values.gpuPercent)+L"%":L"--%"):L"",
        fps(L"FPS NOW = ",gFrameStatistics.current()),
        fps(L"FPS AVG = ",gFrameStatistics.average()),
        fps(L"FPS 1% LOW = ",gFrameStatistics.lowOnePercent())
    };
}

void drawResourceUsageOverlay(HDC dc) {
    if (!gShowResourceUsage || !gBackBufferPixels || gBackBufferWidth <= 0 || gBackBufferHeight <= 0) return;
    BMS_PERF_SCOPE(overlayMs);
    SystemResourceSnapshot values = gSystemResources.sample();
    if (gCompositionEnabled && !values.gpuAvailable) {
        const int rendererUsage = gCompositionPresenter.gpuUtilizationPercent();
        if (rendererUsage >= 0) {
            values.gpuPercent = rendererUsage;
            values.gpuAvailable = true;
        }
    }
    const auto lines=resourceUsageLines(values);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ oldFont = gSmallFont ? SelectObject(dc, gSmallFont) : nullptr;
    const HFONT font = static_cast<HFONT>(GetCurrentObject(dc, OBJ_FONT));
    static HFONT measuredFont{};
    static std::array<std::wstring, 6> measuredLines;
    static std::array<int, 6> measuredWidths{};
    const bool fontChanged = measuredFont != font;
    measuredFont = font;
    constexpr int lineHeight = 22;
    const int right = gBackBufferWidth - 18;
    const TextRasterCache::GpuDraw gpuResource=gGpuFrame?TextRasterCache::GpuDraw(
        [](const uint32_t* pixels,int w,int h,int x,int y,int dw,int dh,uint64_t id){
            return gCompositionPresenter.sprite(pixels,w,h,x,y,dw,dh,id|(uint64_t{1}<<61));
        }):TextRasterCache::GpuDraw{};
    GdiFlush(); // Finish the game HUD before compositing cached transparent text.
    int row = gPlaying && gPractice.enabled ? 3 : 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];
        if (line.empty()) continue;
        if (fontChanged || measuredLines[i] != line) {
            SIZE extent{};
            GetTextExtentPoint32W(dc, line.c_str(), static_cast<int>(line.size()), &extent);
            measuredWidths[i] = extent.cx;
            measuredLines[i] = line;
        }
        const int top = 14 + row++ * lineHeight;
        const RECT bounds{right - measuredWidths[i], top, right, top + lineHeight};
        bool drawn = false;
        try {
            drawn = gResourceTextRaster.draw(gGpuFrame?nullptr:static_cast<uint32_t*>(gBackBufferPixels),
                gBackBufferWidth, gBackBufferHeight, line, bounds, font, RGB(248, 250, 252), 1.0,
                drawResourceUsageLine, false, gpuResource);
        } catch (...) { gResourceTextRaster.clear(); }
        if (!drawn) {
            drawResourceUsageLine(dc, line, bounds, font, RGB(248, 250, 252));
            GdiFlush();
        }
    }
    if (oldFont) SelectObject(dc, oldFont);
}

bool queueGpuText(const uint32_t* pixels, int width, int height, int left, int top, int drawWidth, int drawHeight, uint64_t id) {
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(gPlaying&&!gPaused&&skinOwnsHud()&&!(id&(uint64_t{1}<<60)))return true;
#endif
    return gCompositionPresenter.sprite(pixels, width, height, left, top, drawWidth, drawHeight, id);
}

void drawRightTextBody(HDC dc, const std::wstring& text, RECT rect, HFONT font, COLORREF color) {
    const auto old = font ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (old) SelectObject(dc, old);
}

void drawPracticeHud(HDC dc) {
    if (!gPractice.enabled) return;
    const std::array<std::wstring, 2> lines{L"배속: " + number(gPractice.rate(), 2) + L"x",
        L"구간: " + practiceTime(gPractice.startSeconds) + L" ~ " + practiceTime(gPractice.endSeconds)};
    const auto font = gPracticeHudFont ? gPracticeHudFont : gSmallFont;
    static HFONT measuredFont{};
    static std::array<std::wstring, 2> measuredLines;
    static std::array<int, 2> widths{};
    const bool changedFont = measuredFont != font;
    measuredFont = font;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (changedFont || measuredLines[i] != lines[i]) {
            const auto old = font ? SelectObject(dc, font) : nullptr;
            SIZE extent{}; GetTextExtentPoint32W(dc, lines[i].c_str(), static_cast<int>(lines[i].size()), &extent);
            widths[i] = extent.cx + 16; measuredLines[i] = lines[i];
            if (old) SelectObject(dc, old);
        }
        const RECT bounds{gWindowWidth - 18 - widths[i], 14 + static_cast<int>(i) * 24, gWindowWidth - 18, 38 + static_cast<int>(i) * 24};
        if (dc == gBackBufferDc && gBackBufferPixels) {
            GdiFlush();
            if (gTextRaster.draw(gGpuFrame ? nullptr : static_cast<uint32_t*>(gBackBufferPixels), gBackBufferWidth, gBackBufferHeight,
                lines[i], bounds, font, RGB(255, 255, 255), 1.0, drawRightTextBody, false,
                gGpuFrame ? TextRasterCache::GpuDraw(queueGpuText) : TextRasterCache::GpuDraw{})) continue;
        }
        drawRightTextBody(dc, lines[i], bounds, font, RGB(255, 255, 255));
    }
}

void closePracticeSetup(bool start) {
    if (start && !gSettingsFile.empty() && !gPracticePresetSection.empty()) {
        WritePrivateProfileStringW(gPracticePresetSection.c_str(), L"Start", std::to_wstring(gPractice.startSeconds).c_str(), gSettingsFile.c_str());
        WritePrivateProfileStringW(gPracticePresetSection.c_str(), L"End", std::to_wstring(gPractice.endSeconds).c_str(), gSettingsFile.c_str());
    }
    if (gPracticeWindow) DestroyWindow(gPracticeWindow);
    if (start) beginSelectedChart(true);
    else updateSelection();
}

void advancePracticeSetup() {
    if (gPracticeStage == 0) {
        gPracticeStage = 1;
        gPracticePreview.select(gPractice.startSeconds, gPractice.endSeconds, gPractice.rate());
        SetWindowTextW(gPracticeNext, L"연습 시작 (Enter)");
        SetWindowTextW(gPracticeBack, L"뒤로 (Esc)");
        InvalidateRect(gPracticeWindow, nullptr, TRUE);
    } else if (gPractice.valid()) closePracticeSetup(true);
}

LRESULT CALLBACK practiceSetupProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        SetTimer(window, 3, 15, nullptr);
        gPracticeBack = CreateWindowW(L"BUTTON", L"취소 (Esc)", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 32, 276, 180, 38,
            window, reinterpret_cast<HMENU>(1), nullptr, nullptr);
        gPracticeNext = CreateWindowW(L"BUTTON", L"다음 (Enter)", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 336, 276, 210, 38,
            window, reinterpret_cast<HMENU>(2), nullptr, nullptr);
        for (auto button : {gPracticeBack, gPracticeNext}) SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);
        return 0;
    case WM_TIMER:
        if (wParam == 3) gPracticePreview.tick(gVolumes[0], gPreviewVolume, static_cast<uint32_t>(gSoundBufferFrames));
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{}; const auto dc = BeginPaint(window, &paint);
        RECT client{}; GetClientRect(window, &client); FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        drawText(dc, L"연습 모드", 32, 24, RGB(255, 255, 255), gFont);
        drawText(dc, gPracticeStage == 0 ? L"1 / 2  재생 배속" : L"2 / 2  연습 구간", 32, 64, RGB(210, 210, 210), gFont);
        if (gPracticeStage == 0) {
            drawTextBody(dc, L"<     " + number(gPractice.rate(), 2) + L"x     >", {32, 118, 546, 184}, gBrowseLevelFont, RGB(255, 255, 255));
            drawText(dc, L"← / →  배속 조절 (0.50x ~ 1.50x)", 32, 220, RGB(220, 220, 220), gSmallFont);
        } else {
            const std::array<std::wstring, 2> rows{L"시작   <  " + practiceTime(gPractice.startSeconds) + L"  >",
                L"종료   <  " + practiceTime(gPractice.endSeconds) + L"  >"};
            for (int i = 0; i < 2; ++i) {
                const RECT bounds{32, 112 + i * 54, 546, 158 + i * 54};
                if (gPracticeBoundary == i) FrameRect(dc, &bounds, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
                drawTextBody(dc, rows[static_cast<size_t>(i)], bounds, gFont, RGB(255, 255, 255));
            }
            drawText(dc, L"↑ / ↓  항목 선택    ← / →  1초 조절    Shift: 10초", 32, 230, RGB(220, 220, 220), gSmallFont);
        }
        EndPaint(window, &paint); return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_RETURN) { if (!(lParam & (1LL << 30))) advancePracticeSetup(); return 0; }
        if (wParam == VK_ESCAPE) { SendMessageW(window, WM_COMMAND, 1, 0); return 0; }
        if (gPracticeStage == 0) {
            if (wParam == VK_LEFT || wParam == VK_DOWN) gPractice.adjustRate(-1);
            if (wParam == VK_RIGHT || wParam == VK_UP) gPractice.adjustRate(1);
        } else if (wParam == VK_UP || wParam == VK_DOWN) gPracticeBoundary = 1 - gPracticeBoundary;
        else if (wParam == VK_LEFT || wParam == VK_RIGHT) {
            gPractice.adjustBoundary(gPracticeBoundary == 1, (wParam == VK_LEFT ? -1.0 : 1.0) * ((GetKeyState(VK_SHIFT) & 0x8000) ? 10 : 1));
            gPracticePreview.select(gPractice.previewStart(gPracticeBoundary == 1), gPractice.endSeconds, gPractice.rate());
        }
        InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_LBUTTONDOWN: {
        const int x = static_cast<short>(LOWORD(lParam)), y = static_cast<short>(HIWORD(lParam));
        if (gPracticeStage == 0 && y >= 118 && y < 184) gPractice.adjustRate(x < 289 ? -1 : 1);
        else if (gPracticeStage == 1 && y >= 112 && y < 212) {
            gPracticeBoundary = y >= 166 ? 1 : 0;
            if (x < 180 || x > 400) {
                gPractice.adjustBoundary(gPracticeBoundary == 1, x < 180 ? -1 : 1);
                gPracticePreview.select(gPractice.previewStart(gPracticeBoundary == 1), gPractice.endSeconds, gPractice.rate());
            }
        }
        InvalidateRect(window, nullptr, FALSE); return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 2) { advancePracticeSetup(); return 0; }
        if (LOWORD(wParam) == 1) {
            if (gPracticeStage == 0) closePracticeSetup(false);
            else {
                gPracticeStage = 0; SetWindowTextW(gPracticeNext, L"다음 (Enter)"); SetWindowTextW(gPracticeBack, L"취소 (Esc)");
                gPracticePreview.stop();
                InvalidateRect(window, nullptr, TRUE);
            }
            return 0;
        }
        break;
    case WM_CLOSE: closePracticeSetup(false); return 0;
    case WM_DESTROY:
        KillTimer(window, 3); gPracticePreview.reset();
        gPracticeWindow = nullptr; gPracticeBack = gPracticeNext = nullptr;
        EnableWindow(gWindow, TRUE); SetActiveWindow(gWindow); focusBrowseInput(); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void openPracticeSetup() {
    if (gLoading || gPlaying || gShowingResult || gSettingsWindow || gPracticeWindow) return;
    const int index = selectedIndex();
    if (index < 0 || index >= static_cast<int>(gCharts.size()) || !gCharts[static_cast<size_t>(index)].playable) return;
    gPractice.configure(gCharts[static_cast<size_t>(index)].durationSeconds);
    gPracticePresetSection = L"Practice_" + recordSectionForPath(normalizedRecordPath(gCharts[static_cast<size_t>(index)].path));
    if (!gSettingsFile.empty()) {
        wchar_t start[64]{}, end[64]{};
        GetPrivateProfileStringW(gPracticePresetSection.c_str(), L"Start", L"0", start, 64, gSettingsFile.c_str());
        GetPrivateProfileStringW(gPracticePresetSection.c_str(), L"End", L"0", end, 64, gSettingsFile.c_str());
        gPractice.restoreRange(wcstod(start, nullptr), wcstod(end, nullptr));
    }
    gPracticeStage = gPracticeBoundary = 0;
    stopBrowseMedia(); gSongPreview.pauseWarmup(true);
    gPracticePreview.prepare(gCharts[static_cast<size_t>(index)]);
    WNDCLASSW type{}; type.lpfnWndProc = practiceSetupProc; type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"BmsPlayerPracticeSetup"; type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)); RegisterClassW(&type);
    RECT owner{}; GetWindowRect(gWindow, &owner);
    gPracticeWindow = CreateWindowExW(WS_EX_DLGMODALFRAME, type.lpszClassName, L"연습 모드 설정", WS_POPUP | WS_CAPTION | WS_SYSMENU,
        std::max(0L, (owner.left + owner.right - 600) / 2), std::max(0L, (owner.top + owner.bottom - 370) / 2), 600, 370,
        gWindow, nullptr, type.hInstance, nullptr);
    if (!gPracticeWindow) return;
    EnableWindow(gWindow, FALSE); ShowWindow(gPracticeWindow, SW_SHOW); SetFocus(gPracticeWindow);
}

bool shouldRenderGpuFrame() {
    if (gPaused || gGpuLimitPercent >= 100) return true;
    gGpuFrameAccumulator += std::clamp(gGpuLimitPercent, 10, 100);
    if (gGpuFrameAccumulator < 100) return false;
    gGpuFrameAccumulator -= 100;
    return true;
}

HANDLE pendingGpuOutputHandle() {
    return gPlaying && gCompositionEnabled && !gGpuUnavailable ? gCompositionPresenter.presentationWaitHandle() : nullptr;
}

DWORD waitForPlaybackWork(HANDLE timer, DWORD timeout) {
    BMS_PERF_SCOPE(waitMs);HUD_SCOPE(PacerWait);
    std::array<HANDLE, 2> handles{};
    DWORD count = 0;
    if (timer) handles[count++] = timer;
    const HANDLE output = pendingGpuOutputHandle();
    const DWORD outputIndex = count;
    if (output) handles[count++] = output;
    const DWORD result = MsgWaitForMultipleObjectsEx(count, handles.data(), timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (output && result == WAIT_OBJECT_0 + outputIndex)
        gOutputWakeReady = gCompositionPresenter.acceptPresentationWake(output);
    return result;
}

void retryPendingGpuOutput() {
    if (!gPlaying || !gCompositionEnabled || gGpuUnavailable || !gBackBufferPixels) return;
    if (gCompositionPresenter.hasPendingFrame()) {
        // Submit the newest already-painted frame. Do not run input, judgement,
        // audio or the CPU render loop again, or bypass its FPS ceiling.
        if (!gCompositionPresenter.present(gWindow, gBackBufferPixels, gBackBufferWidth, gBackBufferHeight)) {
            gGpuUnavailable = true;
            InvalidateRect(gWindow, nullptr, FALSE); UpdateWindow(gWindow);
        } else if (!gCompositionPresenter.presentationDeferred()) KillTimer(gWindow, ID_PAUSE_PRESENT_RETRY);
    } else if (gPaused || gTargetFps == vsyncFps) {
        // VSync may have skipped painting entirely while the output was busy.
        InvalidateRect(gWindow, nullptr, FALSE); UpdateWindow(gWindow);
    }
}

void drawAnimatedCenteredText(HDC dc, const std::wstring& text, RECT rect, HFONT font, COLORREF color, double scale, bool rainbow = false) {
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(gPlaying&&skinOwnsJudgement())return ;
#endif
    const int phase = rainbow ? 0 : -1;
    const COLORREF cacheColor = rainbow ? RGB(0,0,1) : RGB(0,0,0);
    auto render = [&](HDC target, const std::wstring& body, RECT bounds, HFONT face, COLORREF) {
        drawHudTextBody(target, body, bounds, face, RGB(0,0,0), phase);
    };
    const TextRasterCache::GpuDraw gpuHud = [](const uint32_t* pixels, int w, int h, int x, int y, int dw, int dh, uint64_t id) {
        return queueGpuText(pixels, w, h, x, y, dw, dh, id | (uint64_t{1} << 60));
    };
    if (dc == gBackBufferDc && gBackBufferPixels) {
        GdiFlush();
        try {
        if (gHudTextRaster.draw(gGpuFrame ? nullptr : static_cast<uint32_t*>(gBackBufferPixels),
            gBackBufferWidth, gBackBufferHeight, text, rect, font, cacheColor, scale,
            render, false, gGpuFrame ? gpuHud : TextRasterCache::GpuDraw{}, false, true)) {
            if(rainbow)return;
            const COLORREF fill=color;
            const TextRasterCache::GpuDraw mask=[&](const uint32_t* pixels,int w,int h,int x,int y,int dw,int dh,uint64_t id){
                if(gGpuFrame)return gCompositionPresenter.maskSprite(pixels,w,h,x,y,dw,dh,id|(uint64_t{1}<<59),fill);
                blendTextMask(pixels,w,h,static_cast<uint32_t*>(gBackBufferPixels),gBackBufferWidth,gBackBufferHeight,x,y,dw,dh,fill);return true;
            };
            if(gHudMaskRaster.draw(nullptr,gBackBufferWidth,gBackBufferHeight,text,rect,font,RGB(255,255,255),scale,drawHudGlyphMask,false,mask,false,true))return;
        }
        } catch (...) { gHudTextRaster.clear(); gHudMaskRaster.clear(); }
    }
    const int measured = SaveDC(dc);
    if (font) SelectObject(dc, font);
    SIZE extent{}; GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &extent);
    RestoreDC(dc, measured);
    scale = std::min(scale, std::max(1L, rect.right - rect.left) / static_cast<double>(std::max(1L, extent.cx) + 16));
    if (scale == 1.0) {
        drawHudTextBody(dc, text, rect, font, color, phase);
        return;
    }
    const int saved = SaveDC(dc);
    const float centerX = (rect.left + rect.right) / 2.0f;
    const float centerY = (rect.top + rect.bottom) / 2.0f;
    const float size = static_cast<float>(scale);
    XFORM transform{size, 0, 0, size, centerX * (1 - size), centerY * (1 - size)};
    SetGraphicsMode(dc, GM_ADVANCED);
    ModifyWorldTransform(dc, &transform, MWT_LEFTMULTIPLY);
    drawHudTextBody(dc, text, rect, font, color, phase);
    RestoreDC(dc, saved);
}

void fillRect(HDC dc, RECT rect, COLORREF color) {
    auto found = gBrushCache.find(color);
    if (found == gBrushCache.end()) found = gBrushCache.emplace(color, CreateSolidBrush(color)).first;
    if (found->second) FillRect(dc, &rect, found->second);
}

void frameRect(HDC dc, RECT rect, COLORREF color) {
    auto found = gBrushCache.find(color);
    if (found == gBrushCache.end()) found = gBrushCache.emplace(color, CreateSolidBrush(color)).first;
    if (found->second) FrameRect(dc, &rect, found->second);
}

#include "LaneSeparator.inl"
void fillGameRect(HDC dc, RECT rect, COLORREF color, const RECT& clip) {
    if (gGpuNotePass) {
        gCompositionPresenter.rectangle(static_cast<float>(std::max(rect.left, clip.left)), static_cast<float>(std::max(rect.top, clip.top)),
            static_cast<float>(std::min(rect.right, clip.right)), static_cast<float>(std::min(rect.bottom, clip.bottom)), color);
        return;
    }
    if (dc != gBackBufferDc || !gBackBufferPixels) {
        fillRect(dc, rect, color);
        return;
    }
    rect.left = std::max({ static_cast<int>(rect.left), static_cast<int>(clip.left), 0 });
    rect.top = std::max({ static_cast<int>(rect.top), static_cast<int>(clip.top), 0 });
    rect.right = std::min({ static_cast<int>(rect.right), static_cast<int>(clip.right), gBackBufferWidth });
    rect.bottom = std::min({ static_cast<int>(rect.bottom), static_cast<int>(clip.bottom), gBackBufferHeight });
    if (rect.left >= rect.right || rect.top >= rect.bottom) return;

    const uint32_t pixel = (static_cast<uint32_t>(GetRValue(color)) << 16) |
        (static_cast<uint32_t>(GetGValue(color)) << 8) | static_cast<uint32_t>(GetBValue(color));
    auto* pixels = static_cast<uint32_t*>(gBackBufferPixels);
    const size_t stride = static_cast<size_t>(gBackBufferWidth);
    const size_t count = static_cast<size_t>(rect.right - rect.left);
    for (int y = rect.top; y < rect.bottom; ++y)
        std::fill_n(pixels + static_cast<size_t>(y) * stride + static_cast<size_t>(rect.left), count, pixel);
}

void fillGameRectSubpixel(HDC dc, int left, double top, int right, double bottom, COLORREF color, const RECT& clip) {
    if (!gNoteSubpixel || !gNoteAntialias) {
        top = std::round(top); bottom = std::round(bottom);
    }
    if (gGpuNotePass) {
        gCompositionPresenter.rectangle(static_cast<float>(std::max(left, static_cast<int>(clip.left))), static_cast<float>(std::max(top, static_cast<double>(clip.top))),
            static_cast<float>(std::min(right, static_cast<int>(clip.right))), static_cast<float>(std::min(bottom, static_cast<double>(clip.bottom))), color);
        return;
    }
    if (dc != gBackBufferDc || !gBackBufferPixels) {
        RECT rect{left, static_cast<LONG>(std::floor(top)), right, static_cast<LONG>(std::ceil(bottom))};
        fillRect(dc, rect, color); return;
    }
    left = std::max({left, static_cast<int>(clip.left), 0});
    right = std::min({right, static_cast<int>(clip.right), gBackBufferWidth});
    top = std::max({top, static_cast<double>(clip.top), 0.0});
    bottom = std::min({bottom, static_cast<double>(clip.bottom), static_cast<double>(gBackBufferHeight)});
    if (left >= right || top >= bottom) return;
    auto* pixels = static_cast<uint32_t*>(gBackBufferPixels);
    const unsigned red = GetRValue(color), green = GetGValue(color), blue = GetBValue(color);
    const uint32_t pixel = (red << 16) | (green << 8) | blue;
    for (int y = static_cast<int>(std::floor(top)); y < static_cast<int>(std::ceil(bottom)); ++y) {
        auto* output = pixels + static_cast<size_t>(y) * gBackBufferWidth + left;
        const double coverage = std::min(bottom, y + 1.0) - std::max(top, static_cast<double>(y));
        if (coverage >= 0.999999) { std::fill_n(output, right - left, pixel); continue; }
        const unsigned alpha = static_cast<unsigned>(std::lround(coverage * 255));
        const unsigned inverse = 255 - alpha;
        for (int x = 0; x < right - left; ++x) {
            const uint32_t before = output[x];
            const unsigned r = (red * alpha + ((before >> 16) & 255) * inverse + 127) / 255;
            const unsigned g = (green * alpha + ((before >> 8) & 255) * inverse + 127) / 255;
            const unsigned b = (blue * alpha + (before & 255) * inverse + 127) / 255;
            output[x] = (r << 16) | (g << 8) | b;
        }
    }
}

bool ensureBgaBackplate(HDC referenceDc, int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (!gBgaBackplateDc) gBgaBackplateDc = CreateCompatibleDC(referenceDc);
    if (!gBgaBackplateDc) return false;
    if (gBgaBackplateBitmap && width == gBgaBackplateWidth && height == gBgaBackplateHeight) return true;
    if (gBgaBackplateBitmap) {
        SelectObject(gBgaBackplateDc, gBgaBackplateOriginalBitmap);
        DeleteObject(gBgaBackplateBitmap);
        gBgaBackplateBitmap = nullptr;
        gDirectBgaFrame.reset(); gDirectBgaOverlay.reset();
        gBgaBackplatePixels = nullptr;
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    gBgaBackplateBitmap = CreateDIBSection(referenceDc, &info, DIB_RGB_COLORS, &gBgaBackplatePixels, nullptr, 0);
    if (!gBgaBackplateBitmap) return false;
    gBgaBackplateOriginalBitmap = SelectObject(gBgaBackplateDc, gBgaBackplateBitmap);
    std::fill_n(static_cast<uint32_t*>(gBgaBackplatePixels), static_cast<size_t>(width) * height, 0xff0a0e15u);
    gBgaBackplateWidth = width;
    gBgaBackplateHeight = height;
    gBgaBackplateDirty = true;
    return true;
}

void drawBgaBackplate(HDC dc, const bms::Chart& chart, const RECT& client,
    int fieldX, int laneWidth, int top, int judgeY, int scratchWidth = 0) {
    const RECT bgaViewport=playBgaViewport();
    const bool customViewport=bgaViewport.left!=0||bgaViewport.top!=0||bgaViewport.right!=client.right||bgaViewport.bottom!=client.bottom;
    if(gPlaybackLoad.preparing()){FillRect(dc,&client,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));return;}
    const auto layout=bms::chartLayout(chart,fieldX,laneWidth,scratchWidth>0?scratchWidth:laneWidth);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    gBgaRaster.canvas(width, height);
    if (gBgaRaster.update(gPlayhead)) gBgaBackplateDirty = true;
    const bool hybrid = hybridRendererEnabled && gGpuFrame && dc == gBackBufferDc && gBackBufferPixels;
    static bool previousHybrid = false;
    static uint64_t backgroundGeneration = 0, backdropRevision = 0;
    if (previousHybrid != hybrid) { gBgaBackplateDirty = true; previousHybrid = hybrid; }
    const double bgaTime = gPlayhead - gBgaVideoStartSeconds;
    if(bgaTime>=0 && (!hybridRendererEnabled || gBgaBrightness>0)) {
        int videoWidth=width,videoHeight=height;
        if(gChartOptions.bgaMode>0)if(const auto current=gVideoBga.frame()) {
            const double scale=gChartOptions.bgaMode==2?.8:1;
            videoWidth=std::max(1,static_cast<int>(current->sourceWidth*scale));videoHeight=std::max(1,static_cast<int>(current->sourceHeight*scale));
        }
        gVideoBga.request(bgaTime,videoWidth,videoHeight);
    }
    const auto videoFrame = bgaTime >= 0 && !gBgaVideos.declared()?gVideoBga.frame():nullptr;
    std::array<std::shared_ptr<const AsyncVideoBga::Frame>,4> layerFrames;
    uint64_t layerGeneration=0;
    for(int layer=0;gBgaBrightness>0 && layer<4;++layer) {
        if(layer==1 && (chart.poorBgaMode==2 || gPlayhead>=gMissBgaUntil))continue;
        layerFrames[layer]=gBgaVideos.frame(chart,layer,gPlayhead,width,height,false,gPoorBgaStart);
        if(layerFrames[layer])layerGeneration=layerGeneration*131+layerFrames[layer]->generation+layer+1;
    }
    static uint64_t previousLayerGeneration=0;
    if(layerGeneration!=previousLayerGeneration){gBgaBackplateDirty=true;previousLayerGeneration=layerGeneration;}
    static std::array<uint64_t,4> previousColors{};
    std::array<bms::BgaColor,4> colors{};
    std::array<bool,4> authoredColor{};
    for(int layer=0;layer<4;++layer) {
        const auto& events=chart.bgaColors[layer];
        auto next=std::upper_bound(events.begin(),events.end(),gPlayhead,[](double t,const bms::ColorEvent&e){return t<e.timeSeconds;});
        if(next!=events.begin()){colors[layer]=std::prev(next)->color;authoredColor[layer]=true;}
        const auto& color=colors[layer];
        const uint64_t signature=(uint64_t(color.alpha)<<32)|(uint64_t(color.red)<<24)|(uint64_t(color.green)<<16)|(uint64_t(color.blue)<<8)|
            static_cast<unsigned>(std::clamp(bms::valueAt(chart.bgaOpacity[layer],gPlayhead,1),0.0,1.0)*255);
        if(previousColors[layer]!=signature){gBgaBackplateDirty=true;previousColors[layer]=signature;}
    }
    const auto switchFrame=gBgaSwitch.frame(chart,gPlayhead,gPressed);
    static std::wstring previousSwitch;
    if(previousSwitch!=switchFrame.id){gBgaBackplateDirty=true;previousSwitch=switchFrame.id;}

    if (!ensureBgaBackplate(dc, width, height)) {
        fillRect(dc, client, RGB(10, 14, 21));
        return;
    }

    const bool missActive = chart.poorBgaMode!=2 && gPlayhead < gMissBgaUntil;
    gBgaRaster.canvas(width, height);
    const bool opaqueMiss = chart.poorBgaMode==0 && gBgaBrightness > 0 && missActive && gGdiPlusToken && gBgaRaster.coversCanvas(chart, gBgaActiveImages[1]);
    auto visibleImages = gBgaActiveImages;
    if (gBgaBrightness == 0) visibleImages.fill(L"");
    else if (opaqueMiss) { visibleImages[0].clear(); visibleImages[2].clear(); visibleImages[3].clear(); }
    else if (!missActive) visibleImages[1].clear();
    const uint64_t videoGeneration = gBgaBrightness > 0 && !opaqueMiss && videoFrame ? videoFrame->generation : 0;
    if (visibleImages != gBgaBackplateImages || videoGeneration != gBgaBackplateVideoGeneration) gBgaBackplateDirty = true;
    if (missActive != gBgaBackplateMissState || fieldX != gBgaBackplateFieldX ||
        laneWidth != gBgaBackplateLaneWidth || layout.scratchWidth != gBgaBackplateScratchWidth || top != gBgaBackplateTop || judgeY != gBgaBackplateJudgeY ||
        gBgaDarkness != gBgaBackplateDarkness || gLaneTransparency != gBgaBackplateTransparency || gBgaBrightness != gBgaBackplateBrightness) {
        gBgaBackplateDirty = true;
    }
    bool imagesPrepared=true;
    if(gBgaBrightness>0) {
        for(int layer=0;layer<4;++layer)if(!visibleImages[layer].empty()&&!layerFrames[layer]) {
            const auto id=bms::assetId(chart,visibleImages[layer]);const auto file=chart.bitmaps.find(id);
            if(file!=chart.bitmaps.end()&&BgaVideoLayers::isVideo(file->second))continue;
            imagesPrepared=gBgaRaster.prepared(chart,visibleImages[layer],!chart.bmson&&(layer==2||layer==3),authoredColor[layer]?&colors[layer]:nullptr)&&imagesPrepared;
        }
        if(!switchFrame.id.empty())imagesPrepared=gBgaRaster.prepared(chart,switchFrame.id,true,&switchFrame.color)&&imagesPrepared;
    }
    // A cold overlay may delay rebuilding even while falling back to CPU painting.
    if (!hybrid && gDirectBgaFrame) {
        if (!imagesPrepared) {
            auto* pixels = static_cast<uint32_t*>(gBgaBackplatePixels);
            GdiFlush();
            std::copy_n(gDirectBgaFrame.get(), static_cast<size_t>(width) * height, pixels);
            const RECT canvas{0,0,width,height};
            if (gDirectBgaOverlay) videoComposite::over(gDirectBgaOverlay.get(),pixels,width,canvas);
            BgaRaster::tint(pixels,width,height,canvas,0xff000000u,gBgaBackplateDarkness*255/100);
            const RECT lane{fieldX,0,fieldX+layout.totalWidth(),height};
            BgaRaster::tint(pixels,width,height,lane,0xff000000u,(100-gBgaBackplateTransparency)*255/100);
        }
        gDirectBgaFrame.reset(); gDirectBgaOverlay.reset();
    }
    // Retain the previous complete backdrop while a cold image is being prepared.
    if (gBgaBackplateDirty && imagesPrepared) {
        BMS_PERF_SCOPE(backplateBuildMs);
        BMS_PERF_SET(rebuiltBga, true);
        const auto previousFrame = std::move(gDirectBgaFrame);
        gDirectBgaOverlay.reset();
        if (hybrid && gGdiPlusToken && gBgaBrightness > 0 && switchFrame.id.empty()) {
            // The topmost visible layer alone can supply the backdrop if it covers
            // every pixel opaquely. Leave transparency and partial layers to the compositor.
            bool foundLayer = false;
            for (int layer : {1, 3, 2, 0}) {
                if ((layer == 1 && !missActive) || (opaqueMiss && layer != 1)) continue;
                if (visibleImages[layer].empty() && !layerFrames[layer]) continue;
                foundLayer = true;
                const bool blackKey = !chart.bmson && (layer == 2 || layer == 3);
                if (!authoredColor[layer] && !chart.bitmapColors.contains(bms::assetId(chart,visibleImages[layer])) &&
                    bms::valueAt(chart.bgaOpacity[layer],gPlayhead,1) >= 1) {
                    const auto& frame = layerFrames[layer];
                    if (frame && (chart.bmson || layer == 0) && frame->width == width && frame->height == height)
                        gDirectBgaFrame = {frame, frame->pixels.data()};
                    if (!frame) gDirectBgaFrame = gBgaRaster.canvasFrame(chart,visibleImages[layer],blackKey,true);
                }
                break;
            }
            // Keep a fullscreen keyed still on a separate GPU surface over an opaque video.
            const auto& base = layerFrames[0];
            if (!gDirectBgaFrame && base && base->width == width && base->height == height &&
                !missActive && !layerFrames[2] && !layerFrames[3] && visibleImages[3].empty() &&
                !visibleImages[2].empty() && !authoredColor[0] && !authoredColor[2] &&
                !chart.bitmapColors.contains(bms::assetId(chart,visibleImages[0])) &&
                !chart.bitmapColors.contains(bms::assetId(chart,visibleImages[2])) &&
                bms::valueAt(chart.bgaOpacity[0],gPlayhead,1)>=1 && bms::valueAt(chart.bgaOpacity[2],gPlayhead,1)>=1) {
                gDirectBgaOverlay=gBgaRaster.canvasFrame(chart,visibleImages[2],!chart.bmson,false);
                if (gDirectBgaOverlay) gDirectBgaFrame={base,base->pixels.data()};
            }
            if (!foundLayer && !opaqueMiss && videoFrame && videoFrame->width == width && videoFrame->height == height)
                gDirectBgaFrame = {videoFrame, videoFrame->pixels.data()};
        }
        if (!gDirectBgaFrame) {
            const RECT cacheRect{ 0, 0, width, height };
            GdiFlush();
            std::fill_n(static_cast<uint32_t*>(gBgaBackplatePixels), static_cast<size_t>(width) * height, 0xff0a0e15u);
            auto* pixels = static_cast<uint32_t*>(gBgaBackplatePixels);
            gBgaRaster.canvas(width, height);
            bool drewImage = false;
            if (gGdiPlusToken && gBgaBrightness > 0) {
                BMS_PERF_SCOPE(imageMs);
                if(!opaqueMiss && videoFrame) {
                    const auto& f=*videoFrame;const int left=(width-f.width)/2;
                    for(int y=0;y<std::min(height,f.height);++y)for(int x=std::max(0,left);x<std::min(width,left+f.width);++x)
                        pixels[y*width+x]=f.pixels[y*f.width+x-left];
                    drewImage=true;
                }
                for(int layer:{0,2,3,1}) {
                    if((layer==1 && !missActive) || (opaqueMiss && layer!=1))continue;
                    const unsigned alpha=static_cast<unsigned>(std::clamp(bms::valueAt(chart.bgaOpacity[layer],gPlayhead,1),0.0,1.0)*255);
                    if(layerFrames[layer]) {
                        const auto& f=*layerFrames[layer];
                        const auto defined=chart.bitmapColors.find(bms::assetId(chart,visibleImages[layer]));
                        const bms::BgaColor blackKey{};
                        const auto* key=authoredColor[layer]?&colors[layer]:defined!=chart.bitmapColors.end()?&defined->second:
                            (!chart.bmson && layer!=0)?&blackKey:nullptr;
                        const unsigned effectiveAlpha=alpha*(authoredColor[layer]?colors[layer].alpha:255)/255*
                            (defined!=chart.bitmapColors.end()?defined->second.alpha:255)/255;
                        videoComposite::blend(f.pixels.data(),f.width,f.height,pixels,width,height,(width-f.width)/2,effectiveAlpha,key);
                        drewImage=true;
                    } else drewImage|=gBgaRaster.draw(chart,visibleImages[layer],pixels,alpha,!chart.bmson && (layer==2 || layer==3),authoredColor[layer]?&colors[layer]:nullptr);
                }
                if(!switchFrame.id.empty())drewImage|=gBgaRaster.draw(chart,switchFrame.id,pixels,255,true,&switchFrame.color);
            }
            if (!hybrid && gBgaBrightness > 0 && (drewImage || gBgaDarkness > 0)) {
                BMS_PERF_SCOPE(shadeMs);
                if (gBgaDarkness > 0) BgaRaster::tint(pixels, width, height, cacheRect, 0xff000000u, static_cast<unsigned>(gBgaDarkness * 255 / 100));
            }
            if (!hybrid&&!customViewport) {
                BMS_PERF_SCOPE(tintMs);
                const RECT laneField{ fieldX, 0, fieldX + layout.totalWidth(), height };
                BgaRaster::tint(pixels, width, height, laneField, 0xff000000u, static_cast<unsigned>((100 - gLaneTransparency) * 255 / 100));
            }
        }
        gBgaBackplateFieldX = fieldX;
        gBgaBackplateLaneWidth = laneWidth;
        gBgaBackplateScratchWidth = layout.scratchWidth;
        gBgaBackplateDarkness = gBgaDarkness;
        gBgaBackplateBrightness = gBgaBrightness;
        gBgaBackplateVideoGeneration = videoGeneration;
        gBgaBackplateImages = visibleImages;
        gBgaBackplateTransparency = gLaneTransparency;
        gBgaBackplateTop = top;
        gBgaBackplateJudgeY = judgeY;
        gBgaBackplateMissState = missActive;
        gBgaBackplateDirty = false;
        ++backdropRevision;
        if (!gDirectBgaFrame || gDirectBgaFrame != previousFrame) ++backgroundGeneration;
    }
    if (hybrid) {
        gCompositionPresenter.setBgaViewport(bgaViewport);
        const auto regions = hybridCpuRegions(width, height, fieldX, layout.totalWidth());
        static uint64_t preparedRevision = 0;
        static std::array<int,4> preparedGeometry{};
        const std::array<int,4> geometry{width,height,fieldX,layout.totalWidth()};
        if (gDirectBgaOverlay && (preparedRevision != backdropRevision || geometry != preparedGeometry)) {
            preparedRevision = backdropRevision;
            preparedGeometry = geometry;
            // CPU-drawn HUD regions need the composited colors too. Prepare them
            // once per changed backdrop, then reuse them for subsequent HUD paints.
            GdiFlush();
            auto* prepared = static_cast<uint32_t*>(gBgaBackplatePixels);
            for (const RECT& rect : regions) {
                for (int y=rect.top;y<rect.bottom;++y) {
                    const size_t start=static_cast<size_t>(y)*width+rect.left;
                    std::copy_n(gDirectBgaFrame.get()+start, rect.right-rect.left, prepared+start);
                }
                videoComposite::over(gDirectBgaOverlay.get(),prepared,width,rect);
            }
        }
        const RECT lane{fieldX, 0, fieldX + layout.totalWidth(), height};
        const int darkness = gBgaBrightness > 0 ? gBgaDarkness : 0;
        const auto* gpuSource = gDirectBgaFrame ? gDirectBgaFrame.get() :
            static_cast<const uint32_t*>(gBgaBackplatePixels);
        gCompositionPresenter.hybridBackground(gpuSource, backgroundGeneration,
            darkness, lane, 100 - gLaneTransparency, regions, gDirectBgaFrame, gDirectBgaOverlay);
        BMS_PERF_SCOPE(bgaCopyMs);
        GdiFlush();
        auto* destination = static_cast<uint32_t*>(gBackBufferPixels);
        const auto* source = gDirectBgaOverlay ? static_cast<const uint32_t*>(gBgaBackplatePixels) : gpuSource;
        for (const auto& region : regions) {
            RECT overlap{};
            const bool intersects = IntersectRect(&overlap, &region, &lane) != FALSE;
            if (gLaneTransparency == 0 && intersects && EqualRect(&overlap, &region)) {
                for (int y = region.top; y < region.bottom; ++y)
                    std::fill_n(destination + static_cast<size_t>(y) * width + region.left, region.right - region.left, 0xff000000u);
                continue;
            }
            for (int y = region.top; y < region.bottom; ++y){
                if(!customViewport)std::copy_n(source+static_cast<size_t>(y)*width+region.left,region.right-region.left,destination+static_cast<size_t>(y)*width+region.left);
                else for(int x=region.left;x<region.right;++x){
                    const int sx=int((int64_t(x)-bgaViewport.left)*width/std::max(1L,bgaViewport.right-bgaViewport.left));
                    const int sy=int((int64_t(y)-bgaViewport.top)*height/std::max(1L,bgaViewport.bottom-bgaViewport.top));
                    destination[size_t(y)*width+x]=x>=bgaViewport.left&&x<bgaViewport.right&&y>=bgaViewport.top&&y<bgaViewport.bottom&&sx>=0&&sx<width&&sy>=0&&sy<height?source[size_t(sy)*width+sx]:0xff000000u;
                }
            }
            BgaRaster::tint(destination, width, height, region, 0xff000000u, static_cast<unsigned>(darkness * 255 / 100));
            if (intersects)
                BgaRaster::tint(destination, width, height, overlap, 0xff000000u, static_cast<unsigned>((100 - gLaneTransparency) * 255 / 100));
        }
        return;
    }
    { BMS_PERF_SCOPE(bgaCopyMs);
      if(customViewport){FillRect(dc,&client,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));SetStretchBltMode(dc,HALFTONE);StretchBlt(dc,bgaViewport.left,bgaViewport.top,bgaViewport.right-bgaViewport.left,bgaViewport.bottom-bgaViewport.top,gBgaBackplateDc,0,0,width,height,SRCCOPY);if(dc==gBackBufferDc&&gBackBufferPixels){GdiFlush();const RECT laneField{fieldX,0,fieldX+layout.totalWidth(),height};BgaRaster::tint(static_cast<uint32_t*>(gBackBufferPixels),width,height,laneField,0xff000000u,static_cast<unsigned>((100-gLaneTransparency)*255/100));}}
      else BitBlt(dc,client.left,client.top,width,height,gBgaBackplateDc,0,0,SRCCOPY);
    }
}

struct BrowsePanelLayout {
    int width{}, genreY{}, titleY{}, titleHeight{}, composerY{}, bpmY{}, levelY{}, startY{}, startHeight{}, infoY{}, infoHeight{}, slotSize{}, slotTop{};
    explicit BrowsePanelLayout(int windowWidth, int windowHeight) {
        width = std::max(1, windowWidth - 24 - windowWidth / 3 - 44);
        constexpr int songInfoOffset = 24;
        genreY = 24 + songInfoOffset; titleY = 84 + songInfoOffset;
        titleHeight = std::clamp(windowHeight / 6, 144, 200);
        composerY = titleY + titleHeight + 12;
        bpmY = composerY + 60;
        levelY = bpmY + 64;
        // Two metadata lines (including Korean font fallback) need more than
        // the former single-row 60px area. Keep the shared clip/info layout below them.
        startY = levelY + 84; startHeight = 96;
        infoY = startY + startHeight;
        slotTop = std::max(windowHeight - 150, infoY + 16);
        infoHeight = std::clamp(slotTop - infoY - 8, 1, 48);
        slotSize = std::max(24, std::min({100, (width - 48) / 5, windowHeight / 8, windowHeight - 48 - slotTop}));
    }
    RECT slot(int index, int windowHeight) const {
        (void)windowHeight;
        const int left = 24 + index * (slotSize + 12);
        return {left, slotTop, left + slotSize, slotTop + slotSize};
    }
};

bool adjustBrowseShortcut(int x, int y, bool increase) {
    if (gPlaying || gShowingResult || gLoading || gSettingsWindow) return false;
    const BrowsePanelLayout panel(gWindowWidth, gWindowHeight);
    const POINT point{x, y};
#ifdef BMS_ENABLE_SKIN_BINDINGS
    const bool skinned=skinOwnsHud();
    const int skinSlot=skinned?selectSkinShortcutAt(x,y):-1;
#endif
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(gSongInfoVisible){gSongInfoVisible=false;ShowWindow(gList,SW_SHOWNA);InvalidateRect(gWindow,nullptr,FALSE);return true;}
    if(skinSlot==3){openSettings();return true;}
    if(skinSlot==4){try{
        const int i=selectedIndex();if(i<0||i>=int(gCharts.size()))return true;
        auto chart=gCharts[i];if(chart.catalogOnly)chart=bms::BmsParser().parse(chart.path).chart;
        size_t normal=0,holds=0;for(const auto& n:chart.notes)(n.longNote()?holds:normal)++;
        using namespace sevenmania::skin;gSongInformationData.beginFrame(ScreenKind::SongInformation);
        gSongInformationData.setWide(BindingId::SongTitle,chart.title);gSongInformationData.setWide(BindingId::SongArtist,chart.artist);gSongInformationData.setWide(BindingId::SongGenre,chart.genre);gSongInformationData.setWide(BindingId::ChartLevelText,chart.playLevel);
        gSongInformationData.set(BindingId::SongBpm,chart.initialBpm);gSongInformationData.set(BindingId::InfoNormalNotes,std::int64_t(normal));gSongInformationData.set(BindingId::InfoLongNotes,std::int64_t(holds));gSongInformationData.set(BindingId::InfoDuration,chart.durationSeconds);gSongInformationData.endFrame();
        gSongInfoText=chart.title+L"\n\n"+chart.artist+L"\n"+chart.genre+L"\nBPM  "+number(chart.initialBpm,1)+L"\n"+bms::BmsParser::modeLabel(chart.mode)+L"   LEVEL "+chart.playLevel+L"\n일반 노트  "+std::to_wstring(normal)+L"   롱노트  "+std::to_wstring(holds)+L"\n채보 길이  "+number(chart.durationSeconds,1)+L"초\n\nEsc 또는 클릭으로 닫기";
        restartSongInformationEntrance();gSongInfoVisible=true;ShowWindow(gList,SW_SHOWNA);InvalidateRect(gWindow,nullptr,FALSE);return true;
    }catch(...){setStatus(L"곡 정보를 읽을 수 없습니다. 파일 경로와 채보를 확인하세요.");return true;}}
#endif
    for (int slot = 0; slot < 3; ++slot) {
        const RECT bounds = panel.slot(slot, gWindowHeight);
#ifdef BMS_ENABLE_SKIN_BINDINGS
        if(skinned?skinSlot!=slot:!PtInRect(&bounds,point))continue;
#else
        if (!PtInRect(&bounds, point)) continue;
#endif
        const int step = increase ? 1 : -1;
        if (slot == 0) gNoteSpeed = std::clamp(gNoteSpeed + step * 0.25, 0.5, 10.0);
        else if (slot == 1) return false;
        else {
            constexpr std::array<GrooveGaugeMode, 3> modes{GrooveGaugeMode::Easy, GrooveGaugeMode::Normal, GrooveGaugeMode::Hard};
            const int current = static_cast<int>(std::find(modes.begin(), modes.end(), gGrooveGaugeMode) - modes.begin());
            gGrooveGaugeMode = modes[static_cast<size_t>(std::clamp(current + step, 0, 2))];
        }
        saveDisplaySettings();
        gAudio.triggerEffect(L"click");
        SetFocus(gWindow);
        InvalidateRect(gWindow, nullptr, FALSE);
        return true;
    }
    return false;
}

std::wstring browseRecordSummary(const PlayRecord* record) {
    if(!record)return L"NO RECORD";
    return L"BEST  " + std::to_wstring(record->bestAccuracyScore) + L"   RANK  " +
        std::wstring(ResultScore::rankForScore(record->bestAccuracyScore)) +
        (record->cleared?L"   CLEAR":L"   FAILED") + L"\nMAX COMBO  " + std::to_wstring(record->maxCombo);
}
std::wstring groupedRecordScore(int score) {
    const auto digits=std::to_wstring(std::clamp(score,0,1'000'000));
    std::wstring result;result.reserve(digits.size()+digits.size()/3);
    for(size_t i=0;i<digits.size();++i){if(i&&(digits.size()-i)%3==0)result.push_back(L',');result.push_back(digits[i]);}
    return result;
}
void drawBrowsePlayRecord(HDC dc, const PlayRecord* record, const BrowsePanelLayout& panel) {
    const auto text=browseRecordSummary(record);
    RECT bounds{24,panel.startY,24+panel.width,panel.infoY};
    const int saved=SaveDC(dc);
    SelectObject(dc,gSmallFont?gSmallFont:gFont);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(226,232,240));
    DrawTextW(dc,text.c_str(),-1,&bounds,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
    if(saved)RestoreDC(dc,saved);
}

void drawBrowseBackground(HDC dc, const RECT* sourceRegion = nullptr) {
    RECT client{}; GetClientRect(gWindow, &client);
    const RECT source = sourceRegion ? *sourceRegion : client;
    const RECT destination{0, 0, source.right - source.left, source.bottom - source.top};
    fillRect(dc, destination, RGB(64, 64, 64));
    const int selected = selectedIndex();
    const auto* selectedChart = selected >= 0 && selected < static_cast<int>(gCharts.size()) ? &gCharts[static_cast<size_t>(selected)] : nullptr;
    double backgroundTime = std::chrono::duration<double>(std::chrono::steady_clock::now() - gBrowseSelectedAt).count();
    if (selectedChart) {
        const double start = selectedChart->previewStartSeconds < selectedChart->durationSeconds ? selectedChart->previewStartSeconds : 0;
        const double length = std::max(1.0, std::min(selectedChart->previewDurationSeconds, selectedChart->durationSeconds - start));
        backgroundTime = start + std::fmod(std::max(0.0, backgroundTime), length);
    }
    backgroundTime = gSongPreview.chartTime(backgroundTime);
    const bool bgaPending=gBrowseBackgroundMode==1&&std::chrono::steady_clock::now()-gBrowseSelectedAt<std::chrono::milliseconds(500);
    const auto& background = gBrowseBackdrop.render(selectedChart, bgaPending?0:gBrowseBackgroundMode,
        gCustomBackgroundName.empty() ? std::filesystem::path{} : executableFolder() / L"Assets" / gCustomBackgroundName,
        gBackgroundBlur, gBackgroundOpacity, client.right, client.bottom, backgroundTime);
    if (!background.empty()) {
        if (!sourceRegion && dc == gBackBufferDc && gBackBufferPixels && gBackBufferWidth == client.right && gBackBufferHeight == client.bottom) {
            GdiFlush(); std::copy(background.begin(), background.end(), static_cast<uint32_t*>(gBackBufferPixels));
        } else if (gGdiPlusToken) {
            Gdiplus::Bitmap bitmap(client.right, client.bottom, client.right * 4, PixelFormat32bppARGB,
                reinterpret_cast<BYTE*>(const_cast<uint32_t*>(background.data())));
            Gdiplus::Graphics graphics(dc);
            graphics.DrawImage(&bitmap, Gdiplus::Rect(0, 0, destination.right, destination.bottom),
                source.left, source.top, destination.right, destination.bottom, Gdiplus::UnitPixel);
        }
    }
}

void drawStartupSplash(HDC dc) {
    const RECT client{0, 0, gWindowWidth, gWindowHeight};
    fillRect(dc, client, RGB(9, 13, 18));
    if (gStartupSplashImage && gStartupSplashImage->GetLastStatus() == Gdiplus::Ok) {
        const double scale = std::min(static_cast<double>(gWindowWidth) / gStartupSplashImage->GetWidth(),
            static_cast<double>(gWindowHeight) / gStartupSplashImage->GetHeight());
        const int width = static_cast<int>(gStartupSplashImage->GetWidth() * scale);
        const int height = static_cast<int>(gStartupSplashImage->GetHeight() * scale);
        Gdiplus::Graphics graphics(dc);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.DrawImage(gStartupSplashImage.get(), (gWindowWidth - width) / 2, (gWindowHeight - height) / 2, width, height);
    } else {
        RECT title{24, gWindowHeight / 3, gWindowWidth - 24, gWindowHeight / 2};
        drawTextBody(dc, L"BMS PLAYER", title, gBrowseTitleFont ? gBrowseTitleFont : gFont, RGB(230, 245, 255));
    }
    const int barWidth = std::min(600, gWindowWidth - 96);
    const int left = (gWindowWidth - barWidth) / 2, top = gWindowHeight - 92;
    fillRect(dc, RECT{left, top, left + barWidth, top + 4}, RGB(36, 52, 66));
    fillRect(dc, RECT{left, top, left + barWidth * std::clamp(gStartupProgress, 0, 100) / 100, top + 4}, RGB(101, 219, 255));
    const int length = GetWindowTextLengthW(gStatus);
    std::wstring status(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(gStatus, status.data(), length + 1); status.resize(length);
    if (status.empty()) status = L"곡 라이브러리를 준비하고 있습니다...";
    RECT text{24, top + 14, gWindowWidth - 24, gWindowHeight - 20};
    drawTextBody(dc, status, text, gSmallFont ? gSmallFont : gFont, RGB(204, 222, 235));
}

COLORREF songDifficultyColor(const bms::Chart& chart);

void drawBrowseDifficultyCarousel(HDC dc, const BrowsePanelLayout& panel, const bms::ui::SongGroup* group,
    const bms::Chart& selectedChart) {
    const size_t count = group && !group->chartIndices.empty() ? group->chartIndices.size() : 1;
    const auto now = std::chrono::steady_clock::now();
    const double position = std::clamp(gBrowseDifficultyCarousel.position(now), 0.0,
        static_cast<double>(count > 0 ? count - 1 : 0));
    constexpr float baseFontSize = 32.0f;
    constexpr float itemGap = 24.0f;
    const float centerX = 24.0f + panel.width / 2.0f;
    const float centerY = (panel.levelY + panel.startY) / 2.0f;

    const int saved = SaveDC(dc);
    IntersectClipRect(dc, 24, panel.levelY, 24 + panel.width, panel.startY);
    Gdiplus::Graphics graphics(dc);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::StringFormat measureFormat;
    Gdiplus::FontFamily family(L"Segoe UI");

    struct CarouselItem {
        const bms::Chart* chart{};
        std::wstring level;
        float emphasis{}, fontSize{}, width{};
    };
    std::vector<CarouselItem> items(count);
    std::vector<float> centers(count);
    for (size_t index = 0; index < count; ++index) {
        const bms::Chart* chart = &selectedChart;
        if (group && !group->chartIndices.empty()) {
            const size_t chartIndex = group->chartIndices[index];
            if (chartIndex >= gCharts.size()) chart = &selectedChart;
            else chart = &gCharts[chartIndex];
        }
        auto& item = items[index];
        item.chart = chart;
        item.level = chart->playLevel.empty() ? L"--" : chart->playLevel;
        item.emphasis = static_cast<float>(std::clamp(1.0 - std::abs(static_cast<double>(index) - position), 0.0, 1.0));
        item.fontSize = baseFontSize * (1.0f + 0.5f * item.emphasis);
        Gdiplus::Font font(&family, item.fontSize, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::RectF measured{};
        graphics.MeasureString(item.level.c_str(), static_cast<INT>(item.level.size()), &font,
            Gdiplus::PointF(0.0f, 0.0f), &measureFormat, &measured);
        item.width = std::max(1.0f, measured.Width);
        if (index == 0) centers[index] = 0.0f;
        else centers[index] = centers[index - 1] + (items[index - 1].width + item.width) / 2.0f + itemGap;
    }

    const size_t leftIndex = static_cast<size_t>(std::floor(position));
    const size_t rightIndex = std::min(leftIndex + 1, count - 1);
    const float fraction = static_cast<float>(position - leftIndex);
    const float selectedCoordinate = centers[leftIndex] + (centers[rightIndex] - centers[leftIndex]) * fraction;
    for (size_t index = 0; index < count; ++index) {
        const auto& item = items[index];
        const float x = centerX + centers[index] - selectedCoordinate;
        if (x + item.width / 2.0f < 24.0f || x - item.width / 2.0f > 24.0f + panel.width) continue;
        const int alpha = static_cast<int>(std::lround(153.0 + 102.0 * item.emphasis));
        const COLORREF accent = songDifficultyColor(*item.chart);
        const auto blend = [emphasis = item.emphasis](int plain, int highlighted) {
            return static_cast<int>(std::lround(plain + (highlighted - plain) * emphasis));
        };
        const Gdiplus::Color color(static_cast<BYTE>(alpha), static_cast<BYTE>(blend(204, GetRValue(accent))),
            static_cast<BYTE>(blend(216, GetGValue(accent))), static_cast<BYTE>(blend(230, GetBValue(accent))));
        Gdiplus::SolidBrush brush(color);
        Gdiplus::Font font(&family, item.fontSize, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        const float textHeight = item.fontSize * 1.8f;
        const Gdiplus::RectF bounds(x - item.width / 2.0f - 3.0f, centerY - textHeight / 2.0f,
            item.width + 6.0f, textHeight);
        graphics.DrawString(item.level.c_str(), static_cast<INT>(item.level.size()), &font, bounds, &format, &brush);
    }
    if (saved) RestoreDC(dc, saved);
}

#ifdef BMS_ENABLE_SKIN_BINDINGS
#include "SkinApplicationHost.inl"
#endif
#include "SongIntroHost.inl"
#include "PlayBgaViewport.inl"

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
    constexpr std::array<const wchar_t*, 3> gaugeNames{L"NORMAL", L"EASY", L"HARD"};
    const std::array<std::wstring, 3> values{number(gNoteSpeed, 2) + L"x", judgementNames[static_cast<size_t>(gJudgementProfile)], gaugeNames[static_cast<size_t>(gGrooveGaugeMode)]};
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
    if (gResultBackdropDc && gResultBackdropBitmap && width == gResultBackdropWidth && height == gResultBackdropHeight)
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
}

void drawResultScreen(HDC dc) {
#ifdef BMS_ENABLE_SKIN_BINDINGS
    drawSkinOnly(dc);return;
#endif
    RECT client{}; GetClientRect(gWindow, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (gResultBackdropDc && gResultBackdropBitmap && width == gResultBackdropWidth && height == gResultBackdropHeight)
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
    RECT titleRect{ 32, 44, width - 32, 104 };
    RECT levelRect{ 32, 105, width - 32, 145 };
    RECT detailsRect{ 32, 145, width - 32, 178 };
    RECT rankRect{ 0, height / 2 - 115, width, height / 2 + 15 };
    RECT scoreRect{ 0, height / 2 + 26, width, height / 2 + 95 };
    drawShadowedCenteredText(dc, title, titleRect, gResultTitleFont ? gResultTitleFont : gFont, RGB(245, 248, 252), false);
    drawShadowedCenteredText(dc, L"LV " + level, levelRect, gResultDetailsFont ? gResultDetailsFont : gFont, RGB(162, 213, 236), false);
    drawShadowedCenteredText(dc, L"BPM: " + bpm + L"    composer: " + artist, detailsRect, gResultDetailsFont ? gResultDetailsFont : gSmallFont, RGB(218, 228, 240), false);
    drawShadowedCenteredText(dc, std::wstring(gResultScore.rank()), rankRect, gResultRankFont ? gResultRankFont : gFont, RGB(255, 214, 20));
    drawShadowedCenteredText(dc, std::to_wstring(gResultScore.value()), scoreRect, gResultScoreFont ? gResultScoreFont : gFont, RGB(245, 248, 252));
    RECT clearRect{ 0, height / 2 + 98, width, height / 2 + 144 };
    const wchar_t* gaugeOutcome = gPractice.enabled ? L"PRACTICE" : gResultGaugeFailed ? L"FAILED" : gResultGaugeCleared ? L"CLEAR" : L"NO CLEAR";
    const COLORREF gaugeOutcomeColor = gPractice.enabled ? RGB(255, 255, 255) : gResultGaugeFailed ? RGB(248, 113, 113) :
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
    layout.title = {24, 16, width - 24, 96};
    layout.speedLine = {24, 104, width - 24, 134};
    layout.guardLine = {24, 138, width - 24, 168};
    layout.shortcuts = {24, std::max(0, height - 28), width - 24, height - 8};
    const int buttonWidth = std::min(std::clamp(width * 34 / 100, 280, 560), std::max(1, width - 48));
    const int buttonGap = std::max(12, height * 2 / 100);
    const int left = (width - buttonWidth) / 2;
    const int top = std::min(std::max(184, height * 18 / 100), std::max(160, height / 2));
    const int availableHeight = std::max(0, (height - top - 32 - 3 * buttonGap) / 4);
    const int buttonHeight = std::max(32, std::min(144, availableHeight));
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
    const bool doublePlay=chart.mode==bms::ChartMode::TenKey || chart.mode==bms::ChartMode::FourteenKey;
    const int laneWidth=doublePlay?std::max(28,LaneLayout::defaultKeyWidth*2/3):LaneLayout::defaultKeyWidth;
    const int scratchWidth=doublePlay?68:LaneLayout::defaultScratchWidth;
    const auto gearLayout=bms::chartLayout(chart,0,laneWidth,scratchWidth);
    const int gearWidth = gearLayout.totalWidth();
    const int edgeInset = std::min(64, std::max(0, (gWindowWidth - gearWidth) / 2));
    const int available = std::max(0, gWindowWidth - gearWidth);
    const int fieldX = gGearPosition == 0 ? edgeInset : gGearPosition == 2 ? std::max(edgeInset, available - edgeInset) : available / 2;
    const auto layout=bms::chartLayout(chart,fieldX,laneWidth,scratchWidth);
    const int top = PLAYFIELD_TOP;
    const int normalJudgeY=judgementLineY(gWindowHeight);
    const int judgeY=bms::optionJudgeLine(gChartOptions,gPlayhead,normalJudgeY,gWindowHeight);
    const bool reverse=gChartOptions.crazy.contains(L"reverse");
    const auto noteY=[&](double distance){return judgeY+(reverse?distance:-distance);};
    const int fieldBottom=reverse?gWindowHeight:judgeY+38;
    { BMS_PERF_SCOPE(bgaMs); HUD_SCOPE(Backdrop);drawBgaBackplate(dc, chart, client, fieldX, laneWidth, top, judgeY, scratchWidth); }
    if (dc == gBackBufferDc && gBackBufferPixels) {
        BMS_PERF_SCOPE(beamsMs);
        GdiFlush();
        gKeyBeams.draw(static_cast<uint32_t*>(gBackBufferPixels), gBackBufferWidth, gBackBufferHeight, fieldX, laneWidth, judgeY, gPlayhead, scratchWidth);
    }
    const bms::TempoEvent* tempo = chart.tempos.empty() ? nullptr : &chart.tempos.front();
    for (const auto& item : chart.tempos) { if (item.timeSeconds <= gPlayhead) tempo = &item; else break; }
    {
    BMS_PERF_SCOPE(hudMs);
    for (size_t i = 0; !gChartOptions.crazy.contains(L"dark") && !gChartOptions.crazy.contains(L"dark2") && i < gLaneSeparators.size(); ++i) {
        if (!gLaneSeparators[i]) continue;
        const int x = layout.left(static_cast<int>(i + 1));
        const RECT separator{ x, 0, x + 1, gWindowHeight };
        drawLaneSeparator(dc,separator,judgeY);
    }
    RECT judge{ fieldX, judgeY, fieldX + layout.totalWidth(), judgeY + 3 };
    fillRect(dc, judge, RGB(248, 250, 252));
    }

    const double pixelsPerSecond = 270.0 * (gChartOptions.hiSpeed>0?gChartOptions.hiSpeed:gNoteSpeed) * bms::speedAt(chart,gPlayhead);
    const double scrollPosition = bms::scrollPositionSeconds(chart, gPlayhead);
    const double visibleBehind = 42.0 / std::max(0.001,std::abs(pixelsPerSecond));
    const double visibleAhead = (std::max(judgeY-top,fieldBottom-judgeY) + gNoteHeight) / std::max(0.001,std::abs(pixelsPerSecond));
    const double firstVisiblePosition = scrollPosition - visibleBehind - 0.01;
    const double lastVisiblePosition = scrollPosition + visibleAhead;
    const bool fullScan=chart.nonMonotonicScroll || pixelsPerSecond<=0 || gChartOptions.speedEffect!=L"off";
    const auto firstNote = fullScan?chart.notes.begin():std::lower_bound(chart.notes.begin(), chart.notes.end(), firstVisiblePosition,
        [&](const bms::Note& note, double position) { return bms::scrollPositionSeconds(chart, note.timeSeconds) < position; });
    const RECT noteBounds{ fieldX, top, fieldX + layout.totalWidth(), fieldBottom };
    const bool directRaster = dc == gBackBufferDc && gBackBufferPixels;
    gGpuNotePass = gGpuFrame && directRaster && !hybridRendererEnabled;
    int noteClip = 0;
    if (directRaster) { BMS_PERF_SCOPE(syncMs);HUD_SCOPE(GdiSync); GdiFlush(); }
    else {
        noteClip = SaveDC(dc);
        IntersectClipRect(dc, noteBounds.left, noteBounds.top, noteBounds.right, noteBounds.bottom);
    }
    {
    BMS_PERF_SCOPE(notesMs);
    size_t visibleNotes = 0;
    auto drawNote = [&](size_t noteIndex) {
        const auto& note = chart.notes[noteIndex];
        const double notePosition = bms::scrollPositionSeconds(chart, note.timeSeconds);
        double distance=(notePosition-scrollPosition)*pixelsPerSecond;
        distance*=bms::optionVisualSpeed(gChartOptions,note,gPlayhead,gFeedback.combo(),distance);
        const auto visual=bms::optionNoteVisual(gChartOptions,note,gPlayhead,distance);distance=visual.distance;
        const double visibility=bms::optionVisibility(gChartOptions,distance,gPlayhead);
        int x=layout.left(note.lane),right=layout.right(note.lane);
        const int shift=static_cast<int>(visual.shift*layout.width(note.lane));x+=shift;right+=shift;
        const int inset=static_cast<int>((right-x)*(1-visual.scale)/2);x+=inset;right-=inset;
        const int noteHeight=std::max(1,static_cast<int>(gNoteHeight*visual.scale));
        const double headY=noteY(distance);
        double tailY=headY;
        if(note.longNote())tailY=noteY((bms::scrollPositionSeconds(chart,note.timeSeconds+note.lengthSeconds)-scrollPosition)*pixelsPerSecond);
        const double drawTop=note.longNote()?std::min(headY,tailY):headY;
        const double drawBottom=note.longNote()?std::max(headY+noteHeight,tailY):headY+noteHeight;
        if(drawBottom<top || drawTop>fieldBottom)return false;
        if(visibility<=0)return true;
        COLORREF color = gPalette[gLaneColors[static_cast<size_t>(note.lane)%8]].color;
        if(gChartOptions.crazy.contains(L"star"))color=RGB(static_cast<int>((std::sin(gPlayhead*12)+1)*127),static_cast<int>((std::sin(gPlayhead*12+2)+1)*127),static_cast<int>((std::sin(gPlayhead*12+4)+1)*127));
        if(gChartOptions.crazy.contains(L"clone") || gChartOptions.crazy.contains(L"clone2") || gChartOptions.crazy.contains(L"s-clone")) {
            const auto shadow=RGB(GetRValue(color)/3,GetGValue(color)/3,GetBValue(color)/3);
            const int copies=gChartOptions.crazy.contains(L"s-clone")?6:gChartOptions.crazy.contains(L"clone2")?3:1;
            for(int copy=0;copy<copies;++copy){const int dx=copy==0?0:(copy%2?1:-1)*(right-x)/2;const double y=headY-(reverse?-1:1)*(24+copy*12);fillGameRectSubpixel(dc,x+dx,y,right+dx,y+noteHeight,shadow,noteBounds);}
        }
        if(gChartOptions.crazy.contains(L"dark2") || visibility<1)color=RGB(GetRValue(color)*visibility*(gChartOptions.crazy.contains(L"dark2")?.5:1),GetGValue(color)*visibility*(gChartOptions.crazy.contains(L"dark2")?.5:1),GetBValue(color)*visibility*(gChartOptions.crazy.contains(L"dark2")?.5:1));
        if(note.longNote()) {
            fillGameRectSubpixel(dc, x, drawTop, right, drawBottom, RGB(GetRValue(color) / 2, GetGValue(color) / 2, GetBValue(color) / 2), noteBounds);
            fillGameRectSubpixel(dc, x, headY, right, headY + noteHeight, color, noteBounds);
        } else if(gChartOptions.crazy.contains(L"spin")) {
            const auto& rotated=noteEffects::rotated(right-x,noteHeight,gPlayhead,color);
            const int left=x+(right-x-rotated.size)/2,topY=static_cast<int>(std::lround(headY))+(noteHeight-rotated.size)/2;
            if(gGpuFrame)gCompositionPresenter.sprite(rotated.pixels.data(),rotated.size,rotated.size,left,topY,rotated.size,rotated.size,rotated.id,1,true,&noteBounds);
            else if(directRaster)noteEffects::draw(static_cast<uint32_t*>(gBackBufferPixels),gBackBufferWidth,gBackBufferHeight,rotated,left,topY,noteBounds);
        } else fillGameRectSubpixel(dc,x,headY,right,headY+noteHeight,color,noteBounds);
        return true;
    };
    for (auto it = firstNote; it != chart.notes.end(); ++it) {
        const double notePosition = bms::scrollPositionSeconds(chart, it->timeSeconds);
        if (!fullScan && notePosition > lastVisiblePosition) break;
        const size_t i = static_cast<size_t>(it - chart.notes.begin());
        if (gHit[i]) continue;
        ++visibleNotes;
        drawNote(i);
    }
    // Missed or released long notes remain visible until their body has scrolled
    // out of the playfield; their judgement and score are already finalized.
    for(auto it=gLingeringLongNotes.begin();it!=gLingeringLongNotes.end();) {
        if(*it>=chart.notes.size() || !drawNote(*it))it=gLingeringLongNotes.erase(it);
        else {++visibleNotes;++it;}
    }
    for(size_t i=0;i<chart.mines.size();++i) {
        const auto& mine=chart.mines[i];if(i<gMineHit.size() && gMineHit[i])continue;
        const double y=noteY((bms::scrollPositionSeconds(chart,mine.timeSeconds)-scrollPosition)*pixelsPerSecond);
        if(y+gNoteHeight<top || y>fieldBottom)continue;
        fillGameRectSubpixel(dc,layout.left(mine.lane),y,layout.right(mine.lane),y+gNoteHeight,RGB(255,48,96),noteBounds);
    }
    // Held notes must be drawn independently of the start-time visibility range:
    // their head can be many seconds in the past while the tail is still above us.
    for (size_t lane = 0; lane < gHolds.size(); ++lane) {
        const auto& hold = gHolds[lane];
        if (!hold.active()) continue;
        const auto& note = chart.notes[hold.noteIndex()];
        const int x = layout.left(note.lane);
        const int right = layout.right(note.lane);
        const double tailY=std::clamp(noteY((bms::scrollPositionSeconds(chart,note.timeSeconds+note.lengthSeconds)-scrollPosition)*pixelsPerSecond),static_cast<double>(reverse?judgeY:top),static_cast<double>(reverse?fieldBottom:judgeY));
        const COLORREF color = gPalette[gLaneColors[lane%8]].color;
        fillGameRectSubpixel(dc, x, std::min(tailY,double(judgeY)), right, std::max(tailY,double(judgeY)), RGB(GetRValue(color) / 2, GetGValue(color) / 2, GetBValue(color) / 2), noteBounds);
        RECT head{ x, judgeY, right, judgeY + gNoteHeight };
        fillGameRect(dc, head, color, noteBounds);
    }
    if (noteClip) RestoreDC(dc, noteClip);
    gGpuNotePass = false;
    BMS_PERF_SET(visibleNotes, visibleNotes);
    }
    {
    BMS_PERF_SCOPE(judgementMs);
    const int coverPercent=std::max(gLaneCoverPercent,gChartOptions.hidden==L"m-sudden"?gAuthoredCoverPercent:0);
    if (coverPercent > 0) {
        const int coverBottom = judgeY * coverPercent / 100;
        const RECT coverRect{ fieldX, 0, fieldX + layout.totalWidth(), coverBottom };
        fillGameRect(dc, coverRect, RGB(12, 12, 12), client);
        if (gGpuFrame) gCompositionPresenter.rectangle(static_cast<float>(coverRect.left), 0,
            static_cast<float>(coverRect.right), static_cast<float>(coverRect.bottom), RGB(12, 12, 12));
    }
    gScratchDisc.update(gPlayhead, gPressed[0]);
    if (directRaster && !gChartOptions.crazy.contains(L"dark2")) {
        BMS_PERF_SCOPE(deckMs);HUD_SCOPE(LegacyDeck);
        if((chart.mode!=bms::ChartMode::SevenKey && chart.mode!=bms::ChartMode::Unknown)) {
            for(int lane=0;lane<layout.laneCount();++lane) {
                if(layout.width(lane)<=0)continue;
                RECT key{layout.left(lane)+2,judgeY+8,layout.right(lane)-2,std::min(gWindowHeight,judgeY+75)};
                fillRect(dc,key,gPressed[lane]?RGB(56,189,248):(lane%2?RGB(220,220,220):RGB(70,70,70)));
            }
        } else if (hybridRendererEnabled && gGpuFrame)
            KeyDeck::drawCached(static_cast<uint32_t*>(gBackBufferPixels), gBackBufferWidth, gBackBufferHeight, fieldX, laneWidth, judgeY, primaryPressed(), gScratchDisc.angle(), scratchWidth, &gTurntableImage, &gBlackKeyImage, &gWhiteKeyImage);
        else KeyDeck::draw(static_cast<uint32_t*>(gBackBufferPixels), gBackBufferWidth, gBackBufferHeight, fieldX, laneWidth, judgeY, primaryPressed(), gScratchDisc.angle(), scratchWidth, &gTurntableImage, &gBlackKeyImage, &gWhiteKeyImage);
    }
    {
    BMS_PERF_SCOPE(feedbackMs);
    if (gGpuFrame) {
        const int left = std::max(0, fieldX), deckTop = std::clamp(judgeY + 4, 0, gWindowHeight);
        GdiFlush();
        if (!hybridRendererEnabled || (chart.mode!=bms::ChartMode::SevenKey && chart.mode!=bms::ChartMode::Unknown)) gCompositionPresenter.deck(static_cast<const uint32_t*>(gBackBufferPixels) + static_cast<size_t>(deckTop) * gBackBufferWidth + left,
            std::max(0, std::min(gWindowWidth, fieldX + gearWidth) - left), std::min(108, gWindowHeight - deckTop), gBackBufferWidth, left, deckTop);
        gFeedback.drawSprites(fieldX, laneWidth, judgeY, gPlayhead, scratchWidth,
            [](const uint32_t* pixels, int width, int height, int left, int top, int dw, int dh, uint64_t id, float opacity) {
                gCompositionPresenter.sprite(pixels, width, height, left, top, dw, dh, id, opacity, false);
            });
    } else if (directRaster) gFeedback.draw(static_cast<uint32_t*>(gBackBufferPixels), gBackBufferWidth, gBackBufferHeight, fieldX, laneWidth, judgeY, gPlayhead, scratchWidth);
    }
    if (keyGuideVisible((gPlayhead - (gPractice.enabled ? gPractice.startSeconds : 0)) / playbackTimingScale())) {
        for (int lane = 0; lane < layout.laneCount(); ++lane) {
            if(layout.width(lane)==0)continue;
            const std::wstring label = keyName(laneKey(lane));
            RECT keyLabel{ layout.left(lane) + 3, judgeY - 30, layout.right(lane) - 3, judgeY - 6 };
            if (gGpuFrame) {
                gTextRaster.draw(nullptr, gBackBufferWidth, gBackBufferHeight,
                    label, keyLabel, gSmallFont, RGB(235, 235, 235), 1.0, drawTextBody, false, queueGpuText);
                continue;
            }
            SetTextColor(dc, RGB(235, 235, 235)); SetBkMode(dc, TRANSPARENT);
            HGDIOBJ oldFont = SelectObject(dc, gSmallFont);
            DrawTextW(dc, label.c_str(), static_cast<int>(label.size()), &keyLabel, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(dc, oldFont);
        }
    }
    const int gaugeX = fieldX + 2;
    const int gaugeY = normalJudgeY + 121;
    const int gaugeWidth = std::max(1, layout.totalWidth() - 4);
    constexpr int gaugeHeight = 28;
    const RECT gaugeBackground{ gaugeX, gaugeY, gaugeX + gaugeWidth, gaugeY + gaugeHeight };
    fillRect(dc, gaugeBackground, RGB(25, 32, 42));
    const RECT gaugeInterior{ gaugeX + 2, gaugeY + 2, gaugeX + gaugeWidth - 2, gaugeY + gaugeHeight - 2 };
    fillRect(dc, gaugeInterior, RGB(43, 54, 65));
    const int gaugeFillWidth = static_cast<int>(std::lround((gaugeWidth - 4) * gGrooveGauge.value() / 100.0));
    if (gaugeFillWidth > 0) {
        const RECT gaugeFill{ gaugeInterior.left, gaugeInterior.top, gaugeInterior.left + gaugeFillWidth, gaugeInterior.bottom };
        fillRect(dc, gaugeFill, RGB(255, 255, 255));
        const RECT gaugeHighlight{ gaugeFill.left, gaugeFill.top, gaugeFill.right, gaugeFill.top + 2 };
        fillRect(dc, gaugeHighlight, RGB(255, 255, 255));
    }
    frameRect(dc, gaugeBackground, RGB(157, 178, 193));

    constexpr int iconSize = 52, iconGap = 7, iconCount = 4;
    constexpr int iconRowWidth = iconSize * iconCount + iconGap * (iconCount - 1);
    const int iconLeft = fieldX + (layout.totalWidth() - iconRowWidth) / 2;
    const int iconTop = gaugeY + gaugeHeight + 8;
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
            RECT valueRect{ x + 2, iconTop + 12, x + iconSize - 2, iconTop + iconSize - 2 };
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
        RECT judgement{fieldX, top + 120 + gJudgementTextYOffset, fieldX + layout.totalWidth() - 2, top + 220 + gJudgementTextYOffset};
        drawAnimatedCenteredText(dc, combinedJudgementText(gJudgementText, gFeedback.combo()), judgement,
            gJudgementFont ? gJudgementFont : gFont, gJudgementColor, gJudgementPop.scale(), gJudgementRainbow);
    }
    const int infoLine = 16;
    const int infoTop = std::max(judgeY + 116, gWindowHeight - 64);
    const COLORREF infoColor = RGB(255, 255, 255);
    const auto chartText=bms::textAt(chart,gPlayhead);
    if(!chartText.empty())drawText(dc,chartText,26,infoTop-32,RGB(255,255,255),gFont);
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
    drawText(dc,diagnosticFps,26,infoTop,infoColor,gSmallFont);
#else
    const auto fpsText = hybridRendererEnabled && gGpuFrame ? L"FPS  " + std::to_wstring(gMeasuredFps) + L" / OUTPUT  " + std::to_wstring(gCompositionPresenter.presentationFps()) :
        L"FPS  " + std::to_wstring(gMeasuredFps) + L" / " + (gTargetFps == vsyncFps ? std::wstring(L"VSYNC") :
            gTargetFps == 0 ? std::wstring(L"UNLIMITED (MAX 1500)") : std::to_wstring(gTargetFps));
    drawText(dc, fpsText, 26, infoTop, infoColor, gSmallFont);
#endif
    drawText(dc, L"BPM  " + number(tempo ? tempo->bpm : chart.initialBpm, 1), 26, infoTop + infoLine, infoColor, gSmallFont);
    drawText(dc, L"NOTE SPEED  " + number(gNoteSpeed, 2) + L"x", 26, infoTop + infoLine * 2, infoColor, gSmallFont);
    }
    }
    }
    drawPracticeHud(dc);
#ifdef BMS_ENABLE_SKIN_BINDINGS
    drawAppliedSkinDc(dc);
#endif
    if(!gGpuFrame&&!gPaused&&gTimingHint&&std::chrono::steady_clock::now()<gJudgementUntil){const auto& hint=gTimingHint<0?gFastHintImage:gSlowHintImage;if(hint.loaded()&&gBackBufferPixels){GdiFlush();hint.draw(static_cast<uint32_t*>(gBackBufferPixels),gBackBufferWidth,gBackBufferHeight,int(gTimingHintX),int(gTimingHintY),0, int(90*gTimingHintScale));}}
    if (gPaused) drawPauseOverlay(dc);
}

#include "PlayPreviewRender.inl"

void ensureSettingsPreviewBuffer(HDC referenceDc, int width, int height) {
    if (!gSettingsPreviewDc) gSettingsPreviewDc = CreateCompatibleDC(referenceDc);
    if (!gSettingsPreviewDc || width <= 0 || height <= 0) return;
    if (gSettingsPreviewBitmap && width == gSettingsPreviewWidth && height == gSettingsPreviewHeight) return;
    if (gSettingsPreviewBitmap) {
        SelectObject(gSettingsPreviewDc, gSettingsPreviewOriginalBitmap);
        DeleteObject(gSettingsPreviewBitmap);
        gSettingsPreviewBitmap = nullptr;
        gSettingsPreviewPixels = nullptr;
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    gSettingsPreviewBitmap = CreateDIBSection(referenceDc, &info, DIB_RGB_COLORS, &gSettingsPreviewPixels, nullptr, 0);
    if (!gSettingsPreviewBitmap) return;
    gSettingsPreviewOriginalBitmap = SelectObject(gSettingsPreviewDc, gSettingsPreviewBitmap);
    gSettingsPreviewWidth = width;
    gSettingsPreviewHeight = height;
}

void drawSettingsGameplayPreview(HDC dc, uint32_t* pixels, int width, int height) {
    fillRect(dc, {0, 0, width, height}, RGB(0, 0, 0));

    const int laneWidth = LaneLayout::defaultKeyWidth;
    const int scratchWidth = LaneLayout::defaultScratchWidth;
    const LaneLayout gear{0, laneWidth, scratchWidth};
    const int gearWidth = gear.totalWidth();
    const int edgeInset = std::min(64, std::max(0, (width - gearWidth) / 2));
    const int available = std::max(0, width - gearWidth);
    const int fieldX = gGearPosition == 0 ? edgeInset :
        gGearPosition == 2 ? std::max(edgeInset, available - edgeInset) : available / 2;
    const LaneLayout layout{fieldX, laneWidth, scratchWidth};
    const int judgeY = judgementLineY(height);

    for (size_t i = 0; i < gLaneSeparators.size(); ++i) {
        if (!gLaneSeparators[i]) continue;
        const int x = layout.left(static_cast<int>(i + 1));
        drawLaneSeparator(dc,{x,0,x+1,height},judgeY);
    }
    if (gLaneCoverPercent > 0) {
        const int coverBottom = judgeY * gLaneCoverPercent / 100;
        fillRect(dc, {fieldX, 0, fieldX + gearWidth, coverBottom}, RGB(12, 12, 12));
    }
    fillRect(dc, {fieldX, judgeY, fieldX + gearWidth, judgeY + 3}, RGB(248, 250, 252));

    if (pixels) {
        GdiFlush();
        const std::array<bool, 8> idleKeys{};
        KeyDeck::draw(pixels, width, height,
            fieldX, laneWidth, judgeY, idleKeys, 0.0, scratchWidth,
            &gTurntableImage, &gBlackKeyImage, &gWhiteKeyImage);
        GdiFlush();
    } else {
        const int panelTop = judgeY + 4;
        RECT deck{fieldX, panelTop, fieldX + gearWidth, std::min(height, panelTop + 108)};
        fillRect(dc, deck, RGB(0, 0, 0));
        for (int lane = 1; lane < 8; ++lane) {
            const int inset = lane % 2 == 1 ? 4 : 7;
            RECT key{layout.left(lane) + inset, panelTop + 7,
                layout.right(lane) - inset, std::min(height, panelTop + 70)};
            fillRect(dc, key, lane % 2 == 1 ? RGB(238, 238, 238) : RGB(42, 42, 42));
            frameRect(dc, key, lane % 2 == 1 ? RGB(120, 120, 120) : RGB(8, 8, 8));
        }
        const int centerX = fieldX + scratchWidth / 2;
        const int centerY = panelTop + 42;
        HBRUSH rim = CreateSolidBrush(RGB(160, 169, 181));
        HBRUSH disc = CreateSolidBrush(RGB(23, 28, 37));
        HGDIOBJ oldBrush = SelectObject(dc, rim);
        Ellipse(dc, centerX - 44, centerY - 44, centerX + 44, centerY + 44);
        SelectObject(dc, disc);
        Ellipse(dc, centerX - 37, centerY - 37, centerX + 37, centerY + 37);
        SelectObject(dc, oldBrush);
        DeleteObject(rim); DeleteObject(disc);
    }

    RECT judgement{fieldX, PLAYFIELD_TOP + 120 + gJudgementTextYOffset,
        fieldX + gearWidth - 2, PLAYFIELD_TOP + 220 + gJudgementTextYOffset};
    drawAnimatedCenteredText(dc, L"GREAT 123", judgement, gJudgementFont ? gJudgementFont : gFont,
        RGB(255, 214, 20), 1.0, true);
}

void showSettingsPreviewOverlay() {
    if (!gWindow || !gSettingsWindow || !gSettingsPreviewOverlay) return;
    RECT client{};
    GetClientRect(gWindow, &client);
    POINT origin{};
    ClientToScreen(gWindow, &origin);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) return;
    SetWindowPos(gSettingsPreviewOverlay, HWND_TOPMOST, origin.x, origin.y, width, height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    gSettingsPreviewShowing = true;
    InvalidateRect(gSettingsPreviewOverlay, nullptr, FALSE);
    ShowWindow(gSettingsPreviewOverlay, SW_SHOWNOACTIVATE);
    UpdateWindow(gSettingsPreviewOverlay);
}

void hideSettingsPreviewOverlay() {
    if (!gSettingsPreviewShowing) return;
    gSettingsPreviewShowing = false;
    if (gSettingsPreviewOverlay) ShowWindow(gSettingsPreviewOverlay, SW_HIDE);
}

void releaseSettingsPreviewResources() {
    hideSettingsPreviewOverlay();
    if(gSettingsPreviewOverlay) DestroyWindow(gSettingsPreviewOverlay);
    gSettingsPreviewOverlay=nullptr;
    if(gSettingsPreviewDc && gSettingsPreviewOriginalBitmap) SelectObject(gSettingsPreviewDc,gSettingsPreviewOriginalBitmap);
    if(gSettingsPreviewBitmap) DeleteObject(gSettingsPreviewBitmap);
    if(gSettingsPreviewDc) DeleteDC(gSettingsPreviewDc);
    gSettingsPreviewDc=nullptr;gSettingsPreviewBitmap=nullptr;gSettingsPreviewOriginalBitmap=nullptr;
    gSettingsPreviewPixels=nullptr;gSettingsPreviewWidth=gSettingsPreviewHeight=0;
}

LRESULT CALLBACK settingsPreviewOverlayProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(window, &ps);
        RECT client{};
        GetClientRect(window, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        ensureSettingsPreviewBuffer(dc, width, height);
        if (gSettingsPreviewDc && gSettingsPreviewBitmap && gSettingsPreviewPixels) {
            drawSettingsGameplayPreview(gSettingsPreviewDc, static_cast<uint32_t*>(gSettingsPreviewPixels), width, height);
            GdiFlush();
            BitBlt(dc, 0, 0, width, height, gSettingsPreviewDc, 0, 0, SRCCOPY);
        } else {
            drawSettingsGameplayPreview(dc, nullptr, width, height);
        }
        EndPaint(window, &ps);
        return 0;
    }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK settingsPreviewTrackbarSubclass(HWND control, UINT message, WPARAM wParam, LPARAM lParam,
    UINT_PTR id, DWORD_PTR ownerData) {
    if (message == WM_LBUTTONDOWN) {
        const LRESULT result = DefSubclassProc(control, message, wParam, lParam);
        if (GetCapture() == control) showSettingsPreviewOverlay();
        return result;
    }
    if (message == WM_MOUSEMOVE && (wParam & MK_LBUTTON) && gSettingsPreviewShowing) {
        const LRESULT result = DefSubclassProc(control, message, wParam, lParam);
        InvalidateRect(gSettingsPreviewOverlay, nullptr, FALSE);
        UpdateWindow(gSettingsPreviewOverlay);
        return result;
    }
    if (message == WM_LBUTTONUP) {
        const LRESULT result = DefSubclassProc(control, message, wParam, lParam);
        hideSettingsPreviewOverlay();
        return result;
    }
    if (message == WM_CAPTURECHANGED || message == WM_CANCELMODE) hideSettingsPreviewOverlay();
    if (message == WM_NCDESTROY) RemoveWindowSubclass(control, settingsPreviewTrackbarSubclass, id);
    (void)lParam;
    (void)ownerData;
    return DefSubclassProc(control, message, wParam, lParam);
}

void layoutControls() {
    if (!gList) return;
    RECT client{};
    if (GetClientRect(gWindow, &client) && client.right > 0 && client.bottom > 0) {
        gWindowWidth = client.right;
        gWindowHeight = client.bottom;
    }
    const int margin = 24;
    const int top = 104;
    const int totalWidth = gWindowWidth - 2 * margin;
    const int listWidth = gWindowWidth / 3;
    const int rightX = gWindowWidth - listWidth;
    const int leftWidth = rightX - margin - 20;
    const BrowsePanelLayout panel(gWindowWidth, gWindowHeight);
    MoveWindow(gFolder, rightX, gWindowHeight - 46, std::max(1, listWidth - margin), 22, TRUE);
    MoveWindow(gList, rightX, top, listWidth, gWindowHeight - top - 116, TRUE);
    MoveWindow(gInfo, margin, panel.infoY, leftWidth, panel.infoHeight, TRUE);
    MoveWindow(gHiddenSongs, rightX, gWindowHeight - 98, listWidth, 38, TRUE);
    if (gResultNext) MoveWindow(gResultNext, std::max(12, gWindowWidth - 196), std::max(12, gWindowHeight - 82), 164, 52, TRUE);
    const PauseMenuLayout pauseLayout = pauseMenuLayout(gWindowWidth, gWindowHeight);
    for (size_t i = 0; i < gPauseButtons.size(); ++i) {
        const RECT button = pauseLayout.buttons[i];
        if (gPauseButtons[i]) MoveWindow(gPauseButtons[i], button.left, button.top,
            button.right - button.left, button.bottom - button.top, TRUE);
    }
    MoveWindow(gStatus, margin, gWindowHeight - 46, totalWidth, 22, TRUE);
    // Reserve the list view's vertical scrollbar width. If the columns reach
    // past the client area, ListView can horizontally auto-scroll on selection,
    // which makes the custom-drawn row bars appear to wobble sideways.
    const int usable = std::max(1, listWidth - GetSystemMetrics(SM_CXVSCROLL) - 4);
    const int levelWidth = std::min(78, std::max(64, usable / 7));
    ListView_SetColumnWidth(gList, 0, levelWidth);
    ListView_SetColumnWidth(gList, 1, usable - levelWidth);
}

void ensureBackBuffer(HDC referenceDc, int width, int height) {
    if (!gBackBufferDc) gBackBufferDc = CreateCompatibleDC(referenceDc);
    if (!gBackBufferDc || width <= 0 || height <= 0) return;
    if (gBackBufferBitmap && width == gBackBufferWidth && height == gBackBufferHeight) return;
    if (gBackBufferBitmap) {
        SelectObject(gBackBufferDc, gBackBufferOriginalBitmap);
        DeleteObject(gBackBufferBitmap);
        gBackBufferBitmap = nullptr;
        gBackBufferPixels = nullptr;
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height; // Top-down rows make the note rasterizer's coordinates match Win32.
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    gBackBufferBitmap = CreateDIBSection(referenceDc, &info, DIB_RGB_COLORS, &gBackBufferPixels, nullptr, 0);
    if (!gBackBufferBitmap) return;
    gBackBufferOriginalBitmap = SelectObject(gBackBufferDc, gBackBufferBitmap);
    gBackBufferWidth = width;
    gBackBufferHeight = height;
}

void restoreFullscreenDisplay() {
    if (!gFullscreenDevice.empty()) {
        ChangeDisplaySettingsExW(gFullscreenDevice.c_str(), nullptr, nullptr, 0, nullptr);
        gFullscreenDevice.clear();
    }
}

bool getMonitorDisplayInfo(HWND window, MONITORINFOEXW& monitor) {
    monitor = {};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) return false;
    if (gMonitorDevice != monitor.szDevice || gFullscreenDevice != monitor.szDevice || gMonitorWidth <= 0 || gMonitorHeight <= 0) {
        DEVMODEW current{};
        current.dmSize = sizeof(current);
        if (EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &current)) {
            gMonitorDevice = monitor.szDevice;
            gMonitorWidth = static_cast<int>(current.dmPelsWidth);
            gMonitorHeight = static_cast<int>(current.dmPelsHeight);
        }
    }
    if (gMonitorWidth <= 0 || gMonitorHeight <= 0) {
        gMonitorWidth = monitor.rcMonitor.right - monitor.rcMonitor.left;
        gMonitorHeight = monitor.rcMonitor.bottom - monitor.rcMonitor.top;
    }
    return true;
}

std::vector<DisplayResolution> availableResolutions(HWND window) {
    std::vector<DisplayResolution> result;
    MONITORINFOEXW monitor{};
    if (!getMonitorDisplayInfo(window, monitor)) return result;
    std::set<std::pair<int, int>> supported;
    DEVMODEW mode{};
    for (DWORD index = 0; index < 512; ++index) {
        mode = {};
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsW(monitor.szDevice, index, &mode)) break;
        const int width = static_cast<int>(mode.dmPelsWidth), height = static_cast<int>(mode.dmPelsHeight);
        if (width > 0 && height > 0) supported.emplace(width, height);
    }
    const std::array<DisplayResolution, 13> presets{{
        {1024, 768}, {1280, 720}, {1280, 800}, {1280, 1024}, {1366, 768},
        {1440, 900}, {1600, 900}, {1680, 1050}, {1920, 1080}, {1920, 1200},
        {2560, 1440}, {3440, 1440}, {3840, 2160}
    }};
    for (const auto& preset : presets) {
        if (preset.width <= gMonitorWidth && preset.height <= gMonitorHeight && supported.contains({preset.width, preset.height}))
            result.push_back(preset);
    }
    const std::pair<int, int> native{gMonitorWidth, gMonitorHeight};
    if (std::find_if(result.begin(), result.end(), [&](const auto& item) { return item.width == native.first && item.height == native.second; }) == result.end())
        result.push_back({native.first, native.second});
    if (gResolutionConfigured && gResolutionWidth > 0 && gResolutionHeight > 0 &&
        gResolutionWidth <= gMonitorWidth && gResolutionHeight <= gMonitorHeight && supported.contains({gResolutionWidth, gResolutionHeight}) &&
        std::find_if(result.begin(), result.end(), [&](const auto& item) { return item.width == gResolutionWidth && item.height == gResolutionHeight; }) == result.end())
        result.push_back({gResolutionWidth, gResolutionHeight});
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        const long long areaA = static_cast<long long>(a.width) * a.height;
        const long long areaB = static_cast<long long>(b.width) * b.height;
        return areaA == areaB ? a.width < b.width : areaA < areaB;
    });
    return result;
}

bool findDisplayMode(const wchar_t* device, int width, int height, DEVMODEW& selected) {
    bool found = false;
    for (DWORD index = 0; index < 512; ++index) {
        DEVMODEW candidate{};
        candidate.dmSize = sizeof(candidate);
        if (!EnumDisplaySettingsW(device, index, &candidate)) break;
        if (static_cast<int>(candidate.dmPelsWidth) != width || static_cast<int>(candidate.dmPelsHeight) != height) continue;
        if (!found || candidate.dmDisplayFrequency > selected.dmDisplayFrequency) {
            selected = candidate;
            found = true;
        }
    }
    return found;
}

void applyScreenMode(bool forceResolution = false) {
    if (!gWindow || (!forceResolution && gScreenMode == gAppliedScreenMode)) return;
    if (gAppliedScreenMode == 0) GetWindowPlacement(gWindow, &gWindowedPlacement);
    restoreFullscreenDisplay();
    MONITORINFOEXW monitor{}; monitor.cbSize = sizeof(monitor);
    if (!getMonitorDisplayInfo(gWindow, monitor)) return;
    if (gResolutionConfigured && (gResolutionWidth > gMonitorWidth || gResolutionHeight > gMonitorHeight)) {
        gResolutionConfigured = false;
        gResolutionWidth = gResolutionHeight = 0;
    }
    const int requestedWidth = gResolutionConfigured ? gResolutionWidth : gMonitorWidth;
    const int requestedHeight = gResolutionConfigured ? gResolutionHeight : gMonitorHeight;
    if (gScreenMode == 2) {
        DEVMODEW mode{}; mode.dmSize = sizeof(mode);
        const bool hasMode = gResolutionConfigured
            ? findDisplayMode(monitor.szDevice, requestedWidth, requestedHeight, mode)
            : EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode) != FALSE;
        if (hasMode &&
            ChangeDisplaySettingsExW(monitor.szDevice, &mode, nullptr, CDS_FULLSCREEN, nullptr) == DISP_CHANGE_SUCCESSFUL) gFullscreenDevice = monitor.szDevice;
        else { gScreenMode = 1; setStatus(L"전체화면 모드 전환을 지원하지 않아 보더리스로 표시합니다."); }
    }
    if (gScreenMode == 0) {
        const DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE;
        SetWindowLongPtrW(gWindow, GWL_STYLE, style);
        if (gResolutionConfigured) {
            ShowWindow(gWindow, SW_RESTORE);
            RECT bounds{0, 0, requestedWidth, requestedHeight};
            AdjustWindowRectEx(&bounds, style, FALSE, static_cast<DWORD>(GetWindowLongPtrW(gWindow, GWL_EXSTYLE)));
            const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
            int left = gWindowedPlacement.rcNormalPosition.left;
            int top = gWindowedPlacement.rcNormalPosition.top;
            const RECT work = monitor.rcWork;
            const int workLeft = static_cast<int>(work.left), workTop = static_cast<int>(work.top);
            const int workRight = static_cast<int>(work.right), workBottom = static_cast<int>(work.bottom);
            left = std::clamp(left, workLeft, std::max(workLeft, workRight - width));
            top = std::clamp(top, workTop, std::max(workTop, workBottom - height));
            SetWindowPos(gWindow, nullptr, left, top, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            GetWindowPlacement(gWindow, &gWindowedPlacement);
        } else {
            SetWindowPlacement(gWindow, &gWindowedPlacement);
            SetWindowPos(gWindow, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
    } else {
        SetWindowLongPtrW(gWindow, GWL_STYLE, WS_POPUP | WS_CLIPCHILDREN | WS_VISIBLE);
        // A display mode switch can change the monitor rectangle. Borderless
        // (including fullscreen fallback) must cover the desktop at its current size.
        MONITORINFOEXW current{};
        if (getMonitorDisplayInfo(gWindow, current)) monitor = current;
        const RECT bounds = monitor.rcMonitor;
        SetWindowPos(gWindow, HWND_TOP, bounds.left, bounds.top,
            bounds.right - bounds.left, bounds.bottom - bounds.top,
            SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    gAppliedScreenMode = gScreenMode;
    gBgaBackplateDirty = true;
}

LRESULT CALLBACK settingsBodyProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_COMMAND: case WM_NOTIFY: case WM_HSCROLL: case WM_DRAWITEM: case WM_MEASUREITEM:
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORBTN: case WM_MOUSEWHEEL:
        return SendMessageW(GetParent(window), message, wParam, lParam);
    case WM_ERASEBKGND: {
        RECT client{}; GetClientRect(window, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        return 1;
    }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void scrollSettings(HWND window, int position) {
    if (!window || !gSettingsBody) return;
    RECT client{}; GetClientRect(window, &client);
    const int viewportHeight = std::max(1, static_cast<int>(client.bottom) - 174);
    MoveWindow(gSettingsBody, 0, 96, client.right, viewportHeight, TRUE);
    const int pageHeight = gSettingsCategory == 0 ? SETTINGS_CONTENT_HEIGHT :
        gSettingsCategory == 2 ? 700 : gSettingsCategory == 3 ? 820 : 530;
    const int contentHeight = pageHeight - 96;
    gSettingsScroll = std::clamp(position, 0, std::max(0, contentHeight - viewportHeight));
    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
    info.nMin = 0; info.nMax = contentHeight - 1; info.nPage = viewportHeight; info.nPos = gSettingsScroll;
    SetScrollInfo(window, SB_VERT, &info, TRUE);
    // Absolute coordinates avoid accumulating offsets when switching pages.
    // The child viewport clips content below the fixed navigation and footer.
    HDWP batch = BeginDeferWindowPos(static_cast<int>(gSettingsControlRects.size()));
    for (const auto& [control, bounds] : gSettingsControlRects) {
        if (batch) batch = DeferWindowPos(batch, control, nullptr, bounds.left, bounds.top - gSettingsScroll,
            bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
        else MoveWindow(control, bounds.left, bounds.top - gSettingsScroll, bounds.right - bounds.left, bounds.bottom - bounds.top, TRUE);
    }
    if (batch) EndDeferWindowPos(batch);
    if (gSettingsStatus) MoveWindow(gSettingsStatus, 24, client.bottom - 66, std::max(100, static_cast<int>(client.right) - 230), 48, TRUE);
    HWND close = GetDlgItem(window, ID_CLOSE_SETTINGS);
    if (close) MoveWindow(close, client.right - 194, client.bottom - 54, 170, 40, TRUE);
    RedrawWindow(gSettingsBody, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    InvalidateRect(window, nullptr, TRUE);
}

bool showRmlSettingsPage();
void hideRmlSettingsPage();
void refreshAllNativeSettings();
void selectSettingsCategory(int category) {
    if (gCapturingBindings) cancelKeyBindingCapture();
    gSettingsCategory = std::clamp(category, 0, 6);
    hideSettingsPreviewOverlay();
    // Prepare the migrated page before replacing the visible native surface.
    if (showRmlSettingsPage()) return;
    hideRmlSettingsPage();
    ShowWindow(gSettingsBody, SW_SHOW);
    for (const auto& [control, group] : gSettingsControlCategory)
        ShowWindow(control, (group < 0 || group == gSettingsCategory) &&
            (control != gDirectCompositionChoice || gCompositionEnabled) ? SW_SHOW : SW_HIDE);
    for (size_t i = 0; i < gCategoryButtons.size(); ++i)
        if (gCategoryButtons[i]) {
            SendMessageW(gCategoryButtons[i], BM_SETCHECK, static_cast<int>(i) == gSettingsCategory ? BST_CHECKED : BST_UNCHECKED, 0);
            InvalidateRect(gCategoryButtons[i], nullptr, TRUE);
            UpdateWindow(gCategoryButtons[i]);
        }
    scrollSettings(gSettingsWindow, 0);
    refreshAllNativeSettings();
    if (gSettingsWindow) InvalidateRect(gSettingsWindow, nullptr, TRUE);
}

LRESULT CALLBACK settingsControlSubclass(HWND control, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR ownerData) {
    HWND owner = reinterpret_cast<HWND>(ownerData);
    if (message == WM_MOUSEWHEEL) return SendMessageW(owner, message, wParam, lParam);
    if (message == WM_SETFOCUS) {
        // Hidden controls from another settings page must never change the
        // scroll position of the currently visible page.
        const auto found = gSettingsControlCategory.find(control);
        if (found == gSettingsControlCategory.end() || found->second < 0 ||
            found->second != gSettingsCategory || !IsWindowVisible(control))
            return DefSubclassProc(control, message, wParam, lParam);
        RECT rect{}, client{}; GetWindowRect(control, &rect); MapWindowPoints(nullptr, gSettingsBody, reinterpret_cast<POINT*>(&rect), 2); GetClientRect(gSettingsBody, &client);
        if (rect.top < 0) scrollSettings(owner, gSettingsScroll + rect.top);
        else if (rect.bottom > client.bottom) scrollSettings(owner, gSettingsScroll + rect.bottom - client.bottom);
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(control, settingsControlSubclass, id);
    return DefSubclassProc(control, message, wParam, lParam);
}

HWND settingsControl(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style,
    int id, int x, int y, int width, int height) {
    HWND owner = parent;
    const bool pageControl = gCreatingSettingsCategory >= 0;
    if (pageControl) { parent = gSettingsBody; y -= 96; }
    HWND control = CreateWindowW(type, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(gSmallFont), TRUE);
    SetWindowSubclass(control, settingsControlSubclass, 1, reinterpret_cast<DWORD_PTR>(owner));
    gSettingsControlCategory[control] = gCreatingSettingsCategory;
    if (pageControl) { RECT bounds{}; GetWindowRect(control, &bounds); MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&bounds), 2); gSettingsControlRects[control] = bounds; }
    COMBOBOXINFO combo{sizeof(combo)};
    if (GetComboBoxInfo(control, &combo) && combo.hwndList) SetWindowSubclass(combo.hwndList, settingsControlSubclass, 1, reinterpret_cast<DWORD_PTR>(owner));
    return control;
}

void updateJudgePositionLabel() {
    if (!gJudgeValue) return;
    const std::wstring label = gJudgeLineOffset == 0 ? L"기본 위치" :
        (gJudgeLineOffset < 0 ? L"위로 " + std::to_wstring(-gJudgeLineOffset) : L"아래로 " + std::to_wstring(gJudgeLineOffset)) + L" px";
    setText(gJudgeValue, label);
}

std::wstring hudTextYOffsetLabel(int offset) {
    if (offset == 0) return L"기본 위치";
    return (offset < 0 ? L"위로 " + std::to_wstring(-offset) : L"아래로 " + std::to_wstring(offset)) + L" px";
}

#include "SettingsActions.inl"
#include "SettingsSummaryOverlay.inl"
#include "SettingsRmlPage.inl"

void openRmlSettingsProbe() {
    if (!gRmlSettingsHost.open(gSettingsWindow, executableFolder() / L"Assets/UI/settings-probe.rml"))
        setBindingStatus(gRmlSettingsHost.error());
}

LRESULT CALLBACK settingsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_SIZE:
        hideSettingsPreviewOverlay();
        if (gRmlSettingsPageActive && gRmlSettingsPage.window()) {
            RECT client{}; GetClientRect(window, &client);
            MoveWindow(gRmlSettingsPage.window(), 0, 0, client.right, client.bottom, TRUE);
        } else scrollSettings(window, gSettingsScroll);
        return 0;
    case WM_RML_SETTINGS_NAVIGATE:
        gNativeSettingsFallback = lParam != 0;
        selectSettingsCategory(int(wParam));
        if (!gRmlSettingsPageActive && IsWindowVisible(window) && gCategoryButtons[gSettingsCategory]) SetFocus(gCategoryButtons[gSettingsCategory]);
        return 0;
    case WM_RML_SETTINGS_PICK_IMAGE:
        runSettingsImagePicker([](HWND owner) { chooseCustomBackground(owner); });
        return 0;
    case WM_RML_SETTINGS_ACTION:
        if(wParam==ID_CONTROLLER_SETTINGS){hideSettingsPreviewOverlay();openControllerSettings();syncRmlSettingsState();return 0;}
#ifdef BMS_ENABLE_SKIN_BINDINGS
        if(wParam==ID_SKIN_LOAD||wParam==ID_SKIN_CLEAR) {
            if(gCapturingBindings)cancelKeyBindingCapture();
            hideSettingsPreviewOverlay();runSkinSettingsAction(static_cast<int>(wParam));
            if(gRmlSettingsPageActive){SetFocus(gRmlSettingsPage.window());gRmlSettingsPage.focus(wParam==ID_SKIN_LOAD?"skin-load":"skin-clear");}
            return 0;
        }
#endif
        if (wParam==ID_BIND) { hideSettingsPreviewOverlay(); beginKeyBindingCapture(); }
        else if (wParam==IDCANCEL) { if(gCapturingBindings) cancelKeyBindingCapture(); }
        else if (wParam==ID_RESET_KEYS) {
            if(gCapturingBindings) cancelKeyBindingCapture();
            if(gBindingMode==1)applyLaneKeys(DEFAULT_KEYS);else {bms::KeyProfiles defaults;gKeyProfiles.keys[gBindingMode]=defaults.keys[gBindingMode];gKeyProfiles.save(gSettingsFile,gBindingMode);}setText(gSettingsKeys,keySummary());updateSelection();setBindingStatus(L"기본 키 배치로 복원했습니다.");
        } else if(wParam==1) {
            if(gCapturingBindings) cancelKeyBindingCapture();
            if(gSettingsPreviewShowing) hideSettingsPreviewOverlay();else showSettingsPreviewOverlay();
        } else if(wParam==ID_HELP) {
            if(gCapturingBindings) cancelKeyBindingCapture();
            hideSettingsPreviewOverlay();
            SendMessageW(gWindow,WM_COMMAND,ID_HELP,0);
        }
        syncRmlSettingsState();
        if(!gCapturingBindings && gRmlSettingsPageActive) SetFocus(gRmlSettingsPage.window());
        return 0;
    case WM_RML_SETTINGS_FALLBACK: {
        const auto error = gRmlSettingsPage.error();
        gRmlSettingsPageFailed = true; hideRmlSettingsPage(); gRmlSettingsPage.close();
        selectSettingsCategory(gSettingsCategory);
        if (IsWindowVisible(window) && gCategoryButtons[gSettingsCategory]) SetFocus(gCategoryButtons[gSettingsCategory]);
        setBindingStatus(wParam ? L"기존 설정창을 사용합니다." : L"새 설정 화면에 오류가 있어 기존 설정으로 돌아왔습니다. " + error);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize = {780, 400}; return 0;
    }
    case WM_MOUSEWHEEL: {
        for (HWND combo : {gFpsChoice, gSpeedChoice, gJudgementChoice, gNoteHeightChoice, gGrooveGaugeChoice, gAccuracyLimitChoice, gSummaryAccuracyLimit, gSkinJudgementChoice, gSkinComboChoice, gSoundBufferChoice, gAudioOutputModeChoice, gBgaDarknessChoice, gScreenModeChoice, gResolutionChoice, gGearChoice}) if (combo) SendMessageW(combo, CB_SHOWDROPDOWN, FALSE, 0);
        for (HWND combo : gColorChoices) if (combo) SendMessageW(combo, CB_SHOWDROPDOWN, FALSE, 0);
        scrollSettings(window, gSettingsScroll - GET_WHEEL_DELTA_WPARAM(wParam) * 100 / WHEEL_DELTA);
        return 0;
    }
    case WM_VSCROLL: {
        SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(window, SB_VERT, &info);
        int position = gSettingsScroll;
        switch (LOWORD(wParam)) {
        case SB_LINEUP: position -= 40; break;
        case SB_LINEDOWN: position += 40; break;
        case SB_PAGEUP: position -= static_cast<int>(info.nPage) * 3 / 4; break;
        case SB_PAGEDOWN: position += static_cast<int>(info.nPage) * 3 / 4; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: position = info.nTrackPos; break;
        case SB_TOP: position = 0; break;
        case SB_BOTTOM: position = gSettingsCategory == 0 ? SETTINGS_CONTENT_HEIGHT :
            gSettingsCategory == 2 ? 700 : gSettingsCategory == 3 ? 820 : 530; break;
        }
        scrollSettings(window, position); return 0;
    }
    case WM_CREATE: {
        gSettingsWindow = window;
        gRmlSettingsPageFailed = false; gNativeSettingsFallback = false;
        gSettingsScroll = 0;
        gSettingsCategory = 0;
        gSettingsControlCategory.clear();
        gSettingsControlRects.clear();
        gSettingsBody = CreateWindowExW(WS_EX_CONTROLPARENT, L"BmsPlayerSettingsBody", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 96, 740, 600, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        constexpr std::array<const wchar_t*, 7> categoryNames{L"게임플레이", L"그래픽", L"사운드", L"기타", L"요약", L"스킨", L"키 설정"};
        constexpr std::array<int, 6> categoryOrder{4, 0, 1, 2, 3, 6};
        for (size_t i = 0; i < categoryOrder.size(); ++i) {
            gCreatingSettingsCategory = -1;
            const int category = categoryOrder[i];
            const int x = 24 + static_cast<int>(i) * 118;
            gCategoryButtons[static_cast<size_t>(category)] = settingsControl(window, L"BUTTON", categoryNames[static_cast<size_t>(category)],
                WS_TABSTOP | (i == 0 ? WS_GROUP : 0) | BS_OWNERDRAW,
                ID_CATEGORY_BASE + category, x, 52, 110, 34);
        }
        gCreatingSettingsCategory = 1;
        auto heading = [&](const wchar_t* text, int x, int y, int width) {
            HWND label = settingsControl(window, L"STATIC", text, 0, 0, x, y, width, 30);
            SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(gFont), TRUE);
        };
        auto percentageSetting = [&](const wchar_t* label, int id, int y, int value, HWND& slider, HWND& display) {
            settingsControl(window, L"STATIC", label, 0, id + 200, 24, y, 600, 24);
            slider = settingsControl(window, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ, id, 24, y + 28, 600, 34);
            SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(slider, TBM_SETTICFREQ, 10, 0);
            SendMessageW(slider, TBM_SETPOS, TRUE, value);
            display = settingsControl(window, L"STATIC", (std::to_wstring(value) + L"%").c_str(), 0, 0, 650, y + 32, 70, 24);
        };
        heading(L"화면", 24, 96, 710);
        settingsControl(window, L"STATIC", L"프레임 목표", 0, 0, 24, 140, 250, 24);
        gFpsChoice = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, ID_FPS, 24, 166, 250, 220);
        for (const wchar_t* label : frameRateLabels) SendMessageW(gFpsChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessageW(gFpsChoice, CB_SETCURSEL, frameRateIndex(gTargetFps), 0);
        settingsControl(window, L"STATIC", L"화면 모드", 0, 0, 400, 140, 330, 24);
        gScreenModeChoice = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, ID_SCREEN_MODE, 400, 166, 330, 180);
        for (const wchar_t* label : {L"전체화면", L"보더리스 창화면", L"창화면"}) SendMessageW(gScreenModeChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessageW(gScreenModeChoice, CB_SETCURSEL, 2 - gScreenMode, 0);
        settingsControl(window, L"STATIC", L"해상도", 0, 0, 24, 220, 330, 24);
        gResolutionOptions = availableResolutions(gWindow);
        gResolutionChoice = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, ID_RESOLUTION, 24, 246, 330, 240);
        std::wstring automaticResolution = L"자동 (창 기본 크기 / 모니터 " + std::to_wstring(gMonitorWidth) + L" × " + std::to_wstring(gMonitorHeight) + L")";
        SendMessageW(gResolutionChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(automaticResolution.c_str()));
        for (const auto& resolution : gResolutionOptions) {
            const std::wstring label = std::to_wstring(resolution.width) + L" × " + std::to_wstring(resolution.height);
            SendMessageW(gResolutionChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        int selectedResolution = 0;
        if (gResolutionConfigured) {
            const auto found = std::find_if(gResolutionOptions.begin(), gResolutionOptions.end(), [](const auto& resolution) {
                return resolution.width == gResolutionWidth && resolution.height == gResolutionHeight;
            });
            if (found != gResolutionOptions.end()) selectedResolution = 1 + static_cast<int>(found - gResolutionOptions.begin());
            else { gResolutionConfigured = false; gResolutionWidth = gResolutionHeight = 0; }
        }
        SendMessageW(gResolutionChoice, CB_SETCURSEL, selectedResolution, 0);
        gCompositionChoice = settingsControl(window, L"BUTTON", hybridRendererEnabled ? L"GPU + CPU 사용" : L"GPU 사용", WS_TABSTOP | BS_AUTOCHECKBOX, ID_COMPOSITION, 400, 220, 330, 32);
        SendMessageW(gCompositionChoice, BM_SETCHECK, gCompositionEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        gDirectCompositionChoice = settingsControl(window, L"BUTTON", L"DirectComposition", WS_TABSTOP | BS_AUTOCHECKBOX,
            ID_DIRECT_COMPOSITION, 24, 298, 330, 32);
        SendMessageW(gDirectCompositionChoice, BM_SETCHECK, gDirectCompositionEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        settingsControl(window, L"STATIC", L"GPU 사용 상한", 0, 0, 400, 270, 250, 24);
        gGpuLimitSlider = settingsControl(window, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ, ID_GPU_LIMIT, 400, 298, 270, 38);
        SendMessageW(gGpuLimitSlider, TBM_SETRANGE, TRUE, MAKELPARAM(10, 100));
        SendMessageW(gGpuLimitSlider, TBM_SETTICFREQ, 10, 0);
        SendMessageW(gGpuLimitSlider, TBM_SETPOS, TRUE, gGpuLimitPercent);
        gGpuLimitValue = settingsControl(window, L"STATIC", (std::to_wstring(gGpuLimitPercent) + L"%").c_str(), 0, 0, 680, 306, 48, 24);
        EnableWindow(gGpuLimitSlider, gCompositionEnabled);
        gSubpixelChoice = settingsControl(window, L"BUTTON", L"노트 서브픽셀 렌더링", WS_TABSTOP | BS_AUTOCHECKBOX, ID_NOTE_SUBPIXEL, 24, 360, 330, 32);
        SendMessageW(gSubpixelChoice, BM_SETCHECK, gNoteSubpixel ? BST_CHECKED : BST_UNCHECKED, 0);
        gAntialiasChoice = settingsControl(window, L"BUTTON", L"노트 안티앨리어싱", WS_TABSTOP | BS_AUTOCHECKBOX, ID_NOTE_ANTIALIAS, 400, 360, 330, 32);
        SendMessageW(gAntialiasChoice, BM_SETCHECK, gNoteAntialias ? BST_CHECKED : BST_UNCHECKED, 0);
        settingsControl(window, L"STATIC", L"저지연 기능", 0, 0, 24, 414, 300, 24);
        gLowLatencyChoice = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, ID_LOW_LATENCY, 24, 442, 330, 160);
        const auto reflexCapability = latency::probeReflex();
        const bool reflexAvailable = reflexCapability.supported && gCompositionEnabled &&
            gCompositionPresenter.lowLatency().status(latency::Mode::Reflex) != latency::Status::SessionDisabled;
        for (const wchar_t* name : {L"끔", L"NVIDIA Reflex"}) SendMessageW(gLowLatencyChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
        SendMessageW(gLowLatencyChoice, CB_SETCURSEL, gReflexRequested ? 1 : 0, 0);
        EnableWindow(gLowLatencyChoice, reflexAvailable);
        gCreatingSettingsCategory = 0;
        heading(L"게임플레이", 24, 96, 710);
        settingsControl(window, L"STATIC", L"BGA 밝기", 0, 0, 24, 194, 250, 24);
        gBgaDarknessChoice = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, ID_BGA_DARKNESS, 24, 220, 250, 330);
        for (int brightness = 0; brightness <= 100; brightness += 10) {
            const std::wstring label = brightness == 0 ? L"0% (BGA 끔)" : std::to_wstring(brightness) + L"%";
            SendMessageW(gBgaDarknessChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        SendMessageW(gBgaDarknessChoice, CB_SETCURSEL, gBgaBrightness / 10, 0);
        settingsControl(window, L"STATIC", L"노트 배속", 0, 0, 400, 194, 330, 24);
        gSpeedChoice = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, ID_SPEED, 400, 220, 330, 300);
        for (int steps = 2; steps <= 40; ++steps) {
            const std::wstring label = number(steps / 4.0, 2) + L"x";
            SendMessageW(gSpeedChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        SendMessageW(gSpeedChoice, CB_SETCURSEL, std::clamp(static_cast<int>(std::lround(gNoteSpeed * 4)) - 2, 0, 38), 0);
        settingsControl(window, L"STATIC", L"노트 세로 크기", 0, 0, 24, 272, 330, 24);
        gNoteHeightChoice = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, ID_NOTE_HEIGHT, 24, 298, 250, 220);
        for (int height = 4; height <= 32; height += 2) {
            const std::wstring label = std::to_wstring(height) + L" px";
            SendMessageW(gNoteHeightChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        SendMessageW(gNoteHeightChoice, CB_SETCURSEL, (gNoteHeight - 4) / 2, 0);
        gCreatingSettingsCategory = 5;
        heading(L"콤보 · 판정 텍스트 위치", 24, 270, 710);
        settingsControl(window, L"STATIC", L"판정 / 콤보 텍스트 Y 위치", 0, 0, 24, 312, 600, 24);
        gJudgementTextYSlider = settingsControl(window, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ, ID_JUDGEMENT_TEXT_Y, 24, 340, 600, 42);
        SetWindowSubclass(gJudgementTextYSlider, settingsPreviewTrackbarSubclass, 1, 0);
        SendMessageW(gJudgementTextYSlider, TBM_SETRANGE, TRUE, MAKELPARAM(HUD_TEXT_Y_MIN, HUD_TEXT_Y_MAX));
        SendMessageW(gJudgementTextYSlider, TBM_SETLINESIZE, 0, 1);
        SendMessageW(gJudgementTextYSlider, TBM_SETPAGESIZE, 0, 16);
        SendMessageW(gJudgementTextYSlider, TBM_SETTICFREQ, 32, 0);
        SendMessageW(gJudgementTextYSlider, TBM_SETPOS, TRUE, gJudgementTextYOffset);
        gJudgementTextYValue = settingsControl(window, L"STATIC", hudTextYOffsetLabel(gJudgementTextYOffset).c_str(), 0, 0, 650, 348, 90, 24);
        gCreatingSettingsCategory = 0;
        settingsControl(window, L"STATIC", L"기어 위치", 0, 0, 24, 1390, 300, 24);
        gGearChoice = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, ID_GEAR_POSITION, 24, 1420, 300, 140);
        for (const wchar_t* name : {L"왼쪽", L"가운데", L"오른쪽"}) SendMessageW(gGearChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
        SendMessageW(gGearChoice, CB_SETCURSEL, gGearPosition, 0);
        settingsControl(window, L"STATIC", L"레인 투명도", 0, 0, 24, 330, 300, 24);
        gLaneTransparencySlider = settingsControl(window, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ, ID_LANE_TRANSPARENCY, 24, 360, 270, 42);
        SendMessageW(gLaneTransparencySlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(gLaneTransparencySlider, TBM_SETTICFREQ, 10, 0);
        SendMessageW(gLaneTransparencySlider, TBM_SETPOS, TRUE, gLaneTransparency);
        gLaneTransparencyValue = settingsControl(window, L"STATIC", (std::to_wstring(gLaneTransparency) + L"%").c_str(), 0, 0, 310, 369, 65, 24);
        settingsControl(window, L"STATIC", L"레인 커버", 0, 0, 400, 330, 330, 24);
        gLaneCoverSlider = settingsControl(window, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ, ID_LANE_COVER, 400, 360, 270, 42);
        SetWindowSubclass(gLaneCoverSlider, settingsPreviewTrackbarSubclass, 1, 0);
        SendMessageW(gLaneCoverSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendMessageW(gLaneCoverSlider, TBM_SETTICFREQ, 10, 0);
        SendMessageW(gLaneCoverSlider, TBM_SETPOS, TRUE, gLaneCoverPercent);
        const std::wstring coverLabel = gLaneCoverPercent == 0 ? L"없음" : std::to_wstring(gLaneCoverPercent) + L"%";
        gLaneCoverValue = settingsControl(window, L"STATIC", coverLabel.c_str(), 0, 0, 690, 369, 45, 24);
        heading(L"판정선 위치", 24, 438, 710);
        gJudgePosition = settingsControl(window, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ, ID_JUDGE_POSITION, 24, 480, 330, 44);
        SetWindowSubclass(gJudgePosition, settingsPreviewTrackbarSubclass, 1, 0);
        const int nominal = static_cast<int>(std::lround(gWindowHeight * 2.0 / 3.0));
        const int minimum = 40 - nominal, maximum = std::max(40, gWindowHeight - 112) - nominal;
        gJudgeLineOffset = std::clamp(gJudgeLineOffset, minimum, maximum);
        SendMessageW(gJudgePosition, TBM_SETRANGEMIN, TRUE, minimum);
        SendMessageW(gJudgePosition, TBM_SETRANGEMAX, TRUE, maximum);
        SendMessageW(gJudgePosition, TBM_SETLINESIZE, 0, 1);
        SendMessageW(gJudgePosition, TBM_SETPAGESIZE, 0, 10);
        SendMessageW(gJudgePosition, TBM_SETTICFREQ, 50, 0);
        SendMessageW(gJudgePosition, TBM_SETPOS, TRUE, gJudgeLineOffset);
        gJudgeValue = settingsControl(window, L"STATIC", L"", 0, 0, 370, 492, 150, 28);
        updateJudgePositionLabel();
        settingsControl(window, L"STATIC", L"그루브 게이지", 0, 0, 400, 522, 130, 24);
        gGrooveGaugeChoice = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, ID_GROOVE_GAUGE, 400, 548, 130, 150);
        for (const wchar_t* label : {L"NORMAL", L"EASY", L"HARD"}) SendMessageW(gGrooveGaugeChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessageW(gGrooveGaugeChoice, CB_SETCURSEL, static_cast<int>(gGrooveGaugeMode), 0);
        settingsControl(window, L"STATIC", L"NORMAL/EASY: 시작 20%, 종료 80% 이상\r\nHARD: 시작 100%, 게이지 0%에서 실패", 0, 0, 400, 580, 330, 38);
        settingsControl(window,L"STATIC",L"정확도 제한",0,0,400,630,300,24);
        gAccuracyLimitChoice=settingsControl(window,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,ID_ACCURACY_LIMIT,400,658,300,180);
        for(const auto* label:accuracyLimitLabels)SendMessageW(gAccuracyLimitChoice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        SendMessageW(gAccuracyLimitChoice,CB_SETCURSEL,static_cast<int>(gAccuracyLimit),0);
        settingsControl(window,L"STATIC",L"기준 미만 판정 시 자동 재시작 · F5: 재시작",0,0,400,698,330,38);
        settingsControl(window,L"STATIC",L"FAST / SLOW 표시",0,0,24,630,300,24);
        gTimingHintChoice=settingsControl(window,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,ID_TIMING_HINT,24,658,300,180);
        for(const auto* label:{L"PGREAT 미만",L"GREAT 미만",L"GOOD 미만",L"없음"})SendMessageW(gTimingHintChoice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        SendMessageW(gTimingHintChoice,CB_SETCURSEL,gTimingHintCondition,0);
        gCreatingSettingsCategory=6;
        heading(L"키 배치", 24, 96, 710);
        auto keyMode=settingsControl(window,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,1299,24,132,300,160);
        for(auto label:{L"5키",L"7키",L"9키",L"10키",L"14키"})SendMessageW(keyMode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendMessageW(keyMode,CB_SETCURSEL,gBindingMode,0);
        gBind = settingsControl(window, L"BUTTON", L"키 배치 변경", WS_TABSTOP | BS_PUSHBUTTON, ID_BIND, 24, 190, 160, 40);
        settingsControl(window, L"BUTTON", L"기본 키로 복원", WS_TABSTOP | BS_PUSHBUTTON, ID_RESET_KEYS, 208, 190, 160, 40);
        gHelp = settingsControl(window, L"BUTTON", L"사용법", WS_TABSTOP | BS_PUSHBUTTON, ID_HELP, 392, 190, 130, 40);
        gSettingsKeys = settingsControl(window, L"STATIC", keySummary().c_str(), 0, ID_SETTINGS_KEYS, 24, 245, 706, 48);
        gCreatingSettingsCategory=0;
        heading(L"레인별 노트 색상", 24, 940, 490);
        heading(L"구분선", 570, 940, 160);
        for (int lane = 0; lane < 8; ++lane) {
            const int y = 992 + lane * 44;
            const std::wstring label = lane == 0 ? L"스크래치" : std::to_wstring(lane) + L"번 건반";
            settingsControl(window, L"STATIC", label.c_str(), 0, 0, 32, y + 5, 120, 26);
            HWND combo = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL, ID_COLOR_BASE + lane, 174, y, 320, 330);
            gColorChoices[static_cast<size_t>(lane)] = combo;
            SendMessageW(combo, CB_SETITEMHEIGHT, 0, 24);
            SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), 24);
            for (const auto& color : gPalette) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(color.name));
            SendMessageW(combo, CB_SETCURSEL, gLaneColors[static_cast<size_t>(lane)], 0);
        }
        for (size_t i = 0; i < gSeparatorButtons.size(); ++i) {
            const std::wstring label = i == 0 ? L"SCR | 1" : std::to_wstring(i) + L" | " + std::to_wstring(i + 1);
            HWND button = settingsControl(window, L"BUTTON", label.c_str(), WS_TABSTOP | BS_AUTOCHECKBOX | BS_PUSHLIKE, ID_SEPARATOR_BASE + static_cast<int>(i), 570, 992 + static_cast<int>(i) * 44, 150, 28);
            gSeparatorButtons[i] = button;
            SendMessageW(button, BM_SETCHECK, gLaneSeparators[i] ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        gCreatingSettingsCategory=0;
        settingsControl(window,L"BUTTON",L"컨트롤러 입력 설정...",WS_TABSTOP|BS_PUSHBUTTON,ID_CONTROLLER_SETTINGS,24,1490,320,36);
#ifdef BMS_ENABLE_SKIN_BINDINGS
        gCreatingSettingsCategory = 5;
        heading(L"외부 스킨",24,96,700);
        gSkinNativeStatus=settingsControl(window,L"STATIC",skinSummary().c_str(),0,0,24,136,700,55);
        settingsControl(window,L"BUTTON",L"스킨 불러오기...",WS_TABSTOP|BS_PUSHBUTTON,ID_SKIN_LOAD,24,200,220,36);
        settingsControl(window,L"BUTTON",L"기본 화면으로 복원",WS_TABSTOP|BS_PUSHBUTTON,ID_SKIN_CLEAR,265,200,220,36);
        for(int i=0;i<2;++i){settingsControl(window,L"STATIC",i?L"콤보 렌더링":L"판정 렌더링",0,0,24,430+i*85,220,24);
            HWND choice=settingsControl(window,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,i?ID_SKIN_COMBO_MODE:ID_SKIN_JUDGEMENT_MODE,260,430+i*85,260,150);
            for(const auto* label:{L"스킨 설정",L"폰트",L"이미지"})SendMessageW(choice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
            SendMessageW(choice,CB_SETCURSEL,(i?gSkinComboMode:gSkinJudgementMode)+1,0);(i?gSkinComboChoice:gSkinJudgementChoice)=choice;}

#endif
        gCreatingSettingsCategory = 2;
        heading(L"볼륨", 24, 96, 710);
        constexpr std::array<const wchar_t*, 4> volumeLabels{L"마스터 볼륨", L"노래 볼륨", L"키음 볼륨", L"효과음 볼륨"};
        for (size_t i = 0; i < volumeLabels.size(); ++i) {
            const int y = 142 + static_cast<int>(i) * 64;
            settingsControl(window, L"STATIC", volumeLabels[i], 0, 0, 24, y, 320, 24);
            gVolumeSliders[i] = settingsControl(window, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ, ID_VOLUME_BASE + static_cast<int>(i), 24, y + 26, 600, 34);
            SendMessageW(gVolumeSliders[i], TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendMessageW(gVolumeSliders[i], TBM_SETTICFREQ, 10, 0);
            SendMessageW(gVolumeSliders[i], TBM_SETPOS, TRUE, gVolumes[i]);
            const std::wstring value = std::to_wstring(gVolumes[i]) + L"%";
            gVolumeValues[i] = settingsControl(window, L"STATIC", value.c_str(), 0, 0, 650, y + 30, 70, 24);
        }
        settingsControl(window, L"STATIC", L"사운드 버퍼 (샘플 프레임)", 0, 0, 24, 420, 330, 24);
        gSoundBufferChoice = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, ID_SOUND_BUFFER, 24, 446, 250, 210);
        for (int frames : SOUND_BUFFER_OPTIONS) {
            const std::wstring label = std::to_wstring(frames);
            SendMessageW(gSoundBufferChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        const auto bufferOption = std::find(SOUND_BUFFER_OPTIONS.begin(), SOUND_BUFFER_OPTIONS.end(), gSoundBufferFrames);
        SendMessageW(gSoundBufferChoice, CB_SETCURSEL, bufferOption - SOUND_BUFFER_OPTIONS.begin(), 0);
        settingsControl(window, L"STATIC", L"게임 플레이 WASAPI 출력 모드", 0, 0, 340, 420, 360, 24);
        gAudioOutputModeChoice = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST,
            ID_AUDIO_OUTPUT_MODE, 340, 446, 380, 210);
        SendMessageW(gAudioOutputModeChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"공유 모드 (다른 앱과 함께 재생)"));
        SendMessageW(gAudioOutputModeChoice, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"독점 모드 (게임 중 다른 앱 소리 중단 가능)"));
        SendMessageW(gAudioOutputModeChoice, CB_SETCURSEL, gAudioOutputMode, 0);
        settingsControl(window, L"STATIC", L"독점 모드는 게임 중 지연 감소를 시도합니다. 사용할 수 없으면 공유 모드로 전환합니다.",
            0, 0, 340, 478, 380, 32);
        gSongPreviewChoice = settingsControl(window, L"BUTTON", L"곡 하이라이트 부분 미리보기", WS_TABSTOP | BS_AUTOCHECKBOX, ID_SONG_PREVIEW, 24, 510, 600, 32);
        SendMessageW(gSongPreviewChoice, BM_SETCHECK, gSongPreviewEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        percentageSetting(L"미리보기 음악 볼륨", ID_PREVIEW_VOLUME, 570, gPreviewVolume, gPreviewVolumeSlider, gPreviewVolumeValue);
        gCreatingSettingsCategory = 3;
        heading(L"기타", 24, 96, 710);
        settingsControl(window, L"BUTTON", L"배경 이미지 불러오기", WS_TABSTOP | BS_PUSHBUTTON, ID_CUSTOM_BACKGROUND, 24, 224, 240, 40);
        gCustomBackgroundLabel = settingsControl(window, L"STATIC", gCustomBackgroundName.c_str(), 0, 0, 24, 274, 706, 28);
        percentageSetting(L"배경 블러 강도", ID_BACKGROUND_BLUR, 326, gBackgroundBlur, gBackgroundBlurSlider, gBackgroundBlurValue);
        percentageSetting(L"배경 불투명도", ID_BACKGROUND_OPACITY, 424, gBackgroundOpacity, gBackgroundOpacitySlider, gBackgroundOpacityValue);
        heading(L"시스템 자원 사용량", 24, 520, 710);
        gResourceUsageChoice = settingsControl(window, L"BUTTON", L"시스템 자원 사용량 보기", WS_TABSTOP | BS_AUTOCHECKBOX,
            ID_RESOURCE_USAGE, 24, 556, 420, 32);
        SendMessageW(gResourceUsageChoice, BM_SETCHECK, gShowResourceUsage ? BST_CHECKED : BST_UNCHECKED, 0);
        gCpuPerformanceChoice = settingsControl(window, L"BUTTON", L"CPU 성능 우선 모드", WS_TABSTOP | BS_AUTOCHECKBOX,
            ID_CPU_PERFORMANCE, 450, 556, 280, 32);
        SendMessageW(gCpuPerformanceChoice, BM_SETCHECK, gCpuPerformanceMode ? BST_CHECKED : BST_UNCHECKED, 0);
        settingsControl(window, L"STATIC", L"CPU 사용 상한", 0, 0, 24, 598, 600, 24);
        gCpuLimitSlider = settingsControl(window, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ, ID_CPU_LIMIT, 24, 626, 600, 36);
        SendMessageW(gCpuLimitSlider, TBM_SETRANGE, TRUE, MAKELPARAM(10, 100));
        SendMessageW(gCpuLimitSlider, TBM_SETTICFREQ, 10, 0);
        SendMessageW(gCpuLimitSlider, TBM_SETPOS, TRUE, gCpuLimitPercent);
        gCpuLimitValue = settingsControl(window, L"STATIC", (std::to_wstring(gCpuLimitPercent) + L"%").c_str(), 0, 0, 650, 632, 70, 24);
        const uint64_t totalRam = gSystemResources.totalPhysicalMemoryBytes();
        const std::wstring ramLabel = totalRam == 0 ? L"메모리 사용 상한" :
            L"메모리 사용 상한 (총 RAM " + number(static_cast<double>(totalRam) / (1024.0 * 1024.0 * 1024.0), 1) + L" GB)";
        settingsControl(window, L"STATIC", ramLabel.c_str(), 0, 0, 24, 676, 600, 24);
        gMemoryLimitSlider = settingsControl(window, TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ, ID_MEMORY_LIMIT, 24, 704, 560, 36);
        SendMessageW(gMemoryLimitSlider, TBM_SETRANGE, TRUE, MAKELPARAM(10, 100));
        SendMessageW(gMemoryLimitSlider, TBM_SETTICFREQ, 10, 0);
        SendMessageW(gMemoryLimitSlider, TBM_SETPOS, TRUE, gMemoryLimitPercent);
        const double memoryBudgetGb = static_cast<double>(totalRam) * gMemoryLimitPercent / (100.0 * 1024.0 * 1024.0 * 1024.0);
        const std::wstring memoryLimitLabel = std::to_wstring(gMemoryLimitPercent) + L"% (" + number(memoryBudgetGb, 1) + L" GB)";
        gMemoryLimitValue = settingsControl(window, L"STATIC", memoryLimitLabel.c_str(), 0, 0, 592, 710, 136, 24);
        gCreatingSettingsCategory = -1;
        gSettingsStatus = settingsControl(window, L"STATIC", L"", 0, ID_SETTINGS_STATUS, 24, 1210, 706, 40);
        settingsControl(window, L"BUTTON", L"닫기 (Esc)", WS_TABSTOP | BS_DEFPUSHBUTTON, ID_CLOSE_SETTINGS, 560, 1370, 170, 40);
        gCreatingSettingsCategory = 4;
        heading(L"요약", 24, 96, 710);
        auto summaryChoice = [&](const wchar_t* label, int id, int x, int y, int width, int count, int selection, const wchar_t* const* items) {
            settingsControl(window, L"STATIC", label, 0, 0, x, y, width, 24);
            HWND combo = settingsControl(window, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, id, x, y + 28, width, 220);
            for (int i = 0; i < count; ++i) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(items[i]));
            SendMessageW(combo, CB_SETCURSEL, selection, 0);
            return combo;
        };
        constexpr std::array<const wchar_t*, 3> screenModeLabels{L"전체화면", L"보더리스 창화면", L"창화면"};
        constexpr std::array<const wchar_t*, 3> gaugeLabels{L"NORMAL", L"EASY", L"HARD"};
        constexpr std::array<const wchar_t*, 4> judgementLabels{L"BASIC", L"EASY", L"NORMAL", L"HARD"};
        gSummaryFps = summaryChoice(L"FPS", ID_SUMMARY_FPS, 24, 146, 300, static_cast<int>(frameRateLabels.size()),
            frameRateIndex(gTargetFps), frameRateLabels.data());
        std::array<std::wstring, 11> brightnessStorage{};
        std::array<const wchar_t*, 11> brightnessLabels{};
        for (int i = 0; i <= 10; ++i) {
            brightnessStorage[static_cast<size_t>(i)] = i == 0 ? L"0% (BGA 끔)" : std::to_wstring(i * 10) + L"%";
            brightnessLabels[static_cast<size_t>(i)] = brightnessStorage[static_cast<size_t>(i)].c_str();
        }
        gSummaryBga = summaryChoice(L"BGA 밝기", ID_SUMMARY_BGA, 400, 146, 330, static_cast<int>(brightnessLabels.size()), gBgaBrightness / 10, brightnessLabels.data());
        std::array<std::wstring, 39> speedStorage{};
        std::array<const wchar_t*, 39> speedLabels{};
        for (int i = 0; i < 39; ++i) {
            speedStorage[static_cast<size_t>(i)] = number((i + 2) / 4.0, 2) + L"x";
            speedLabels[static_cast<size_t>(i)] = speedStorage[static_cast<size_t>(i)].c_str();
        }
        gSummarySpeed = summaryChoice(L"노트 배속", ID_SUMMARY_SPEED, 24, 246, 300, static_cast<int>(speedLabels.size()),
            std::clamp(static_cast<int>(std::lround(gNoteSpeed * 4)) - 2, 0, 38), speedLabels.data());
        gSummaryScreenMode = summaryChoice(L"화면 모드", ID_SUMMARY_SCREEN_MODE, 24, 346, 300, static_cast<int>(screenModeLabels.size()), 2 - gScreenMode, screenModeLabels.data());
        gSummaryGauge = summaryChoice(L"게이지 모드", ID_SUMMARY_GAUGE, 400, 346, 330, static_cast<int>(gaugeLabels.size()), static_cast<int>(gGrooveGaugeMode), gaugeLabels.data());
        gSummaryAccuracyLimit=summaryChoice(L"정확도 제한",ID_SUMMARY_ACCURACY_LIMIT,24,446,300,int(accuracyLimitLabels.size()),static_cast<int>(gAccuracyLimit),accuracyLimitLabels.data());
        gCreatingSettingsCategory = -1;
        selectSettingsCategory(4);
        if (!gMemoryLimitError.empty()) setText(gSettingsStatus, gMemoryLimitError);
        return 0;
    }
    case WM_MEASUREITEM: {
        auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (item->CtlID >= ID_COLOR_BASE && item->CtlID < ID_COLOR_BASE + 8) {
            item->itemHeight = 24;
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (item->CtlID >= ID_CATEGORY_BASE && item->CtlID < ID_CATEGORY_BASE + 7) {
            const int category = static_cast<int>(item->CtlID - ID_CATEGORY_BASE);
            const bool selected = category == gSettingsCategory;
            fillRect(item->hDC, item->rcItem, selected ? RGB(34, 57, 79) : RGB(25, 32, 43));
            HBRUSH border = CreateSolidBrush(selected ? RGB(56, 189, 248) : RGB(68, 78, 92));
            FrameRect(item->hDC, &item->rcItem, border);
            DeleteObject(border);
            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, selected ? RGB(248, 250, 252) : RGB(188, 199, 214));
            HFONT oldFont = static_cast<HFONT>(SelectObject(item->hDC, gSmallFont));
            wchar_t label[32]{};
            GetWindowTextW(item->hwndItem, label, static_cast<int>(std::size(label)));
            RECT textRect = item->rcItem;
            DrawTextW(item->hDC, label, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(item->hDC, oldFont);
            if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &item->rcItem);
            return TRUE;
        }
        if (item->CtlID >= ID_COLOR_BASE && item->CtlID < ID_COLOR_BASE + 8 && item->itemID < gPalette.size()) {
            const auto& color = gPalette[item->itemID];
            fillRect(item->hDC, item->rcItem, (item->itemState & ODS_SELECTED) ? RGB(34, 57, 79) : RGB(25, 32, 43));
            RECT swatch{ item->rcItem.left + 8, item->rcItem.top + 4, item->rcItem.left + 66, item->rcItem.bottom - 4 };
            fillRect(item->hDC, swatch, color.color);
            drawText(item->hDC, color.name, item->rcItem.left + 80, item->rcItem.top + 3, RGB(241, 245, 249), gSmallFont);
            if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &item->rcItem);
            return TRUE;
        }
        break;
    }
    case WM_HSCROLL: {
        if (LOWORD(wParam) == TB_ENDTRACK) gAudio.triggerEffect(L"click");
        const HWND slider = reinterpret_cast<HWND>(lParam);
        if (!slider) break;
        int id = 0;
        for (int candidate : {ID_CPU_LIMIT, ID_MEMORY_LIMIT, ID_GPU_LIMIT, ID_BACKGROUND_BLUR, ID_BACKGROUND_OPACITY,
            ID_PREVIEW_VOLUME, ID_LANE_COVER, ID_LANE_TRANSPARENCY, ID_JUDGE_POSITION, ID_COMBO_TEXT_Y, ID_JUDGEMENT_TEXT_Y,
            ID_VOLUME_BASE, ID_VOLUME_BASE+1, ID_VOLUME_BASE+2, ID_VOLUME_BASE+3})
            if (settingControl(candidate) == slider) { id = candidate; break; }
        if (!id) break;
        const bool applied = applyNativeSetting(id, int(SendMessageW(slider, TBM_GETPOS, 0, 0)));
        if (applied && id == ID_MEMORY_LIMIT && (LOWORD(wParam) == TB_ENDTRACK || LOWORD(wParam) == TB_THUMBPOSITION))
            setBindingStatus(L"메모리 사용 상한을 적용했습니다.");
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, RGB(218, 228, 240));
        SetBkColor(dc, RGB(0, 0, 0));
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (lParam && (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == CBN_SELCHANGE))
            gAudio.triggerEffect(L"click");
        if (id >= ID_CATEGORY_BASE && id < ID_CATEGORY_BASE + 7) {
            gNativeSettingsFallback = false;
            selectSettingsCategory(id - ID_CATEGORY_BASE);
            return 0;
        }
#ifdef BMS_ENABLE_SKIN_BINDINGS
        if(id==ID_SKIN_LOAD||id==ID_SKIN_CLEAR){if(gCapturingBindings)cancelKeyBindingCapture();hideSettingsPreviewOverlay();runSkinSettingsAction(id);return 0;}
#endif
        if(id==ID_CONTROLLER_SETTINGS){hideSettingsPreviewOverlay();openControllerSettings();return 0;}
        if (id == ID_CUSTOM_BACKGROUND) { chooseCustomBackground(window); return 0; }
        if (id == ID_LOW_LATENCY && HIWORD(wParam) == CBN_SELCHANGE) {
            applyNativeSetting(id, int(SendMessageW(gLowLatencyChoice, CB_GETCURSEL, 0, 0))); return 0;
        }
        if (id == ID_SONG_PREVIEW || id == ID_NOTE_SUBPIXEL || id == ID_NOTE_ANTIALIAS || id == ID_DIRECT_COMPOSITION ||
            id == ID_COMPOSITION || id == ID_RESOURCE_USAGE || id == ID_CPU_PERFORMANCE || (id >= ID_SEPARATOR_BASE && id < ID_SEPARATOR_BASE + 7)) {
            const int value = SendMessageW(settingControl(id), BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (id == ID_COMPOSITION || id == ID_DIRECT_COMPOSITION) postSetting(id, value);
            else if (applyNativeSetting(id, value) && id == ID_CPU_PERFORMANCE)
                setBindingStatus(value ? L"게임 처리 스레드 우선순위를 높였습니다." : L"게임 처리 스레드 우선순위를 복원했습니다.");
            return 0;
        }
        if (id == ID_CLOSE_SETTINGS || id == IDOK || id == IDCANCEL) { SendMessageW(window, WM_CLOSE, 0, 0); return 0; }
        if (id == ID_BIND) { beginKeyBindingCapture(); return 0; }
        if (gCapturingBindings && (id == ID_HELP || id == ID_RESET_KEYS || HIWORD(wParam) == CBN_SELCHANGE)) cancelKeyBindingCapture();
        if (id == ID_HELP) { SendMessageW(gWindow, WM_COMMAND, ID_HELP, 0); return 0; }
        if (id == ID_RESET_KEYS) {
            if(gBindingMode==1)applyLaneKeys(DEFAULT_KEYS);else {bms::KeyProfiles defaults;gKeyProfiles.keys[gBindingMode]=defaults.keys[gBindingMode];gKeyProfiles.save(gSettingsFile,gBindingMode);}
            setText(gSettingsKeys, keySummary());
            setBindingStatus(L"기본 키 배치로 복원했습니다.");
            updateSelection();
            return 0;
        }
        if (HIWORD(wParam) == CBN_SELCHANGE) {
            const int selection = int(SendMessageW(reinterpret_cast<HWND>(lParam), CB_GETCURSEL, 0, 0));
            if(id==1299){if(gCapturingBindings)cancelKeyBindingCapture();gBindingMode=std::clamp(selection,0,4);return 0;}
            if (selection < 0) return 0;
            id = bms::SettingsController::canonicalId(id);
            int value = selection;
            if (id == ID_FPS) {
                if (selection >= int(frameRateValues.size())) return 0;
                value = frameRateValues[selection];
            } else if (id == ID_SPEED) value = selection + 2;
            else if(id==ID_SKIN_JUDGEMENT_MODE||id==ID_SKIN_COMBO_MODE)value=selection-1;
            else if (id == ID_NOTE_HEIGHT) value = 4 + selection * 2;
            else if (id == ID_BGA_DARKNESS) value = selection * 10;
            else if (id == ID_SCREEN_MODE) value = 2 - selection;
            else if (id == ID_SOUND_BUFFER) {
                if (selection >= int(SOUND_BUFFER_OPTIONS.size())) return 0;
                value = SOUND_BUFFER_OPTIONS[selection];
            }
            if (id == ID_SCREEN_MODE || id == ID_RESOLUTION) postSetting(id, value);
            else if (applyNativeSetting(id, value)) setBindingStatus(L"설정을 저장했습니다.");
            return 0;
        }
        break;
    }
    case WM_KEYDOWN:
        if (wParam == VK_F9 && !gCapturingBindings) { openRmlSettingsProbe(); return 0; }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && gCapturingBindings) cancelKeyBindingCapture();
        if (LOWORD(wParam) == WA_INACTIVE) hideSettingsPreviewOverlay();
        break;
    case WM_CANCELMODE:
        if(gCapturingBindings) cancelKeyBindingCapture();
        hideSettingsPreviewOverlay();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(window, &ps);
        drawText(dc, L"설정", 24, 18, RGB(125, 211, 252), gFont);
        EndPaint(window, &ps);
        return 0;
    }
    case WM_CLOSE:
        // Finish queued value/device commands before saving and releasing the UI.
        if(IsWindow(gWindow)) SendMessageW(gWindow,WM_APPLY_SETTINGS,0,0);
        gRmlSettingsPage.close(); gRmlSettingsPageActive = false;
        gRmlSettingsHost.close();
        if (gCapturingBindings) cancelKeyBindingCapture();
        hideSettingsPreviewOverlay();
        saveDisplaySettings();
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if(gCapturingBindings) cancelKeyBindingCapture();
        releaseSettingsPreviewResources();
        gRmlSettingsPage.close(); gRmlSettingsPageActive = false;
        gSettingsWindow = nullptr;
#ifdef BMS_ENABLE_SKIN_BINDINGS
        gSkinNativeStatus=nullptr;
#endif
        gCategoryButtons.fill(nullptr); gVolumeSliders.fill(nullptr); gVolumeValues.fill(nullptr); gSettingsControlCategory.clear(); gSettingsControlRects.clear(); gSettingsBody = nullptr;
        gSettingsStatus = gSettingsKeys = gFpsChoice = gSpeedChoice = gJudgementChoice = gNoteHeightChoice = gGrooveGaugeChoice = gBind = gHelp = nullptr;
        gTimingHintChoice=nullptr;gAccuracyLimitChoice=gSummaryAccuracyLimit=nullptr;gSkinJudgementChoice=gSkinComboChoice=nullptr;
        gSummaryFps = gSummaryBga = gSummarySpeed = gSummaryJudgement = gSummaryScreenMode = gSummaryGauge = nullptr;
        gSoundBufferChoice = nullptr;
        gAudioOutputModeChoice = nullptr;
        gCpuLimitSlider = gCpuLimitValue = gMemoryLimitSlider = gMemoryLimitValue = nullptr;
        gGpuLimitSlider = gGpuLimitValue = gResourceUsageChoice = nullptr;
        gComboTextYSlider = gComboTextYValue = gJudgementTextYSlider = gJudgementTextYValue = nullptr;
        gJudgePosition = gJudgeValue = nullptr;
        gLaneTransparencySlider = gLaneTransparencyValue = nullptr;
        gBgaDarknessChoice = nullptr;
        gLaneCoverSlider = gLaneCoverValue = nullptr;
        gScreenModeChoice = gResolutionChoice = gGearChoice = gCompositionChoice = gDirectCompositionChoice = nullptr; gResolutionOptions.clear(); gSettingsScroll = 0;
        gSubpixelChoice = gAntialiasChoice = gLowLatencyChoice = nullptr;
        gColorChoices.fill(nullptr);
        gSeparatorButtons.fill(nullptr);
        EnableWindow(gWindow, TRUE);
        SetActiveWindow(gWindow);
        focusBrowseInput();
        InvalidateRect(gWindow, nullptr, FALSE);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void openFullSettings() {
    if (gPlaying || gPracticeWindow || (gLoading && gLoadResult.playback)) return;
    if (gSettingsWindow) { selectSettingsCategory(4); SetForegroundWindow(gSettingsWindow); return; }
    RECT owner{}, work{};
    GetWindowRect(gWindow, &owner);
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = 780, height = std::min(820, static_cast<int>(work.bottom - work.top) - 30);
    const int x = std::clamp((owner.left + owner.right - width) / 2, work.left, std::max(work.left, work.right - width));
    const int y = std::clamp((owner.top + owner.bottom - height) / 2, work.top, std::max(work.top, work.bottom - height));
    HWND settings = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, L"BmsPlayerSettingsWindow", L"BMS Player 설정",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VSCROLL | WS_CLIPCHILDREN, x, y, width, height, gWindow, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!settings) return;
    if (!gSettingsPreviewOverlay) {
        gSettingsPreviewOverlay = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            L"BmsPlayerSettingsPreviewOverlay", L"", WS_POPUP, 0, 0, 1, 1, gWindow, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (gSettingsPreviewOverlay) SetLayeredWindowAttributes(gSettingsPreviewOverlay, 0, 255, LWA_ALPHA);
    }
    EnableWindow(gWindow, FALSE);
    if(gList)ShowWindow(gList,SW_SHOWNA);
    InvalidateRect(gWindow,nullptr,FALSE);
    ShowWindow(settings, SW_SHOW);
    // All categories are connected; open the summary by default. Keep native fallback.
    selectSettingsCategory(4);
    if (!gRmlSettingsPageActive && gCategoryButtons[4]) SetFocus(gCategoryButtons[4]);
}

void openSettings() {
    if(gPlaying||gPracticeWindow||gLoading)return;
    if(gSummaryVisible){toggleSummary();openFullSettings();}
    else if(gSettingsWindow)SetForegroundWindow(gSettingsWindow);
    else toggleSummary();
}

COLORREF songDifficultyColor(const bms::Chart& chart) {
    wchar_t* end{};
    const wchar_t* levelStart = chart.playLevel.c_str();
    const double level = std::wcstod(levelStart, &end);
    while (end && std::iswspace(*end)) ++end;
    if (end == levelStart || !end || *end != L'\0' || !std::isfinite(level) || level <= 0.0 || level > 12.0)
        return RGB(167, 139, 250);

    if(chart.difficulty>=1 && chart.difficulty<=5) {
        constexpr COLORREF categories[]{RGB(74,222,128),RGB(96,165,250),RGB(251,146,60),RGB(248,113,113),RGB(167,139,250)};
        return categories[chart.difficulty-1];
    }
    const std::wstring name = bms::toLower(chart.title + L" " + chart.subtitle);
    if (name.find(L"another") != std::wstring::npos || name.find(L"어나더") != std::wstring::npos)
        return RGB(248, 113, 113);
    if (name.find(L"hyper") != std::wstring::npos || name.find(L"하이퍼") != std::wstring::npos)
        return RGB(251, 146, 60);
    if (name.find(L"easy") != std::wstring::npos || name.find(L"beginner") != std::wstring::npos ||
        name.find(L"light") != std::wstring::npos || name.find(L"이지") != std::wstring::npos ||
        name.find(L"basic") != std::wstring::npos || name.find(L"베이직") != std::wstring::npos)
        return RGB(74, 222, 128);
    if (name.find(L"normal") != std::wstring::npos || name.find(L"노말") != std::wstring::npos ||
        name.find(L"노멀") != std::wstring::npos)
        return RGB(96, 165, 250);

    // Some charts distinguish variants only with a final one-letter suffix.
    // Accept isolated suffixes and bracketed forms such as "Song H" / "Song [H]".
    auto trailingCode = [](std::wstring text) -> wchar_t {
        while (!text.empty() && std::iswspace(text.back())) text.pop_back();
        if (text.empty()) return L'\0';
        size_t codeIndex = text.size() - 1;
        const wchar_t last = text.back();
        if (last == L']' || last == L')' || last == L'}') {
            if (text.size() < 3) return L'\0';
            codeIndex = text.size() - 2;
            while (codeIndex > 0 && std::iswspace(text[codeIndex])) --codeIndex;
            const wchar_t open = last == L']' ? L'[' : last == L')' ? L'(' : L'{';
            if (codeIndex == 0 || text[codeIndex - 1] != open) return L'\0';
        } else if (text.size() > 1) {
            const wchar_t before = text[text.size() - 2];
            if (!std::iswspace(before) && before != L'_' && before != L'-' && before != L'/') return L'\0';
        }
        const wchar_t code = static_cast<wchar_t>(std::towupper(text[codeIndex]));
        return code == L'B' || code == L'E' || code == L'N' || code == L'H' || code == L'A' ? code : L'\0';
    };
    const wchar_t titleSuffix = trailingCode(chart.title);
    const std::wstring fileStem = chart.path.stem().wstring();
    wchar_t suffix = titleSuffix != L'\0' ? titleSuffix : trailingCode(chart.subtitle);
    if (suffix == L'\0') suffix = trailingCode(fileStem);
    if (suffix == L'\0') {
        // Support compact file names such as "rainbowH.bms" when #TITLE is "rainbow".
        auto compact = [](const std::wstring& value) {
            std::wstring result;
            for (wchar_t ch : bms::toLower(value)) {
                if (!std::iswspace(ch) && ch != L'_' && ch != L'-' && ch != L'[' && ch != L']' &&
                    ch != L'(' && ch != L')' && ch != L'{' && ch != L'}')
                    result.push_back(ch);
            }
            return result;
        };
        const std::wstring titleKey = compact(chart.title);
        const std::wstring fileKey = compact(fileStem);
        if (!titleKey.empty() && fileKey.size() == titleKey.size() + 1 &&
            fileKey.compare(0, titleKey.size(), titleKey) == 0) {
            const wchar_t code = static_cast<wchar_t>(std::towupper(fileKey.back()));
            if (code == L'B' || code == L'E' || code == L'N' || code == L'H' || code == L'A') suffix = code;
        }
    }
    if (suffix == L'B' || suffix == L'E') return RGB(74, 222, 128);
    if (suffix == L'H') return RGB(251, 146, 60);
    if (suffix == L'A') return RGB(248, 113, 113);
    if (suffix == L'N') return RGB(96, 165, 250);

    // If the chart name carries no difficulty label, infer a display category from #PLAYLEVEL.
    if (level <= 3.0) return RGB(74, 222, 128);
    if (level <= 6.0) return RGB(96, 165, 250);
    if (level <= 9.0) return RGB(251, 146, 60);
    return RGB(248, 113, 113);
}

bool handleBrowseShortcut(UINT message, WPARAM key, LPARAM flags) {
    if (message != WM_KEYDOWN && message != WM_KEYUP) return false;
    const int command = key == VK_SPACE ? ID_SETTINGS : key == VK_F2 ? ID_OPEN : key == VK_F5 ? ID_RESCAN : 0;
    if (!command || gStartupSplash || gPlaying || gShowingResult || gSettingsWindow || gPracticeWindow) return false;
    if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000) || (GetKeyState(VK_SHIFT) & 0x8000)) return false;
    if (message == WM_KEYDOWN && (flags & (1LL << 30)) == 0 && (!gLoading || command == ID_SETTINGS))
        SendMessageW(gWindow, WM_COMMAND, command, 0);
    return true;
}

bool paintRmlSongList(HWND window, HDC dc) {
    RECT client{};
    if (!GetClientRect(window, &client)) return false;
    std::vector<bms::ui::SongListVisualRow> rows;
    const int count = ListView_GetItemCount(window);
    const int viewportHeight = static_cast<int>(std::max<LONG>(0, client.bottom - client.top));
    const int rowHeight = songListRowHeight();
    if (count > 0 && viewportHeight > 0 && rowHeight > 0) {
        const double visualPosition = gSongListScroll.position;
        const double halfViewportRows = static_cast<double>(viewportHeight) / (2.0 * rowHeight);
        const int overscanRows = 4;
        const int visibleRows = std::max(1, static_cast<int>(std::ceil(static_cast<double>(viewportHeight) / rowHeight)) + 1);
        const int windowSize = visibleRows + overscanRows * 2;
        const auto visibleFirst = static_cast<std::int64_t>(std::floor(visualPosition - halfViewportRows));
        const auto visibleLast = static_cast<std::int64_t>(std::ceil(visualPosition + halfViewportRows));
        const std::int64_t desiredStart = visibleFirst - overscanRows;
        const bool windowInvalid = gSongListRenderWindowStart == songListInvalidRenderWindow ||
            gSongListRenderWindowSize != windowSize;
        const bool nearWindowEdge = !windowInvalid &&
            (visibleFirst < gSongListRenderWindowStart + 2 ||
             visibleLast > gSongListRenderWindowStart + gSongListRenderWindowSize - 3);
        if (windowInvalid || nearWindowEdge) gSongListRenderWindowStart = desiredStart;
        gSongListRenderWindowSize = windowSize;
    } else {
        gSongListRenderWindowStart = 0;
        gSongListRenderWindowSize = 0;
    }
    const std::int64_t first = count > 0 ? gSongListRenderWindowStart : 0;
    const std::int64_t end = first + gSongListRenderWindowSize;
    const auto& groups = gSongSelection.groups();
    rows.reserve(static_cast<size_t>(std::max<std::int64_t>(0, end - first)));
    const double visualPosition = count > 0 ? gSongListScroll.position : 0.0;
    const float guideTop = static_cast<float>((viewportHeight - rowHeight) * 0.5);
    const float trackTop = guideTop + static_cast<float>((static_cast<double>(first) - visualPosition) * rowHeight);
    const int selectedRow = ListView_GetNextItem(window, -1, LVNI_SELECTED);
    const auto selectedVisualRow = static_cast<std::int64_t>(std::llround(gSongListScroll.target));
    for (std::int64_t virtualRow = first; virtualRow < end; ++virtualRow) {
        const int rowIndex = wrapSongListRow(virtualRow, count);
        LVITEMW item{}; item.mask = LVIF_PARAM; item.iItem = rowIndex;
        if (!ListView_GetItem(window, &item)) continue;
        bms::ui::SongListVisualRow row;
        row.index = static_cast<int>(virtualRow - first);
        row.top = static_cast<float>(virtualRow - first) * rowHeight;
        row.height = rowHeight;
        row.selected = rowIndex == selectedRow && virtualRow == selectedVisualRow;
        row.alternate = rowIndex % 2 != 0;
        const int groupIndex = static_cast<int>(item.lParam);
        if (groupIndex < 0 || groupIndex >= static_cast<int>(groups.size()) || groups[static_cast<size_t>(groupIndex)].chartIndices.empty()) {
            row.kind=gLoading?bms::ui::SongEntryKind::Song:navigationKind(groupIndex);
            wchar_t label[1024]{};ListView_GetItemText(window,rowIndex,1,label,1024);row.title = label[0]?label:L"불러오는 중..."; row.level = gLoading?L"…":row.kind==bms::ui::SongEntryKind::LibraryRoot?L"LIB":bms::ui::entryTypeLabel(row.kind); row.difficultyColor = RGB(173, 215, 238);
        } else {
            const auto& group = groups[static_cast<size_t>(groupIndex)];
            const size_t variant = std::min(group.selectedVariant, group.chartIndices.size() - 1);
            const size_t chartIndex = group.chartIndices[variant];
            if (chartIndex >= gCharts.size()) continue;
            const auto& chart = gCharts[chartIndex];
            row.title = chart.title + (chart.subtitle.empty() ? L"" : L"  " + chart.subtitle);
            row.artist = chart.artist.empty() ? L"작곡가 미표기" : chart.artist;
            row.banner = chart.banner.generic_wstring();
            row.level = chart.playLevel.empty() ? L"-" : chart.playLevel;
            row.variant = std::to_wstring(variant + 1) + L" / " + std::to_wstring(group.chartIndices.size());
            row.difficultyColor = songDifficultyColor(chart); row.blocked = !chart.playable || !bms::gameplayModeAllowed(chart);
            if (const auto* record = playRecordForChart(chartIndex)) row.lamp = record->cleared ? 1 : 2;
        }
        rows.push_back(std::move(row));
    }
    // Compose off-screen over the already-rendered parent frame. The ListView
    // suppresses WM_ERASEBKGND, so its background must come from the skin/page
    // underneath rather than a second opaque browse-background fill.
    HDC composedDc{};
    HPAINTBUFFER buffer = BeginBufferedPaint(dc, &client, BPBF_TOPDOWNDIB, nullptr, &composedDc);
    if (!buffer || !composedDc) {
        if (buffer) EndBufferedPaint(buffer, FALSE);
        return false;
    }
    POINT origin{}; MapWindowPoints(window, gWindow, &origin, 1);
    const RECT source{origin.x, origin.y, origin.x + client.right, origin.y + client.bottom};
    if (!gBackBufferDc || source.left < 0 || source.top < 0 ||
        source.right > gBackBufferWidth || source.bottom > gBackBufferHeight ||
        !BitBlt(composedDc, 0, 0, client.right, client.bottom,
            gBackBufferDc, source.left, source.top, SRCCOPY)) {
        EndBufferedPaint(buffer, FALSE);
        return false;
    }
    const bool painted = gRmlSongList.paint(composedDc, client.right, client.bottom, rows,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - gBrowseTextStartedAt).count(),
        trackTop, guideTop, rowHeight);
    const HRESULT presented = EndBufferedPaint(buffer, painted ? TRUE : FALSE);
    return painted && SUCCEEDED(presented);
}

LRESULT CALLBACK songListSubclass(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
    if(gSongInfoVisible&&(message==WM_KEYDOWN||message==WM_LBUTTONDOWN||message==WM_MOUSEWHEEL)){if(message==WM_KEYDOWN&&wParam==VK_ESCAPE){gSongInfoVisible=false;ShowWindow(gList,SW_SHOWNA);InvalidateRect(gWindow,nullptr,FALSE);}return 0;}

    if(summaryInput(message,wParam,lParam,window))return 0;
    if (handleBrowseShortcut(message, wParam, lParam)) return 0;
    if (gRmlSongList.enabled() && message == WM_ERASEBKGND) return 1;
    if (gRmlSongList.enabled() && message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        if (!paintRmlSongList(window, dc)) DefSubclassProc(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
        EndPaint(window, &paint);
        return 0;
    }
    if (gRmlSongList.enabled() && (message == WM_PRINTCLIENT || (message == WM_PRINT && (lParam & PRF_CLIENT)))) {
        if (paintRmlSongList(window, reinterpret_cast<HDC>(wParam))) return 0;
    }
    if (gRmlSongList.enabled() && !gPlaying && !gShowingResult && !gLoading) {
        if (message == WM_LBUTTONDOWN) {
            SetFocus(window);
            const auto hit = songListHitTestAtVisualY(static_cast<short>(HIWORD(lParam)));
            if (hit) selectSongListRow(hit.row, hit.virtualRow);
            return 0;
        }
        if (message == WM_LBUTTONUP || message == WM_RBUTTONDOWN || message == WM_RBUTTONUP) return 0;
        if (message == WM_LBUTTONDBLCLK) {
            SetFocus(window);
            const auto hit = songListHitTestAtVisualY(static_cast<short>(HIWORD(lParam)));
            if (hit && selectSongListRow(hit.row, hit.virtualRow)) beginSelectedChart();
            return 0;
        }
        if (message == WM_RBUTTONDBLCLK) {
            const auto hit = songListHitTestAtVisualY(static_cast<short>(HIWORD(lParam)));
            if (hit && selectSongListRow(hit.row, hit.virtualRow)) openPracticeSetup();
            return 0;
        }
        if (message == WM_MOUSEWHEEL) {
            gSongListWheelRemainder += GET_WHEEL_DELTA_WPARAM(wParam);
            const int notches = gSongListWheelRemainder / WHEEL_DELTA;
            gSongListWheelRemainder -= notches * WHEEL_DELTA;
            if (notches != 0) {
                const int count = ListView_GetItemCount(window);
                const int steps = 1;
                const long long requested = static_cast<long long>(notches) * steps;
                const int boundedSteps = static_cast<int>(std::clamp(std::abs(requested), 1LL, static_cast<long long>(std::max(1, count))));
                navigateSongList(requested > 0 ? -1 : 1, boundedSteps);
            }
            return 0;
        }
        if (message == WM_KEYUP && (wParam == VK_UP || wParam == VK_DOWN)) {
            if (gSongListHeldKey == wParam) { clearSongListHeldKey(); scheduleSongListAnimationTimer(); }
            return 0;
        }
        if ((message == WM_KILLFOCUS || message == WM_CANCELMODE || message == WM_CAPTURECHANGED) && gSongListHeldKey) {
            clearSongListHeldKey(); scheduleSongListAnimationTimer();
        }
    }
    // Give the list the full client width without a scrollbar gutter.
    if (message == WM_KEYDOWN && !gLoading && wParam == 'S') {
        if ((lParam & (1LL << 30)) == 0) toggleSongSort();
        return 0;
    }
    if (message == WM_KEYDOWN && !gLoading && (wParam == VK_UP || wParam == VK_DOWN)) {
        if ((lParam & (1LL << 30)) == 0) {
            const auto now = std::chrono::steady_clock::now();
            const UINT direction = static_cast<UINT>(wParam);
            clearSongListHeldKey();
            const bool moved = navigateSongList(direction == VK_DOWN ? 1 : -1);
            if (moved) {
                gSongListHeldKey = direction;
                gSongListNextRepeat = now + songListRepeatInitialDelay;
                scheduleSongListAnimationTimer();
            }
        }
        return 0;
    }
    if (message == WM_KILLFOCUS || message == WM_CANCELMODE || message == WM_CAPTURECHANGED) {
        if (gSongListHeldKey) { clearSongListHeldKey(); scheduleSongListAnimationTimer(); }
    }
    if(message==WM_MOUSEWHEEL&&!gLoading&&!gPlaying&&!gShowingResult&&ListView_GetItemCount(window)>1&&GET_WHEEL_DELTA_WPARAM(wParam)!=0)
        gAudio.triggerEffect(L"songlistscroll");
    if (message == WM_NCCALCSIZE || message == WM_NCPAINT) return 0;
    if (message == WM_NCHITTEST) return HTCLIENT;
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, songListSubclass, id);
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT handleCustomDraw(LPARAM lParam) {
    auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
    switch (draw->nmcd.dwDrawStage) {
    case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
        const auto groupIndex = static_cast<int>(draw->nmcd.lItemlParam);
        const int row = static_cast<int>(draw->nmcd.dwItemSpec);
        const bool selected = (ListView_GetItemState(gList, row, LVIS_SELECTED) & LVIS_SELECTED) != 0;
        RECT bounds{}; ListView_GetItemRect(gList, row, &bounds, LVIR_BOUNDS);
        bounds.left = 0;
        bounds.right = gWindowWidth / 3;
        HDC dc = draw->nmcd.hdc;
        const int saved = SaveDC(dc);
        SetBkMode(dc, TRANSPARENT);
        // Keep native fallback geometry and flat colors aligned with RmlUi.
        fillRect(dc,bounds,RGB(32,32,32));
        bounds.left+=selected?4:20;bounds.right-=4;
        fillRect(dc,bounds,selected?RGB(220,226,232):RGB(188,194,201));
        SetTextColor(dc,RGB(35,42,51));
        const auto& groups = gSongSelection.groups();
        if (groupIndex < 0 || groupIndex >= static_cast<int>(groups.size())) {
            RECT loading = bounds;
            loading.left += 10;
            SelectObject(dc, gListFont); SetTextColor(dc, RGB(35, 42, 51));
            if(groupIndex==-1&&!gLoading){
                fillRect(dc,bounds,selected?RGB(82,121,149):RGB(69,103,128));
                SetTextColor(dc,RGB(242,246,250));
            }
            wchar_t text[1024]{};ListView_GetItemText(gList,row,1,text,1024);
            std::wstring label=text[0]?text:L"Loading...";
            if(!gLoading&&groupIndex<=-3)label=L"[DIR] "+label;
            else if(!gLoading&&groupIndex==-1)label=L"[..] "+label;
            DrawTextW(dc,label.c_str(),-1,&loading,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
        } else {
            const auto& group = groups[static_cast<size_t>(groupIndex)];
            const size_t chartIndex = group.chartIndices[std::min(group.selectedVariant, group.chartIndices.size() - 1)];
            const auto& chart = gCharts[chartIndex];
            if (const auto* record = playRecordForChart(chartIndex)) {
                const COLORREF lampColor = record->cleared ? RGB(74, 222, 128) : RGB(248, 113, 113);
                fillRect(dc, {bounds.left + 2, bounds.top + 5, bounds.left + 7, bounds.bottom - 5}, lampColor);
            }
            std::wstring title = chart.title;
            if (!chart.subtitle.empty()) title += L"  " + chart.subtitle;
            const std::array<std::wstring, 2> labels{chart.playLevel.empty() ? L"-" : chart.playLevel, title};
            int x = bounds.left;
            for (int column = 0; column < 2; ++column) {
                const int width = ListView_GetColumnWidth(gList, column);
                constexpr int cellInset = 10;
                RECT cell{x + cellInset, bounds.top + 2, std::min<LONG>(x + width - cellInset, bounds.right - 12), bounds.bottom - 2};
                SelectObject(dc, column == 0 ? (selected ? gListSelectedLevelFont : gListLevelFont) :
                    (selected ? gListSelectedFont : gListFont));
                SetTextColor(dc, column == 0 ? songDifficultyColor(chart) :
                    !chart.playable ? RGB(128, 134, 145) : column == 1 ?
                    (selected ? RGB(21, 28, 36) : RGB(35, 42, 51)) : RGB(35, 42, 51));
                DrawTextW(dc, labels[static_cast<size_t>(column)].c_str(), -1, &cell,
                    DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | (column == 0 ? DT_CENTER : DT_LEFT));
                x += width;
            }
        }
        RestoreDC(dc, saved);
        return CDRF_SKIPDEFAULT;
    }
    default: return CDRF_DODEFAULT;
    }
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if(summaryInput(message,wParam,lParam,window))return 0;
    switch (message) {
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN:
        if (gStartupSplash) return 0;
        if (adjustBrowseShortcut(static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)), message == WM_LBUTTONDOWN)) return 0;
        break;
    case WM_CREATE: {
        gWindow = window;
        DragAcceptFiles(window,TRUE);
        BufferedPaintInit();
        const int currentThreadPriority = GetThreadPriority(GetCurrentThread());
        if (currentThreadPriority != THREAD_PRIORITY_ERROR_RETURN)
            gDefaultThreadPriority = currentThreadPriority;
        loadKeyBindings();
        loadDisplaySettings();
        gController.initialize(window);gController.load(gSettingsFile.wstring());
        if(gController.disableIfUnavailable())WritePrivateProfileStringW(L"Controller",L"Enabled",L"0",gSettingsFile.c_str());
        SetTimer(window,ID_CONTROLLER_POLL,gController.config.enabled?4:1000,nullptr);
        if (gCpuPerformanceMode && !applyCpuPerformanceMode()) gCpuPerformanceMode = false;
        gControlBrush = CreateSolidBrush(RGB(16, 20, 28));
        gFont = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        gSmallFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        gPracticeHudFont = CreateFontW(-18, 0, 0, 0, FW_LIGHT, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        gListFont = CreateFontW(-25, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        gListSelectedFont = CreateFontW(-28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        gListLevelFont = CreateFontW(-25, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        gListSelectedLevelFont = CreateFontW(-28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        gBrowseTitleFont = CreateFontW(-80, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        gBrowseSongTitleFont = CreateFontW(-120, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        gBrowseSongInfoFont = CreateFontW(-48, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        gBrowseRankFont = CreateFontW(-64, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        gBrowseLevelFont = CreateFontW(-28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        gBrowseMetadataFont = CreateFontW(-30, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        gHudBoldFont = CreateFontW(-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        gGaugeModeFont = CreateFontW(-13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
        const bool hudFontLoaded = gEmbeddedHudFont.load();
        const auto hudFace = hudFontLoaded ? L"Michroma" : L"Segoe UI";
        gComboFont = CreateFontW(-64, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, hudFace);
        gJudgementFont = CreateFontW(-80, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, hudFace);
        gResultRankFont = CreateFontW(-112, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        gResultScoreFont = CreateFontW(-52, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        gResultTitleFont = CreateFontW(-34, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        gResultDetailsFont = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        gResultNextFont = CreateFontW(-22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        gFolder = CreateWindowW(L"STATIC", L"곡 폴더를 선택하세요.", WS_CHILD | SS_ENDELLIPSIS | SS_NOPREFIX, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_FOLDER)), nullptr, nullptr);
        gList = CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOCOLUMNHEADER | LVS_SHAREIMAGELISTS, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_LIST)), nullptr, nullptr);
        SetWindowSubclass(gList, songListSubclass, 1, 0);
        ListView_SetExtendedListViewStyle(gList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        ListView_SetBkColor(gList, RGB(64, 64, 64));
        ListView_SetTextBkColor(gList, RGB(64, 64, 64));
        gSongRowImages = ImageList_Create(1, 58, ILC_COLOR32, 1, 1);
        if (gSongRowImages) ListView_SetImageList(gList, gSongRowImages, LVSIL_SMALL);
        const std::array<std::pair<const wchar_t*, int>, 2> columns{ {{L"레벨", 72}, {L"곡명 / 채보", 380}} };
        for (int i = 0; i < static_cast<int>(columns.size()); ++i) {
            LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            column.pszText = const_cast<LPWSTR>(columns[static_cast<size_t>(i)].first);
            column.cx = columns[static_cast<size_t>(i)].second; column.iSubItem = i;
            ListView_InsertColumn(gList, i, &column);
        }
        gInfo = CreateWindowW(L"STATIC", L"곡을 선택하면 상세 정보가 표시됩니다.", WS_CHILD | SS_NOPREFIX, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_INFO)), nullptr, nullptr);
        gStatus = CreateWindowW(L"STATIC", L"곡 폴더를 선택하거나 기존 곡 라이브러리를 불러오세요.", WS_CHILD, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS)), nullptr, nullptr);
        gHiddenSongs = CreateWindowW(L"BUTTON", L"숨겨진 파일 보기 (0)", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_HIDDEN_SONGS)), nullptr, nullptr);
        gResultNext = CreateWindowW(L"BUTTON", L"NEXT", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_RESULT_NEXT)), nullptr, nullptr);
        SendMessageW(gResultNext, WM_SETFONT, reinterpret_cast<WPARAM>(gResultNextFont), TRUE);
        ShowWindow(gResultNext, SW_HIDE);
        constexpr std::array<const wchar_t*, 4> pauseLabels{L"RESUME", L"RESTART", L"QUIT", L"Practice"};
        constexpr std::array<int, 4> pauseIds{ID_PAUSE_RESUME, ID_PAUSE_RESTART, ID_PAUSE_QUIT, ID_PAUSE_PRACTICE};
        for (size_t i = 0; i < gPauseButtons.size(); ++i) {
            gPauseButtons[i] = CreateWindowExW(WS_EX_TRANSPARENT, L"BUTTON", pauseLabels[i], WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0,
                window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(pauseIds[i])), nullptr, nullptr);
            SendMessageW(gPauseButtons[i], WM_SETFONT, reinterpret_cast<WPARAM>(gResultNextFont), TRUE);
            ShowWindow(gPauseButtons[i], SW_HIDE);
        }
        EnableWindow(gHiddenSongs, FALSE);
        for (HWND control : { gFolder, gInfo, gStatus, gHiddenSongs }) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(gSmallFont), TRUE);
        SendMessageW(gList, WM_SETFONT, reinterpret_cast<WPARAM>(gListFont), TRUE);
        if (gStartupSplash) ShowWindow(gList, SW_HIDE);
        updateHiddenSongsVisibility();
        layoutControls();
#ifdef BMS_ENABLE_SKIN_BINDINGS
        restoreSkinSelection();
#endif
        return 0;
    }
    case WM_SIZE:
        if(!songEntryActive()&&(gWindowWidth!=LOWORD(lParam)||gWindowHeight!=HIWORD(lParam)))gSongIntro.reset();
        hideSettingsPreviewOverlay();
        gWindowWidth = LOWORD(lParam); gWindowHeight = HIWORD(lParam); layoutControls();
        if(gRmlSettingsPageActive) syncRmlSettingsState();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize.x = 960;
        limits->ptMinTrackSize.y = 680;
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, RGB(218, 228, 240));
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    }
    case WM_APPLY_SETTINGS: {
        const auto results = settingsController().drain();
        for (const auto& [id, value] : settingsController().snapshot()) { (void)value; refreshSettingControls(id); }
        for (const auto& result : results) if (!result.applied) setBindingStatus(result.error);
        std::wstring status = L"설정을 저장했습니다.";
        for (const auto& result : results) if (!result.applied) status = result.error;
        syncRmlSettingsState(status);
        if(gSettingsPreviewShowing) { InvalidateRect(gSettingsPreviewOverlay,nullptr,FALSE); UpdateWindow(gSettingsPreviewOverlay); }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_LOAD_PROGRESS:
        if (gLoading && !gLoadResult.playback) {
            if (gStartupSplash) gStartupProgress = wParam == 1 ? static_cast<int>(lParam) / 4 : 25 + static_cast<int>(lParam) * 3 / 4;
            setStatus((wParam == 1 ? L"곡 폴더 복사 중... " : L"채보 분석 중... ") + std::to_wstring(lParam) + L"%");
        }
        return 0;
    case WM_LIBRARY_PROGRESS:
        if(!gPlaying&&!gShowingResult){gLibraryIndexStatus=L"라이브러리 확인 중... "+std::to_wstring(wParam)+L"%";setStatus(gLibraryIndexStatus);if(gRmlSettingsPage.window())gRmlSettingsPage.setText("library-status",settingsUtf8(gLibraryIndexStatus));}
        return 0;
    case WM_LIBRARY_ACTION: {
        if(gPlaying||gShowingResult||gLoading){setStatus(L"곡 선택 화면에서 라이브러리를 관리하세요.");return 0;}
        if(wParam==1){std::vector<std::filesystem::path> paths;if(bms::ui::chooseSongSources(gSettingsWindow?gSettingsWindow:window,gCurrentFolder.empty()?executableFolder():gCurrentFolder,paths))registerLibraryPaths(paths);}
        else if(wParam==2&&gLibrarySourceSelection<gLibrarySources.roots.size()){
            gSongIndexWorker.cancel();gLibrarySources.remove(gLibrarySourceSelection);gLibrarySourceSelection=0;gLibrarySources.save();startLibraryValidation();
        } else if(wParam==3&&gLibrarySourceSelection<gLibrarySources.roots.size())startLibraryValidation(gLibrarySources.roots[gLibrarySourceSelection]);
        else if(wParam==4){gSongPreview.clearCache();gBrowseAudioKey.clear();startLibraryValidation();}
        else if(wParam==5||wParam==6)applyLibraryView();
        syncLibrarySettings();return 0;
    }
    case WM_DROPFILES: {
        const auto drop=reinterpret_cast<HDROP>(wParam);std::vector<std::filesystem::path> paths;
        const UINT count=DragQueryFileW(drop,0xffffffff,nullptr,0);
        for(UINT i=0;i<count;++i){const UINT n=DragQueryFileW(drop,i,nullptr,0);std::wstring path(n+1,L'\0');DragQueryFileW(drop,i,path.data(),n+1);path.resize(n);paths.emplace_back(path);}
        DragFinish(drop);
        if(!gPlaying&&!gShowingResult&&!gLoading&&!gSettingsWindow)registerLibraryPaths(paths);
        else setStatus(L"곡 선택 화면으로 돌아온 뒤 폴더를 드롭하세요.");
        return 0;
    }
    case WM_LIBRARY_INDEX: {
        if(gSettingsWindow&&gSongIndexWorker.hasPending()){
            gLibraryIndexStatus=L"라이브러리 확인 완료 · 설정을 닫으면 목록에 반영됩니다.";syncLibrarySettings();return 0;
        }
        if(gPlaying||gShowingResult||gLoading||gSettingsWindow||gPracticeWindow)return 0;
        auto update=gSongIndexWorker.take();if(!update)return 0;
        if(!update->complete){gLibraryIndexStatus=update->warnings.front();setStatus(gLibraryIndexStatus);syncLibrarySettings();return 0;}
        const int oldIndex=selectedIndex();const auto selected=oldIndex>=0&&oldIndex<int(gCharts.size())?gCharts[size_t(oldIndex)].path:std::filesystem::path{};
        gSongFolders.remember(std::max(0,ListView_GetNextItem(gList,-1,LVNI_SELECTED)));
        gCurrentFolder=gLibrarySources.roots.empty()?std::filesystem::path{}:gLibrarySources.roots.front();
        gSongFolders.invalidate();gCharts=std::move(update->charts);gSongFolders.knownFolders=std::move(update->folders);
        rebuildPartialLoadSummary();rebuildChartRecordKeys();buildSongGroups();int preferred=restoreLibraryChart(selected);
        rebuildSongRows(preferred);gLibraryIndexStatus=gLibrarySources.roots.empty()?L"곡 폴더: (없음) · F2 또는 기타 설정에서 폴더를 등록하세요.":update->warnings.empty()?L"곡 라이브러리 확인 완료":update->warnings.front();setStatus(gLibraryIndexStatus);syncLibrarySettings();return 0;
    }
    case WM_PLAY_CHART_READY: acceptPlaybackChart(static_cast<std::uintptr_t>(wParam));return 0;
    case WM_LOAD_FINISHED:
        if(wParam){completePlaybackWorker(static_cast<std::uintptr_t>(wParam));return 0;}
        if (gLoadWorker.joinable()) gLoadWorker.join();
        if(gLoadResult.playback){if(gLoadCancelled)gPlaybackLoad.cancel();else if(!gLoadResult.error.empty())gPlaybackLoad.fail();else gPlaybackLoad.resourcesLoaded();}
        if (gLoadResult.playback && !gLoadCancelled && gLoadResult.error.empty()) {
            const bool hasTitle = gLoadResult.index >= 0 && gLoadResult.index < static_cast<int>(gCharts.size()) &&
                !gCharts[static_cast<size_t>(gLoadResult.index)].titleImage.empty();
            if (!hasTitle) {
                finishSelectedChart();
                return 0;
            }
            gTitleFadeOutStarted = std::max(std::chrono::steady_clock::now(), gPlaybackLoadStarted + titleFadeDuration);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        gLoading = false;
        EnableWindow(gHiddenSongs, gShowHiddenCharts || std::any_of(gCharts.begin(), gCharts.end(), [](const auto& chart) { return !chart.playable; }));
        if (gLoadCancelled) {
            if (!gLoadResult.playback) ListView_DeleteItem(gList, 0);
            else {
                gAudio.releaseSong(); gLoadResult.chart = {};
                gVideoBga.reset();
    gBgaVideos.reset();gBgaSwitch.reset();
                startUiEffects();
                for (HWND control : {gList, gHiddenSongs})
                    if (control) ShowWindow(control, SW_SHOW);
            }
            setStatus(L"불러오기를 취소했습니다."); updateSelection(); finishStartupSplash(); return 0;
        }
        if (!gLoadResult.error.empty()) {
            if (!gLoadResult.playback) ListView_DeleteItem(gList, 0);
            else {
                gAudio.releaseSong(); gLoadResult.chart = {};
                gVideoBga.reset();
    gBgaVideos.reset();gBgaSwitch.reset();
                startUiEffects();
                for (HWND control : {gList, gHiddenSongs})
                    if (control) ShowWindow(control, SW_SHOW);
            }
            setStatus(gLoadResult.error); updateSelection(); finishStartupSplash(); return 0;
        }
        if (gLoadResult.playback) finishSelectedChart();
        else {
            gCurrentFolder = gLoadResult.folder;
            setText(gFolder, L"곡 폴더: " + gCurrentFolder.wstring());
            if (!gSettingsFile.empty()) WritePrivateProfileStringW(L"Library", L"LastFolder", gCurrentFolder.c_str(), gSettingsFile.c_str());
            gSongFolders.invalidate();gCharts = std::move(gLoadResult.charts);
            rebuildPartialLoadSummary();
            rebuildChartRecordKeys();
            buildSongGroups();
            std::vector<bms::Chart> previews;
            for (const auto& group : gSongSelection.groups()) {
                const auto& chart = gCharts[group.previewChartIndex];
                if (!chart.playable && chart.previewAudio.empty()) continue;
                previews.push_back(chart);
                previews.back().bitmaps.clear(); previews.back().bga.clear(); previews.back().bgaClips.clear(); previews.back().tempos.clear();
            }
            gSongPreview.prewarm(std::move(previews));
            gSongFolders.configure(gLibrarySources.navigationRoots(),gLibrarySources.originalFolders);const auto restored=gSongFolders.restoreSaved(gCharts);
            rebuildSongRows(restoreLibraryChart(restored));
            startLibraryValidation();
            std::wstring status = std::to_wstring(ListView_GetItemCount(gList)) + L"곡 · " + std::to_wstring(gCharts.size()) + L"개 채보를 불러왔습니다. 분석 " + number(gLoadResult.scanSeconds, 2) + L"초";
            if (gLoadResult.copiedFiles) status += L" · " + std::to_wstring(gLoadResult.copiedFiles) + L"개 파일을 Songs에 복사했습니다.";
            if (gLoadResult.unchangedFiles) status += L" · 변경 없는 " + std::to_wstring(gLoadResult.unchangedFiles) + L"개 파일은 유지했습니다.";
            if(!gLoadResult.importWarnings.empty())status+=L" · "+std::to_wstring(gLoadResult.importWarnings.size())+L"개 항목 추가 실패: "+gLoadResult.importWarnings.front();
            if(gLoadResult.cachedIndex)status+=L" · 캐시 확인 중";
            setStatus(status);
            startUiEffects();
        }
        finishStartupSplash();
        return 0;
    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (item && item->CtlID >= ID_PAUSE_RESUME && item->CtlID <= ID_PAUSE_PRACTICE) {
            // The parent pause overlay draws both borders and labels on the
            // same surface as the dimmed game, including in GPU mode.
            return TRUE;
        }
        if (item && item->CtlID == ID_RESULT_NEXT) {
            fillRect(item->hDC, item->rcItem, (item->itemState & ODS_SELECTED) ? RGB(32, 75, 102) : RGB(21, 54, 77));
            HBRUSH border = CreateSolidBrush(RGB(90, 205, 245));
            if (border) { FrameRect(item->hDC, &item->rcItem, border); DeleteObject(border); }
            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, RGB(245, 250, 255));
            HGDIOBJ oldFont = SelectObject(item->hDC, gResultNextFont ? gResultNextFont : gFont);
            RECT text = item->rcItem;
            DrawTextW(item->hDC, L"NEXT", -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (oldFont) SelectObject(item->hDC, oldFont);
            if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &item->rcItem);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_OPEN: { gAudio.triggerEffect(L"click"); PostMessageW(window,WM_LIBRARY_ACTION,1,0); return 0; }
        case ID_RESCAN: gAudio.triggerEffect(L"click"); if(!gLoading&&!gPlaying){gSongPreview.clearCache();gBrowseAudioKey.clear();startLibraryValidation();} return 0;
        case ID_PLAY: gAudio.triggerEffect(L"click"); beginSelectedChart(); return 0;
        case ID_PRACTICE_PLAY: gAudio.triggerEffect(L"click"); openPracticeSetup(); return 0;
        case ID_RESULT_NEXT: if (gResultFade.active()) return 0; gAudio.triggerEffect(L"click"); if (gShowingResult) showBrowseView(); return 0;
        case ID_PAUSE_RESUME: resumePlayback(); return 0;
        case ID_PAUSE_RESTART: restartPlayback(); return 0;
        case ID_PAUSE_QUIT: showBrowseView(); return 0;
        case ID_PAUSE_PRACTICE:
            if (gPlaying && gPaused) { showBrowseView(); openPracticeSetup(); }
            return 0;
        case ID_SETTINGS: gAudio.triggerEffect(L"click"); openSettings(); return 0;
        case ID_HIDDEN_SONGS:
            gAudio.triggerEffect(L"click");
            if (gLoading || gPlaying || gShowingResult) return 0;
            gBrowseSelections[gShowHiddenCharts ? 1 : 0] = selectedGroupIndex();
            gShowHiddenCharts = !gShowHiddenCharts;
            rebuildSongRows(gBrowseSelections[gShowHiddenCharts ? 1 : 0]);
            return 0;
        case ID_HELP:
            MessageBoxW(gSettingsWindow ? gSettingsWindow : window, (L"현재 키 배치: " + keySummary() + L"\r\n설정창에서 키 배치, 판정선 위치, 기본 배속, 레인 투명도, 레인 커버, 레인 구분선, BGA 밝기, 기어 위치, DirectComposition, 레인별 노트 색상, 60 / 144 / 240 / 400 FPS 또는 무제한을 변경합니다. 판정선 위치는 미리보기를 보며 조절할 수 있습니다.\r\n\r\n곡 목록의 Space: 설정창\r\nEnter: 선택 곡 시작\r\n재생 중 F5: 즉시 재시작 / Esc: 일시정지 창\r\n일시정지 중 방향키: 메뉴 선택, Enter: 선택, Esc: 재개, R: 재시작, Q: 곡 선택으로\r\n일시정지 중 F1/F2: 노트 배속 낮추기/올리기 (0.50x~10.00x)\r\n일시정지 중 PageDown/PageUp: 레인 가드 낮추기/올리기 (5% 단위)\r\n재생 중 PageDown/PageUp: BGA 밝기 낮추기/올리기 (10단위)\r\n\r\n노트 입력은 ±150ms에서 PERFECT / GREAT / GOOD로 표시하고, 놓친 노트는 MISS로 표시합니다. 실제 화면의 표시 빈도는 Windows 디스플레이 설정의 모니터 주사율에도 영향을 받습니다.").c_str(), L"사용법", MB_ICONINFORMATION | MB_OK);
            return 0;
        }
        break;
    case WM_NOTIFY: {
        auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header->hwndFrom == gList && header->code == LVN_ITEMCHANGED) {
            const auto* change = reinterpret_cast<const NMLISTVIEW*>(lParam);
            if (change->iItem >= 0 && (change->uNewState & LVIS_SELECTED))
                selectSongGroupFromListRow(change->iItem);
            updateSelection();
            InvalidateRect(gList, nullptr, FALSE);
            return 0;
        }
        if (header->hwndFrom == gList && header->code == NM_CLICK) { gAudio.triggerEffect(L"click"); return 0; }
        if (header->hwndFrom == gList && header->code == NM_DBLCLK) { beginSelectedChart(); return 0; }
        if (header->hwndFrom == gList && header->code == NM_RDBLCLK) {
            const int row = reinterpret_cast<NMLISTVIEW*>(lParam)->iItem;
            if (!gLoading && !gPlaying && !gShowingResult && row >= 0 && row < ListView_GetItemCount(gList)) {
                ListView_SetItemState(gList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_SetItemState(gList, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                gAudio.triggerEffect(L"click");
                openPracticeSetup();
            }
            return 0;
        }
        if (header->hwndFrom == gList && header->code == LVN_KEYDOWN) {
            const auto key = reinterpret_cast<NMLVKEYDOWN*>(lParam)->wVKey;
            if((key==VK_BACK||key==VK_ESCAPE)&&navigateSongFolder(true))return 0;
            if (key == VK_UP || key == VK_DOWN) gAudio.triggerEffect(L"songlistscroll");
            if (key == VK_RETURN) { gAudio.triggerEffect(L"click"); if (GetKeyState(VK_SHIFT) & 0x8000) openPracticeSetup(); else beginSelectedChart(); return 0; }
            if (key == VK_LEFT && cycleSelectedVariant(-1)) return 0;
            if (key == VK_RIGHT && cycleSelectedVariant(1)) return 0;
        }
        if (header->hwndFrom == gList && header->code == NM_CUSTOMDRAW) return handleCustomDraw(lParam);
        break;
    }
    case WM_KEYDOWN:
        if(gSongInfoVisible){if(wParam==VK_ESCAPE){gSongInfoVisible=false;ShowWindow(gList,SW_SHOWNA);InvalidateRect(window,nullptr,FALSE);}return 0;}
        if((wParam==VK_BACK||wParam==VK_ESCAPE)&&navigateSongFolder(true))return 0;
        if(summaryInput(message,wParam,lParam,window))return 0;
    if (handleBrowseShortcut(message, wParam, lParam)) return 0;
        if (gLoading) { if (wParam == VK_ESCAPE) { gLoadCancelled = true; setStatus(L"불러오기를 취소하고 있습니다..."); } return 0; }
        if (gCapturingBindings) {
            if (wParam == VK_ESCAPE) { cancelKeyBindingCapture(); return 0; }
            if (gCaptureAwaitRelease || (lParam & (1LL << 30)) != 0) return 0;
            acceptBindingKey(normalizedKey(wParam, lParam));
            return 0;
        }
        if (gPlaying) {
            if(wParam==VK_F5&&!gSettingsWindow){if((lParam&(1LL<<30))==0)restartPlayback();return 0;}
            if (gPaused) { handlePausedKey(wParam, (lParam & (1LL << 30)) != 0); return 0; }
            if (wParam == VK_ESCAPE) { if ((lParam & (1LL << 30)) == 0) pausePlayback(); return 0; }
            if((GetKeyState(VK_CONTROL)&0x8000)&&(wParam==VK_PRIOR||wParam==VK_NEXT||wParam==VK_UP||wParam==VK_DOWN)){
                if(wParam==VK_PRIOR||wParam==VK_NEXT)gLaneCoverPercent=std::clamp(gLaneCoverPercent+(wParam==VK_PRIOR?1:-1),0,100);
                else gJudgeLineOffset=std::clamp(gJudgeLineOffset+(wParam==VK_UP?-1:1),-4096,4096);
                gBgaBackplateDirty=true;saveDisplaySettings();InvalidateRect(window,nullptr,FALSE);return 0;
            }
            if (wParam == VK_RETURN) return 0;
            if (wParam == VK_F1 || wParam == VK_F2) {
                const double delta = (wParam == VK_F2) ? 0.25 : -0.25;
                gNoteSpeed = std::clamp(gNoteSpeed + delta, 0.5, 10.0);
                saveDisplaySettings();
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            if (wParam == VK_PRIOR || wParam == VK_NEXT) {
                gBgaBrightness = std::clamp(gBgaBrightness + (wParam == VK_PRIOR ? 10 : -10), 0, 100);
                gBgaDarkness = 100 - gBgaBrightness;
                gBgaBackplateDirty = true;
                saveDisplaySettings();
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            int lane = keyLane(wParam, lParam);
            if (lane >= 0) { if((lParam&(1LL<<30))!=0)return 0;gKeyboardPressed[lane]=true;pressLane(lane); return 0; }
        } else if (gShowingResult) {
            if (gResultFade.active()) return 0;
            if (wParam == VK_RETURN || wParam == VK_ESCAPE) { showBrowseView(); return 0; }
            return 0;
        } else {
            if (wParam == 'S') {
                if ((lParam & (1LL << 30)) == 0) toggleSongSort();
                return 0;
            }
            if (wParam == VK_LEFT && cycleSelectedVariant(-1)) return 0;
            if (wParam == VK_RIGHT && cycleSelectedVariant(1)) return 0;
            if (wParam == VK_RETURN) { gAudio.triggerEffect(L"click"); if (GetKeyState(VK_SHIFT) & 0x8000) openPracticeSetup(); else beginSelectedChart(); return 0; }
        }
        break;
    case WM_SYSKEYDOWN:
        if (gCapturingBindings) {
            setStatus(L"Alt 키와 Alt 조합은 Windows 메뉴 키이므로 레인에 지정할 수 없습니다.");
            return 0;
        }
        break;
    case WM_KEYUP:
        if(summaryInput(message,wParam,lParam,window))return 0;
    if (handleBrowseShortcut(message, wParam, lParam)) return 0;
        if (gCapturingBindings) {
            gCaptureAwaitRelease = false;
            return 0;
        }
        if (gPlaying) {
            int lane = keyLane(wParam, lParam);
            if (lane >= 0) {
                gKeyboardPressed[lane]=false;
                if(lane>=8||!gControllerPressed[lane])releaseLaneInput(lane);
                return 0;
            }
        }
        break;
    case WM_SETFOCUS:
        if (gPaused && gPauseSelection < gPauseButtons.size() && gPauseButtons[gPauseSelection])
            SetFocus(gPauseButtons[gPauseSelection]);
        else if (!gPlaying && !gShowingResult && (!gLoading || !gLoadResult.playback) && !gSettingsWindow)
            focusBrowseInput();
        break;
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE && !gPlaying && !gShowingResult &&
            (!gLoading || !gLoadResult.playback) && !gSettingsWindow)
            focusBrowseInput();
        break;
    case WM_KILLFOCUS:
        if (gCapturingBindings && !gSettingsWindow) cancelKeyBindingCapture();
        if (gPlaying && !gPaused) pausePlayback(false);
        gKeyboardPressed.fill(false);gControllerPressed.fill(false);gController.mapper.reset();
        for(int lane=0;lane<16;++lane)releaseLaneInput(lane);
        if (gShowingResult && GetFocus() != gResultNext) showBrowseView();
        break;
    case WM_ACCURACY_RESTART:
        if(gAccuracyRestartPending&&wParam==static_cast<WPARAM>(gPlaybackGeneration)){
            gAccuracyRestartPending=false;
            if(gPlaying&&!gPaused&&!gLoading&&gAccuracyLimit!=AccuracyLimit::None)restartPlayback();
        }
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_TIMER:
        if(wParam==ID_CONTROLLER_POLL){pollControllerInput();return 0;}
#ifdef BMS_ENABLE_SKIN_BINDINGS
        if(wParam==ID_SKIN_RETRY){retrySkinSelection();return 0;}
        if(wParam==ID_SKIN_ANIMATION){if(!gPlaying)InvalidateRect(window,nullptr,FALSE);return 0;}
#endif
        if (wParam == ID_RESULT_FADE) {
            if (gResultFade.finished(ResultFade::Clock::now())) {
                resetResultFade();
                if (gShowingResult) SetWindowPos(gResultNext, HWND_TOP, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            }
            InvalidateRect(window, nullptr, FALSE);
            UpdateWindow(window);
            return 0;
        }
        if (wParam == ID_SONG_LIST_ANIMATION) { tickSongListAnimations(); return 0; }
        if (wParam == ID_PAUSE_PRESENT_RETRY) {
            KillTimer(window, ID_PAUSE_PRESENT_RETRY);
            if (gPlaying && gPaused && gCompositionEnabled && !gGpuUnavailable) {
                InvalidateRect(window, nullptr, FALSE);
                UpdateWindow(window);
            }
            return 0;
        }
        break;
    case WM_PAINT: {
        HUD_SCOPE(WindowPaint);
#ifdef BMS_HUD_FRAME_PROBE
        if(gLatencyInputPump)HUD_COUNT(InputPaint);if(gPaused)HUD_COUNT(PausedPaint);if(GetForegroundWindow()!=gWindow)HUD_COUNT(UnfocusedPaint);
#endif
        if (gPlaying && !gPaused && gLatencyInputPump) {
            HUD_COUNT(DeferredInputPaint);
            PAINTSTRUCT deferred{}; BeginPaint(window, &deferred); EndPaint(window, &deferred);
            return 0; // The simulation update below re-invalidates after input sampling.
        }
        if (!gPlaying && !gShowingResult && gRmlSongList.enabled() && gList)
            InvalidateRect(gList, nullptr, FALSE);
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(window, &ps);
        RECT client{};
        GetClientRect(window, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
#ifdef BMS_ENABLE_SKIN_BINDINGS
        if(gSkinRetryPending||!gSkinApplication.active()){gCompositionPresenter.reset();fillRect(dc,client,RGB(0,0,0));skinVisibility(false);EndPaint(window,&ps);return 0;}
#endif
        ensureBackBuffer(dc, width, height);
        if (gBackBufferDc && gBackBufferBitmap) {
            gGpuFrame = gPlaying && gCompositionEnabled && !gGpuUnavailable;
            gCompositionPresenter.setDirectComposition(gDirectCompositionEnabled);
            gCompositionPresenter.setVsync(gTargetFps == vsyncFps);
            gCompositionPresenter.setAntialias(gNoteAntialias);
            if (gGpuFrame && !gCompositionPresenter.prepare(window, width, height)) { gGpuFrame = false; gGpuUnavailable = true; }
            if (gGpuFrame) {
                const int limit = gTargetFps > 0 ? gTargetFps : unlimitedFpsLimit;
                gCompositionPresenter.lowLatency().request(gReflexRequested && !gPaused ? latency::Mode::Reflex : latency::Mode::Off,
                    static_cast<uint32_t>((1'000'000 + limit - 1) / limit));
            }
            if (gGpuFrame && !shouldRenderGpuFrame()) {
                gGpuFrame = false;
                EndPaint(window, &ps);
                return 0;
            }
            if (hybridRendererEnabled && gGpuFrame && gTargetFps == vsyncFps && !gCompositionPresenter.canBeginHybridFrame()) {
                // Keep audio/input/judgement updates running, but avoid painting
                // CPU gear/HUD frames the compositor cannot accept yet.
                BMS_PERF_SET(presentDeferred, true);
                if (gPaused) SetTimer(window, ID_PAUSE_PRESENT_RETRY, 24, nullptr);
                gGpuFrame = false;
                EndPaint(window, &ps);
                return 0;
            }
            {HUD_SCOPE(ResourceStats);recordResourceFrame(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());}
            if (!HUD_LEGACY_REFLEX()&&gPlaying && !gPaused && gGpuFrame) gCompositionPresenter.lowLatency().renderStart();
#ifdef BMS_ENABLE_SKIN_BINDINGS
            prepareAppliedSkinGpu();
#endif
            if (HUD_LEGACY_REFLEX()&&gPlaying&&!gPaused&&gGpuFrame) gCompositionPresenter.lowLatency().renderStart();
            if (gPlaying) drawGameScreen(gBackBufferDc);
            else if (gShowingResult) drawResultScreen(gBackBufferDc);
            else if (gLoading && gLoadResult.playback) drawPreparingPlayback(gBackBufferDc);
            else if (gStartupSplash) drawStartupSplash(gBackBufferDc);
            else drawBrowseScreen(gBackBufferDc);
            if (!gStartupSplash&&!songEntryActive()) drawResourceUsageOverlay(gBackBufferDc);
            if (gShowingResult) drawResultFade(gBackBufferDc);
            drawSummary(gBackBufferDc);
            {HUD_SCOPE(FinalGdiFlush);GdiFlush();} // Complete GDI writes before the D3D texture upload reads the DIB.
            bool pauseFrameDeferred = false;
            { BMS_PERF_SCOPE(presentMs);
                const bool presented = gGpuFrame && gCompositionPresenter.present(window, gBackBufferPixels, width, height);
                BMS_PERF_SET(presentDeferred, presented && gCompositionPresenter.presentationDeferred());
                pauseFrameDeferred = presented && gPaused && gCompositionPresenter.presentationDeferred();
                if (!presented) {
                    if (gGpuFrame) {
                        gGpuFrame = false; gGpuUnavailable = true;
                        drawGameScreen(gBackBufferDc); GdiFlush();
                    }
                    gCompositionPresenter.reset();
                    BitBlt(dc, 0, 0, width, height, gBackBufferDc, 0, 0, SRCCOPY);
                    if (gPlaying && !gPaused && gTargetFps == vsyncFps) {
                        GdiFlush();
                        DwmFlush();
                    }
                }
            }
            if (pauseFrameDeferred) SetTimer(window, ID_PAUSE_PRESENT_RETRY, 24, nullptr);
            else KillTimer(window, ID_PAUSE_PRESENT_RETRY);
            gGpuFrame = false;
        } else {
            if (gPlaying) drawGameScreen(dc);
            else if (gShowingResult) { drawResultScreen(dc); drawResultFade(dc); }
            else if (gLoading && gLoadResult.playback) drawPreparingPlayback(dc);
            else if (gStartupSplash) drawStartupSplash(dc);
            else drawBrowseScreen(dc);
            drawSummary(dc);
        }
        EndPaint(window, &ps); return 0;
    }
    case WM_CLOSE:
        if (!gPlaying && MessageBoxW(window, L"BMS Player를 종료하시겠습니까?", L"종료 확인", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES) return 0;
        if (gLoading) gLoadCancelled = true;
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        saveLibraryNavigation();gSongIndexWorker.cancel();
        KillTimer(window,ID_CONTROLLER_POLL);gController.reconnect(nullptr);
#ifdef BMS_ENABLE_SKIN_BINDINGS
        unloadSkinSelection();
#endif
        settingsController().clearPending();
        gRmlSettingsPage.close();
        gRmlSettingsHost.close();
        gRmlSongList.reset();
        gStartupSplashImage.reset();
        BufferedPaintUnInit();
        resetResultFade();
        gLoadCancelled = true;gPlaybackLoad.cancel();
        gPauseButtons.fill(nullptr);
        if (gLoadWorker.joinable()) gLoadWorker.join();
        if(gPlaybackJob.active){samplePlaybackLoadMemory();reportPlaybackLoad("shutdown");gPlaybackJob.releaseChart();gPlaybackJob.active=false;}
        gCompositionPresenter.reset();
        gAudio.stop();
        gSongPreview.shutdown();
        gSelectionAudio.shutdown();
        gBrowseBackdrop.reset();
        gVideoBga.reset();
    gBgaVideos.reset();gBgaSwitch.reset();
        restoreFullscreenDisplay();
        gTextRaster.clear(); gHudTextRaster.clear(); gHudMaskRaster.clear();
        gResourceTextRaster.clear();
        if (gFont) DeleteObject(gFont);
        if (gSmallFont) DeleteObject(gSmallFont);
        if (gPracticeHudFont) DeleteObject(gPracticeHudFont);
        if (gListFont) DeleteObject(gListFont);
        if (gListSelectedFont) DeleteObject(gListSelectedFont);
        if (gListLevelFont) DeleteObject(gListLevelFont);
        if (gListSelectedLevelFont) DeleteObject(gListSelectedLevelFont);
        if (gBrowseTitleFont) DeleteObject(gBrowseTitleFont);
        if (gBrowseSongTitleFont) DeleteObject(gBrowseSongTitleFont);
        if (gBrowseSongInfoFont) DeleteObject(gBrowseSongInfoFont);
        if (gBrowseLevelFont) DeleteObject(gBrowseLevelFont);
        if (gBrowseRankFont) DeleteObject(gBrowseRankFont);
        if (gBrowseMetadataFont) DeleteObject(gBrowseMetadataFont);
        if (gSongRowImages) ImageList_Destroy(gSongRowImages);
        if (gHudBoldFont) DeleteObject(gHudBoldFont);
        if (gGaugeModeFont) DeleteObject(gGaugeModeFont);
        if (gComboFont) DeleteObject(gComboFont);
        if (gJudgementFont) DeleteObject(gJudgementFont);
        gEmbeddedHudFont.reset();
        if (gResultRankFont) DeleteObject(gResultRankFont);
        if (gResultScoreFont) DeleteObject(gResultScoreFont);
        if (gResultTitleFont) DeleteObject(gResultTitleFont);
        if (gResultDetailsFont) DeleteObject(gResultDetailsFont);
        if (gResultNextFont) DeleteObject(gResultNextFont);
        releaseSettingsPreviewResources();
        if (gControlBrush) DeleteObject(gControlBrush);
        if (gBackBufferDc && gBackBufferOriginalBitmap) SelectObject(gBackBufferDc, gBackBufferOriginalBitmap);
        if (gBackBufferBitmap) DeleteObject(gBackBufferBitmap);
        if (gBackBufferDc) DeleteDC(gBackBufferDc);
        if (gResultBackdropDc && gResultBackdropOriginalBitmap) SelectObject(gResultBackdropDc, gResultBackdropOriginalBitmap);
        if (gResultBackdropBitmap) DeleteObject(gResultBackdropBitmap);
        if (gResultBackdropDc) DeleteDC(gResultBackdropDc);
        if (gBgaBackplateDc && gBgaBackplateOriginalBitmap) SelectObject(gBgaBackplateDc, gBgaBackplateOriginalBitmap);
        if (gBgaBackplateBitmap) DeleteObject(gBgaBackplateBitmap);
        if (gBgaBackplateDc) DeleteDC(gBgaBackplateDc);
        for (const auto& [color, brush] : gBrushCache) { (void)color; if (brush) DeleteObject(brush); }
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool loadRasterPngFile(const std::filesystem::path& path, TurntableImage& image, UINT expectedWidth = 0, UINT expectedHeight = 0) {
    Gdiplus::Bitmap bitmap(path.c_str());
    const UINT width = bitmap.GetWidth(), height = bitmap.GetHeight();
    if (bitmap.GetLastStatus() != Gdiplus::Ok || width == 0 || height == 0 || width > 4096 || height > 4096 ||
        (expectedWidth && width != expectedWidth) || (expectedHeight && height != expectedHeight)) return false;
    Gdiplus::Rect bounds(0, 0, static_cast<INT>(width), static_cast<INT>(height));
    Gdiplus::BitmapData data{};
    if (bitmap.LockBits(&bounds, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) return false;
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height);
    const auto* bytes = static_cast<const uint8_t*>(data.Scan0);
    for (UINT y = 0; y < height; ++y) {
        const auto* row = reinterpret_cast<const uint32_t*>(bytes + static_cast<ptrdiff_t>(y) * data.Stride);
        std::copy_n(row, width, pixels.data() + static_cast<size_t>(y) * width);
    }
    bitmap.UnlockBits(&data);
    return image.setPixels(static_cast<int>(width), static_cast<int>(height), pixels.data());
}

bool loadTurntableImageFile(const std::filesystem::path& path, TurntableImage& image) {
    return loadRasterPngFile(path, image, 64, 64);
}

bool writeUtf8(FILE* stream, const std::wstring& text) {
    if (text.empty()) return true;
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return false;
    std::string utf8(static_cast<size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
        utf8.data(), length, nullptr, nullptr) != length) return false;
    _setmode(_fileno(stream), _O_BINARY);
    return std::fwrite(utf8.data(), 1, utf8.size(), stream) == utf8.size() && std::fflush(stream) == 0;
}

int runCatalogCommand(int argumentCount, wchar_t** arguments) {
    if (argumentCount < 3) {
        writeUtf8(stderr, L"사용법: BmsPlayer.exe --catalog <곡 폴더> [--seed <정수>]\n");
        return 2;
    }

    const std::filesystem::path folder(arguments[2]);
    uint64_t seed{};
    for (int index = 3; index < argumentCount; ++index) {
        if (std::wcscmp(arguments[index], L"--seed") != 0 || index + 1 >= argumentCount) {
            writeUtf8(stderr, L"알 수 없는 카탈로그 옵션입니다.\n");
            return 2;
        }
        wchar_t* end{};
        errno = 0;
        seed = static_cast<uint64_t>(std::wcstoull(arguments[++index], &end, 10));
        if (errno == ERANGE || !end || end == arguments[index] || *end != L'\0') {
            writeUtf8(stderr, L"--seed 값이 올바른 정수가 아닙니다.\n");
            return 2;
        }
    }

    try {
        const auto charts = bms::scanSongFolder(folder, nullptr, {}, seed);
        const auto json = bms::serializeSongCatalog(charts, folder, seed);
        _setmode(_fileno(stdout), _O_BINARY);
        if (std::fwrite(json.data(), 1, json.size(), stdout) != json.size() || std::fputc('\n', stdout) == EOF ||
            std::fflush(stdout) != 0) return 3;
        return 0;
    } catch (const bms::LibraryError& error) {
        writeUtf8(stderr, error.message + L"\n");
        return 1;
    } catch (...) {
        writeUtf8(stderr, L"곡 카탈로그를 생성하지 못했습니다.\n");
        return 1;
    }
}

} // namespace

#ifdef BMS_ENABLE_SKIN_BINDINGS
namespace { const DWORD gSkinDataOwnerThread=GetCurrentThreadId(); }
bool skinOwnsHud(){return gSkinApplication.active()&&!gSkinRetryPending;}
int selectSkinShortcutAt(int x,int y){return gSkinApplication.selectShortcutAt(x,y,gWindowWidth,gWindowHeight);}
bool skinNeedsJudgementText(){return gSkinApplication.needsJudgementText();}
bool skinCoversLegacySlots(int left,int top,int right,int bottom){return gSkinApplication.coversLegacySlots(left,top,right,bottom);}
void prepareSkinForPlayback(){
    if((!gPlaying&&!gPlaybackLoad.preparing())||(!gPlaybackLoad.preparing()&&!gSkinApplication.hasImageHud())||!gCompositionEnabled||gGpuUnavailable)return;
    gCompositionPresenter.setDirectComposition(gDirectCompositionEnabled);gCompositionPresenter.setVsync(gTargetFps==vsyncFps);gCompositionPresenter.setAntialias(gNoteAntialias);
    const bool previous=gGpuFrame;
    if(gCompositionPresenter.prepare(gWindow,gWindowWidth,gWindowHeight)){gGpuFrame=true;prepareAppliedSkinGpu();}
    else gGpuUnavailable=true;
    gGpuFrame=previous;
}
bool drawPreparingSkin(HDC dc){
    if(!gSkinApplication.active()||!gSkinApplication.supports(sevenmania::skin::ScreenKind::Play))return false;
    RECT bounds{0,0,gWindowWidth,gWindowHeight};FillRect(dc,&bounds,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    updateSkinJudgementAnchor();
    return captureSevenManiaSkinData(gSkinData)&&gSkinApplication.drawDc(dc,gWindowWidth,gWindowHeight,gSkinData.snapshot());
}
bool preparingSkinAnimated(){return gSkinApplication.animating()||gSkinApplication.scrolling();}
bool skinOwnsJudgement(){return skinOwnsHud()&&gSkinApplication.ownsJudgement();}
void emitSevenManiaSkinEvent(std::string_view event) {gSkinApplication.emit(event);}
bool captureSevenManiaSkinData(sevenmania::skin::SkinDataProvider& provider) {
    HUD_SCOPE(Snapshot);
    if(GetCurrentThreadId()!=gSkinDataOwnerThread)return false;
    using namespace sevenmania::skin;
    const auto screen=(gPlaying||gPlaybackLoad.preparing())?ScreenKind::Play:gShowingResult?ScreenKind::Result:ScreenKind::Select;
    provider.beginFrame(screen);
    if(screen==ScreenKind::Select){
        constexpr std::array<const wchar_t*,4> judgementNames{L"BASIC",L"EASY",L"NORMAL",L"HARD"};
        provider.setWide(BindingId::SelectSpeedText,number(gNoteSpeed,2)+L"x");
        provider.setWide(BindingId::SelectJudgementText,judgementNames[static_cast<size_t>(gJudgementProfile)]);
        provider.setWide(BindingId::SelectGaugeMode,activeGaugeModeText());
        const int selected=selectedIndex();
        const PlayRecord* record=selected>=0&&selected<static_cast<int>(gCharts.size())?playRecordForChart(static_cast<size_t>(selected)):nullptr;
        provider.setWide(BindingId::SelectRecordBestScore,record?L"BEST "+groupedRecordScore(record->bestAccuracyScore):L"NO RECORD");
        provider.setWide(BindingId::SelectRecordBestRank,record?L"RANK "+std::wstring(ResultScore::rankForScore(record->bestAccuracyScore)):L"");
        provider.setWide(BindingId::SelectRecordClearType,record?(record->cleared?L"CLEAR":L"FAILED"):L"");
        provider.setWide(BindingId::SelectRecordMaxCombo,record?L"MAX COMBO "+std::to_wstring(record->maxCombo):L"");
    }
    if(screen==ScreenKind::Play){constexpr std::array<const wchar_t*,4> names{L"BASIC",L"EASY",L"NORMAL",L"HARD"};provider.setWide(BindingId::PlayJudgementProfile,names[static_cast<size_t>(gJudgementProfile)]);}
    const int index=screen==ScreenKind::Select?selectedIndex():gActiveChartIndex;
    if((!gLoading||gPlaybackLoad.preparing())&&!gStartupSplash&&index>=0&&index<static_cast<int>(gCharts.size())) {
        const auto& chart=gCharts[static_cast<std::size_t>(index)];
        provider.setWide(BindingId::SongTitle,chart.title);
        provider.setWide(BindingId::SongArtist,chart.artist);
        provider.setWide(BindingId::SongGenre,chart.genre.empty()?L"장르 미표기":chart.genre);
        provider.setWide(BindingId::ChartLevelText,chart.playLevel);
        if(screen==ScreenKind::Select)provider.set(BindingId::ChartDifficultyColor,static_cast<std::int64_t>(songDifficultyColor(chart)));
        if(const auto level=bms::numericPlayLevel(chart))provider.set(BindingId::ChartLevel,*level);
        double bpm=chart.initialBpm;
        if(screen==ScreenKind::Play) {
            const bms::TempoEvent* tempo=chart.tempos.empty()?nullptr:&chart.tempos.front();
            for(const auto& item:chart.tempos){if(item.timeSeconds<=gPlayhead)tempo=&item;else break;}
            if(tempo)bpm=tempo->bpm;
        }
        provider.set(BindingId::SongBpm,bpm);
        if(screen==ScreenKind::Play) {
            provider.set(BindingId::PlayCombo,static_cast<std::int64_t>(gFeedback.combo()));
            provider.set(BindingId::PlayScore,static_cast<std::int64_t>(gResultScore.value()));
            provider.set(BindingId::PlayGauge,gGrooveGauge.value());
            provider.setWide(BindingId::PlayGaugeMode,activeGaugeModeText());
            provider.set(BindingId::PlayPaused,gPaused);
            if(!gChartOptions.crazy.contains(L"hidjudge")&&!gJudgementText.empty()&&gSkinJudgementGrade>=0){
                provider.set(BindingId::PlayJudgementGrade,static_cast<std::int64_t>(gSkinJudgementGrade));
                provider.set(BindingId::PlayJudgementAge,std::chrono::duration<double>(std::chrono::steady_clock::now()-gSkinJudgementAt).count());}
            if(skinNeedsJudgementText()&&!gChartOptions.crazy.contains(L"hidjudge") && !gJudgementText.empty() && std::chrono::steady_clock::now()<gJudgementUntil){provider.setWide(BindingId::PlayJudgement,gJudgementText);provider.setWide(BindingId::PlayJudgementCombo,combinedJudgementText(gJudgementText,gFeedback.combo()));}
        } else if(screen==ScreenKind::Result) {
            provider.set(BindingId::ResultScore,static_cast<std::int64_t>(gResultScore.value()));
            provider.setWide(BindingId::ResultRank,gResultScore.rank());
            provider.setWide(BindingId::ResultClearType,gPractice.enabled?L"PRACTICE":gResultGaugeFailed?L"FAILED":gResultGaugeCleared?L"CLEAR":L"NO CLEAR");
            provider.set(BindingId::ResultMaxCombo,static_cast<std::int64_t>(gFeedback.maxCombo()));
            provider.set(BindingId::ResultCleared,gResultGaugeCleared);
        }
    }
    provider.endFrame();
    return true;
}
#endif

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    int argumentCount{};
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments && argumentCount > 1 && std::wcscmp(arguments[1], L"--catalog") == 0) {
        const int result = runCatalogCommand(argumentCount, arguments);
        LocalFree(arguments);
        return result;
    }
    if (arguments) LocalFree(arguments);

    initializeSettingsPath();
    sevenmania::SettingsGate settingsGate(gSettingsFile);
    if(!settingsGate.acquire()){MessageBoxW(nullptr,L"게임 또는 런처의 설정/캐시 작업이 실행 중입니다.",L"SevenMania",MB_ICONINFORMATION);return 0;}
    gLibrarySources.initialize(executableFolder()/L"Songs",gSettingsFile);
    gPlayRecordsFile = gSettingsFile.empty() ? executableFolder() / L"BmsPlayer-records.ini" :
        gSettingsFile.parent_path() / L"BmsPlayer-records.ini";
#ifdef BMS_FRAME_DIAGNOSTICS
    gPlayRecordsFile=gSettingsFile.parent_path()/L"BmsPlayer-Test-records.ini";
#endif
    loadPlayRecords();
    gSystemResources.initialize();
    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&gGdiPlusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) gGdiPlusToken = 0;
    if (gGdiPlusToken) {
        std::array<wchar_t, 32768> executablePath{};
        const DWORD pathLength = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
        if (pathLength > 0 && pathLength < executablePath.size()) {
            const auto assetPath = std::filesystem::path(executablePath.data()).parent_path() / L"Assets" / L"turntable.png";
            loadTurntableImageFile(assetPath, gTurntableImage);
            const auto blackKeyPath = std::filesystem::path(executablePath.data()).parent_path() / L"Assets" / L"blackbutton.png";
            loadRasterPngFile(blackKeyPath, gBlackKeyImage);
            const auto whiteKeyPath = std::filesystem::path(executablePath.data()).parent_path() / L"Assets" / L"whitebutton.png";
            loadRasterPngFile(whiteKeyPath, gWhiteKeyImage);
            const auto hudSlotPath = std::filesystem::path(executablePath.data()).parent_path() / L"Assets" / L"hud-icon-slot.png";
            loadRasterPngFile(hudSlotPath, gHudSlotImage);
            loadRasterPngFile(hudSlotPath.parent_path()/L"hud-hi-speed.png",gHiSpeedSlotImage);
            loadRasterPngFile(hudSlotPath.parent_path()/L"hud-fast.png",gFastHintImage);
            loadRasterPngFile(hudSlotPath.parent_path()/L"hud-slow.png",gSlowHintImage);
            loadRasterPngFile(hudSlotPath.parent_path()/L"hud-gauge-mode.png",gGaugeSlotImage);
            const auto hitSplashPath = std::filesystem::path(executablePath.data()).parent_path() / L"Assets" / L"hit-splash.png";
            if (loadRasterPngFile(hitSplashPath, gHitSplashSheet))
                gFeedback.setSpriteSheet(gHitSplashSheet.width(), gHitSplashSheet.height(), gHitSplashSheet.pixels());
        }
    }
    gTimerResolutionRequested = timeBeginPeriod(1) == TIMERR_NOERROR;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HRESULT mf = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(mf)) {
        MessageBoxW(nullptr, L"Windows Media Foundation을 초기화하지 못했습니다.", L"BMS Player", MB_ICONERROR | MB_OK);
        if (gTimerResolutionRequested) timeEndPeriod(1);
        CoUninitialize();
        return 1;
    }
    const auto effectAssets = executableFolder() / L"Assets";
    const auto skinSounds = defaultSkinPath().parent_path() / L"sounds";
    const auto resolveUiSound = [&](const wchar_t* name) {
        const auto skinSound = skinSounds / name;
        std::error_code ec;
        return std::filesystem::is_regular_file(skinSound, ec) ? skinSound : effectAssets / name;
    };
    std::wstring effectLoadError;
    gAudio.loadEffect(L"levelchange", effectAssets / L"levelchange.mp3", effectLoadError);
    effectLoadError.clear();
    gAudio.loadEffect(L"click", resolveUiSound(L"click.mp3"), effectLoadError);
    gAudio.loadEffect(L"songlistscroll", resolveUiSound(L"songlistscroll.mp3"), effectLoadError);
    gSelectionAudio.initialize(effectAssets, skinSounds);
#ifdef BMS_FRAME_DIAGNOSTICS
    gAudio.loadDiagnosticBeep();
#endif
    INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&controls);
    WNDCLASSEXW cls{ sizeof(cls) };
    cls.lpfnWndProc = windowProc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    cls.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, 16, 16, 0));
    cls.hbrBackground = CreateSolidBrush(RGB(16, 20, 28));
    cls.lpszClassName = L"BmsPlayerNativeWindow";
    RegisterClassExW(&cls);
    WNDCLASSEXW settingsClass = cls;
    settingsClass.lpfnWndProc = settingsProc;
    settingsClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    settingsClass.lpszClassName = L"BmsPlayerSettingsWindow";
    RegisterClassExW(&settingsClass);
    WNDCLASSEXW settingsBodyClass = settingsClass;
    settingsBodyClass.lpfnWndProc = settingsBodyProc;
    settingsBodyClass.lpszClassName = L"BmsPlayerSettingsBody";
    RegisterClassExW(&settingsBodyClass);
    WNDCLASSEXW previewClass = settingsClass;
    previewClass.lpfnWndProc = settingsPreviewOverlayProc;
    previewClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    previewClass.lpszClassName = L"BmsPlayerSettingsPreviewOverlay";
    RegisterClassExW(&previewClass);
    gStartupSplash = true;
    gStartupProgress = 0;
    gStartupSplashImage.reset(Gdiplus::Bitmap::FromFile((executableFolder() / L"Assets/startup-splash.png").c_str()));
    if (gStartupSplashImage && gStartupSplashImage->GetLastStatus() != Gdiplus::Ok) gStartupSplashImage.reset();
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"BMS Player — 7+1 Key", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 860, nullptr, nullptr, instance, nullptr);
#ifdef BMS_FRAME_DIAGNOSTICS
    if(window)SetWindowTextW(window,L"BMS Player — TEST / FPS Diagnostics");
#endif
    if (!window) {
        MFShutdown(); if (gTimerResolutionRequested) timeEndPeriod(1); CoUninitialize(); return 1;
    }
    setStatus(L"곡 라이브러리를 준비하고 있습니다...");
    sevenmania::placeConfiguredMonitor(window,gSettingsFile);
    ShowWindow(window, show);
    applyScreenMode(true);
    UpdateWindow(window);
    scanFolders({}); // Only explicitly registered sources; LastFolder is not a source.
    if (!gLoading) finishStartupSplash();
    MSG message{};
    bool running = true;
    while (running) {
        auto& latencyControl = gCompositionPresenter.lowLatency();
        bool latencyFrame = false;
        if (gPlaying && !gPaused && gCompositionEnabled && !gGpuUnavailable &&
            (latencyControl.active() == latency::Mode::Reflex || gFramePacer.due())) {
            BMS_PERF_SCOPE(latencyWaitMs);
            latencyFrame = latencyControl.beginFrame();
        }
        gSelectionAudio.tickDecision();
        gLatencyInputPump = latencyFrame;
        {HUD_SCOPE_IF(InputPump,latencyFrame);
        // Bound message processing so continuous input cannot starve a due frame.
        for (int processed = 0; processed < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++processed) {
            if (message.message == WM_QUIT) { running = false; break; }
            if(!gSettingsWindow&&!gPracticeWindow&&handleOverlayEscape(message.message,message.wParam))continue;
            if (handleSettingsCaptureMessage(message.message,message.wParam,message.lParam)) continue;
            if (gRmlSettingsHost.owns(message.hwnd) || gRmlSettingsPage.owns(message.hwnd)) {
                TranslateMessage(&message); DispatchMessageW(&message); continue;
            }
            if (gSettingsWindow && !gCapturingBindings && message.message == WM_KEYDOWN && message.wParam == VK_F9) {
                SendMessageW(gSettingsWindow, WM_KEYDOWN, VK_F9, message.lParam); continue;
            }
            if (gPracticeWindow && (message.message == WM_KEYDOWN || message.message == WM_KEYUP)) {
                if (message.message == WM_KEYDOWN) SendMessageW(gPracticeWindow, WM_KEYDOWN, message.wParam, message.lParam);
                continue;
            }
            if(gLoading&&gPlaybackLoad.preparing()&&message.message==WM_KEYDOWN&&previewKey(message.wParam))continue;
            if (gLoading && message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
                gLoadCancelled = true; setStatus(L"불러오기를 취소하고 있습니다..."); continue;
            }
            if(gPlaying&&!gLoading&&!gSettingsWindow&&message.message==WM_KEYDOWN&&message.wParam==VK_F5){
                if((message.lParam&(1LL<<30))==0)restartPlayback();
                continue;
            }
            if(gPlaying && !gPaused && message.message==WM_KEYDOWN && gChartOptions.manualHiSpeed &&
               (message.wParam==VK_F1 || message.wParam==VK_F2)) {
                gChartOptions.hiSpeed=std::clamp((gChartOptions.hiSpeed>0?gChartOptions.hiSpeed:gNoteSpeed)+(message.wParam==VK_F2?.25:-.25),.25,100.0);
                continue;
            }
            if(gPlaying && !gPaused && message.message==WM_KEYDOWN && gChartOptions.hidden==L"m-sudden" &&
               (message.wParam==VK_F3 || message.wParam==VK_F4)) {
                gAuthoredCoverPercent=std::clamp(gAuthoredCoverPercent+(message.wParam==VK_F3?5:-5),0,100);
                continue;
            }
            if (gPaused && message.message == WM_KEYDOWN) {
                handlePausedKey(message.wParam, (message.lParam & (1LL << 30)) != 0);
                continue;
            }
            if (gPaused && message.message == WM_KEYUP) {
                SendMessageW(gWindow, message.message, message.wParam, message.lParam);
                continue;
            }
            // Browse shortcuts also work while the list or hidden-files control owns focus.
            if (gShowingResult && !gSettingsWindow && message.message == WM_KEYDOWN &&
                (message.wParam == VK_RETURN || message.wParam == VK_ESCAPE)) {
                if (!gResultFade.active()) showBrowseView();
                continue;
            }
            if (!gPlaying && !gShowingResult && !gSettingsWindow && !gPracticeWindow && !gLoading &&
                message.message == WM_KEYDOWN && message.wParam == VK_F1) {
                if ((message.lParam & (1LL << 30)) == 0) showSelectedChartErrors();
                continue;
            }
            if (!gPlaying && !gShowingResult && !gSettingsWindow && !gPracticeWindow && !gLoading &&
                message.message == WM_KEYDOWN && message.wParam == VK_RETURN) {
                if ((message.lParam & (1LL << 30)) == 0) {
                    gAudio.triggerEffect(L"click");
                    if (GetKeyState(VK_SHIFT) & 0x8000) openPracticeSetup();
                    else beginSelectedChart();
                }
                continue;
            }
            if (handleBrowseShortcut(message.message, message.wParam, message.lParam)) continue;
            if (!gPlaying && !gShowingResult && !gSettingsWindow && message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
                if (!gLoading && (message.lParam & (1LL << 30)) == 0) {
                    if (MessageBoxW(gWindow, L"BMS Player를 종료하시겠습니까?", L"종료 확인", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) == IDYES)
                        DestroyWindow(gWindow);
                }
                continue;
            }
            if (!gPlaying && !gShowingResult && !gSettingsWindow && message.message == WM_KEYUP && message.wParam == VK_ESCAPE) continue;
            if (gSettingsWindow && !gCapturingBindings && IsDialogMessageW(gSettingsWindow, &message)) continue;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        }
        gLatencyInputPump = false;
        if (!running) { latencyControl.endFrame(); break; }
        if (!gPlaying || gPaused) latencyControl.endFrame();
        if (gOutputWakeReady) {
            gOutputWakeReady = false;
            // A due simulation/Reflex frame will paint the freshest state and
            // consume the reserved permit itself. Never invoke SDK Sleep twice.
            if (!latencyFrame && (gPaused || !gFramePacer.due())) retryPendingGpuOutput();
        }
        if (!gPlaying && !gShowingResult && !gLoading) {
            tickBrowseMedia();
            if (gBrowseBackgroundMode == 1 || gBrowseTextOverflow || gRmlSongList.needsAnimation()) {
                InvalidateRect(gWindow, nullptr, FALSE); UpdateWindow(gWindow);
            }
        } else {gSelectionAudio.stopMusic();if(!gBrowseMediaKey.empty())stopBrowseMedia();}
        if (gPlaying && gPaused) {
            const DWORD waitResult = waitForPlaybackWork(nullptr, gShowResourceUsage ? 500 : INFINITE);
            if (gShowResourceUsage && waitResult == WAIT_TIMEOUT) {
                gSystemResources.sample();
                InvalidateRect(gWindow, nullptr, FALSE);
                UpdateWindow(gWindow);
            }
            continue;
        }
        if (gPlaying) {
            if (latencyFrame || gFramePacer.due()) {
                if (gShowResourceUsage || gCpuLimitPercent < 100) gSystemResources.sample();
                const int cpuWait = gSystemResources.cpuThrottleWaitMilliseconds(gCpuLimitPercent, gTargetFps);
                if (cpuWait > 0)
                    MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(cpuWait), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                updatePlaybackFrame();
                latencyControl.endFrame();
                gFramePacer.advance();
                continue;
            }
            if (gFramePacer.pollBeforeDeadline()) {
                if (const HANDLE output = pendingGpuOutputHandle(); output && WaitForSingleObject(output, 0) == WAIT_OBJECT_0) {
                    gOutputWakeReady = gCompositionPresenter.acceptPresentationWake(output);
                    continue;
                }
                YieldProcessor();
                continue;
            }
            HANDLE timer = gFramePacer.arm();
            waitForPlaybackWork(timer, timer ? INFINITE : 1);
        } else if (gLoading && gLoadResult.playback) {
            pollPreparingPlayback();
            InvalidateRect(gWindow,nullptr,FALSE);UpdateWindow(gWindow);
            MsgWaitForMultipleObjectsEx(0,nullptr,16,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        } else {
            const bool hasPeriodicBrowseWork = gBrowseBackgroundMode == 1 || gSongPreviewEnabled ||
                gBrowseTextOverflow || gRmlSongList.needsAnimation();
            const DWORD waitMs = hasPeriodicBrowseWork ? 33 : gShowResourceUsage ? 500 : INFINITE;
            const DWORD waitResult = MsgWaitForMultipleObjectsEx(0, nullptr, waitMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (gShowResourceUsage && !hasPeriodicBrowseWork && waitResult == WAIT_TIMEOUT) {
                gSystemResources.sample();
                InvalidateRect(gWindow, nullptr, FALSE);
                UpdateWindow(gWindow);
            }
        }
    }
    DeleteObject(cls.hbrBackground);
    gBgaRaster.reset();
    if (gGdiPlusToken) Gdiplus::GdiplusShutdown(gGdiPlusToken);
    MFShutdown();
    if (gTimerResolutionRequested) timeEndPeriod(1);
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
