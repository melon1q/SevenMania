#pragma once
#include "Lr2ImageModel.h"
#include "Lr2ScreenState.h"
#include "Lr2FrameScale.h"
#include <memory>
#include <span>

struct ID2D1DeviceContext;
struct ID2D1Bitmap;

namespace sevenmania::skin::lr2 {
struct PlayLane { int left{}, right{}, noteBottom{}, noteHeight{}; };
struct NoteSprite { ID2D1Bitmap* bitmap{}; float x{}, y{}, width{}, height{}; };
struct PlayFrame {
    // Indexed by LR2 lane (0-9 1P, 10-19 2P), matching timers 50-69, 70-89, 100-119 and 120-139.
    std::array<double,maxPlayLanes> keyOn{}, keyOff{}, hold{}, bomb{};
    std::array<int,5> judgementCounts{};
    std::span<const float> measureOffsets;
    double judgementAge=-1, readyAge=-1, playAge=-1, gaugeIncreaseAge=-1, gaugeMaxAge=-1;
    // Before the song: false while loading (option 80/81), and the load bar (graph 2).
    bool loaded=true;
    float loadProgress=1.f;
    // The drawn lane cover, 0 open to 1 closed (sliders 4 and 5). laneCover stays the setting.
    float laneCoverShare{};
    // Milliseconds since the last beat, or -1 outside play.
    double rhythmAge=-1;
    // Notes judged so far; the "current rank" options rate the score against these only.
    int judgedNotes{};
    // A result screen: its graph and score-update timers run from the start of the scene.
    bool result{};
    // Milliseconds since the play failed, since the closing fade began and since the full combo; -1 before.
    double failedAge=-1, fadeoutAge=-1, fullComboAge=-1;
    // For the result screen: the gauge over the whole song at even intervals, 0 to 100, with -1 where
    // the play never got to; and the best max combo before this play, or -1.
    std::vector<float> gaugeHistory;
    int bestMaxCombo=-1;
    std::optional<int> bestExScore;
    // MY BEST before this play for the result's target numbers (120-134) and previous min BP (176):
    // its score points, judgements and lowest miss count. Empty when the record does not hold them.
    std::optional<int> bestPoints, bestMissCount;
    std::optional<std::array<int,5>> bestJudgements;
    // EX score the play aims at (LR2's MY BEST target): the best before this play, or 0 without one.
    int targetExScore{};
    // Option 39 (score graph shown) and 38 (hidden).
    bool scoreGraph=true;
    // Built-in offsets 1 and 2: the 1P and 2P turntable rotation in degrees.
    std::array<float,2> turntableAngle{};
    // Part of the chart judged so far, 0 to 1: the "now" graphs and the target difference scale by it.
    double judgedShare() const noexcept {return noteCount>0?std::clamp(double(judgedNotes)/noteCount,0.,1.):0.;}
    // Fill ratio of a BARGRAPH type, or nothing when the type is not a play-screen graph.
    std::optional<float> graph(int type) const noexcept;
    double gauge{}, bpm{}, minBpm{}, maxBpm{}, elapsed{}, remaining{}, hiSpeed{};
    int judge=-1, combo{}, maxCombo{}, points{}, exScore{}, noteCount{}, laneCover{}, difficulty{};
    // The 2P side of a double play skin: its latest judgement and milliseconds since it.
    int judge2P=-1;
    double judgementAge2P=-1;
    // Keys of the chart being played: 7, 5, 14, 10 or 9.
    int keys=7;
    float judgeLineOffset{};
    bool bgaEnabled{}, bgaExtended{}, hardGauge{}, autoplay{}, hasBga{}, hasLongNotes{};
    ID2D1Bitmap* bga{};
    PlayFrame() {keyOn.fill(-1);keyOff.fill(-1);hold.fill(-1);bomb.fill(-1);}
    double timer(int id,double sceneMilliseconds) const noexcept;
    std::optional<bool> option(int id) const noexcept;
    std::optional<int> number(int id) const noexcept;
};
enum class DrawPass { All, BeforeNotes, AfterNotes, NativeBeforeNotes, OutputBeforeNotes };
struct ScreenData {
    std::array<std::wstring,7> songText; // LR2 string references 10 through 16.
    std::filesystem::path stageImage;
    PlayFrame numbers;
};
struct SelectRow {
    std::wstring title,level;
    // lamp is 0 not played, 1 failed, 2 easy, 3 normal, 4 hard, 5 full combo.
    int body{},lamp{};
    // difficulty is 0 (unset) to 5; levelValue is the numeric play level or -1;
    // rank is 0 (no score), then 1 F up to 8 AAA.
    int difficulty{},levelValue=-1,rank{};
    int row=-1;
};
// The best record of the selected chart. A field the record does not hold is empty and is not drawn.
struct SelectRecord {
    std::optional<int> exScore, missCount, playCount, clearCount, maxCombo;
    std::optional<std::array<int,5>> judgements;
    // Notes the rates divide by: the chart's when known, else the count saved with the record.
    int noteCount{};
};
// Every saved play added up (select numbers 30-37).
struct SelectTotals {
    long long playCount{},clearCount{};
    std::array<long long,5> judgements{};
    long long playSeconds{};
};
// Event timers of the select, decide and result screens in milliseconds since each began, or -1.
struct ScreenTimers {
    double fadeout=-1;  // Timer 2: the closing fade.
    double skip=-1;     // Timer 3 on the decide screen: a key skipped it.
    // Timer 152 on the result screen (score update shown). Empty follows the scene clock (preview).
    std::optional<double> updateScore;
};
struct SelectFrame {
    ScreenData song;
    SelectRecord record;
    std::filesystem::path banner;
    std::array<SelectRow,maxSelectBars> rows;
    bool songSelected{},hasBanner{};
    int difficulty{},level=-1;
    // State of the selected chart. keys is 5, 7, 9, 10 or 14; judgeRank is #RANK (0 very hard .. 3 easy)
    // or -1; clearLamp is 0 not played, 1 failed, 2 easy, 3 normal, 4 hard, 5 full combo.
    int keys{},judgeRank=-1,clearLamp{};
    // Best score rank of the selected chart: 0 no score, then 1 F up to 8 AAA.
    int rank{};
    bool hasBga{},hasLongNotes{},bpmChange{};
    // Cell of the gauge button: LR2 orders groove, survival, death, easy, p-attack, g-attack.
    int gaugeCell{};
    // The charts filed with the selected one, by difficulty 1 to 5: how many, and the level of the first.
    std::array<int,5> folderCharts{},folderLevels{-1,-1,-1,-1,-1};
    // Rows the list still has to travel after a move: 1 right after moving down one row, easing to 0.
    float scroll{};
    // Cursor in window pixels; negative when it is not over the window.
    float mouseX=-1,mouseY=-1;
    // Conditions this frame can answer; anything else is decided when the skin loads.
    std::optional<bool> option(int id) const noexcept;
    // Record numbers 71, 73 and 75-89; nothing for other IDs or when the record lacks the value.
    std::optional<int> recordNumber(int id) const noexcept;
    static bool isRecordNumber(int id) noexcept {return id==71||id==73||(id>=75&&id<=89);}
    // Totals of every record: 30 plays, 31 clears, 32 failures, 33-37 PG GR GD BD PR. Drawn with or without a song.
    SelectTotals totals;
    std::optional<int> totalNumber(int id) const noexcept;
    static bool isTotalNumber(int id) noexcept {return (id>=17&&id<=19)||(id>=30&&id<=37);}
    // Timer 2: milliseconds since a song was chosen and the screen began to fade out, or -1.
    double fadeoutAge=-1;
    // Fill ratio of a BARGRAPH type, or nothing when the type is not a select-screen graph.
    std::optional<float> graph(int type) const noexcept;
    // Highest level a level graph can show for the selected chart's key mode.
    int levelLimit() const noexcept {return keys==5||keys==10?9:keys==9?42:12;}
    double selectionAge{};
    int panel{},keyMode{};
    // Difficulty filter cell (0 all, 1-5 BEGINNER..INSANE), target kind (0 MY BEST, 1-3 RANK AAA/AA/A,
    // 4 DEFAULT) and the default target's percent (number 13).
    int difficultyFilter{},targetKind{},targetRate=50;
    // 1P lane option (0 OFF, 1 MIRROR, 2 RANDOM, 3 S-RANDOM: button 42, string 63) and the best clear's.
    int laneOption{},bestLaneOption{};
    float listPosition{};
    bool canScroll{};
    std::array<float,3> volumes{};
    std::array<double,6> panelOn{-1,-1,-1,-1,-1,-1},panelOff{-1,-1,-1,-1,-1,-1};
    // Play settings the option panels show and change. hiSpeed is the note speed multiplier, judgeTiming
    // the judge offset in milliseconds, laneCover the lane cover percent (0 is off), autoJudge 0 off,
    // 1 suggest, 2 realtime, and screenMode 0 window, 1 borderless, 2 fullscreen.
    double hiSpeed=1;
    int judgeTiming{},laneCover{},autoJudge{},screenMode{};
    // HI-SPEED FIX: 0 off, 1 start, 2 max, 3 main, 4 min BPM, 5 constant.
    int hiSpeedFix{};
    bool bgaEnabled=true,scoreGraph=true;
    // Gauge of the selected chart's best clear as a gauge cell (0 groove, 1 survival, 3 easy), or -1.
    int bestGauge=-1;
    // Milliseconds each LR2 key (0-9 1P, 10-19 2P) has been held, or -1: timers 100-119.
    std::array<double,20> keyOn{-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
    // Cell of a select button: the setting it shows, or the "none" cell of a feature SevenMania lacks.
    // Nothing for buttons this player does not understand.
    std::optional<int> buttonState(int id) const noexcept;
    // Buttons a click acts on; the others only show their state.
    static bool clickableButton(int id) noexcept;
    // Option panel labels (LR2 string references 1, 60-85); empty for references without a label.
    std::wstring optionText(int ref) const;
    static bool isOptionText(int ref) noexcept;
    // String reference 10-16 (the song) or an option label.
    std::wstring text(int ref) const;
    // Setting numbers: 10 hi-speed x100, 12 judge timing.
    std::optional<int> settingNumber(int id) const noexcept;
    static bool isSettingNumber(int id) noexcept {return id==10||id==12||id==13;}
    // Sliders of features SevenMania lacks (EQ, effects, pitch), held at their neutral position.
    static std::optional<float> fixedSlider(int id) noexcept;
};
struct SelectBar {float x{},y{},width{},height{};bool clickable{};};
struct SelectControl {
    int button{},slider{},clickMode{};
    float x{},y{},width{},height{},dx{},dy{},angle{},pivotX{},pivotY{};
    int row=-1;
    float viewportScaleX=1.f,viewportScaleY=1.f,viewportOffsetX{},viewportOffsetY{};
    bool contains(float x,float y) const noexcept;
    float sliderPosition(float x,float y) const noexcept;
    bool increment(float x,float y) const noexcept;
private:
    void unrotate(float& x,float& y) const noexcept;
};
class PreviewScene {
public:
    PreviewScene();
    ~PreviewScene();
    bool load(const Document& document, std::vector<Diagnostic>& diagnostics, bool play=false,const ScreenData* screen=nullptr);
    bool prepare(ID2D1DeviceContext* target);
    void releaseDevice();
    void setStretch(bool stretch) noexcept;
    std::optional<FrameScale> upscale(UpscaleFilter filter, int width, int height) const noexcept;
    void draw(ID2D1DeviceContext* target, double milliseconds, int width, int height,
              const PlayFrame* play=nullptr,DrawPass pass=DrawPass::All,const SelectFrame* select=nullptr,
              const ScreenTimers& timers={}, const FrameScale* upscale=nullptr,
              bool inheritTargetTransform=false) const;
    int selectCenter() const noexcept;
    std::optional<SelectBar> selectBar(int slot,int width,int height) const noexcept;
    std::optional<SelectControl> selectControlAt(float x,float y) const noexcept;
    bool updateSelectBanner(const std::filesystem::path& path);
    bool hasBga() const noexcept;
    // Zero keeps the original BGA canvas for skins with any BGA layer in the native pass.
    int outputBgaCanvasSize(const FrameScale& scale) const noexcept;
    // The skin draws the lane cover itself (play slider 4 or 5), so the player's own cover stays off.
    bool hasLaneCover() const noexcept;
    // Select MOUSECURSOR: the skin draws the cursor, so the host hides the system one over the window.
    bool hasMouseCursor() const noexcept;
    int sceneMilliseconds() const noexcept;
    int startInputMilliseconds() const noexcept;
    // Result #STARTINPUT with its rank and update times.
    ResultInput resultInput() const noexcept;
    // Decide #SKIP: milliseconds after which a key ends the screen early; 0 if not declared.
    int skipMilliseconds() const noexcept;
    // Play #LOADSTART, #LOADEND, #PLAYSTART in milliseconds; 0 if not declared.
    int loadStartMilliseconds() const noexcept;
    int loadEndMilliseconds() const noexcept;
    int playStartMilliseconds() const noexcept;
    // Play #SCRATCHSIDE for player 0 (1P) or 1 (2P): 0 left, 1 right.
    int scratchSide(int player) const noexcept;
    // Play #CLOSE and #FADEOUT of play, select, decide and result, or 0.
    int closeMilliseconds() const noexcept;
    int fadeoutMilliseconds() const noexcept;
    ID2D1Bitmap* updateBga(const std::uint32_t* pixels,int width,int height,std::uint64_t revision);
    std::size_t imageCount() const noexcept;
    std::size_t layerCount() const noexcept;
    // States a select button's picture shows (the fewest among its layers), or 0 without that button.
    int buttonStates(int id) const noexcept;
    // Indexed by SevenMania lane (see lr2Lane); lanes the key mode lacks have zero width.
    std::optional<std::array<PlayLane,16>> playLanes(int width,int height) const noexcept;
    // Draw the skin's SRC_AUTO_* notes, where it has them, instead of its usual notes.
    void setAutoplay(bool autoplay) noexcept;
    NoteSprite noteSprite(int lane,double milliseconds,PlayNotePart part=PlayNotePart::Tap,
                          double heldMilliseconds=-1) const noexcept;
private:
    std::optional<SelectBar> fittedSelectBar(int slot,int width,int height) const noexcept;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

int runPreview(const std::filesystem::path& file, unsigned designWidth=0, unsigned designHeight=0);
}
