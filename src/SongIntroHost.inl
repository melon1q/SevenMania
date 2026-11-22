// Selection-time foreground. The UI owns this snapshot; no decoder-owned PCM/video is touched.
#ifdef BMS_ENABLE_SKIN_BINDINGS
sevenmania::skin::SkinDataProvider gSongEntryData;

struct Lr2LaunchOptions {
 std::filesystem::path play,play5,play14,play10,play9,decide,result,select;
 unsigned width{},height{};
};
std::optional<Lr2LaunchOptions> parseLr2LaunchOptions(int count,wchar_t* const* args) {
 if(count<3)return std::nullopt;
 Lr2LaunchOptions options;
 const bool preview=std::wstring_view(args[1])==L"--lr2-preview";
 for(int i=1;i<count;i+=2) {
  const std::wstring_view flag=args[i];
  if(i+1==count)return std::nullopt;
  const std::wstring_view value=args[i+1];
  if(value.empty()||value.starts_with(L"--"))return std::nullopt;
  if(((preview&&i==1)||(!preview&&flag==L"--lr2-play-skin"))&&options.play.empty())
   options.play=value;
  else if(!preview&&flag==L"--lr2-play5-skin"&&options.play5.empty())
   options.play5=value;
  else if(!preview&&flag==L"--lr2-play14-skin"&&options.play14.empty())
   options.play14=value;
  else if(!preview&&flag==L"--lr2-play10-skin"&&options.play10.empty())
   options.play10=value;
  else if(!preview&&flag==L"--lr2-play9-skin"&&options.play9.empty())
   options.play9=value;
  else if(!preview&&flag==L"--lr2-decide-skin"&&options.decide.empty())
   options.decide=value;
  else if(!preview&&flag==L"--lr2-result-skin"&&options.result.empty())
   options.result=value;
  else if(!preview&&flag==L"--lr2-select-skin"&&options.select.empty())
   options.select=value;
  else if(flag==L"--resolution"&&!options.width) {
   if(value==L"640x480"){options.width=640;options.height=480;}
   else if(value==L"1280x720"){options.width=1280;options.height=720;}
   else if(value==L"1920x1080"){options.width=1920;options.height=1080;}
   else if(value==L"3840x2160"){options.width=3840;options.height=2160;}
   else return std::nullopt;
  } else return std::nullopt;
 }
 if(options.play.empty()&&options.play5.empty()&&options.play14.empty()&&options.play10.empty()&&options.play9.empty()&&((options.decide.empty()&&options.result.empty()&&options.select.empty())||options.width))return std::nullopt;
 return options;
}

sevenmania::skin::lr2::ScreenData lr2SongData(const bms::Chart& chart) {
 const auto join=[](const std::wstring& first,const std::wstring& second) {
  return first.empty()?second:second.empty()?first:first+L" "+second;
 };
 sevenmania::skin::lr2::ScreenData data;
 data.songText={chart.title,chart.subtitle,join(chart.title,chart.subtitle),chart.genre,
     chart.artist,chart.subartist,join(chart.artist,chart.subartist)};
 return data;
}

// The user's CUSTOMOPTION/CUSTOMFILE/CUSTOMOFFSET selections for one skin file.
sevenmania::skin::lr2::SkinChoices lr2SkinChoices(const std::filesystem::path& skin) {
 try {
  if(!gSettingsFile.empty())
   return sevenmania::skin::lr2::loadSkinChoices(gSettingsFile.parent_path()/L"lr2-skins.txt",skin);
 } catch(const std::exception&) {
 }
 return {};
}

std::unique_ptr<sevenmania::skin::lr2::PreviewScene> loadLr2Screen(const std::filesystem::path& path,
    sevenmania::skin::lr2::Options options,const sevenmania::skin::lr2::ScreenData& data) {
 using namespace sevenmania::skin::lr2;
 if(path.empty())return {};
 if(!gCompositionEnabled) {
  reportLr2Failure(options.screen,path,L"GPU 렌더링을 사용할 수 없습니다");
  return {};
 }
 try {
  options.choices=lr2SkinChoices(path);
  auto result=inspectLr2Scene(path,options,&data);
  if(result.scene) {
   gLr2RuntimeErrors[static_cast<std::size_t>(options.screen)].clear();
   return std::move(result.scene);
  }
  reportLr2Failure(options.screen,path,result.failure,result.diagnostics);
 } catch(const std::exception&) {
  reportLr2Failure(options.screen,path,L"스킨 로드 예외");
 }
 return {};
}

