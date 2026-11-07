#include "skin/SkinAnimation.h"
#include "skin/SkinEntrance.h"
#include <iostream>
using namespace sevenmania::skin;
int main(){try{
 for(int preset=1;preset<9;++preset){Entrance entrance;entrance.preset=preset;
  auto first=entrance.sample(0,100),last=entrance.sample(.6,100);if(first.opacity!=0||last.opacity!=1||last.scale!=1||last.y!=0||last.rotation!=0||last.wipe!=1||last.blur!=0)return 20;
  for(int hz:{60,120,144,240})for(int frame=0;frame<=hz;++frame){auto v=entrance.sample(double(frame)/hz,100);if(!std::isfinite(v.scale)||v.scale<=0||v.opacity<0||v.opacity>1||v.wipe<0||v.wipe>1)return 21;}
  std::string source="{\"entrance\":{\"preset\":\""+std::string(entrancePresets[preset])+"\",\"duration\":0.6}}";auto value=Entrance::parse(detail::json(Bytes(source.begin(),source.end()),"entrance",{}));if(value.preset!=preset)return 22;
 }
 {const std::string bad=R"({"entrance":{"preset":"Unknown"}})";bool rejected=false;try{Entrance::parse(detail::json(Bytes(bad.begin(),bad.end()),"entrance",{}));}catch(...){rejected=true;}if(!rejected)return 23;}
auto json=detail::json(Bytes{'{','}'},"test",{});(void)json;AnimationTrack t;t.duration=1;t.active=true;t.keys={{0,0},{1,100}};
 if(t.sample(.5)!=50||t.sample(5)!=100)return 1;t.loop=true;if(std::abs(t.sample(1.25)-25)>1e-9)return 2;t.loop=false;t.finish="restore";if(t.running(2))return 3;
 for(auto e:{"linear","ease-in","ease-out","ease-in-out"}){t.easing=e;double expected=t.sample(.5);for(int hz:{60,120,144,240}){double now=double(hz/2)/hz;if(std::abs(t.sample(now)-expected)>1e-9)return 4;}}
 const std::string s=R"({"animations":[{"id":"slide","property":"x","duration":1,"keys":[{"time":0,"value":0},{"time":1,"value":100}]}]})";
 auto tracks=parseAnimations(detail::json(Bytes(s.begin(),s.end()),"test",{}));if(tracks.size()!=1||!tracks[0].active)return 5;
 tracks[0].keys[1].first=0; // malformed JSON variant is tested through the actual parser below
 std::string bad=s;bad.replace(bad.find("\"duration\":1"),12,"\"duration\":0");bool rejected=false;try{parseAnimations(detail::json(Bytes(bad.begin(),bad.end()),"bad",{}));}catch(...){rejected=true;}if(!rejected)return 6;
 std::cout<<"8 entrance presets: start/end, four refresh rates, JSON and unknown preset; animation evaluator: interpolation, looping, completion, four refresh rates, JSON and invalid duration passed\n";return 0;}catch(...){return 9;}}
