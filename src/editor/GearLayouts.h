#pragma once
#include "EditorDocument.h"
#include "skin/GearMode.h"
namespace sevenmania::editor {
inline const wchar_t* gearLayoutName(int position){return position==0?L"left":position==2?L"right":L"center";}
inline int editorGearPosition(const Document& doc){const auto& root=doc.screens.at(doc.screen);return int(root[L"extensions"][L"editorGearPosition"].num(1));}
inline bool followsGear(const J& e,int position){
 if(e[L"extensions"][L"nativeGearPart"].boolean||e[L"extensions"][L"followGear"].boolean)return true;
 if(e[L"binding"].str()==L"play.judgementCombo")return false;
 if(e[L"type"].str()!=L"text"&&e[L"type"].str()!=L"number")return false;
 const double centers[]={342,960,1578};const double x=e[L"x"].num()+e[L"width"].num()/2;
 return x>=centers[position]-278&&x<=centers[position]+278&&e[L"y"].num()>=0&&e[L"y"].num()<1080;
}
inline int editorGearMode(const Document& doc){
 const auto found=doc.screens.find("play");if(found==doc.screens.end())return 0;
 return std::clamp(int(found->second[L"extensions"][L"editorGearMode"].num(0)),0,sevenmania::skin::gearModeCount-1);
}
inline bool isGearElement(const J& e){return e[L"extensions"][L"nativeGearPart"].boolean||e[L"extensions"][L"followGear"].boolean;}
// The play screen's gear parts are edited one key mode at a time. The elements themselves always hold
// the geometry of the mode being edited; the 7K geometry is the persisted base and the other modes live
// under extensions.gearModes.<name> = {x,y,width,height,visible}. gearModes.7k is only an editor cache
// of the base geometry (the runtime never reads it).
struct PlayScreenScope{Document& doc;std::string previous;explicit PlayScreenScope(Document& d):doc(d),previous(d.screen){doc.screen="play";}~PlayScreenScope(){doc.screen=previous;}};
inline void setEditorGearMode(Document& doc,int mode){auto& ext=doc.screens.at("play").object[L"extensions"];ext.type=T::Object;ext.object[L"editorGearMode"]=num(mode);}
inline void storeGearMode(Document& doc){
 if(!doc.screens.contains("play"))return;PlayScreenScope scope(doc);
 const auto name=sevenmania::skin::gearModeName(editorGearMode(doc));
 for(auto& e:doc.elements()){
  if(!isGearElement(e))continue;
  auto& ext=e.object[L"extensions"];ext.type=T::Object;
  auto& modes=ext.object[L"gearModes"];modes.type=T::Object;
  auto& entry=modes.object[name];entry.type=T::Object;
  for(auto key:{L"x",L"y",L"width",L"height"})entry.object[key]=e[key];
  entry.object[L"visible"]=jsonBool(e[L"visible"].type==T::Boolean?e[L"visible"].boolean:true);
 }
}
inline void applyGearMode(Document& doc,int mode){
 if(!doc.screens.contains("play"))return;PlayScreenScope scope(doc);
 const auto name=sevenmania::skin::gearModeName(mode),base=sevenmania::skin::gearModeName(0);
 for(auto& e:doc.elements()){
  if(!isGearElement(e))continue;
  const auto& modes=e[L"extensions"][L"gearModes"];
  const auto& source=modes[name].type==T::Object?modes[name]:modes[base];
  if(source.type!=T::Object)continue;
  for(auto key:{L"x",L"y",L"width",L"height"})if(source[key].type==T::Number)e.object[key]=source[key];
  if(source[L"visible"].type==T::Boolean)e.object[L"visible"]=source[L"visible"];
 }
}
inline void switchGearMode(Document& doc,int next){
 if(!doc.screens.contains("play"))return;
 const int previous=editorGearMode(doc);if(previous==next||next<0||next>=sevenmania::skin::gearModeCount)return;
 storeGearMode(doc);applyGearMode(doc,next);setEditorGearMode(doc,next);
}
// Runs fn with the persisted (7K) geometry in the play elements, then restores the edited mode.
template<class F> void withBaseGearGeometry(Document& doc,F&& fn){
 const int mode=editorGearMode(doc);
 if(mode==0){fn();return;}
 storeGearMode(doc);applyGearMode(doc,0);setEditorGearMode(doc,0);
 const auto restore=[&]{applyGearMode(doc,mode);setEditorGearMode(doc,mode);};
 try{fn();}catch(...){restore();throw;}
 restore();
}
inline void storeGearLayout(Document& doc){if(doc.screen!="play"||editorGearMode(doc)!=0)return;const int position=editorGearPosition(doc);const auto name=gearLayoutName(position);for(auto& e:doc.elements()){auto& ext=e.object[L"extensions"];ext.type=T::Object;if(followsGear(e,position))ext.object[L"followGear"]=jsonBool(true);auto& layouts=ext.object[L"gearLayouts"];layouts.type=T::Object;auto& layout=layouts.object[name];layout.type=T::Object;for(auto key:{L"x",L"y"})layout.object[key]=e[key];if(followsGear(e,position)&&!ext[L"nativeGearPart"].boolean){const double centers[]={342,960,1578};auto& center=layouts.object[L"center"];center.type=T::Object;center.object[L"x"]=num(e[L"x"].num()+960-centers[position]);center.object[L"y"]=e[L"y"];}}}
inline void switchGearLayout(Document& doc,int next,bool mirror,double width,bool reuseCurrent=false){
 if(doc.screen!="play")return;const int previous=editorGearPosition(doc);if(previous==next)return;
 storeGearLayout(doc);const double centers[]={342,960,1578};
 for(auto& e:doc.elements()){auto& ext=e.object[L"extensions"];auto& layouts=ext.object[L"gearLayouts"];auto& layout=layouts.object[gearLayoutName(next)];
 if(reuseCurrent||followsGear(e,previous)){layout.type=T::Object;layout.object[L"x"]=num(e[L"x"].num()+(followsGear(e,previous)?centers[next]-centers[previous]:0));layout.object[L"y"]=e[L"y"];}
 if(mirror&&!followsGear(e,previous)&&e[L"binding"].str()!=L"play.judgementCombo"){layout.type=T::Object;layout.object[L"x"]=num(width-e[L"x"].num()-e[L"width"].num());layout.object[L"y"]=e[L"y"];}
 if(layout.type!=T::Object){layout.type=T::Object;layout.object[L"x"]=num(e[L"x"].num()+(followsGear(e,previous)?centers[next]-centers[previous]:0));layout.object[L"y"]=e[L"y"];}
 for(auto key:{L"x",L"y"})e.object[key]=layout[key];
 }
 auto& root=doc.screens.at(doc.screen).object[L"extensions"];root.type=T::Object;root.object[L"editorGearPosition"]=num(next);
}
}