void loadLr2Decide(const bms::Chart& chart) {
 using namespace sevenmania::skin::lr2;
 if(gLr2DecideFile.empty())return;
 auto data=lr2SongData(chart);
 std::error_code error;
 const bool stage=!chart.titleImage.empty()&&std::filesystem::is_regular_file(chart.titleImage,error);
 if(stage)data.stageImage=chart.titleImage;
 Options options;
 options.screen=ScreenType::Decide;
 options.initialOptions={{52,true},{53,false},{190,!stage},{191,stage}};
 for(int difficulty=0;difficulty<=5;++difficulty)options.initialOptions[150+difficulty]=chart.difficulty==difficulty;
 gLr2DecideScene=loadLr2Screen(gLr2DecideFile,options,data);
}

bool lr2DecideShowing() {return gLoading&&songEntryActive()&&gLr2DecideScene!=nullptr;}

// Scene time of the first key on the LR2 decide screen that skipped it, and of the first key on the
// result screen (it shows the score update early); -1 before.
double gLr2DecideSkipAt=-1;
double gLr2ResultKeyAt=-1;

void loadLr2Result() {
 using namespace sevenmania::skin::lr2;
 gLr2ResultScene.reset();
 if(gLr2ResultFile.empty()||gActiveChartIndex<0||gActiveChartIndex>=static_cast<int>(gCharts.size()))return;
 const auto& chart=gCharts[gActiveChartIndex];
 auto data=lr2SongData(chart);
 auto& stats=data.numbers;
 stats.noteCount=static_cast<int>(bms::judgementNoteCount(chart));
 stats.points=gResultScore.value()/5;
 stats.exScore=gResultScore.exScore();
 stats.judgedNotes=stats.noteCount;
 stats.result=true;
 stats.maxCombo=static_cast<int>(gFeedback.maxCombo());
 for(std::size_t grade=0;grade<stats.judgementCounts.size();++grade)
  stats.judgementCounts[grade]=gLr2Play.counts[grade]/2;
 // Slots the play never reached stay -1.
 stats.hardGauge=gGrooveGaugeMode==GrooveGaugeMode::Hard;
 // The record as it was before this play, for the new-record conditions.
 ResultState state;
 state.cleared=gResultGaugeCleared;
 state.exScore=stats.exScore;
 state.noteCount=stats.noteCount;
 state.maxCombo=stats.maxCombo;
 state.missCount=gResultScore.missCount();
 state.recordSaved=playSavesRecord();
 state.autoplay=gAutoplay;
 if(gLr2Play.gaugeSamples>1) {
  stats.gaugeHistory.assign(gLr2Play.gaugeHistory.size(),-1.f);
  std::copy_n(gLr2Play.gaugeHistory.begin(),gLr2Play.gaugeSamples,stats.gaugeHistory.begin());
 }
 // The record is saved after this, so it still holds the best before this play.
 stats.targetExScore=currentTargetExScore(std::nullopt,stats.noteCount);
 if(const auto* record=playRecordForChart(static_cast<std::size_t>(gActiveChartIndex));record&&record->played) {
  stats.bestMaxCombo=static_cast<int>(record->maxCombo);
  if(record->bestExScore&&*record->bestExScore<=2LL*stats.noteCount)stats.bestExScore=record->bestExScore;
  stats.targetExScore=currentTargetExScore(stats.bestExScore,stats.noteCount);
  stats.bestPoints=record->bestAccuracyScore/5;
  state.played=true;
  state.bestExScore=stats.bestExScore;
  state.bestMaxCombo=stats.bestMaxCombo;
  // Counts beyond this chart's notes belong to a different version of it; leave them unrecorded.
  if(record->minMissCount&&*record->minMissCount<=stats.noteCount)stats.bestMissCount=record->minMissCount;
  state.bestMissCount=stats.bestMissCount;
  if(record->bestJudgements) {
   long long total=0;
   for(const int count:*record->bestJudgements)total+=count;
   if(total<=stats.noteCount)stats.bestJudgements=record->bestJudgements;
  }
 }
 Options options;
 options.screen=ScreenType::Result;
 options.initialOptions=resultOptions(state);
 gLr2ResultScene=loadLr2Screen(gLr2ResultFile,options,data);
 gLr2ResultStart={};
 gLr2ResultExitStart={};
 gLr2ResultKeyAt=-1;
 constexpr UINT resultFrameIntervalMs=(1000+30)/60;
 if(gLr2ResultScene&&!SetTimer(gWindow,ID_LR2_RESULT_ANIMATION,resultFrameIntervalMs,nullptr)) {
  reportLr2Failure(ScreenType::Result,gLr2ResultFile,L"결과 애니메이션 타이머 시작 실패");
  gLr2ResultScene.reset();
 }
}

