#include "AudioEngine.h"
#include "PlaybackLoadState.h"
#include "PlaybackLoadJob.h"
#include <psapi.h>
#include "ControllerDialog.h"
#ifdef BMS_ENABLE_SKIN_BINDINGS
#include "skin/SevenManiaSkinData.h"
#include "skin/SkinApplication.h"
#include "skin/SkinAnchors.h"
void emitSevenManiaSkinEvent(std::string_view event);
void setSevenManiaSkinAnimationPaused(bool paused);
bool skinOwnsHud();
bool skinOwnsJudgement();
int selectSkinShortcutAt(int x,int y);
bool skinNeedsJudgementText();
void prepareSkinForPlayback();
bool preparingSkinAnimated();
bool drawPreparingSkin(HDC);
bool skinCoversLegacySlots(int left,int top,int right,int bottom);
bool skinDrawsKeyGuides(int gearMode);
bool skinGearForMode(int gearMode);
#endif
#include "BmsParser.h"
#include "GameplayModePolicy.h"
#include "KeyModeFilter.h"
#include "TargetScore.h"
#include "LaneOption.h"
#include "DifficultyTableFetch.h"
#include "KeyProfiles.h"
#include "KeyLaneMap.h"
#include "SongCatalog.h"
#include "ChartRules.h"
#include "ResolutionScale.h"
#include "ChartOptions.h"
#include "NoteEffects.h"
#include "skin/NoteImages.h"
#include "SkinHitEffectFiles.h"
#include "SkinRasterImage.h"
#include "ScrollTimeline.h"
#include "BgaTimeline.h"
#include "ChartOrdering.h"
#include "SongLibrary.h"
#include "SharedStartupSettings.h"
#include "SongImportDialog.h"
#include "SystemSoundSet.h"
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
#include "HiSpeedFix.h"
#include "SelectFrameClock.h"
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
#include "TimingOffsets.h"
#include "TimingCalibration.h"
#include "UserBackground.h"
#include "PlayFeedback.h"
#include "TextPopAnimation.h"
#include "TextRasterCache.h"
#include "HudTextStyle.h"
#include "TextMask.h"
#include "GrooveGauge.h"
#include "PlayOutcome.h"
#include "PlaybackGauge.h"
#include "GaugeTrialOptions.h"
#include "ResultScore.h"
#include "PlayRecordStore.h"
#include "PracticeMode.h"
#include "AutoPlay.h"
#include "PracticePreview.h"
#include "KeyBeams.h"
#include "KeyDeck.h"
#include "TurntableImage.h"
#include "CompositionPresenter.h"
#ifdef BMS_ENABLE_SKIN_BINDINGS
#include "skin/lr2/Lr2PreviewWindow.inl"
#include "skin/lr2/Lr2SkinLibrary.h"
#include "skin/lr2/Lr2PlayTimeline.h"
#endif
#include "PresentationDiagnostics.h"
#include "HybridFrame.h"
#include "SystemResources.h"
#include <thread>
#include <tuple>
#include <atomic>

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <windows.h>
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "msimg32.lib")
#include <gdiplus.h>
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <mfapi.h>
#include <dwmapi.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwmapi.lib")

#pragma comment(lib, "comctl32.lib")

namespace {
void beginPresentDiagnostics();void finishPresentDiagnostics();void prewarmPlayText();bool gPrewarmTextPending=false;
void applyPresentTuning();void updateVblankAlignment();
// Pacing toggles (env, read at startup):
//   SEVENMANIA_CAP_VBLANK_PACING=0/1    FPS caps at a refresh multiple (default 1)
//   SEVENMANIA_VSYNC_VBLANK_PACING=0/1  VSync mode (default 0)
//   SEVENMANIA_VSYNC_READY_WAKE=0/1     VSync+aligned: on a busy queue, fall back to ready-signal
//                                       painting for a while instead of waiting a refresh (default 1)
//   SEVENMANIA_ALIGNED_STALL_CLASSIFIER=0/1  diagnostics: aligned loop-stall threshold (default 1)
//   SEVENMANIA_VBLANK_PACING=1/0        legacy: both pacing toggles on/off
bool gCapVblankPacing = true, gVsyncVblankPacing = false, gVsyncReadyWake = true, gAlignedStallClassifier = true;
std::chrono::nanoseconds gVblankLead{std::chrono::microseconds(2000)};
bool vblankPacingForMode();
// No-input benchmark (SEVENMANIA_NO_INPUT_BENCHMARK=1): lane input ignored, every note MISS,
// keysounds triggered at note time, gauge never ends the play, nothing is saved.
bool gNoInputBenchmark = false;size_t gBenchmarkSoundIndex = 0;
// Benchmark repeat: a chart the user starts is played SEVENMANIA_BENCH_REPEAT times in a row
// (default 2, 0 = off), then the game closes itself (SEVENMANIA_BENCH_EXIT, default 1).
int gBenchmarkRepeat = 2, gBenchmarkPlays = 0;bool gBenchmarkExit = true, gBenchmarkAutoStarting = false;
std::filesystem::path gBenchmarkChart;

constexpr int ID_OPEN = 1001;
constexpr wchar_t BROWSE_SHORTCUT_HINT[] = L"[Enter] 플레이   [Shift + Enter] 연습모드   [Ctrl + Enter] 오토플레이   [Space] 설정   [F2] 곡 폴더 열기   [F5] 다시 검색";
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
constexpr int ID_SUMMARY_SETTINGS = 1038;
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
constexpr int ID_LR2_ASPECT = 1274;
constexpr int ID_JUDGEMENT_PROFILE = 1026;
constexpr int ID_NOTE_HEIGHT = 1027;
constexpr int ID_HIDDEN_SONGS = 1028;
constexpr int ID_GROOVE_GAUGE = 1029;
constexpr int ID_SOUND_BUFFER = 1034;
constexpr int ID_AUDIO_OUTPUT_MODE = 1053;
constexpr int ID_SELECT_BGM_VOLUME = 1054;
constexpr int ID_AUDIO_LATENCY = 1055;
constexpr int ID_NOTE_DISPLAY_OFFSET = 1056;
constexpr int ID_JUDGE_TIMING_OFFSET = 1057;
constexpr int ID_AUTO_JUDGE_MODE = 1058;
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
constexpr UINT WM_AUTO_JUDGE_SUGGEST=WM_APP+110;
constexpr UINT WM_LOAD_FINISHED = WM_APP + 20;
constexpr UINT WM_LOAD_PROGRESS = WM_APP + 21;
constexpr UINT WM_APPLY_SETTINGS = WM_APP + 42;
constexpr UINT WM_RML_SETTINGS_NAVIGATE = WM_APP + 43;
constexpr UINT WM_RML_SETTINGS_FALLBACK = WM_APP + 44;
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
constexpr int ID_INTRO_ENABLE=1097;
constexpr int ID_PREVIEW_START=1099;
constexpr int ID_HI_SPEED_FIX=1290,ID_SELECT_FPS=1291,ID_LANE_OPTION=1295;
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
bms::KeyModeFilter gKeyModeFilter{ bms::KeyModeFilter::Key7 };
int gDifficultyFilter{}; // bms::difficultyFilterMatches: 0 all, 1-5 BEGINNER..INSANE
bms::TargetKind gTargetKind{ bms::TargetKind::MyBest };
// 1P lane option for the next play, and the one the current play was arranged with.
bms::LaneOption gLaneOption{ bms::LaneOption::Off }, gPlayLaneOption{ bms::LaneOption::Off };
int gDefaultTargetRate{ bms::initialDefaultTargetRate };
bool gHasPartialLoadCharts{};
std::wstring gPartialLoadSummary;
std::array<int, 2> gBrowseSelections{ -1, -1 };
HWND gHelp{}, gBind{};
HWND gPracticeWindow{}, gPracticeNext{}, gPracticeBack{};
HFONT gPracticeHudFont{};
PracticeMode gPractice;
// Autoplay (Ctrl+Enter, or the LR2 select button 16): every note is pressed for the player and nothing is
// recorded. Set when a play starts and kept through restarts and onto its result screen.
bool gAutoplay{};
bms::AutoPlayCursor gAutoPlayCursor;
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
HWND gBrowseBackgroundChoice{}, gBackgroundBlurSlider{}, gBackgroundBlurValue{};
HWND gBackgroundOpacitySlider{}, gBackgroundOpacityValue{}, gSongPreviewChoice{}, gPreviewVolumeSlider{}, gPreviewVolumeValue{};
HWND gCpuLimitSlider{}, gCpuLimitValue{}, gMemoryLimitSlider{}, gMemoryLimitValue{}, gGpuLimitSlider{}, gGpuLimitValue{}, gResourceUsageChoice{}, gCpuPerformanceChoice{};
HWND gComboTextYSlider{}, gComboTextYValue{}, gJudgementTextYSlider{}, gJudgementTextYValue{};
int gBrowseBackgroundMode{2}, gBackgroundBlur = 20, gBackgroundOpacity = 10, gPreviewVolume = 50, gSelectBgmVolume = 100;
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
HWND gLr2AspectChoice{};
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
bool gSkinMirrorUi = false;
bool gCompositionEnabled{};
bool gDirectCompositionEnabled = true;
bool gLr2Stretch{};
CompositionPresenter gCompositionPresenter;
#ifdef BMS_ENABLE_SKIN_BINDINGS
std::filesystem::path gLr2PlayFile;
std::filesystem::path gLr2Play5File; // 5KEY charts; gLr2PlayFile is for 7KEY.
std::filesystem::path gLr2Play14File,gLr2Play10File,gLr2Play9File;
std::filesystem::path gLr2DecideFile;
std::filesystem::path gLr2ResultFile;
std::filesystem::path gLr2SelectFile;
std::unique_ptr<sevenmania::skin::lr2::PreviewScene> gLr2SelectScene;
std::unique_ptr<sevenmania::skin::lr2::PreviewScene> gLr2DecideScene;
std::unique_ptr<sevenmania::skin::lr2::PreviewScene> gLr2ResultScene;
std::chrono::steady_clock::time_point gLr2ResultStart;
std::chrono::steady_clock::time_point gLr2ResultExitStart;
constexpr UINT_PTR ID_LR2_RESULT_ANIMATION=0xB58A;
// LR2 select #FADEOUT: a chosen song waits while timer 2 fades the select screen out.
constexpr UINT_PTR ID_LR2_SELECT_FADE=0xB58B;
std::chrono::steady_clock::time_point gLr2SelectFadeAt;
int gLr2SelectFadeIndex=-1;
bool gLr2SelectFadePractice{},gLr2SelectFadeAutoplay{},gLr2SelectFadeFinishing{};
extern bool gLr2SelectActive;
void loadLr2Result();
bool lr2DecideShowing();
std::array<std::wstring,8> gLr2RuntimeErrors;
struct Lr2SceneLoad {
    std::unique_ptr<sevenmania::skin::lr2::PreviewScene> scene;
    std::vector<sevenmania::skin::lr2::Diagnostic> diagnostics;
    std::wstring_view failure;
};
Lr2SceneLoad inspectLr2Scene(const std::filesystem::path& path,const sevenmania::skin::lr2::Options& options,
    const sevenmania::skin::lr2::ScreenData* data=nullptr,unsigned width=0,unsigned height=0) {
    using namespace sevenmania::skin::lr2;
    try {
        auto result=parse(path,options);
        if(!result)return {{},std::move(result.diagnostics),L"스킨 파싱 실패"};
        if(width&&height) {
            result.document->header.width=width;
            result.document->header.height=height;
        }
        auto scene=std::make_unique<PreviewScene>();
        scene->setStretch(gLr2Stretch);
        if(!scene->load(*result.document,result.diagnostics,playLaneCount(static_cast<int>(options.screen))>0,data))
            return {{},std::move(result.diagnostics),L"화면 구성 실패"};
        return {std::move(scene),std::move(result.diagnostics),{}};
    } catch(const std::exception&) {
        return {{},{},L"스킨 로드 예외"};
    }
}
void reportLr2Failure(sevenmania::skin::lr2::ScreenType screen,const std::filesystem::path& path,
                      std::wstring_view stage,const std::vector<sevenmania::skin::lr2::Diagnostic>& diagnostics={}) {
    auto message=path.wstring()+L" · "+std::wstring(stage);
    for(const auto& diagnostic:diagnostics) {
        if(!diagnostic.error&&!diagnostic.message.starts_with("INCLUDE skipped"))continue;
        message+=L" · "+diagnostic.source.wstring()+L":"+std::to_wstring(diagnostic.line)+L" "+
            std::filesystem::path(std::u8string(diagnostic.message.begin(),diagnostic.message.end())).wstring();
        break;
    }
    auto& previous=gLr2RuntimeErrors[static_cast<std::size_t>(screen)];
    if(previous==message)return;
    previous=message;
    OutputDebugStringW((message+L"\n").c_str());
}
sevenmania::skin::lr2::SkinChoices lr2SkinChoices(const std::filesystem::path& skin);
bool gLr2CommandLine{};      // LR2 skins came from the command line: this run only, not saved.
bool gLr2SelectAttempted{};  // Cleared when the select skin or its options change.
bool gLr2SkinsScanned{};
bool gLr2SelectGpuFailed{};
unsigned gLr2PlayWidth{},gLr2PlayHeight{};
std::unique_ptr<sevenmania::skin::lr2::PreviewScene> gLr2PlayScene;
struct Lr2PlayState {
    // Indexed by SevenMania lane.
    std::array<double,16> keyOn{},keyOff{},bomb{};
    std::array<int,5> counts{};
    std::vector<double> measurePositions;
    std::vector<float> visibleMeasures;
    sevenmania::skin::lr2::PlayFrame frame;
    double judgeAt=-1, start{}, gaugeUpAt=-1, gaugeFullAt=-1, previousGauge=-1, fullComboAt=-1;
    // The 2P side of a double play skin; judgeSide is the side of the lane being judged.
    double judgeAt2P=-1;
    int judge2P=-1,judgeSide{},type{};
    double minBpm{},maxBpm{};
    // The gauge at even intervals over the song, for the result screen's chart.
    std::array<float,256> gaugeHistory{};
    std::size_t gaugeSamples{};
    // Set once the play is over and the skin's own ending (fail shutters or fade) is running.
    std::chrono::steady_clock::time_point endingAt{};
    int endingMilliseconds{};
    bool endingFailed{};
    // Pause QUIT: the ending's clock starts on the first frame drawn after the menu closes, so a slow
    // first frame cannot use up the fail animation.
    bool endingClockPending{};
    int judge=-1,noteCount{};
    bool hasLongNotes{},scoreGraph=true;
    // The best EX score before this play, which is also the target score (LR2's MY BEST target).
    std::optional<int> bestExScore;
    // Turntable rotation in degrees for 1P and 2P, and the playhead it was last advanced at.
    std::array<float,2> turntableAngle{};
    double turntableAt=-1;
    // Before the song the scene is shown while the chart loads (LR2's LOADSTART/LOADEND/PLAYSTART).
    // leadInStart is when it first appeared, loadedAt the scene milliseconds at which loading ended.
    // sceneLead and readyLead carry timer 0 and READY (40) into the song: they are its scene and
    // READY milliseconds at the moment the song started.
    std::chrono::steady_clock::time_point leadInStart{};
    double loadedAt=-1, sceneLead{}, readyLead{};
    // Playhead seconds at which the last note (a long note's tail included) was judged, or -1.
    double allJudgedAt=-1;
    bool leadInFailed{};
    sevenmania::skin::lr2::PlayLeadInTimes leadInTimes;
    double leadInAge() const {
        return leadInStart==std::chrono::steady_clock::time_point{}?-1.:
            std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-leadInStart).count();
    }
    void resetLeadIn() {leadInStart={};loadedAt=-1;sceneLead=readyLead=0;leadInFailed=false;}
    void resetTimers(double time) {
        keyOn.fill(-1);keyOff.fill(-1);bomb.fill(-1);counts.fill(0);
        judge=judge2P=-1;judgeSide=0;judgeAt=judgeAt2P=gaugeUpAt=gaugeFullAt=previousGauge=fullComboAt=-1;start=time;
        gaugeSamples=0;endingAt={};endingMilliseconds=0;endingFailed=endingClockPending=false;
        // A restart begins at the song itself: timers 0, 40 and 41 start together again.
        sceneLead=readyLead=0;
        allJudgedAt=-1;
        frame={};visibleMeasures.clear();
    }
    Lr2PlayState() {resetTimers(0);}
} gLr2Play;
bool lr2PlayEnding();
bool gLr2GpuFailed{};
#endif
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
bool gGaugeCompatibilityTest = false;
bool gResultGaugeCleared = true;
bool gResultGaugeFailed{};
// Set when the player picks QUIT in the pause menu: the play then ends as a failure.
bool gPlayAbandoned{};
// The window lost focus while a play was loading or counting down: the song starts paused,
// unless focus comes back first (then it starts normally).
bool gPauseWhenPlayStarts{};
std::array<int, 8> gLaneColors{ 4, 7, 2, 7, 2, 7, 2, 7 };
constexpr std::array<int, 2> noteColors(int lane) {
    return lane == 0 ? std::array{3, 4} : std::array{7, 2};
}
FramePacer gFramePacer;
bool gOutputWakeReady{};
HFONT gFont{}, gSmallFont{}, gHudBoldFont{}, gListFont{}, gListSelectedFont{}, gListLevelFont{}, gListSelectedLevelFont{};
HFONT gBrowseTitleFont{}, gBrowseLevelFont{}, gBrowseMetadataFont{}, gBrowseRankFont{};
HFONT gBrowseSongTitleFont{}, gBrowseSongInfoFont{};
HFONT gComboFont{}, gJudgementFont{};
// Scaled copies of gFont/gSmallFont for gameplay drawing; the originals stay fixed for native controls.
HFONT gPlayFont{}, gPlaySmallFont{};
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
bool gResultBackdropValid{};
HDC gBgaBackplateDc{};
HBITMAP gBgaBackplateBitmap{};
HGDIOBJ gBgaBackplateOriginalBitmap{};
void* gBgaBackplatePixels{};
std::shared_ptr<const uint32_t> gDirectBgaFrame, gDirectBgaOverlay;
std::shared_ptr<const BgaSourceImage> gDirectBgaSource; // GPU-scaled still (SEVENMANIA_BGA_GPU_SCALE)
nv12::View gDirectBgaVideo; // NV12 video backdrop (SEVENMANIA_VIDEO_NV12)
template<class VideoFrame> nv12::View nv12ViewOf(const std::shared_ptr<VideoFrame>& frame) {
    nv12::View view;
    if (!frame || frame->nv12.size() != nv12::bytes(frame->sourceWidth, frame->sourceHeight)) return view;
    view.width = frame->sourceWidth; view.height = frame->sourceHeight; view.matrix = frame->nv12Matrix;
    view.fullRange = frame->nv12FullRange; view.generation = frame->generation; view.data = frame->nv12.data(); view.owner = frame;
    return view;
}
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
struct Lr2ChartSummary {
    int judgementNotes{};
    bool hasLongNotes{};
    double minBpm{}, maxBpm{};
};
// UI-thread cache keyed by the current library's chart indices.
std::unordered_map<size_t, Lr2ChartSummary> gLr2ChartSummaries;
PlayRecordStore gPlayRecordStore;
// Aliases kept so host code and the tests that include Main.cpp see the same names.
std::unordered_map<std::wstring, PlayRecord>& gPlayRecords = gPlayRecordStore.records;
std::filesystem::path& gPlayRecordsFile = gPlayRecordStore.file;
std::vector<std::wstring> gChartRecordKeys;
std::filesystem::path gCurrentFolder;
std::filesystem::path gSettingsFile;
constexpr UINT_PTR ID_SONG_LIST_ANIMATION = 0xB501;
constexpr UINT_PTR ID_PAUSE_PRESENT_RETRY = 0xB502;
constexpr UINT_PTR ID_BENCHMARK_NEXT = 0xB50F;
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

