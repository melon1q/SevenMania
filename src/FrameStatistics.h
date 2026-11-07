#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>

// Render interval statistics, independent of the audio clock. Recording is O(1)
// without allocation; the slowest-percentile partition runs at most once/second.
class FrameStatistics {
    struct Interval {double end{},duration{};};
    static constexpr size_t capacity=32768;
    std::array<Interval,capacity> intervals_{};
    std::array<double,capacity> scratch_{};
    size_t begin_{},count_{},totalFrames_{},windowFrames_{};
    double previous_{},elapsed_{},windowTime_{},continuous_{},lastLowUpdate_{},lastAlert_=-100;
    double current_{},average_{},low_{};
    bool hasPrevious_{},armed_=true;
public:
    void reset(){begin_=count_=totalFrames_=windowFrames_=0;previous_=elapsed_=windowTime_=continuous_=lastLowUpdate_=0;lastAlert_=-100;current_=average_=low_=0;hasPrevious_=false;armed_=true;}
    void suspend(){hasPrevious_=false;windowFrames_=0;windowTime_=continuous_=0;armed_=true;}
    bool record(double now) {
        if(!std::isfinite(now))return false;
        if(!hasPrevious_){previous_=now;hasPrevious_=true;return false;}
        const double dt=now-previous_;previous_=now;
        if(dt<=0)return false;
        elapsed_+=dt;continuous_+=dt;++totalFrames_;windowTime_+=dt;++windowFrames_;
        while(count_ && now-intervals_[begin_].end>10){begin_=(begin_+1)%capacity;--count_;}
        if(count_==capacity){begin_=(begin_+1)%capacity;--count_;}
        intervals_[(begin_+count_)%capacity]={now,dt};++count_;
        average_=totalFrames_/elapsed_;
        if(now-lastLowUpdate_>=1) {
            for(size_t i=0;i<count_;++i)scratch_[i]=intervals_[(begin_+i)%capacity].duration;
            const size_t slow=std::max<size_t>(1,(count_+99)/100);
            if(slow<count_)std::nth_element(scratch_.begin(),scratch_.begin()+slow,scratch_.begin()+count_,std::greater<>{});
            double sum=0;for(size_t i=0;i<slow;++i)sum+=scratch_[i];
            low_=slow/sum;lastLowUpdate_=now;
        }
        if(windowTime_<.25)return false;
        current_=windowFrames_/windowTime_;windowFrames_=0;windowTime_=0;
        if(current_>=average_-50)armed_=true;
        if(continuous_>=3 && armed_ && average_-current_>=100 && now-lastAlert_>=2) {
            armed_=false;lastAlert_=now;return true;
        }
        return false;
    }
    double current()const{return current_;}
    double average()const{return average_;}
    double lowOnePercent()const{return low_;}
};