void beginLr2ResultExit(std::chrono::steady_clock::time_point now) {
 if(gLr2ResultExitStart!=std::chrono::steady_clock::time_point{})return;
 if(gLr2ResultScene->fadeoutMilliseconds()==0) {
  showBrowseView();
  return;
 }
 gLr2ResultExitStart=now;
 InvalidateRect(gWindow,nullptr,FALSE);
}

bool presentLr2Screen(sevenmania::skin::lr2::PreviewScene& scene,double elapsed,int width,int height,
    sevenmania::skin::lr2::SelectFrame* select=nullptr,const sevenmania::skin::lr2::ScreenTimers& timers={},
    const sevenmania::skin::lr2::PlayFrame* play=nullptr) {
 gCompositionPresenter.setModernDrawing(true);
 gCompositionPresenter.setDirectComposition(select||gDirectCompositionEnabled);
 gCompositionPresenter.setVsync(gTargetFps==vsyncFps);
 if(!gCompositionPresenter.prepare(gWindow,width,height))return false;
 auto* target=static_cast<ID2D1DeviceContext*>(gCompositionPresenter.renderTarget());
 if(!scene.prepare(target))return false;
 if(select)select->hasBanner=scene.updateSelectBanner(select->banner);
 gCompositionPresenter.setFrameOverlay(nullptr);
 gCompositionPresenter.setFrameForeground(nullptr);
 gCompositionPresenter.setFrameIntro(nullptr);
 struct DrawData {sevenmania::skin::lr2::PreviewScene& scene;double elapsed;sevenmania::skin::lr2::SelectFrame* select;
  const sevenmania::skin::lr2::ScreenTimers& timers;
  const sevenmania::skin::lr2::PlayFrame* play;};
 DrawData data{scene,elapsed,select,timers,play};
 return gCompositionPresenter.present(gWindow,gBackBufferPixels,width,height,nullptr,0,0,0,
  {[](void* payload,ID2D1RenderTarget* drawing,int w,int h) {
   const auto& frame=*static_cast<DrawData*>(payload);
   drawing->Clear(D2D1::ColorF(D2D1::ColorF::Black));
   frame.scene.draw(static_cast<ID2D1DeviceContext*>(drawing),frame.elapsed,w,h,frame.play,
       sevenmania::skin::lr2::DrawPass::All,frame.select,frame.timers);
  },&data});
}
// The LR2 play scene before the song, while the chart loads. False leaves the black loading screen.
bool presentLr2PlayLeadIn(int width,int height) {
 auto& state=gLr2Play;
 if(!gPlaybackJob.active||!gPlaybackJob.applied||!gLr2PlayScene||gLr2GpuFailed||state.leadInFailed||songEntryActive())return false;
 if(state.leadInStart==std::chrono::steady_clock::time_point{})state.leadInStart=std::chrono::steady_clock::now();
 const auto lead=sevenmania::skin::lr2::playLeadIn(state.leadInTimes,state.leadInAge(),state.loadedAt);
 auto frame=state.frame;
 frame.loaded=lead.loaded;frame.loadProgress=lead.progress;
 frame.readyAge=lead.readyAge;frame.playAge=-1;frame.elapsed=0;
 frame.laneCover=std::max(gLaneCoverPercent,gChartOptions.hidden==L"m-sudden"?gAuthoredCoverPercent:0);
 frame.laneCoverShare=sevenmania::skin::lr2::laneCoverDisplay(frame.laneCover,-1,0);
 if(!presentLr2Screen(*gLr2PlayScene,lead.sceneAge,width,height,nullptr,{},&frame)) {
  state.leadInFailed=true;
  return false;
 }
 // The loader waits for a paint before preparing GPU resources.
 gPlaybackJob.painted=true;
 return true;
}
// The decide clock starts when its first frame is on screen: preparing the scene's textures takes
// 150 ms or more, and starting earlier skipped the opening of the animation.
bool gLr2DecideClockPending{};
int gSongEntryDuration{};
bool presentLr2Decide(int width,int height) {
 if(gLr2DecideClockPending) {
  if(!presentLr2Screen(*gLr2DecideScene,0,width,height))return false;
  gLr2DecideClockPending=false;
  gSongIntro.start(gSongEntryDuration);
  return true;
 }
 const double elapsed=gSongIntro.elapsedMilliseconds();
 sevenmania::skin::lr2::ScreenTimers timers;
 timers.fadeout=sevenmania::skin::lr2::decideFadeoutAge(gLr2DecideScene->sceneMilliseconds(),elapsed,gLr2DecideSkipAt);
 timers.skip=sevenmania::skin::lr2::decideSkipAge(elapsed,gLr2DecideSkipAt);
 return presentLr2Screen(*gLr2DecideScene,elapsed,width,height,nullptr,timers);
}
#endif
void beginSongEntry(const bms::Chart& chart){
 int duration=SongIntroOverlay::kFixedMilliseconds;
 gSongIntro.reset();
#ifdef BMS_ENABLE_SKIN_BINDINGS
 gLr2DecideScene.reset();
#endif
 if(!gIntroEnabled)return;
 gSongIntro.prepare(chart,RECT{0,0,gWindowWidth,gWindowHeight},SongIntroOverlay::kFixedMilliseconds,nullptr);
#ifdef BMS_ENABLE_SKIN_BINDINGS
 loadLr2Decide(chart);
 gLr2DecideSkipAt=-1;
 // #SCENETIME, then timer 2 for #FADEOUT.
 if(gLr2DecideScene&&gLr2DecideScene->sceneMilliseconds()>0)
  duration=sevenmania::skin::lr2::decideDuration(gLr2DecideScene->sceneMilliseconds(),gLr2DecideScene->fadeoutMilliseconds());
 using namespace sevenmania::skin;
 gSongEntryData.beginFrame(ScreenKind::SongIntro);
 gSongEntryData.setWide(BindingId::SongTitle,chart.title);gSongEntryData.setWide(BindingId::SongSubtitle,chart.subtitle);
 gSongEntryData.setWide(BindingId::SongArtist,chart.artist);gSongEntryData.setWide(BindingId::SongGenre,chart.genre);
 gSongEntryData.set(BindingId::SongBpm,chart.initialBpm);gSongEntryData.setWide(BindingId::ChartLevelText,chart.playLevel);
 if(const auto level=bms::numericPlayLevel(chart))gSongEntryData.set(BindingId::ChartLevel,*level);
 gSongEntryData.endFrame();
 if(gSkinApplication.supports(ScreenKind::SongIntro)&&gSkinApplication.hasBox("title",ScreenKind::SongIntro)){
  const int w=std::clamp(gWindowWidth,1,1920),h=std::clamp(gWindowHeight,1,1080);
  std::error_code ec;const std::vector<uint32_t> empty;
  const auto& pixels=!chart.titleImage.empty()&&std::filesystem::is_regular_file(chart.titleImage,ec)?gSkinTitleBackdrop.render(&chart,2,{},0,100,w,h,0):empty;
  if(!pixels.empty())gSkinApplication.updateBox("title",pixels.data(),w,h,ScreenKind::SongIntro);
  else {const std::uint32_t blank=0;gSkinApplication.updateBox("title",&blank,1,1,ScreenKind::SongIntro);}
  gSkinTitleRevision=0; // Browse refresh must restore its own selected image.
 }
#endif
 gSongIntro.start(duration);
#ifdef BMS_ENABLE_SKIN_BINDINGS
 gSongEntryDuration=duration;
 gLr2DecideClockPending=gLr2DecideScene!=nullptr;
#endif
}
bool songEntryActive(){return gSongIntro.active();}
// An LR2 decide screen with #SKIP ends early on a key once that many milliseconds (and #STARTINPUT) have
// passed: timer 3 starts and the screen closes #FADEOUT later.
bool skipSongEntry(){
#ifdef BMS_ENABLE_SKIN_BINDINGS
 if(!gSongIntro.active()||!gLr2DecideScene)return false;
 const auto& scene=*gLr2DecideScene;
 const double elapsed=gSongIntro.elapsedMilliseconds();
 if(!sevenmania::skin::lr2::decideCanSkip(scene.skipMilliseconds(),scene.startInputMilliseconds(),scene.sceneMilliseconds(),elapsed,gLr2DecideSkipAt))
  return false;
 if(scene.fadeoutMilliseconds()<=0) {
  gSongIntro.reset();
  return true;
 }
 gLr2DecideSkipAt=elapsed;
 gSongIntro.shorten(elapsed+scene.fadeoutMilliseconds());
 return true;
#else
 return false;
#endif
}
void drawSongEntry(HDC dc){
 RECT all{0,0,gWindowWidth,gWindowHeight};FillRect(dc,&all,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
#ifdef BMS_ENABLE_SKIN_BINDINGS
 if(gSkinApplication.supports(sevenmania::skin::ScreenKind::SongIntro)&&gSkinApplication.drawDc(dc,gWindowWidth,gWindowHeight,gSongEntryData.snapshot()))return;
#endif
 gSongIntro.draw(dc);
}
