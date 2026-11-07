// In-window summary; shares the existing settings controller and persistence.
bool gSummaryVisible=false;int gSummaryRow=0;std::wstring gSummaryError;
constexpr int summaryCommands[]{ID_SPEED,ID_GROOVE_GAUGE,ID_ACCURACY_LIMIT,ID_VOLUME_BASE};
RECT summaryBounds(){int w=std::min(560,gWindowWidth-24),h=std::min(380,gWindowHeight-24);return {(gWindowWidth-w)/2,(gWindowHeight-h)/2,(gWindowWidth+w)/2,(gWindowHeight+h)/2};}
// Use the same contain transform for drawing and hit testing.
RECT summaryElement(const char* id,RECT fallback){
#ifdef BMS_ENABLE_SKIN_BINDINGS
 using namespace sevenmania::skin;
 if(const auto* layout=gSkinApplication.screenLayout(ScreenKind::SettingsSummary)){
  const double scale=std::min(double(gWindowWidth)/layout->resolution.width,double(gWindowHeight)/layout->resolution.height);
  const double ox=(gWindowWidth-layout->resolution.width*scale)/2,oy=(gWindowHeight-layout->resolution.height*scale)/2;
  for(const auto& e:layout->elements)if(e.id==id&&e.visible)return {LONG(ox+e.x*scale),LONG(oy+e.y*scale),LONG(ox+(e.x+e.width)*scale),LONG(oy+(e.y+e.height)*scale)};
 }
#endif
 return fallback;
}
RECT summaryRowBounds(int i){auto r=summaryBounds();return summaryElement(("summary-row-"+std::to_string(i)).c_str(),{r.left+16,r.top+64+i*48,r.right-16,r.top+108+i*48});}
void toggleSummary(){gSummaryVisible=!gSummaryVisible;
#ifdef BMS_ENABLE_SKIN_BINDINGS
 if(gSummaryVisible)gSkinApplication.enterScreen(sevenmania::skin::ScreenKind::SettingsSummary);
#endif
gSummaryError.clear();SetFocus(gWindow);InvalidateRect(gWindow,nullptr,FALSE);}
void changeSummary(int direction){int id=summaryCommands[gSummaryRow],v=readSettingValue(id);if(id==ID_SPEED)v=std::clamp(v+direction,2,40);else if(id==ID_VOLUME_BASE)v=std::clamp(v+direction*5,0,100);else {int count=id==ID_GROOVE_GAUGE?3:5;v=(v+direction+count)%count;}auto result=settingsController().set(id,v);gSummaryError=result.error;InvalidateRect(gWindow,nullptr,FALSE);}
bool summaryInput(UINT message,WPARAM key,LPARAM data,HWND source=nullptr){if(!gSummaryVisible)return false;
 if(message==WM_KEYDOWN){if(key==VK_ESCAPE||key==VK_SPACE){if(!(data&(1LL<<30))){toggleSummary();if(key==VK_SPACE)openFullSettings();}}else if(key==VK_UP)gSummaryRow=(gSummaryRow+3)%4;else if(key==VK_DOWN)gSummaryRow=(gSummaryRow+1)%4;else if(key==VK_LEFT||key==VK_RIGHT)changeSummary(key==VK_RIGHT?1:-1);InvalidateRect(gWindow,nullptr,FALSE);return true;}
 if(message==WM_LBUTTONDOWN||message==WM_RBUTTONDOWN){
  POINT point{short(LOWORD(data)),short(HIWORD(data))};if(source&&source!=gWindow)MapWindowPoints(source,gWindow,&point,1);
  auto panel=summaryElement("summary-panel",summaryBounds());
  if(!PtInRect(&panel,point)){toggleSummary();return true;}
  for(int i=0;i<4;++i){auto row=summaryRowBounds(i);if(PtInRect(&row,point)){gSummaryRow=i;changeSummary(message==WM_RBUTTONDOWN||point.x<(row.left+row.right)/2?-1:1);break;}}
  return true;
 }
 return message==WM_KEYUP||message==WM_CHAR||message==WM_MOUSEWHEEL||message==WM_LBUTTONUP||message==WM_RBUTTONUP;
}
void drawSummaryFallback(HDC dc){if(!gSummaryVisible)return;auto r=summaryBounds();fillRect(dc,r,RGB(24,28,34));int saved=SaveDC(dc);IntersectClipRect(dc,r.left,r.top,r.right,r.bottom);SelectObject(dc,gFont);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(240,240,240));RECT title{r.left+20,r.top+16,r.right-20,r.top+50};DrawTextW(dc,L"설정 / 요약",-1,&title,DT_LEFT|DT_SINGLELINE);
 const wchar_t* labels[]{L"노트 배속",L"게이지 모드",L"정확도 제한",L"마스터 음량"};
 for(int i=0;i<4;++i){RECT row{r.left+16,r.top+64+i*48,r.right-16,r.top+108+i*48};if(i==gSummaryRow)fillRect(dc,row,RGB(55,70,86));int value=readSettingValue(summaryCommands[i]);std::wstring v=i==0?number(value/4.0,2)+L"x":i==1?std::wstring(value==0?L"NORMAL":value==1?L"EASY":L"HARD"):i==2?std::wstring(accuracyLimitLabels[value]):std::to_wstring(value)+L"%";auto text=std::wstring(labels[i])+L"     <  "+v+L"  >";row.left+=12;DrawTextW(dc,text.c_str(),-1,&row,DT_LEFT|DT_VCENTER|DT_SINGLELINE);}
 RECT help{r.left+20,r.top+270,r.right-20,r.bottom-12};auto text=gSummaryError.empty()?L"↑↓ 항목 · ←→ 변경 · 클릭 증가 / 우클릭 감소\nSpace 전체 설정 · Esc 닫기 · 변경 즉시 저장":gSummaryError.c_str();DrawTextW(dc,text,-1,&help,DT_LEFT|DT_WORDBREAK);RestoreDC(dc,saved);
}