// Fail ending (LR2 / beatoraja): an empty gauge or QUIT stops the song, freezes the picture at the
// last heard time and plays playstop; the result's clear/fail sound waits until playstop has finished.
constexpr UINT_PTR ID_RESULT_SOUND = 0xB511;
bool gFailureEndingStarted{};
double gFailureEndingPlayhead{};
double gPlayStopSeconds{};
std::chrono::steady_clock::time_point gPlayStopStartedAt{};
bms::SystemSound gPendingResultSound{bms::SystemSound::Count};
void resetFailureEnding() {
    gFailureEndingStarted = false;
    gPlayStopSeconds = 0;
    gPendingResultSound = bms::SystemSound::Count;
    if (gWindow) KillTimer(gWindow, ID_RESULT_SOUND);
}

void syncNativeResultNext() {
    if(!IsWindow(gResultNext))return;
    bool visible=gShowingResult&&!gResultFade.active();
#ifdef BMS_ENABLE_SKIN_BINDINGS
    visible=visible&&!gLr2ResultScene;
#endif
    if(visible)SetWindowPos(gResultNext,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    else ShowWindow(gResultNext,SW_HIDE);
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
// Practice, benchmark, gauge diagnostics and autoplay share one rule for what is not a record.
bms::PlayRecordExclusions playRecordExclusions() {
    return {gPractice.enabled, gNoInputBenchmark, gGaugeCompatibilityTest, gAutoplay};
}
bool playSavesRecord() { return bms::playSavesRecord(playRecordExclusions()); }
// The autoplay schedule for the chart being played, from where the play starts.
void resetAutoPlay() {
    if (!gAutoplay || gActiveChartIndex < 0 || gActiveChartIndex >= static_cast<int>(gCharts.size())) { gAutoPlayCursor.clear(); return; }
    gAutoPlayCursor.reset(bms::buildAutoPlayEvents(gCharts[static_cast<size_t>(gActiveChartIndex)]),
        gPractice.enabled ? gPractice.startSeconds : 0);
}
bms::PlaybackLoadState gPlaybackLoad;
PlaybackLoadJob gPlaybackJob;
#include "SongIntroOverlay.h"
SongIntroOverlay gSongIntro;int gIntroEnabled=1;
#include "PlayPreview.h"
PlayPreview gPlayPreview;
int gPreviewStartMode{};
std::chrono::steady_clock::time_point gPlaybackLoadStarted{};
std::chrono::steady_clock::time_point gTitleFadeOutStarted{};
constexpr auto titleFadeDuration = std::chrono::milliseconds(250);
double gNoteSpeed = 1.0;
bms::HiSpeedFix gHiSpeedFix{bms::HiSpeedFix::Off};
SelectFrameClock gSelectFrameClock;
// Hi-speed of the play: the practice override or the setting, times the HI-SPEED FIX scale of the chart.
double playHiSpeedScale(const bms::Chart& chart) {
    static const bms::Chart* cached{};static std::size_t cachedNotes{};static bms::HiSpeedFix cachedFix{};static double scale=1;
    if(cached!=&chart||cachedNotes!=chart.notes.size()||cachedFix!=gHiSpeedFix){
        cached=&chart;cachedNotes=chart.notes.size();cachedFix=gHiSpeedFix;scale=bms::hiSpeedFixScale(chart,gHiSpeedFix);
    }
    return scale;
}
double playScrollPosition(const bms::Chart& chart,double seconds){return bms::fixedScrollPosition(chart,seconds,gHiSpeedFix);}
// Test: the note height is fixed to the NoteGroup sheet cell height (36px); it is no longer a setting.
constexpr int kFixedNoteHeight = 36;
int gNoteHeight = kFixedNoteHeight;
JudgementProfile gJudgementProfile = JudgementProfile::Normal;
GrooveGaugeMode gGrooveGaugeMode = GrooveGaugeMode::Normal;
int gTimingHintCondition=0;
AccuracyLimit gAccuracyLimit=AccuracyLimit::None;
bool gAccuracyRestartPending=false;
std::uint64_t gPlaybackGeneration=0;
void clearAccuracyRestart(){gAccuracyRestartPending=false;++gPlaybackGeneration;}
void requestAccuracyRestart(TimingJudgement grade){
    if(!gPlaying||gPaused||gLoading||gAccuracyRestartPending||gNoInputBenchmark||!belowAccuracyLimit(gAccuracyLimit,grade))return;
    gAccuracyRestartPending=true;
    if(!PostMessageW(gWindow,WM_ACCURACY_RESTART,static_cast<WPARAM>(gPlaybackGeneration),0))gAccuracyRestartPending=false;
}
const wchar_t* activeGaugeModeText() {
    if(gGaugeCompatibilityTest && ((gPlaying||gShowingResult)?gGrooveGauge.compatibilityActive():gGrooveGauge.hasCompatibilityOverride()))
        return sevenmania::gauge_trial::trialKindName(gGrooveGauge.compatibilityKind());
    switch(gGrooveGaugeMode){case GrooveGaugeMode::Easy:return L"EASY";case GrooveGaugeMode::Hard:return L"HARD";default:return L"NORMAL";}
}
std::array<UINT, 8> gLaneKeys = DEFAULT_KEYS;
std::array<UINT, 16> gPendingLaneKeys{};
bms::KeyProfiles gKeyProfiles;int gBindingMode=1;
int gCaptureLane{};
size_t gMissScanIndex{};
std::vector<unsigned char> gHit;
std::vector<size_t> gLingeringLongNotes;
// Long notes released shortly before their tail (accepted as a clean release): their remaining body stays
// visible, inactive, until the tail reaches the judgement line.
std::vector<size_t> gReleasedLongNotes;
// Indices of the long notes of the active chart (ascending). The note scan starts near the playhead by head
// time, so a long note whose head has scrolled far past the line is drawn from this list while its body is visible.
struct LongNoteIndex { const bms::Note* data{}; size_t size{}; std::vector<size_t> indices; };
LongNoteIndex gLongNoteIndex;
std::array<std::vector<size_t>, 16> gLaneNoteIndices;
std::vector<bool> gMineHit;
std::array<bool, 16> gPressed{};
std::array<bool,16> gKeyboardPressed{};
std::array<bool,16> gControllerPressed{};
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
// Window pixels per 1920x1080 design pixel (height based, limited by the width; ResolutionScale.h).
// Engine geometry, fonts and scroll speed are authored at FHD and multiplied by this.
double gUiScale = 1.0;
int ui(int designPixels) { return bms::ui::scaled(designPixels, gUiScale); }
// The game window is per-monitor DPI aware (true pixels; gUiScale does the scaling). Native popup
// dialogs keep their fixed pixel layouts, so they are created DPI unaware and Windows scales them.
struct DpiContextScope {
    DPI_AWARENESS_CONTEXT previous{};
    explicit DpiContextScope(DPI_AWARENESS_CONTEXT context) : previous(SetThreadDpiAwarenessContext(context)) {}
    ~DpiContextScope() { if (previous) SetThreadDpiAwarenessContext(previous); }
    DpiContextScope(const DpiContextScope&) = delete;
    DpiContextScope& operator=(const DpiContextScope&) = delete;
};

int judgementLineY(int height) {
    const int nominal = static_cast<int>(std::lround(height * 2.0 / 3.0));
    const int upper = ui(40);
    const int lower = std::max(upper, height - ui(112));
    return std::clamp(nominal + ui(gJudgeLineOffset), upper, lower);
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
void beginSelectedChart(bool practice = false, bool autoplay = false);
void beginSongEntry(const bms::Chart&);
bool songEntryActive();
bool skipSongEntry();
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
// gPlayhead is the "heard" time: the device clock minus the user's audio-latency correction.
bms::TimingOffsets gTimingOffsets;
double heardPlayhead() {
    if (gFailureEndingStarted) return gFailureEndingPlayhead; // the song has stopped; the picture holds still
    return bms::heardSeconds(gAudio.positionSeconds(),gTimingOffsets,playbackTimingScale());
}
// Judgement time = heard time minus the judge offset (manual or auto). Notes are drawn from gPlayhead, never from this.
double judgePlayhead() {return bms::judgeSeconds(gPlayhead,gTimingOffsets,playbackTimingScale());}
bms::AutoJudgeAdjuster gAutoJudge;
int gAutoJudgeMode{bms::AutoJudgeAdjuster::Suggest};
bool gAutoJudgeUsedThisPlay{};
int gAutoJudgeSuggestion{};
bool gAutoJudgePending{};

void restorePracticeNotes() {
    if (gPracticeNotesChartIndex >= 0 && gPracticeNotesChartIndex < static_cast<int>(gCharts.size())) {
        auto& chart = gCharts[static_cast<size_t>(gPracticeNotesChartIndex)];
        chart.notes = std::move(gPracticeOriginalNotes);
        gLr2ChartSummaries.erase(gPracticeNotesChartIndex);
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
    gKeyModeFilter = bms::keyModeFilterFromInt(static_cast<int>(GetPrivateProfileIntW(L"Browse", L"KeyMode", 0, gSettingsFile.c_str())));
    gDifficultyFilter = bms::difficultyFilterFromInt(static_cast<int>(GetPrivateProfileIntW(L"Browse", L"Difficulty", 0, gSettingsFile.c_str())));
    gTargetKind = bms::targetKindFromInt(static_cast<int>(GetPrivateProfileIntW(L"Browse", L"Target", 0, gSettingsFile.c_str())));
    gDefaultTargetRate = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Browse", L"TargetRate", bms::initialDefaultTargetRate, gSettingsFile.c_str())),
        bms::minDefaultTargetRate, bms::maxDefaultTargetRate);
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
    gSkinMirrorUi = GetPrivateProfileIntW(L"Skin", L"MirrorGearUi", 0, gSettingsFile.c_str()) != 0;
    const UINT legacyComposition = GetPrivateProfileIntW(L"Display", L"DirectComposition", 1, gSettingsFile.c_str());
    gDirectCompositionEnabled = legacyComposition != 0;
    gLr2Stretch = GetPrivateProfileIntW(L"Display",L"Lr2Stretch",0,gSettingsFile.c_str()) == 1;
    gCompositionEnabled = GetPrivateProfileIntW(L"Display", L"GpuRendering", legacyComposition, gSettingsFile.c_str()) != 0;
    gNoteSubpixel = GetPrivateProfileIntW(L"Display", L"NoteSubpixel", 1, gSettingsFile.c_str()) != 0;
    gReflexRequested = GetPrivateProfileIntW(L"Display", L"NvidiaReflex", 0, gSettingsFile.c_str()) != 0;
    gNoteAntialias = GetPrivateProfileIntW(L"Display", L"NoteAntialias", 1, gSettingsFile.c_str()) != 0;
    constexpr std::array<const wchar_t*, 4> volumeKeys{L"MasterVolume", L"MusicVolume", L"KeyVolume", L"SfxVolume"};
    for (size_t i = 0; i < volumeKeys.size(); ++i)
        gVolumes[i] = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Sound", volumeKeys[i], 100, gSettingsFile.c_str())), 0, 100);
    gAudio.setVolumes(gVolumes[0], gVolumes[1], gVolumes[2], gVolumes[3]);
    // Older configs shared MusicVolume with the select-screen BGM; keep their loudness until the new key is saved.
    gSelectBgmVolume = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Sound", L"SelectBgmVolume", gVolumes[1], gSettingsFile.c_str())), 0, 100);
    gTimingOffsets.audioMs = bms::clampOffsetMs(static_cast<int>(GetPrivateProfileIntW(L"Sound", L"AudioLatencyMs", 0, gSettingsFile.c_str())));
    gTimingOffsets.displayMs = bms::clampOffsetMs(static_cast<int>(GetPrivateProfileIntW(L"Display", L"NoteDisplayOffsetMs", 0, gSettingsFile.c_str())));
    gTimingOffsets.judgeMs = bms::clampOffsetMs(static_cast<int>(GetPrivateProfileIntW(L"Gameplay", L"JudgeOffsetMs", 0, gSettingsFile.c_str())), bms::TimingOffsets::judgeLimitMs);
    gAutoJudgeMode = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Gameplay", L"AutoJudgeMode", bms::AutoJudgeAdjuster::Suggest, gSettingsFile.c_str())), 0, 2);
    const int fps = startup.fps;
    if (validFpsSetting(fps)) gTargetFps = fps;
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
    gHiSpeedFix = static_cast<bms::HiSpeedFix>(std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Display", L"HiSpeedFix", 0, gSettingsFile.c_str())), 0, bms::hiSpeedFixCount - 1));
    gSelectFrameClock.setFps(static_cast<int>(GetPrivateProfileIntW(L"Display", L"SelectFPS", SelectFrameClock::defaultFps, gSettingsFile.c_str())));
    gJudgementProfile = JudgementProfile::Normal;
    gGrooveGaugeMode = static_cast<GrooveGaugeMode>(std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Gameplay", L"GrooveGaugeMode", 0, gSettingsFile.c_str())), 0, 2));
    gLaneOption = bms::laneOptionFromInt(static_cast<int>(GetPrivateProfileIntW(L"Gameplay", L"LaneOption", 0, gSettingsFile.c_str())));
    gIntroEnabled=GetPrivateProfileIntW(L"Gameplay",L"SongIntro",1,gSettingsFile.c_str())!=0;
    gPreviewStartMode=std::clamp(int(GetPrivateProfileIntW(L"Gameplay",L"PreviewStart",0,gSettingsFile.c_str())),0,2);
    gTimingHintCondition=std::clamp(int(GetPrivateProfileIntW(L"Gameplay",L"TimingHintCondition",0,gSettingsFile.c_str())),0,3);
    gAccuracyLimit=static_cast<AccuracyLimit>(std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Gameplay",L"AccuracyLimit",0,gSettingsFile.c_str())),0,4));
    gSkinJudgementMode=std::clamp(int(GetPrivateProfileIntW(L"Skin",L"JudgementMode",1,gSettingsFile.c_str())),-1,1);
    gSkinComboMode=std::clamp(int(GetPrivateProfileIntW(L"Skin",L"ComboMode",1,gSettingsFile.c_str())),-1,1);
    gComboTextYOffset = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Gameplay", L"ComboTextYOffset", 0, gSettingsFile.c_str())), HUD_TEXT_Y_MIN, HUD_TEXT_Y_MAX);
    gJudgementTextYOffset = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Gameplay", L"JudgementTextYOffset", 0, gSettingsFile.c_str())), HUD_TEXT_Y_MIN, HUD_TEXT_Y_MAX);
    const int bufferFrames = startup.buffer;
    gSoundBufferFrames = std::find(SOUND_BUFFER_OPTIONS.begin(), SOUND_BUFFER_OPTIONS.end(), bufferFrames) != SOUND_BUFFER_OPTIONS.end() ? bufferFrames : 256;
    gAudio.setBufferFrames(static_cast<uint32_t>(gSoundBufferFrames));
    gNoteHeight = ui(kFixedNoteHeight);
    for (size_t lane = 0; lane < gLaneColors.size(); ++lane) {
        const std::wstring key = L"Lane" + std::to_wstring(lane);
        const UINT value = GetPrivateProfileIntW(L"NoteColors", key.c_str(), gLaneColors[lane%8], gSettingsFile.c_str());
        const auto allowed = noteColors(static_cast<int>(lane));
        if (value == static_cast<UINT>(allowed[0]) || value == static_cast<UINT>(allowed[1]))
            gLaneColors[lane] = static_cast<int>(value);
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
    WritePrivateProfileStringW(L"Browse", L"KeyMode", std::to_wstring(static_cast<int>(gKeyModeFilter)).c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Browse", L"Difficulty", std::to_wstring(gDifficultyFilter).c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Browse", L"Target", std::to_wstring(static_cast<int>(gTargetKind)).c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Browse", L"TargetRate", std::to_wstring(gDefaultTargetRate).c_str(), gSettingsFile.c_str());
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
    WritePrivateProfileStringW(L"Display",L"Lr2Stretch",gLr2Stretch?L"1":L"0",gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Display", L"GpuRendering", gCompositionEnabled ? L"1" : L"0", gSettingsFile.c_str());
    constexpr std::array<const wchar_t*, 4> volumeKeys{L"MasterVolume", L"MusicVolume", L"KeyVolume", L"SfxVolume"};
    for (size_t i = 0; i < volumeKeys.size(); ++i)
        WritePrivateProfileStringW(L"Sound", volumeKeys[i], std::to_wstring(gVolumes[i]).c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Sound", L"SelectBgmVolume", std::to_wstring(gSelectBgmVolume).c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Sound", L"AudioLatencyMs", std::to_wstring(gTimingOffsets.audioMs).c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Display", L"NoteDisplayOffsetMs", std::to_wstring(gTimingOffsets.displayMs).c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Gameplay", L"JudgeOffsetMs", std::to_wstring(gTimingOffsets.judgeMs).c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Gameplay", L"AutoJudgeMode", std::to_wstring(gAutoJudgeMode).c_str(), gSettingsFile.c_str());
    const std::wstring fps = std::to_wstring(gTargetFps);
    const std::wstring judgeOffset = std::to_wstring(gJudgeLineOffset);
    const std::wstring speed = std::to_wstring(static_cast<int>(std::lround(gNoteSpeed * 4)));
    const std::wstring judgementProfile = std::to_wstring(static_cast<int>(gJudgementProfile));
    const std::wstring grooveGaugeMode = std::to_wstring(static_cast<int>(gGrooveGaugeMode));
    const std::wstring comboTextYOffset = std::to_wstring(gComboTextYOffset);
    const std::wstring judgementTextYOffset = std::to_wstring(gJudgementTextYOffset);
    WritePrivateProfileStringW(L"Display", L"FPS", fps.c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Display", L"NoteSubpixel", gNoteSubpixel ? L"1" : L"0", gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Display", L"NvidiaReflex", gReflexRequested ? L"1" : L"0", gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Display", L"NoteAntialias", gNoteAntialias ? L"1" : L"0", gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Display", L"JudgeLineOffset", judgeOffset.c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Display", L"SpeedSteps", speed.c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Display", L"HiSpeedFix", std::to_wstring(static_cast<int>(gHiSpeedFix)).c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Display", L"SelectFPS", std::to_wstring(gSelectFrameClock.fps()).c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Gameplay", L"JudgementProfile", judgementProfile.c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Gameplay", L"GrooveGaugeMode", grooveGaugeMode.c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Gameplay", L"LaneOption", std::to_wstring(static_cast<int>(gLaneOption)).c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Display",L"SeparatorOpacity",std::to_wstring(gSeparatorOpacity).c_str(),gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Gameplay",L"SongIntro",std::to_wstring(gIntroEnabled).c_str(),gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Gameplay",L"SongIntroMs",nullptr,gSettingsFile.c_str()); // legacy key removed
    WritePrivateProfileStringW(L"Gameplay",L"PreviewStart",std::to_wstring(gPreviewStartMode).c_str(),gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Gameplay",L"TimingHintCondition",std::to_wstring(gTimingHintCondition).c_str(),gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Gameplay",L"AccuracyLimit",std::to_wstring(static_cast<int>(gAccuracyLimit)).c_str(),gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Skin",L"JudgementMode",std::to_wstring(gSkinJudgementMode).c_str(),gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Skin",L"ComboMode",std::to_wstring(gSkinComboMode).c_str(),gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Gameplay", L"ComboTextYOffset", comboTextYOffset.c_str(), gSettingsFile.c_str());
    WritePrivateProfileStringW(L"Gameplay", L"JudgementTextYOffset", judgementTextYOffset.c_str(), gSettingsFile.c_str());
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
    setBindingStatus(L"키 배치 설정: " + bms::keyProfileSlotName(gBindingMode,bms::keyProfileCaptureSlot(gBindingMode,gCaptureLane)) + L" (" + std::to_wstring(gCaptureLane+1) + L"/" + std::to_wstring(bms::KeyProfiles::counts[gBindingMode]) + L") 키를 누르세요 (Esc 취소)");
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
    for (int captured = 0; captured < gCaptureLane; ++captured) {
        if (gPendingLaneKeys[static_cast<size_t>(bms::keyProfileCaptureSlot(gBindingMode, captured))] == key) {
            setBindingStatus(L"이미 다른 레인에 지정한 키입니다. 각 레인은 서로 다른 키를 사용해야 합니다.");
            return;
        }
    }
    gPendingLaneKeys[static_cast<size_t>(bms::keyProfileCaptureSlot(gBindingMode, gCaptureLane))] = key;
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
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(gLr2PlayScene){
        (gLr2Play.judgeSide?gLr2Play.judge2P:gLr2Play.judge)=text==L"MINE"?0:-1;
        (gLr2Play.judgeSide?gLr2Play.judgeAt2P:gLr2Play.judgeAt)=gPlayhead;
    }
#endif
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
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if (poor) emitSevenManiaSkinEvent("combo.break");
#endif
    const bool bad = text.find(L"BAD") != std::wstring::npos;
    if (poor || bad) gFeedback.miss();
    // No-input benchmark: every note misses; keep the normal BGA layers (closer to real play).
    if (poor && !gNoInputBenchmark) {gMissBgaUntil=gPlayhead+2.5;gPoorBgaStart=gPlayhead;}
}

void finishPlayback();
void captureBrowseBackdrop();
void startUiEffects() { gAudio.setWasapiExclusiveEnabled(false); std::wstring ignored; gAudio.startEffectsOnly(ignored); }

// System sounds (beatoraja names, see SystemSoundSet.h). User folders win over the skin "sounds" folder
// and Assets; a sound with no file stays silent. Reloading stops UI output because the render thread reads
// the effect bank without a lock, so it is refused during play / loading.
bms::SystemSoundFolders gSystemSoundFolders;
std::filesystem::path gSystemSoundAssets, gSystemSoundSkin;
bool reloadSystemSounds(bool restartOutput) {
    if (gPlaying || gLoading) return false;
    if (restartOutput) gAudio.stop();
    for (const auto& info : bms::systemSoundTable) {
        if (info.bgm) continue;
        gAudio.unloadEffect(info.effectId);
        const auto file = bms::resolveSystemSound(info.sound, gSystemSoundFolders, gSystemSoundSkin, gSystemSoundAssets);
        std::wstring error;
        if (!file.empty()) gAudio.loadEffect(info.effectId, file, error);
    }
    gSelectionAudio.initialize(gSystemSoundAssets, gSystemSoundSkin, gSystemSoundFolders);
    if (restartOutput) startUiEffects();
    return true;
}
bool triggerSystemSound(bms::SystemSound sound) { return gAudio.triggerEffect(bms::systemSoundInfo(sound).effectId); }
constexpr WPARAM RML_ACTION_PICK_SOUND_FOLDER = 0x7101, RML_ACTION_CLEAR_SOUND_FOLDER = 0x7102,
    RML_ACTION_PICK_BGM_FOLDER = 0x7103, RML_ACTION_CLEAR_BGM_FOLDER = 0x7104;
bool pickSystemSoundFolder(HWND owner, const std::filesystem::path& initial, const wchar_t* title, std::filesystem::path& selected) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return false;
    DWORD flags{}; if (FAILED(dialog->GetOptions(&flags))) return false;
    if (FAILED(dialog->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR))) return false;
    dialog->SetTitle(title);
    Microsoft::WRL::ComPtr<IShellItem> initialItem;
    if (!initial.empty() && SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&initialItem)))) dialog->SetFolder(initialItem.Get());
    if (dialog->Show(owner) != S_OK) return false;
    Microsoft::WRL::ComPtr<IShellItem> item; PWSTR value{};
    if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &value))) return false;
    selected = value; CoTaskMemFree(value);
    return true;
}
// Returns the status line for the settings page.
std::wstring applySystemSoundFolderAction(HWND owner, WPARAM action) {
    if (gPlaying || gLoading) return L"플레이 중에는 사운드 폴더를 바꿀 수 없습니다.";
    auto folders = gSystemSoundFolders;
    const bool bgm = action == RML_ACTION_PICK_BGM_FOLDER || action == RML_ACTION_CLEAR_BGM_FOLDER;
    auto& target = bgm ? folders.bgm : folders.sound;
    if (action == RML_ACTION_PICK_SOUND_FOLDER || action == RML_ACTION_PICK_BGM_FOLDER) {
        std::filesystem::path picked;
        if (!pickSystemSoundFolder(owner, target, bgm ? L"음악(BGM) 폴더 선택 — select / decide" : L"효과음 폴더 선택 — scratch, f-open, clear, fail …", picked)) return {};
        target = picked;
    } else target.clear();
    if (folders == gSystemSoundFolders) return {};
    gSystemSoundFolders = folders;
    bms::saveSystemSoundFolders(gSettingsFile, gSystemSoundFolders);
    reloadSystemSounds(true);
    if (target.empty()) return bgm ? L"기본 선곡 음악을 사용합니다." : L"기본 효과음을 사용합니다.";
    const auto set = bms::resolveSystemSoundSet(target, bgm);
    if (set.empty()) return bgm ? L"select / decide 파일이 없는 폴더입니다. 없는 소리는 기본 소리를 사용합니다."
                                : L"clear 파일이 없는 폴더입니다. 있는 파일만 사용하고 나머지는 기본 소리를 사용합니다.";
    return (bgm ? L"음악 폴더 적용: " : L"효과음 폴더 적용: ") + set.wstring();
}

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
void showTimingJudgement(TimingJudgement judgement, int scoreUnits = 2, bool missed=false) {
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
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(gLr2PlayScene||!gLr2ResultFile.empty()) {
        (gLr2Play.judgeSide?gLr2Play.judge2P:gLr2Play.judge)=missed?0:5-static_cast<int>(judgement);
        if(scoreUnits>0)gLr2Play.counts[static_cast<std::size_t>(judgement)]+=scoreUnits;
    }
#else
    (void)missed;
#endif
    requestAccuracyRestart(judgement);
    if (gPlaying && !gAccuracyRestartPending && !gPractice.enabled && !gNoInputBenchmark && gGrooveGauge.depleted()) finishPlayback();
}

void showHoldJudgement(HoldResult result, int scoreUnits=1) {
    switch (result) {
    case HoldResult::PGreat: showTimingJudgement(TimingJudgement::PGreat, scoreUnits); break;
    case HoldResult::Poor: showTimingJudgement(TimingJudgement::Poor, scoreUnits); break;
    case HoldResult::Bad: showTimingJudgement(TimingJudgement::Bad, scoreUnits); break;
    case HoldResult::None: break;
    }
}

void lr2JudgeLane(std::size_t lane) {
#ifdef BMS_ENABLE_SKIN_BINDINGS
    gLr2Play.judgeSide=sevenmania::skin::lr2::lr2Side(gLr2Play.type,static_cast<int>(lane));
#else
    (void)lane;
#endif
}
void resolveHold(size_t lane,HoldResult result) {
    if(result==HoldResult::None || gActiveChartIndex<0)return;
    lr2JudgeLane(lane);
    const auto& notes=gCharts[gActiveChartIndex].notes;const auto index=gHolds[lane].noteIndex();
    if(index>=notes.size())return;const auto& note=notes[index];
    if(result!=HoldResult::PGreat && note.longNote())gLingeringLongNotes.push_back(index);
    else if(result==HoldResult::PGreat && note.longNote() && judgePlayhead()<note.timeSeconds+note.lengthSeconds)gReleasedLongNotes.push_back(index);
    if(result==HoldResult::PGreat && !note.endSampleId.empty())gAudio.trigger(note.endSampleId);
    if(note.mode==bms::LongNoteMode::LN)showTimingJudgement(result==HoldResult::PGreat?gHolds[lane].headJudgement():TimingJudgement::Poor,2);
    else if(note.mode==bms::LongNoteMode::CN && result==HoldResult::PGreat) {
        // HoldResult marks an accepted release; its exact grade must still use
        // the authored rank and the player's real release offset.
        // Autoplay releases on the tail itself.
        const auto grade=gAutoplay?TimingJudgement::PGreat:bms::judgeChartOffset(gCharts[gActiveChartIndex],gPlayhead,
            (judgePlayhead()-note.timeSeconds-note.lengthSeconds)/playbackTimingScale(),gJudgementProfile);
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
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(gLr2PlayScene)(gLr2Play.judgeSide?gLr2Play.judge2P:gLr2Play.judge)=5;
#endif
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

void loadPlayRecords() { gPlayRecordStore.load(); }

RecordChartId chartRecordId(const bms::Chart& chart) { return recordChartId(chart.path, chart.md5, chart.sha256); }

// Charts with a file hash use it as their record key. A record an older build stored under the
// chart's path moves to that key here, the first time the chart is listed, and the file is written once.
void rebuildChartRecordKeys() {
    gLr2ChartSummaries.clear();
    gChartRecordKeys.resize(gCharts.size());
    bool adopted = false;
    for (size_t i = 0; i < gCharts.size(); ++i) {
        const auto id = chartRecordId(gCharts[i]);
        gChartRecordKeys[i] = id.key;
        if (!id.legacyKey().empty() && gPlayRecords.contains(id.legacyKey())) adopted = gPlayRecordStore.adoptLegacyRecord(id) || adopted;
    }
    if (adopted) gPlayRecordStore.flush();
}

// The conditions the play just finished ran under, for the record.
RecordPlayOptions currentPlayOptions(const bms::Chart& chart) {
    RecordPlayOptions options;
    options.mode = static_cast<int>(chart.mode);
    options.gauge = activeGaugeModeText();
    options.lane = bms::laneOptionName(gPlayLaneOption);
    for (const int branch : chart.randomSelections) options.random += (options.random.empty() ? L"" : L",") + std::to_wstring(branch);
    return options;
}

// Seconds of song the play just finished lasted, at the speed it was played (pauses not counted).
int currentPlaySeconds() {
    const double scale = playbackTimingScale();
    if (!std::isfinite(gPlayhead) || gPlayhead <= 0 || !(scale > 0)) return 0;
    return static_cast<int>(std::min(gPlayhead / scale, 24.0 * 3600));
}

// Clear lamp of the play just finished: failed, the gauge's clear, full combo or perfect.
RecordClearInfo currentClearInfo(bool cleared) {
    RecordClearInfo info{RecordClearLamp::Failed, L"7KEYS", activeGaugeModeText()};
    if (!cleared) return info;
    const auto counts = gResultScore.judgementCounts();
    if (gResultScore.missCount() == 0 && gResultScore.noteCount() > 0)
        info.lamp = counts[1] == 0 && counts[2] == 0 ? RecordClearLamp::Perfect : RecordClearLamp::FullCombo;
    else
        info.lamp = gGrooveGaugeMode == GrooveGaugeMode::Easy ? RecordClearLamp::Easy :
            gGrooveGaugeMode == GrooveGaugeMode::Hard ? RecordClearLamp::Hard : RecordClearLamp::Normal;
    return info;
}

const PlayRecord* playRecordForChart(size_t chartIndex) {
    if (chartIndex >= gChartRecordKeys.size()) return nullptr;
    return gPlayRecordStore.findPlayed(gChartRecordKeys[chartIndex]);
}

void savePlayRecord(const bms::Chart& chart, bool cleared, unsigned maxCombo, int accuracyScore, const std::wstring& spec=L"LEGACY", int rank=-1) {
    if (!playSavesRecord()) return; // practice, diagnostic and autoplay plays are not records
    if (gPlayRecordStore.saveChart(chartRecordId(chart), cleared, maxCombo, accuracyScore, spec, rank, currentClearInfo(cleared),
            gResultScore.exScore(), gResultScore.judgementCounts(), gResultScore.missCount(), gResultScore.noteCount(),
            currentPlayOptions(chart), currentPlaySeconds()) && gList)
        InvalidateRect(gList, nullptr, FALSE);
}
// Tests and tools that only know a path record it under the path key.
void savePlayRecord(const std::filesystem::path& chartPath, bool cleared, unsigned maxCombo, int accuracyScore, const std::wstring& spec=L"LEGACY", int rank=-1) {
    bms::Chart chart; chart.path = chartPath;
    savePlayRecord(chart, cleared, maxCombo, accuracyScore, spec, rank);
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
    if(!gStartupSplash)gSelectionAudio.tickMusic(gVolumes[0],gSelectBgmVolume,gSongPreview.playing(),gSoundBufferFrames);
    const auto error = gSongPreview.lastError();
    if (!error.empty() && error != gBrowseAudioError) {
        gBrowseAudioError = error; setStatus(L"미리보기: " + error);
    }
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
#include "DifficultyTableHost.inl"
// The song list's filters: key mode (Tab, LR2 button 11), difficulty (LR2 button 10) and, inside a
// difficulty table level, the charts that level lists.
bool chartMatchesKeyMode(size_t chartIndex) {
    return chartIndex < gCharts.size() && bms::keyModeFilterMatches(gKeyModeFilter, gCharts[chartIndex].mode) &&
        bms::difficultyFilterMatches(gDifficultyFilter, gCharts[chartIndex].difficulty) && chartInTableLevel(gCharts[chartIndex]);
}
bool groupMatchesKeyMode(const bms::ui::SongGroup& group) {
    for (const size_t c : group.chartIndices) if (chartMatchesKeyMode(c)) return true;
    return false;
}
// Moves the group's displayed variant onto a chart that matches the key mode filter.
void normalizeGroupVariantForKeyMode(size_t index) {
    const auto& groups = gSongSelection.groups();
    if (index >= groups.size() || !groupMatchesKeyMode(groups[index])) return;
    const auto current = [&] { const auto& g = gSongSelection.groups()[index]; return g.chartIndices[std::min(g.selectedVariant, g.chartIndices.size() - 1)]; };
    if (chartMatchesKeyMode(current())) return;
    if (!gSongSelection.selectGroup(static_cast<int>(index))) return;
    for (size_t i = 0, n = groups[index].chartIndices.size(); i < n && !chartMatchesKeyMode(current()); ++i)
        if (!gSongSelection.cycleSelectedVariant(1)) break;
}
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
    rebuildTableLevels();
    const bool inTable=gTableView>=0;
    if(inTable)setText(gFolder,tableLocationText());
    const auto folderRow=[&](int id,const std::wstring& title){LVITEMW item{};item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=ListView_GetItemCount(gList);item.pszText=const_cast<wchar_t*>(bms::ui::entryTypeLabel(navigationKind(id)));item.lParam=id;const int row=ListView_InsertItem(gList,&item);ListView_SetItemText(gList,row,1,const_cast<wchar_t*>(title.c_str()));};
    if(inTable) {
        folderRow(-1,L"UP");
        if(gTableLevel<0) {
            const auto& loaded=gTables[static_cast<size_t>(gTableView)];
            for(size_t i=0;i<loaded.levels.size();++i)folderRow(levelRowBase-int(i),tableLevelLabel(loaded,loaded.levels[i]));
        }
    } else {
        if(gLibrarySources.configured()?!gLibrarySources.roots.empty():!gCurrentFolder.empty()){if(!gSongFolders.isRoot())folderRow(-1,L"UP");else folderRow(-2,L"ALL SONGS");}
        // Difficulty tables sit beside ALL SONGS at the root (not yet downloaded tables are not listed).
        if(gSongFolders.isRoot())for(size_t i=0;i<gTables.size();++i)if(!gTables[i].table.name.empty())folderRow(tableRowBase-int(i),tableDisplayName(gTables[i]));
        for(size_t i=0;i<gSongFolders.folders.size();++i)folderRow(-3-int(i),gSongFolders.folders[i].filename().wstring());
    }
    int selectedRow = -1;
    size_t hiddenCount = 0;
    for (const auto& chart : gCharts) if (!chart.playable) ++hiddenCount;
    const auto& groups = gSongSelection.groups();
    for (size_t i = 0; i < groups.size(); ++i) {
        const auto& group = groups[i];
        // A table level lists charts from the whole library; a table's level list has no charts.
        if (inTable && gTableLevel < 0) break;
        if (group.chartIndices.empty() || (!inTable && !gSongFolders.accepts(gCharts[group.chartIndices.front()].path)) || gCharts[group.chartIndices.front()].playable == gShowHiddenCharts) continue;
        if (!groupMatchesKeyMode(group)) continue;
        normalizeGroupVariantForKeyMode(i);
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
    const int id=back?-1:int(item.lParam);
    if(gTableView>=0&&id==-1){if(gTableLevel>=0)gTableLevel=-1;else gTableView=-1;} // out of a level, then out of the table
    else if(id<=levelRowBase){gTableLevel=levelRowBase-id;}
    else if(id<=tableRowBase){gTableView=tableRowBase-id;gTableLevel=-1;}
    else if(back){if(!gSongFolders.back())return false;}
    else gSongFolders.enter(id);
    rebuildSongRows();saveLibraryNavigation();
    triggerSystemSound(back||item.lParam==-1?bms::SystemSound::FolderClose:bms::SystemSound::FolderOpen);
    return true;
}

int monitorRefreshHz() {
    int refreshHz = 60;
    if (gWindow) {
        const HMONITOR monitor = MonitorFromWindow(gWindow, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEXW info{}; info.cbSize = sizeof(info);
        DEVMODEW mode{}; mode.dmSize = sizeof(mode);
        if (monitor && GetMonitorInfoW(monitor, &info) &&
            EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1)
            refreshHz = static_cast<int>(mode.dmDisplayFrequency);
    }
    return refreshHz;
}
UINT songListAnimationIntervalMs() {
    return static_cast<UINT>(std::clamp(static_cast<int>(std::lround(1000.0 / monitorRefreshHz())), 4, 16));
}
// Frame clock of the screens between select and play (song entry, loading lead-in, result). They used
// to repaint every 16 ms, which looked choppy on high refresh monitors; now they follow the monitor.
SelectFrameClock gTransitionFrameClock;
bool gTransitionClockActive{};
void tickTransitionFrame() {
    if(!gTransitionClockActive) {gTransitionFrameClock.setRate(monitorRefreshHz());gTransitionClockActive=true;}
    const auto now=SelectFrameClock::Clock::now();
    if(!gTransitionFrameClock.due(now))return;
    gTransitionFrameClock.advance(now);
    InvalidateRect(gWindow,nullptr,FALSE);UpdateWindow(gWindow);
}
DWORD transitionWaitMilliseconds() {return gTransitionFrameClock.waitMilliseconds(SelectFrameClock::Clock::now());}

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
    if (groupIndex < 0) return false;
    {
        // Skip variants that the key mode filter hides.
        const auto currentChart = [&] { const auto& g = gSongSelection.groups()[static_cast<size_t>(groupIndex)]; return g.chartIndices[std::min(g.selectedVariant, g.chartIndices.size() - 1)]; };
        const size_t startChart = currentChart();
        bool moved = false;
        for (size_t i = 0, n = gSongSelection.groups()[static_cast<size_t>(groupIndex)].chartIndices.size(); i < n; ++i) {
            if (!gSongSelection.cycleSelectedVariant(delta)) break;
            if (chartMatchesKeyMode(currentChart())) { moved = currentChart() != startChart; break; }
        }
        if (!moved) return false;
    }
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

void cycleKeyModeFilter(bool forward = true) {
    if (gPlaying || gShowingResult || gLoading) return;
    const int previous = gSongSelection.selectedGroupIndex();
    auto next = bms::nextKeyModeFilter(gKeyModeFilter);
    if (!forward) {
        while (bms::nextKeyModeFilter(next) != gKeyModeFilter) next = bms::nextKeyModeFilter(next);
    }
    gKeyModeFilter = next;
    gBrowseSelections.fill(-1);
    gBrowseSelections[gShowHiddenCharts ? 1 : 0] = previous;
    rebuildSongRows(previous);
    saveDisplaySettings();
    setStatus(std::wstring(L"키 모드: ") + bms::keyModeFilterLabel(gKeyModeFilter));
}

void cycleDifficultyFilter(bool forward = true) {
    if (gPlaying || gShowingResult || gLoading) return;
    const int previous = gSongSelection.selectedGroupIndex();
    gDifficultyFilter = bms::nextDifficultyFilter(gDifficultyFilter, forward);
    gBrowseSelections.fill(-1);
    gBrowseSelections[gShowHiddenCharts ? 1 : 0] = previous;
    rebuildSongRows(previous);
    saveDisplaySettings();
    setStatus(std::wstring(L"난이도: ") + bms::difficultyFilterLabel(gDifficultyFilter));
}

// EX score the play races against (LR2 play numbers 151 / 108, bar graphs 14 / 15).
int currentTargetExScore(std::optional<int> bestExScore, int notes) {
    return bms::targetExScore(gTargetKind, bestExScore, notes, gDefaultTargetRate);
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
    finishPresentDiagnostics();
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
    gLr2ChartSummaries.clear();
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

void releaseBgaBackplate() {
    if (gBgaBackplateDc && gBgaBackplateOriginalBitmap) SelectObject(gBgaBackplateDc, gBgaBackplateOriginalBitmap);
    if (gBgaBackplateBitmap) DeleteObject(gBgaBackplateBitmap);
    if (gBgaBackplateDc) DeleteDC(gBgaBackplateDc);
    gBgaBackplateDc = nullptr; gBgaBackplateBitmap = nullptr; gBgaBackplateOriginalBitmap = nullptr;
    gDirectBgaFrame.reset(); gDirectBgaOverlay.reset(); gDirectBgaSource.reset(); gDirectBgaVideo = {};
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
    const bool leavingResult = gShowingResult;
    gShowingResult = false;
    gSongIntro.reset();
#ifdef BMS_ENABLE_SKIN_BINDINGS
    gLr2ResultScene.reset();
    gLr2ResultExitStart={};
    KillTimer(gWindow,ID_LR2_RESULT_ANIMATION);
#endif
    // Ask about the auto-adjust suggestion only after the player has seen the result and left it.
    if (gAutoJudgePending) { gAutoJudgePending = false; PostMessageW(gWindow, WM_AUTO_JUDGE_SUGGEST, 0, 0); }
    finishPresentDiagnostics();
    gPlaying = false;
    gPaused = false;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    // Quitting from the pause menu must not leave the skin animations frozen on the select screen.
    setSevenManiaSkinAnimationPaused(false);
#endif
    gGpuFrame = false;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    gLr2PlayScene.reset();
#endif
    gCompositionPresenter.reset();
    gFeedback.reset();
    gGrooveGauge.reset();
    gPlayAbandoned = false;
    gPauseWhenPlayStarts = false;
    resetFailureEnding();
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
    if (leavingResult) triggerSystemSound(bms::SystemSound::ResultClose);
    gPressed.fill(false);gKeyboardPressed.fill(false);gControllerPressed.fill(false);gController.mapper.reset();clearAccuracyRestart();
    gHolds.fill(LongNoteHold{});
    gLingeringLongNotes.clear();gReleasedLongNotes.clear();
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

// Enter starts the selected chart, Shift+Enter opens practice and Ctrl+Enter starts it as autoplay.
void startSelectedChartFromEnter() {
    if (GetKeyState(VK_SHIFT) & 0x8000) openPracticeSetup();
    else beginSelectedChart(false, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
}

void beginSelectedChart(bool practice, bool autoplay) {
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
    bool decided=false;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    // An LR2 select skin with #FADEOUT fades out (timer 2) first; the song starts when the fade ends.
    if(gLr2SelectFadeAt!=std::chrono::steady_clock::time_point{}) {
        if(!gLr2SelectFadeFinishing)return; // Already fading toward a song.
        gLr2SelectFadeAt={};
        decided=true;
    // With an LR2 decide screen next, the screen changes at once with the decide sound: waiting for the
    // select fade put the entry screen about half a second behind its sound.
    } else if(gLr2SelectActive&&gLr2SelectScene&&gLr2SelectScene->fadeoutMilliseconds()>0&&!gNoInputBenchmark&&
              !(gIntroEnabled&&!gLr2DecideFile.empty())&&
              SetTimer(gWindow,ID_LR2_SELECT_FADE,static_cast<UINT>(gLr2SelectScene->fadeoutMilliseconds()),nullptr)) {
        gLr2SelectFadeAt=std::chrono::steady_clock::now();
        gLr2SelectFadeIndex=index;
        gLr2SelectFadePractice=practice;
        gLr2SelectFadeAutoplay=autoplay;
        gSelectionAudio.decide(gVolumes[0],gVolumes[3],gSoundBufferFrames);
        InvalidateRect(gWindow,nullptr,FALSE);
        return;
    }
#endif
    if (gNoInputBenchmark && !gBenchmarkAutoStarting) { gBenchmarkPlays = 0; gBenchmarkChart = path; }
    gPractice.enabled = practice;
    // Practice has its own start and speed; the no-input benchmark keeps its own note handling.
    gAutoplay = autoplay && !practice && !gNoInputBenchmark;
    if(!decided)gSelectionAudio.decide(gVolumes[0],gVolumes[3],gSoundBufferFrames);
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
    bool entryStarted=false;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(gIntroEnabled&&!gLr2DecideFile.empty()) {
        beginSongEntry(gCharts[index]);
        entryStarted=true;
    }
#endif
    for (HWND control : {gList, gFolder, gStatus, gInfo, gHiddenSongs})
        if (control) ShowWindow(control, SW_HIDE);
    setStatus(L"음원을 미리 디코딩하고 있습니다. 파일 수에 따라 잠시 걸릴 수 있습니다.");
    UpdateWindow(gWindow);
    const int videoWidth = gWindowWidth, videoHeight = gWindowHeight;
    const auto practiceSettings = gPractice;
    beginPlaybackLoadWorker(path,videoWidth,videoHeight,practiceSettings);
    if(!entryStarted)beginSongEntry(gCharts[index]);
    InvalidateRect(gWindow,nullptr,FALSE);UpdateWindow(gWindow);
}

#include "PlaybackLoadWorker.inl"

#ifdef BMS_ENABLE_SKIN_BINDINGS
// The LR2 play screen for a chart's key mode, or nothing (battle and other modes have none).
std::optional<sevenmania::skin::lr2::ScreenType> lr2PlayScreen(bms::ChartMode mode) {
    using sevenmania::skin::lr2::ScreenType;
    switch(mode) {
        case bms::ChartMode::SevenKey: return ScreenType::Play7Key;
        case bms::ChartMode::FiveKey: return ScreenType::Play5Key;
        case bms::ChartMode::FourteenKey: return ScreenType::Play14Key;
        case bms::ChartMode::TenKey: return ScreenType::Play10Key;
        case bms::ChartMode::PopnNine: return ScreenType::Play9Key;
        default: return std::nullopt;
    }
}
std::filesystem::path& lr2PlayFile(sevenmania::skin::lr2::ScreenType screen) {
    using sevenmania::skin::lr2::ScreenType;
    switch(screen) {
        case ScreenType::Play5Key: return gLr2Play5File;
        case ScreenType::Play14Key: return gLr2Play14File;
        case ScreenType::Play10Key: return gLr2Play10File;
        case ScreenType::Play9Key: return gLr2Play9File;
        default: return gLr2PlayFile;
    }
}
void loadLr2PlaySkin(const bms::Chart& chart) {
    using namespace sevenmania::skin::lr2;
    gLr2PlayScene.reset();
    gLr2Play=Lr2PlayState{};
    gLr2GpuFailed=false;
    const auto screen=lr2PlayScreen(chart.mode);
    if(!screen)return;
    const auto& skinFile=lr2PlayFile(*screen);
    if(skinFile.empty())return;
    if(!gCompositionEnabled) {
        reportLr2Failure(*screen,skinFile,L"GPU 렌더링을 사용할 수 없습니다");
        return;
    }
    try {
        Options options;
        options.screen=*screen;
        PlayFrame initial;
        initial.keys=playKeys(static_cast<int>(*screen));
        initial.bgaEnabled=gBgaBrightness>0;
        initial.hardGauge=gGrooveGaugeMode==GrooveGaugeMode::Hard;
        initial.hasBga=!chart.bga.empty()||!chart.fallbackVideo.empty();
        initial.hasLongNotes=std::any_of(chart.notes.begin(),chart.notes.end(),[](const auto& note){return note.longNote();});
        initial.difficulty=chart.difficulty;
        initial.scoreGraph=GetPrivateProfileIntW(L"LR2Skin",L"ScoreGraph",1,gSettingsFile.c_str())!=0;
        initial.minBpm=initial.maxBpm=chart.initialBpm;
        for(const auto& event:chart.tempos) {
            initial.minBpm=std::min(initial.minBpm,event.bpm);
            initial.maxBpm=std::max(initial.maxBpm,event.bpm);
        }
        options.condition=[&](int id)->std::optional<bool> {
            if(const auto enabled=initial.option(std::abs(id)))return *enabled==(id>0);
            return std::nullopt;
        };
        options.choices=lr2SkinChoices(skinFile);
        auto result=inspectLr2Scene(skinFile,options,nullptr,gLr2PlayWidth,gLr2PlayHeight);
        if(result.scene) {
            gLr2Play.start=gPractice.enabled?gPractice.startSeconds:0;
            gLr2Play.type=static_cast<int>(*screen);
            gLr2Play.noteCount=static_cast<int>(bms::judgementNoteCount(chart));
            gLr2Play.hasLongNotes=initial.hasLongNotes;
            gLr2Play.minBpm=initial.minBpm;gLr2Play.maxBpm=initial.maxBpm;
            gLr2Play.scoreGraph=initial.scoreGraph;
            if(const auto* record=gActiveChartIndex>=0?playRecordForChart(static_cast<std::size_t>(gActiveChartIndex)):nullptr;
               record&&record->played&&record->bestExScore&&*record->bestExScore<=2LL*gLr2Play.noteCount)
                gLr2Play.bestExScore=static_cast<int>(*record->bestExScore);
            auto& positions=gLr2Play.measurePositions;
            positions.reserve(chart.measureTimes.size());
            for(double seconds:chart.measureTimes)positions.push_back(playScrollPosition(chart,seconds));
            std::sort(positions.begin(),positions.end());
            gLr2Play.resetLeadIn();
            gLr2Play.leadInTimes={result.scene->loadStartMilliseconds(),result.scene->loadEndMilliseconds(),
                result.scene->playStartMilliseconds()};
            // The lead-in draws with the chart's static state until the song's frames take over.
            gLr2Play.frame=initial;
            gLr2Play.frame.remaining=chart.durationSeconds;
            gLr2PlayScene=std::move(result.scene);
            gLr2RuntimeErrors[static_cast<std::size_t>(options.screen)].clear();
        } else {
            reportLr2Failure(options.screen,skinFile,result.failure,result.diagnostics);
        }
        for(const auto& diagnostic:result.diagnostics) {
            const auto message=diagnostic.source.wstring()+L":"+std::to_wstring(diagnostic.line)+
                L" "+std::filesystem::path(std::u8string(diagnostic.message.begin(),diagnostic.message.end())).wstring()+L"\n";
            OutputDebugStringW(message.c_str());
        }
    } catch(const std::exception&) {
        gLr2PlayScene.reset();
        reportLr2Failure(*screen,skinFile,L"스킨 로드 예외");
    }
}
#endif
RECT playBgaCanvas() noexcept {
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(gLr2PlayScene&&!gLr2GpuFailed&&gLr2PlayScene->hasBga()) {
        const int size=std::min(gWindowWidth,gWindowHeight);
        return {0,0,size,size};
    }
#endif
    return {0,0,gWindowWidth,gWindowHeight};
}
#include "PlaybackLoading.inl"
#include "PlaybackLoadRuntime.inl"

#include "PlaybackTransitions.inl"

#include "GameplayInput.inl"

#include "PlaybackFrame.inl"

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

// Always-visible marker for no-input benchmark and autoplay plays (independent of the FPS/resource line
// and skins; an LR2 skin shows autoplay through its own option 33 instead).
void drawBenchmarkBadge(HDC dc) {
    if (!gPlaying || (!gNoInputBenchmark && !gAutoplay)) return;
    std::wstring text = gNoInputBenchmark ? L"BENCH  ·  NO INPUT" : L"AUTO PLAY  ·  기록되지 않음";
    if (gNoInputBenchmark && gBenchmarkRepeat > 0 && !gBenchmarkChart.empty())
        text += L"  ·  " + std::to_wstring(std::min(gBenchmarkPlays + 1, gBenchmarkRepeat)) + L"/" + std::to_wstring(gBenchmarkRepeat);
    const auto font = gPracticeHudFont ? gPracticeHudFont : gSmallFont;
    const RECT bounds{18, 14, 18 + 420, 40};
    if (dc == gBackBufferDc && gBackBufferPixels) {
        GdiFlush();
        if (gTextRaster.draw(gGpuFrame ? nullptr : static_cast<uint32_t*>(gBackBufferPixels), gBackBufferWidth, gBackBufferHeight,
            text, bounds, font, RGB(255, 214, 20), 1.0, drawLeftTextBody, false,
            gGpuFrame ? TextRasterCache::GpuDraw(queueGpuText) : TextRasterCache::GpuDraw{}, true)) return;
    }
    drawLeftTextBody(dc, text, bounds, font, RGB(255, 214, 20));
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
    {   DpiContextScope unaware(DPI_AWARENESS_CONTEXT_UNAWARE);
        GetWindowRect(gWindow, &owner);   // owner position in the dialog's (unaware) coordinates
        gPracticeWindow = CreateWindowExW(WS_EX_DLGMODALFRAME, type.lpszClassName, L"연습 모드 설정", WS_POPUP | WS_CAPTION | WS_SYSMENU,
            std::max(0L, (owner.left + owner.right - 600) / 2), std::max(0L, (owner.top + owner.bottom - 370) / 2), 600, 370,
            gWindow, nullptr, type.hInstance, nullptr); }
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

// Per-play presentation diagnostics (%APPDATA%\BmsPlayer\presentation-diagnostics.csv).
presentdiag::Session gPresentDiag;
// Per-frame CPU work for hitch attribution. Timestamps are set in WM_PAINT.
std::chrono::steady_clock::time_point gDiagPaintStart{}, gDiagPresentStart{}, gDiagLastPaintEntry{};
double gDiagLoopGapMs = 0;
uint64_t gDiagUploadBase = 0, gDiagTextBase = 0;
bool gDiagBgaRebuilt = false;
presentdiag::Config currentPresentConfig() {
    presentdiag::Config c;
    c.vsync = gTargetFps == vsyncFps; c.targetFps = gTargetFps < 0 ? 0 : gTargetFps;
    c.composition = gCompositionEnabled; c.directComposition = gDirectCompositionEnabled; c.hybrid = hybridRendererEnabled;
    c.width = gWindowWidth; c.height = gWindowHeight;
    {
        wchar_t label[128]{};
        const DWORD n = GetEnvironmentVariableW(L"SEVENMANIA_DIAG_VARIANT", label, 128);
        std::string variant;
        if (n && n < 128) for (const wchar_t* p = label; *p; ++p) if (*p < 128 && *p != L'"' && *p != L',') variant += char(*p);
        c.variant = (variant.empty() ? std::string("default") : variant) +
            (gCompositionPresenter.skipUnchangedUploads() ? "+skip-hud-upload" : "") +
            (gCapVblankPacing ? "+cap-vblank" : "") + (gVsyncVblankPacing ? "+vsync-vblank" : "") +
            (gVsyncVblankPacing && gVsyncReadyWake ? "+ready-wake" : "") +
            (gVblankLead != std::chrono::microseconds(2000) ? "+lead" + std::to_string(gVblankLead.count() / 1000) + "us" : "") +
            (gAlignedStallClassifier ? "" : "+raw-stall") +
            (gCompositionPresenter.frameLatency() != 1 ? "+latency" + std::to_string(gCompositionPresenter.frameLatency()) : "") +
            (BgaRaster::gpuSourceEnabled ? "+bga-gpu-scale" : "") + (VideoBga::nv12Output ? "+video-nv12" : "") +
            (gNoInputBenchmark ? "+noinput" : "");
    }
    MONITORINFOEXW monitor{}; monitor.cbSize = sizeof(monitor);
    if (GetMonitorInfoW(MonitorFromWindow(gWindow, MONITOR_DEFAULTTONEAREST), &monitor)) {
        DEVMODEW mode{}; mode.dmSize = sizeof(mode);
        if (EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1)
            c.refreshHz = int(mode.dmDisplayFrequency);
        RECT window{};
        c.coversMonitor = GetWindowRect(gWindow, &window) && EqualRect(&window, &monitor.rcMonitor);
    }
    {   // Mirrors FramePacer: VSync, or a cap within 5% of an integer multiple of the refresh period.
        bool multiple = false;
        if (c.refreshHz > 0 && c.targetFps > 0 && c.targetFps * 20 <= c.refreshHz * 21) {
            const double ratio = double(c.refreshHz) / c.targetFps; const double step = std::max(1.0, std::round(ratio));
            multiple = std::abs(ratio - step) <= 0.05 * step;
        }
        c.vblankAligned = vblankPacingForMode() && c.refreshHz > 0 && (c.vsync || multiple);
        c.alignedStallClassifier = gAlignedStallClassifier;
    }
    return c;
}
// Experimental pacing switches, read once at startup (A/B runs via tools/Start-PresentVariant.ps1).
//   SEVENMANIA_VBLANK_PACING=1      align frame starts to DWM vblanks (VSync, or cap <= refresh)
//   SEVENMANIA_VBLANK_LEAD_US=2000  start that long before the target vblank
//   SEVENMANIA_FRAME_LATENCY=2      output queue depth (default 1)
long long environmentInteger(const wchar_t* name, long long fallback) {
    wchar_t text[32]{};
    const DWORD n = GetEnvironmentVariableW(name, text, 32);
    if (!n || n >= 32) return fallback;
    wchar_t* end{}; const long long value = std::wcstoll(text, &end, 10);
    return end != text ? value : fallback;
}
bool vblankPacingForMode() { return gTargetFps == vsyncFps ? gVsyncVblankPacing : gCapVblankPacing; }
void applyPresentTuning() {
    const long long legacy = environmentInteger(L"SEVENMANIA_VBLANK_PACING", -1);
    gCapVblankPacing = environmentInteger(L"SEVENMANIA_CAP_VBLANK_PACING", legacy >= 0 ? legacy : 1) == 1;
    gVsyncVblankPacing = environmentInteger(L"SEVENMANIA_VSYNC_VBLANK_PACING", legacy >= 0 ? legacy : 0) == 1;
    gVsyncReadyWake = environmentInteger(L"SEVENMANIA_VSYNC_READY_WAKE", 1) == 1;
    gAlignedStallClassifier = environmentInteger(L"SEVENMANIA_ALIGNED_STALL_CLASSIFIER", 1) == 1;
    gNoInputBenchmark = environmentInteger(L"SEVENMANIA_NO_INPUT_BENCHMARK", 0) == 1;
    BgaRaster::gpuSourceEnabled = environmentInteger(L"SEVENMANIA_BGA_GPU_SCALE", 0) == 1;
    VideoBga::nv12Output = environmentInteger(L"SEVENMANIA_VIDEO_NV12", 1) == 1; // default on (measured: fewer hitches, 3.1 MB vs 8.3 MB per 1080p frame)
    gBenchmarkRepeat = static_cast<int>(std::clamp<long long>(environmentInteger(L"SEVENMANIA_BENCH_REPEAT", 2), 0, 100));
    gBenchmarkExit = environmentInteger(L"SEVENMANIA_BENCH_EXIT", 1) == 1;
    gVblankLead = std::chrono::microseconds(std::clamp<long long>(environmentInteger(L"SEVENMANIA_VBLANK_LEAD_US", 2000), 200, 8000));
    gCompositionPresenter.setFrameLatency(static_cast<unsigned>(std::clamp<long long>(environmentInteger(L"SEVENMANIA_FRAME_LATENCY", 1), 1, 3)));
}
void updateVblankAlignment() {
    if (!vblankPacingForMode()) { gFramePacer.clearVblank(); return; }
    DWM_TIMING_INFO info{}; info.cbSize = sizeof(info);
    LARGE_INTEGER now{}, frequency{};
    if (FAILED(DwmGetCompositionTimingInfo(nullptr, &info)) || !info.qpcRefreshPeriod || !info.qpcVBlank ||
        !QueryPerformanceCounter(&now) || !QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
        gFramePacer.clearVblank(); return;
    }
    const auto steadyNow = std::chrono::steady_clock::now();
    const auto toNs = [&](long long ticks) { return std::chrono::nanoseconds(static_cast<long long>(static_cast<double>(ticks) * 1e9 / static_cast<double>(frequency.QuadPart))); };
    const auto vblank = steadyNow - toNs(static_cast<long long>(now.QuadPart) - static_cast<long long>(info.qpcVBlank));
    gFramePacer.alignToVblank(vblank, toNs(static_cast<long long>(info.qpcRefreshPeriod)), gVblankLead);
}
uint64_t gDiagVideoOfferedBase = 0, gDiagVideoUploadsBase = 0;
void beginPresentDiagnostics() {
    gDiagVideoOfferedBase = gCompositionPresenter.videoOffered(); gDiagVideoUploadsBase = gCompositionPresenter.videoUploads();
    gBenchmarkSoundIndex = 0; // play start: no keysound has been triggered yet
    if (gActiveChartIndex >= 0 && gActiveChartIndex < int(gCharts.size())) {
        const auto& notes = gCharts[gActiveChartIndex].notes; // skip notes before the start (practice)
        while (gBenchmarkSoundIndex < notes.size() && notes[gBenchmarkSoundIndex].timeSeconds < gPlayhead - 0.05) ++gBenchmarkSoundIndex;
    }
    // SEVENMANIA_SKIP_UNCHANGED_UPLOADS=1 opts into skipping unchanged HUD/overlay uploads (A/B runs).
    wchar_t skip[8]{};
    const DWORD skipLength = GetEnvironmentVariableW(L"SEVENMANIA_SKIP_UNCHANGED_UPLOADS", skip, 8);
    gCompositionPresenter.setSkipUnchangedUploads(skipLength && skip[0] == L'1');
    gPresentDiag.begin(currentPresentConfig(), gCompositionPresenter.deferredFrames());
    gDiagLastPaintEntry = {}; gDiagLoopGapMs = 0; gDiagBgaRebuilt = false;
    gDiagUploadBase = gCompositionPresenter.uploadedBytes(); gDiagTextBase = gTextRasterCacheMisses;
}
void finishPresentDiagnostics() {
    if (!gPresentDiag.active()) return;
    auto summary = gPresentDiag.finish(gCompositionPresenter.deferredFrames());
    summary.videoNv12Offered = gCompositionPresenter.videoOffered() - gDiagVideoOfferedBase;
    summary.videoNv12Uploads = gCompositionPresenter.videoUploads() - gDiagVideoUploadsBase;
    summary.videoNv12Fail = gCompositionPresenter.videoFailStage();
    std::string chart;
    if (gActiveChartIndex >= 0 && gActiveChartIndex < int(gCharts.size())) {
        const auto name = gCharts[gActiveChartIndex].path.filename().u8string(); chart.assign(name.begin(), name.end());
    }
    try { presentdiag::append(sevenmania::settingsPath().parent_path() / L"presentation-diagnostics.csv", chart, summary); } catch (...) {}
}
void notePresentFallback() { gPresentDiag.fallback(gCompositionPresenter.lastFailureStage(), gCompositionPresenter.lastFailureResult()); }
void notePresentSubmitted(bool gpu) { if (gPaused) gPresentDiag.gap(); else if (gPlaying) gPresentDiag.submitted(gpu); }
void notePaintEntry() {
    const auto now = std::chrono::steady_clock::now();
    if (gPlaying && !gPaused && gDiagLastPaintEntry != std::chrono::steady_clock::time_point{})
        gDiagLoopGapMs = std::max(gDiagLoopGapMs, std::chrono::duration<double, std::milli>(now - gDiagLastPaintEntry).count());
    gDiagLastPaintEntry = gPlaying && !gPaused ? now : std::chrono::steady_clock::time_point{};
    gDiagPaintStart = now;
}
void notePresentWork() {
    const uint64_t upload = gCompositionPresenter.uploadedBytes(), text = gTextRasterCacheMisses;
    if (gPlaying && !gPaused) {
        const auto now = std::chrono::steady_clock::now();
        presentdiag::FrameWork work;
        work.paintMs = std::chrono::duration<double, std::milli>(gDiagPresentStart - gDiagPaintStart).count();
        work.presentMs = std::chrono::duration<double, std::milli>(now - gDiagPresentStart).count();
        work.loopGapMs = gDiagLoopGapMs;
        work.uploadBytes = upload >= gDiagUploadBase ? upload - gDiagUploadBase : 0;
        work.textMisses = text >= gDiagTextBase ? text - gDiagTextBase : 0;
        work.bgaRebuilt = gDiagBgaRebuilt;
        gPresentDiag.work(work);
    }
    gDiagLoopGapMs = 0; gDiagUploadBase = upload; gDiagTextBase = text; gDiagBgaRebuilt = false;
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
        const uint64_t submittedBefore = gCompositionPresenter.submittedFrames();
        if (!gCompositionPresenter.present(gWindow, gBackBufferPixels, gBackBufferWidth, gBackBufferHeight)) {
            gGpuUnavailable = true; notePresentFallback();
            InvalidateRect(gWindow, nullptr, FALSE); UpdateWindow(gWindow);
        } else {
            if (gCompositionPresenter.submittedFrames() != submittedBefore) notePresentSubmitted(true);
            if (!gCompositionPresenter.presentationDeferred()) KillTimer(gWindow, ID_PAUSE_PRESENT_RETRY);
        }
    } else if (gPaused || gTargetFps == vsyncFps) {
        // VSync may have skipped painting entirely while the output was busy.
        InvalidateRect(gWindow, nullptr, FALSE); UpdateWindow(gWindow);
    }
}

// Load judgement glyphs and grow raster surfaces before the first note, not on it.
void prewarmPlayText() {
    gPrewarmTextPending = false;
    const HFONT font = gJudgementFont ? gJudgementFont : gFont;
    if (!font) return;
    for (const wchar_t* word : {L"GREAT", L"GOOD", L"BAD", L"POOR", L"MINE"}) {
        const std::wstring text = std::wstring(word) + L" 0123456789";
        const bool rainbowWord = std::wstring_view(word) == L"GREAT"; // PGREAT uses the rainbow phase
        for (const int phase : {-1, 0}) {
            if (phase == 0 && !rainbowWord) continue;
            gHudTextRaster.prewarm(text, font, phase < 0 ? RGB(0,0,0) : RGB(0,0,1),
                [phase](HDC dc, const std::wstring& body, RECT bounds, HFONT face, COLORREF) { drawHudTextBody(dc, body, bounds, face, RGB(0,0,0), phase); });
        }
        gHudMaskRaster.prewarm(text, font, RGB(255,255,255), drawHudGlyphMask);
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
using sevenmania::skin::NotePart;
sevenmania::skin::NoteImages gNoteImages;

bool drawNoteImage(HDC dc, const sevenmania::skin::NoteImages::Image* image,
    int left, double top, int right, double bottom, const RECT& clip, float opacity = 1.0f) {
    if (!image) return false;
    const bool directRaster = dc == gBackBufferDc && gBackBufferPixels;
    if (!directRaster || !gNoteSubpixel || !gNoteAntialias) {
        top = std::round(top);
        bottom = std::round(bottom);
    }
    if (left >= right || top >= bottom || left >= clip.right || right <= clip.left ||
        top >= clip.bottom || bottom <= clip.top) return true;
    if (gGpuNotePass) {
        return gCompositionPresenter.bitmapSprite(image->gpuBitmap.Get(),
            D2D1::RectF(static_cast<float>(left), static_cast<float>(top), static_cast<float>(right), static_cast<float>(bottom)),
            opacity, &clip);
    }
    if (directRaster) {
        image->drawRaster(static_cast<uint32_t*>(gBackBufferPixels), gBackBufferWidth, gBackBufferHeight,
            left, top, right, bottom, clip, opacity);
        return true;
    }
    constexpr double coordinateLimit = std::numeric_limits<int>::max();
    if (top < -coordinateLimit || bottom > coordinateLimit || bottom - top > coordinateLimit) return false;
    const BLENDFUNCTION blend{AC_SRC_OVER, 0, static_cast<BYTE>(std::lround(opacity * 255)), AC_SRC_ALPHA};
    return AlphaBlend(dc, left, static_cast<int>(top), right - left, static_cast<int>(bottom - top),
        image->dc, 0, 0, image->width, image->height, blend) != FALSE;
}

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
        gDirectBgaFrame.reset(); gDirectBgaOverlay.reset(); gDirectBgaSource.reset(); gDirectBgaVideo = {};
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
    int fieldX, int laneWidth, int top, int judgeY, int scratchWidth = 0,
    std::uint64_t* lr2Revision = nullptr) {
    const RECT bgaViewport=playBgaViewport();
    const bool customViewport=lr2Revision||bgaViewport.left!=0||bgaViewport.top!=0||bgaViewport.right!=client.right||bgaViewport.bottom!=client.bottom;
    if(gPlaybackLoad.preparing()){if(!lr2Revision)FillRect(dc,&client,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));return;}
    const auto layout=bms::chartLayout(chart,fieldX,laneWidth,scratchWidth>0?scratchWidth:laneWidth);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    gBgaRaster.canvas(width, height);
    if (gBgaRaster.update(gPlayhead)) gBgaBackplateDirty = true;
    const bool hybrid = !lr2Revision && hybridRendererEnabled && gGpuFrame && dc == gBackBufferDc && gBackBufferPixels;
    static bool previousHybrid = false;
    static bool previousLr2 = false;
    static uint64_t backgroundGeneration = 0, backdropRevision = 0;
    if (previousHybrid != hybrid) { gBgaBackplateDirty = true; previousHybrid = hybrid; }
    if(previousLr2!=(lr2Revision!=nullptr)) {previousLr2=lr2Revision!=nullptr;gBgaBackplateDirty=true;}
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
        if(!lr2Revision)fillRect(dc, client, RGB(10, 14, 21));
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
        gDirectBgaFrame.reset(); gDirectBgaOverlay.reset(); gDirectBgaSource.reset(); gDirectBgaVideo = {};
    }
    // Retain the previous complete backdrop while a cold image is being prepared.
    if (gBgaBackplateDirty && imagesPrepared) {
        BMS_PERF_SCOPE(backplateBuildMs);
        BMS_PERF_SET(rebuiltBga, true); gDiagBgaRebuilt = true;
        const auto previousFrame = std::move(gDirectBgaFrame);
        gDirectBgaOverlay.reset(); gDirectBgaSource.reset(); gDirectBgaVideo = {};
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
                        { gDirectBgaFrame = {frame, frame->pixels.data()}; gDirectBgaVideo = nv12ViewOf(frame); }
                    if (!frame) {
                        gDirectBgaFrame = gBgaRaster.canvasFrame(chart,visibleImages[layer],blackKey,true);
                        if (gDirectBgaFrame) gDirectBgaSource = gBgaRaster.gpuSourceFrame(chart,visibleImages[layer],blackKey);
                    }
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
                if (gDirectBgaOverlay) { gDirectBgaFrame={base,base->pixels.data()}; gDirectBgaVideo = nv12ViewOf(base); }
            }
            if (!foundLayer && !opaqueMiss && videoFrame && videoFrame->width == width && videoFrame->height == height)
                { gDirectBgaFrame = {videoFrame, videoFrame->pixels.data()}; gDirectBgaVideo = nv12ViewOf(videoFrame); }
        }
        if (!gDirectBgaFrame) {
            const RECT cacheRect{ 0, 0, width, height };
            GdiFlush();
            std::fill_n(static_cast<uint32_t*>(gBgaBackplatePixels), static_cast<size_t>(width) * height,
                lr2Revision?0xff000000u:0xff0a0e15u);
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
    if(lr2Revision){*lr2Revision=backdropRevision;return;}
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
            darkness, lane, 100 - gLaneTransparency, regions, gDirectBgaFrame, gDirectBgaOverlay, gDirectBgaSource, gDirectBgaVideo);
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
void requestResultExit(bool clickSound=false) {
    if(!gShowingResult||gResultFade.active())return;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(gLr2ResultScene) {
        const auto now=std::chrono::steady_clock::now();
        if(gLr2ResultExitStart!=std::chrono::steady_clock::time_point{}||gLr2ResultStart==std::chrono::steady_clock::time_point{})return;
        // The first key shows the score update (timer 152) early; once it has been up long enough a key leaves.
        using sevenmania::skin::lr2::ResultKey;
        const double elapsed=std::chrono::duration<double,std::milli>(now-gLr2ResultStart).count();
        switch(sevenmania::skin::lr2::resultKey(gLr2ResultScene->resultInput(),elapsed,gLr2ResultKeyAt)) {
            case ResultKey::ShowUpdate:
                if(clickSound)gAudio.triggerEffect(L"click");
                gLr2ResultKeyAt=elapsed;
                InvalidateRect(gWindow,nullptr,FALSE);
                break;
            case ResultKey::Leave:
                if(clickSound)gAudio.triggerEffect(L"click");
                beginLr2ResultExit(now);
                break;
            case ResultKey::Ignored: break;
        }
        return;
    }
#endif
    if(clickSound)gAudio.triggerEffect(L"click");
    showBrowseView();
}
#ifdef BMS_ENABLE_SKIN_BINDINGS
#include "Lr2SelectHost.inl"
#endif
#include "PlayBgaViewport.inl"

#include "ScreenRender.inl"

#include "GameplayRender.inl"

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
    DpiContextScope gameDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);   // overlay matches the game window in true pixels
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

// Drawing fonts are authored at FHD sizes and recreated when the window scale changes.
void createScaledFonts() {
    for (HFONT* font : {&gPracticeHudFont, &gListSelectedFont, &gListLevelFont, &gListSelectedLevelFont, &gBrowseTitleFont, &gBrowseSongTitleFont,
        &gBrowseSongInfoFont, &gBrowseRankFont, &gBrowseLevelFont, &gBrowseMetadataFont, &gHudBoldFont, &gGaugeModeFont, &gComboFont, &gJudgementFont,
        &gResultRankFont, &gResultScoreFont, &gResultTitleFont, &gResultDetailsFont, &gPlayFont, &gPlaySmallFont, &gResultNextFont, &gListFont}) if (*font) { DeleteObject(*font); *font = nullptr; }
    gListFont = CreateFontW(-ui(25), 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    // Raster caches are keyed by HFONT handle, and GDI may reuse a deleted handle value.
    gTextRaster.clear(); gHudTextRaster.clear(); gHudMaskRaster.clear(); gResourceTextRaster.clear();
    gPlayFont = CreateFontW(-ui(20), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gResultNextFont = CreateFontW(-ui(22), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gPlaySmallFont = CreateFontW(-ui(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gPracticeHudFont = CreateFontW(-ui(18), 0, 0, 0, FW_LIGHT, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gListSelectedFont = CreateFontW(-ui(28), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gListLevelFont = CreateFontW(-ui(25), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gListSelectedLevelFont = CreateFontW(-ui(28), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gBrowseTitleFont = CreateFontW(-ui(80), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gBrowseSongTitleFont = CreateFontW(-ui(120), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gBrowseSongInfoFont = CreateFontW(-ui(48), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gBrowseRankFont = CreateFontW(-ui(64), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    gBrowseLevelFont = CreateFontW(-ui(28), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gBrowseMetadataFont = CreateFontW(-ui(30), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gHudBoldFont = CreateFontW(-ui(16), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gGaugeModeFont = CreateFontW(-ui(13),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");
    const bool hudFontLoaded = gEmbeddedHudFont.load();
    const auto hudFace = hudFontLoaded ? L"Michroma" : L"Segoe UI";
    gComboFont = CreateFontW(-ui(64), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, hudFace);
    gJudgementFont = CreateFontW(-ui(80), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, hudFace);
    gResultRankFont = CreateFontW(-ui(112), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gResultScoreFont = CreateFontW(-ui(52), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gResultTitleFont = CreateFontW(-ui(34), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    gResultDetailsFont = CreateFontW(-ui(20), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}
double gFontScale = 0;
// Recomputes the design scale from the client size; recreates scaled resources when it changed.
void refreshUiScale() {
    gUiScale = bms::ui::viewport(gWindowWidth, gWindowHeight).scale;
    gTimingHintScale = gUiScale;
    gNoteHeight = ui(kFixedNoteHeight);
    if (gFontScale > 0 && std::abs(gFontScale - gUiScale) < 0.005) return;
    gFontScale = gUiScale;
    createScaledFonts();
    // Main-window controls (all children of the true-pixel game window) follow the scale.
    if (gResultNext) SendMessageW(gResultNext, WM_SETFONT, reinterpret_cast<WPARAM>(gResultNextFont), TRUE);
    for (HWND button : gPauseButtons) if (button) SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(gResultNextFont), TRUE);
    if (gList) {
        SendMessageW(gList, WM_SETFONT, reinterpret_cast<WPARAM>(gListFont), TRUE);
        // ListView rows take the image height; the song list (and its RmlUi rows) scale with it.
        if (HIMAGELIST rows = ImageList_Create(1, ui(songListFallbackRowHeight), ILC_COLOR32, 1, 1)) {
            ListView_SetImageList(gList, rows, LVSIL_SMALL);
            if (gSongRowImages) ImageList_Destroy(gSongRowImages);
            gSongRowImages = rows;
        }
    }
}

void layoutControls() {
    if (!gList) return;
    RECT client{};
    if (GetClientRect(gWindow, &client) && client.right > 0 && client.bottom > 0) {
        gWindowWidth = client.right;
        gWindowHeight = client.bottom;
    }
    refreshUiScale();
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
    if (gResultNext) MoveWindow(gResultNext, std::max(12, gWindowWidth - ui(196)), std::max(12, gWindowHeight - ui(82)), ui(164), ui(52), TRUE);
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

#include "SettingsWindow.inl"

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
    const int command = key == VK_SPACE ? (gSummaryVisible ? ID_SETTINGS : ID_SUMMARY_SETTINGS) : key == VK_F2 ? ID_OPEN : key == VK_F5 ? ID_RESCAN : 0;
    if (!command || gStartupSplash || gPlaying || gShowingResult || gSettingsWindow || gPracticeWindow) return false;
    if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000) || (GetKeyState(VK_SHIFT) & 0x8000)) return false;
    if (message == WM_KEYDOWN && (flags & (1LL << 30)) == 0 && (!gLoading || command == ID_SUMMARY_SETTINGS))
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
        trackTop, guideTop, rowHeight, static_cast<float>(gUiScale));
    const HRESULT presented = EndBufferedPaint(buffer, painted ? TRUE : FALSE);
    return painted && SUCCEEDED(presented);
}

LRESULT CALLBACK songListSubclass(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
    if(gSongInfoVisible&&(message==WM_KEYDOWN||message==WM_LBUTTONDOWN||message==WM_MOUSEWHEEL)){if(message==WM_KEYDOWN&&wParam==VK_ESCAPE){gSongInfoVisible=false;ShowWindow(gList,SW_SHOWNA);InvalidateRect(gWindow,nullptr,FALSE);}return 0;}

    if(summaryInput(message,wParam,lParam,window))return 0;
#ifdef BMS_ENABLE_SKIN_BINDINGS
    if(lr2SelectInputLocked(message))return 0;
    if(message==WM_SETCURSOR&&lr2SelectSetCursor(lParam))return TRUE;
    if(lr2SelectInput(window,message,wParam,lParam))return 0;
#endif
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
    if (message == WM_KEYDOWN && !gLoading && wParam == VK_TAB) {
        if ((lParam & (1LL << 30)) == 0) cycleKeyModeFilter();
        return 0;
    }
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

#include "PlayerWindow.inl"

bool loadTurntableImageFile(const std::filesystem::path& path, TurntableImage& image) {
    return loadRasterPngFile(path, image, 64, 64);
}

} // namespace

#ifdef BMS_ENABLE_SKIN_BINDINGS
namespace { const DWORD gSkinDataOwnerThread=GetCurrentThreadId(); }
bool skinOwnsHud(){return gSkinApplication.active()&&!gSkinRetryPending;}
int selectSkinShortcutAt(int x,int y){return gSkinApplication.selectShortcutAt(x,y,gWindowWidth,gWindowHeight);}
bool skinNeedsJudgementText(){return gSkinApplication.needsJudgementText();}
bool skinCoversLegacySlots(int left,int top,int right,int bottom){return gSkinApplication.coversLegacySlots(left,top,right,bottom);}
// The skin shows the (live) key names itself when its gear has guide elements for this key mode.
// The skin has a native gear for this key mode, so it (not the engine deck) draws the buttons.
bool skinGearForMode(int gearMode){return gSkinApplication.active()&&gSkinApplication.supports(sevenmania::skin::ScreenKind::Play)&&(gearMode==0||gSkinApplication.supportsGearMode(gearMode));}
bool skinDrawsKeyGuides(int gearMode){return gSkinApplication.hasKeyGuides()&&(gearMode==0||gSkinApplication.supportsGearMode(gearMode));}
void prepareSkinForPlayback(){
    const bool gpuNotes = !hybridRendererEnabled && !gNoteImages.empty();
    if((!gPlaying&&!gPlaybackLoad.preparing())||(!gPlaybackLoad.preparing()&&!gSkinApplication.hasImageHud()&&!gpuNotes)||!gCompositionEnabled||gGpuUnavailable)return;
    gCompositionPresenter.setDirectComposition(gDirectCompositionEnabled);gCompositionPresenter.setVsync(gTargetFps==vsyncFps);gCompositionPresenter.setAntialias(gNoteAntialias);
    const bool previous=gGpuFrame;
    if(gLr2PlayScene) {
        gCompositionPresenter.setModernDrawing(true);
        if(gCompositionPresenter.prepare(gWindow,gWindowWidth,gWindowHeight))
            gLr2PlayScene->prepare(static_cast<ID2D1DeviceContext*>(gCompositionPresenter.renderTarget()));
    }
    else if(gCompositionPresenter.prepare(gWindow,gWindowWidth,gWindowHeight)){gGpuFrame=true;prepareAppliedSkinGpu();}
    else {gGpuUnavailable=true;notePresentFallback();}
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
void setSevenManiaSkinAnimationPaused(bool paused) {gSkinApplication.setAnimationPaused(paused);}
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
        provider.setWide(BindingId::SelectKeyMode,bms::keyModeFilterLabel(gKeyModeFilter));
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
            provider.set(BindingId::PlayPaused,gPaused);provider.set(BindingId::PlayKeyGuide,skinKeyGuideShown());
            if(!gChartOptions.crazy.contains(L"hidjudge")&&!gJudgementText.empty()&&gSkinJudgementGrade>=0){
                provider.set(BindingId::PlayJudgementGrade,static_cast<std::int64_t>(gSkinJudgementGrade));
                provider.set(BindingId::PlayJudgementAge,std::chrono::duration<double>(std::chrono::steady_clock::now()-gSkinJudgementAt).count());}
            if(skinNeedsJudgementText()&&!gChartOptions.crazy.contains(L"hidjudge") && !gJudgementText.empty() && std::chrono::steady_clock::now()<gJudgementUntil){provider.setWide(BindingId::PlayJudgement,gJudgementText);provider.setWide(BindingId::PlayJudgementCombo,combinedJudgementText(gJudgementText,gFeedback.combo()));}
        } else if(screen==ScreenKind::Result) {
            provider.set(BindingId::ResultScore,static_cast<std::int64_t>(gResultScore.value()));
            provider.setWide(BindingId::ResultRank,gResultScore.rank());
            provider.setWide(BindingId::ResultClearType,gPractice.enabled?L"PRACTICE":gAutoplay?L"AUTO PLAY":gResultGaugeFailed?L"FAILED":gResultGaugeCleared?L"CLEAR":L"NO CLEAR");
            provider.set(BindingId::ResultMaxCombo,static_cast<std::int64_t>(gFeedback.maxCombo()));
            provider.set(BindingId::ResultCleared,gResultGaugeCleared);
        }
    }
    provider.endFrame();
    return true;
}
#endif

namespace {
#include "PlayerLoop.inl"
#include "PlayerStartup.inl"
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    if (const auto result = handlePlayerArguments()) return *result;
    return runPlayer(instance, show);
}
