bool previewKey(WPARAM key){
 if(key==VK_RETURN){gPlayPreview.requested=gPlaybackLoad.canStart();return true;}
 if((key==VK_PRIOR||key==VK_NEXT||key==VK_UP||key==VK_DOWN)&&!(GetKeyState(VK_CONTROL)&0x8000))return false;
 if(key==VK_F1||key==VK_F2)gNoteSpeed=std::clamp(gNoteSpeed+(key==VK_F2?.25:-.25),.5,10.);
 else if(key==VK_PRIOR||key==VK_NEXT)gLaneCoverPercent=std::clamp(gLaneCoverPercent+(key==VK_PRIOR?1:-1),0,100);
 else if(key==VK_UP||key==VK_DOWN)gJudgeLineOffset=std::clamp(gJudgeLineOffset+(key==VK_UP?-1:1),-4096,4096);
 else if(key==VK_LEFT||key==VK_RIGHT)gGearPosition=std::clamp(gGearPosition+(key==VK_LEFT?-1:1),0,2);
 else return false;
 gPlayPreview.touched();gPlayPreview.requested=false;gPlaybackJob.previewDirty=true;saveDisplaySettings();return true;
}
void drawPreviewNotes(HDC dc){
 const int w=LaneLayout::defaultKeyWidth,scratch=LaneLayout::defaultScratchWidth;
 const auto base=bms::chartLayout(gCharts[gActiveChartIndex],0,w,scratch);const int available=std::max(0,gWindowWidth-base.totalWidth()),edge=std::min(64,available/2);
 const int left=gGearPosition==0?edge:gGearPosition==2?available-edge:available/2;
 const auto lanes=bms::chartLayout(gCharts[gActiveChartIndex],left,w,scratch);
 const int judge=bms::optionJudgeLine(gChartOptions,gPlayhead,judgementLineY(gWindowHeight),gWindowHeight),guard=judge*gLaneCoverPercent/100;
 for(int i=0;i<7;++i)if(gLaneSeparators[i]){const int x=lanes.left(i+1);drawLaneSeparator(dc,{x,0,x+1,judge},judge);}
 const RECT clip{left,std::max(PLAYFIELD_TOP,guard),left+lanes.totalWidth(),judge};
 const double speed=270*(gChartOptions.hiSpeed>0?gChartOptions.hiSpeed:gNoteSpeed)*bms::speedAt(gCharts[gActiveChartIndex],gPlayhead),time=gPlayPreview.seconds();
 // One whole chord traverses the field, followed by a short empty interval.
 const double travel=std::max(0,judge-PLAYFIELD_TOP)+gNoteHeight*2;
 const double duration=travel/std::max(.001,speed);
 const double phase=std::fmod(time,duration+.35);
 const double y=PLAYFIELD_TOP-gNoteHeight+phase*speed;
 if((GetKeyState(VK_CONTROL)&0x8000)&&phase<=duration)for(int lane=0;lane<8;++lane){
  if(!gPlayPreview.group[lane]||y+gNoteHeight<clip.top||y>=judge)continue;
  fillGameRectSubpixel(dc,lanes.left(lane),y,lanes.right(lane),y+gNoteHeight,gPalette[gLaneColors[lane%8]].color,clip);
 }
 if(!gPlaybackLoad.canStart()){
  RECT info{left,std::max(0,judge-48),left+lanes.totalWidth(),judge-8};
  SetTextColor(dc,RGB(230,230,230));SetBkMode(dc,TRANSPARENT);
  DrawTextW(dc,L"LOADING...",-1,&info,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
 }
}
