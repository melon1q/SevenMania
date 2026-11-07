#pragma once
struct PlayPreview {
 using Clock=std::chrono::steady_clock;
 Clock::time_point epoch=Clock::now(),last=epoch;bool requested{};
 std::array<bool,8> group{},held{};
 void extract(const bms::Chart& chart){
  group.fill(false);double first=std::numeric_limits<double>::infinity();
  for(const auto& note:chart.notes)if(note.lane>=0&&note.lane<8&&std::isfinite(note.timeSeconds))first=std::min(first,note.timeSeconds);
  // Equal parser timestamps only: even adjacent representable times remain separate.
  for(const auto& note:chart.notes)if(note.lane>=0&&note.lane<8&&note.timeSeconds==first)group[note.lane]=true;
 }
 std::vector<uint32_t> background;size_t revision{};
 void reset(){epoch=last=Clock::now();requested=false;held.fill(false);group.fill(false);background.clear();revision=0;}
 double seconds()const{return std::chrono::duration<double>(Clock::now()-epoch).count();}
 void touched(){last=Clock::now();}
 bool startAllowed(int mode)const{return mode==0||requested||(mode==1&&seconds()>=2&&std::chrono::duration<double>(Clock::now()-last).count()>=2);}
};
