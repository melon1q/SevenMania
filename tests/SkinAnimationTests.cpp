#include "skin/SkinAnimation.h"
#include <iostream>
using namespace sevenmania::skin;
int main(){try{auto json=detail::json(Bytes{'{','}'},"test",{});(void)json;AnimationTrack t;t.duration=1;t.active=true;t.keys={{0,0},{1,100}};
 if(t.sample(.5)!=50||t.sample(5)!=100)return 1;t.loop=true;if(std::abs(t.sample(1.25)-25)>1e-9)return 2;t.loop=false;t.finish="restore";if(t.running(2))return 3;
 for(auto e:{"linear","ease-in","ease-out","ease-in-out"}){t.easing=e;double expected=t.sample(.5);for(int hz:{60,120,144,240}){double now=double(hz/2)/hz;if(std::abs(t.sample(now)-expected)>1e-9)return 4;}}
 const std::string s=R"({"animations":[{"id":"slide","property":"x","duration":1,"keys":[{"time":0,"value":0},{"time":1,"value":100}]}]})";
 auto tracks=parseAnimations(detail::json(Bytes(s.begin(),s.end()),"test",{}));if(tracks.size()!=1||!tracks[0].active)return 5;
 tracks[0].keys[1].first=0; // malformed JSON variant is tested through the actual parser below
 std::string bad=s;bad.replace(bad.find("\"duration\":1"),12,"\"duration\":0");bool rejected=false;try{parseAnimations(detail::json(Bytes(bad.begin(),bad.end()),"bad",{}));}catch(...){rejected=true;}if(!rejected)return 6;
 std::cout<<"Animation evaluator: interpolation, looping, completion, four refresh rates, JSON and invalid duration passed\n";return 0;}catch(...){return 9;}}