void drawSummary(HDC dc){
 if(!gSummaryVisible)return;
#ifdef BMS_ENABLE_SKIN_BINDINGS
 using namespace sevenmania::skin;
 if(gSkinApplication.supports(ScreenKind::SettingsSummary)){
  SkinDataProvider data;data.beginFrame(ScreenKind::SettingsSummary);
  data.setWide(BindingId::SummarySpeed,L"< "+number(readSettingValue(ID_SPEED)/4.0,2)+L"x >");
  const int gauge=readSettingValue(ID_GROOVE_GAUGE);
  data.setWide(BindingId::SummaryGauge,gauge==0?L"< NORMAL >":gauge==1?L"< EASY >":L"< HARD >");
  data.setWide(BindingId::SummaryAccuracy,accuracyLimitLabels[readSettingValue(ID_ACCURACY_LIMIT)]);
  data.setWide(BindingId::SummaryVolume,L"< "+std::to_wstring(readSettingValue(ID_VOLUME_BASE))+L"% >");data.endFrame();
  if(gSkinApplication.drawDc(dc,gWindowWidth,gWindowHeight,data.snapshot())){
   auto row=summaryRowBounds(gSummaryRow);COLORREF accent=RGB(88,190,225);if(const auto* layout=gSkinApplication.screenLayout(ScreenKind::SettingsSummary))for(const auto& e:layout->elements)if(e.id=="summary-accent")accent=RGB(e.tint.r,e.tint.g,e.tint.b);auto brush=CreateSolidBrush(accent);FrameRect(dc,&row,brush);DeleteObject(brush);
   if(!gSummaryError.empty()){auto panel=summaryElement("summary-panel",summaryBounds());panel.top=panel.bottom-54;SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,160,160));DrawTextW(dc,gSummaryError.c_str(),-1,&panel,DT_CENTER|DT_WORDBREAK);}
   return;
  }
 }
#endif
 drawSummaryFallback(dc);
}

// Consume the whole Esc press so key-repeat cannot fall through to exit confirmation.
bool handleOverlayEscape(UINT message,WPARAM key){
 static bool consumed=false;if(key!=VK_ESCAPE)return false;
 if(message==WM_KEYUP){const bool result=consumed;consumed=false;return result;}
 if(message!=WM_KEYDOWN)return false;
 if(consumed)return true;
 if(gSummaryVisible){toggleSummary();consumed=true;return true;}
 if(gSongInfoVisible){gSongInfoVisible=false;InvalidateRect(gWindow,nullptr,FALSE);consumed=true;return true;}
 return false;
}
