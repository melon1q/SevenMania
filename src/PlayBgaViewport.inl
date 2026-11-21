RECT playBgaViewport(){
 RECT full{0,0,gWindowWidth,gWindowHeight};
#ifdef BMS_ENABLE_SKIN_BINDINGS
 using namespace sevenmania::skin;
 if(const auto* screen=gSkinApplication.screenLayout(ScreenKind::Play))for(const auto& e:screen->elements)if(e.id=="play-bga-viewport"){
  // Same height-based scale and anchors as the skin renderer.
  const auto view=bms::ui::viewport(gWindowWidth,gWindowHeight,screen->resolution.width,screen->resolution.height);
  const auto anchors=computeAnchors(screen->elements,screen->resolution);
  const auto r=bms::ui::mapRect(view,{e.x,e.y,e.width,e.height},anchors[static_cast<size_t>(&e-screen->elements.data())]);
  return {LONG(std::lround(r.x)),LONG(std::lround(r.y)),LONG(std::lround(r.x+r.width)),LONG(std::lround(r.y+r.height))};
 }
#endif
 return full;
}
