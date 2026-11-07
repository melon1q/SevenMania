#pragma once
#include "EditorDocument.h"
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
inline void storeGearLayout(Document& doc){if(doc.screen!="play")return;const int position=editorGearPosition(doc);const auto name=gearLayoutName(position);for(auto& e:doc.elements()){auto& ext=e.object[L"extensions"];ext.type=T::Object;if(followsGear(e,position))ext.object[L"followGear"]=jsonBool(true);auto& layouts=ext.object[L"gearLayouts"];layouts.type=T::Object;auto& layout=layouts.object[name];layout.type=T::Object;for(auto key:{L"x",L"y"})layout.object[key]=e[key];if(followsGear(e,position)&&!ext[L"nativeGearPart"].boolean){const double centers[]={342,960,1578};auto& center=layouts.object[L"center"];center.type=T::Object;center.object[L"x"]=num(e[L"x"].num()+960-centers[position]);center.object[L"y"]=e[L"y"];}}}
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
