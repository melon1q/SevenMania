// Selection-time foreground. The UI owns this snapshot; no decoder-owned PCM/video is touched.
#ifdef BMS_ENABLE_SKIN_BINDINGS
sevenmania::skin::SkinDataProvider gSongEntryData;
#endif
void beginSongEntry(const bms::Chart& chart){
 gSongIntro.reset();if(!gIntroEnabled)return;
 gSongIntro.prepare(chart,RECT{0,0,gWindowWidth,gWindowHeight},gIntroDuration,nullptr);
#ifdef BMS_ENABLE_SKIN_BINDINGS
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
 gSongIntro.start();
}
bool songEntryActive(){return gSongIntro.active();}
void drawSongEntry(HDC dc){
 RECT all{0,0,gWindowWidth,gWindowHeight};FillRect(dc,&all,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
#ifdef BMS_ENABLE_SKIN_BINDINGS
 if(gSkinApplication.supports(sevenmania::skin::ScreenKind::SongIntro)&&gSkinApplication.drawDc(dc,gWindowWidth,gWindowHeight,gSongEntryData.snapshot()))return;
#endif
 gSongIntro.draw(dc);
}
