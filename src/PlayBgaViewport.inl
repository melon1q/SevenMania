RECT playBgaViewport(){
 RECT full{0,0,gWindowWidth,gWindowHeight};
#ifdef BMS_ENABLE_SKIN_BINDINGS
 using namespace sevenmania::skin;
 if(const auto* screen=gSkinApplication.screenLayout(ScreenKind::Play))for(const auto& e:screen->elements)if(e.id=="play-bga-viewport"){
  const double scale=std::min(double(gWindowWidth)/screen->resolution.width,double(gWindowHeight)/screen->resolution.height);
  const double x=(gWindowWidth-screen->resolution.width*scale)/2,y=(gWindowHeight-screen->resolution.height*scale)/2;
  return {LONG(x+e.x*scale),LONG(y+e.y*scale),LONG(x+(e.x+e.width)*scale),LONG(y+(e.y+e.height)*scale)};
 }
#endif
 return full;
}
