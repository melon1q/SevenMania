#include "FrameStatistics.h"
#include <iostream>
#include <stdexcept>
static void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
int main(){try{
    FrameStatistics stats;double time=0;stats.record(time);
    for(int i=0;i<5000;++i){time+=.001;check(!stats.record(time),"stable FPS alarmed");}
    check(std::abs(stats.average()-1000)<.01 && std::abs(stats.current()-1000)<.01 && std::abs(stats.lowOnePercent()-1000)<.01,"steady frame statistics wrong");
    unsigned alarms=0;for(int i=0;i<800;++i){time+=.00125;alarms+=stats.record(time);}
    check(alarms==1,"200 FPS drop should sound once, without repeated beeps");
    for(int i=0;i<1500;++i){time+=.001;stats.record(time);}
    alarms=0;for(int i=0;i<800;++i){time+=.00125;alarms+=stats.record(time);}
    check(alarms==1,"recovered drop did not rearm");
    const double average=stats.average();stats.suspend();check(!stats.record(time+100),"pause caused alarm");
    check(stats.average()==average,"pause counted toward average");
    stats.reset();time=0;stats.record(time);
    for(int i=0;i<600;++i){time+=.001;stats.record(time);time+=.003;stats.record(time);}
    check(std::abs(stats.average()-500)<.01 && std::abs(stats.lowOnePercent()-1000/3.0)<.01,"1% low must average slowest frame times, not the average FPS");
    stats.reset();time=0;stats.record(time);
    for(int i=0;i<20000;++i){time+=.001;stats.record(time);}
    check(std::abs(stats.lowOnePercent()-1000)<.01,"rolling window expiry failed");
    std::cout<<"Frame statistics: rates, slowest 1%, alarms, cooldown, pause and rolling window passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
