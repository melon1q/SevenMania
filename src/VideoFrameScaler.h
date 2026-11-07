#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <utility>
#include <vector>

// Owned by one decoder worker. Published frames never share these scratch rows.
class VideoFrameScaler {
    struct Axis { int first{}, second{}; unsigned weight{}; };
    int sourceWidth_{}, sourceHeight_{}, width_{}, height_{};
    std::vector<Axis> x_, y_;
    std::vector<uint32_t> first_, second_;
    static void axis(std::vector<Axis>& result, int source, int target) {
        result.resize(static_cast<size_t>(target));
        for (int i=0;i<target;++i) {
            const double p=std::clamp((i+.5)*source/target-.5,0.0,double(source-1));
            const int a=static_cast<int>(p);
            result[static_cast<size_t>(i)]={a,std::min(a+1,source-1),static_cast<unsigned>(std::lround((p-a)*256))};
        }
    }
    static uint32_t mix(uint32_t a,uint32_t b,unsigned weight) {
        const unsigned inverse=256-weight;
        const auto rb=(((a&0x00ff00ffu)*inverse+(b&0x00ff00ffu)*weight)>>8)&0x00ff00ffu;
        const auto ga=((((a>>8)&0x00ff00ffu)*inverse+((b>>8)&0x00ff00ffu)*weight)>>8)&0x00ff00ffu;
        return rb|(ga<<8);
    }
public:
    bool scale(int sw,int sh,const std::vector<uint32_t>& input,int width,int height,std::vector<uint32_t>& output) {
        if(sw<=0||sh<=0||uint64_t(sw)*sh>input.size()||width<=0||height<=0||uint64_t(width)*height*4>256ull*1024*1024)return false;
        try {
            // Preserve the compatibility API's in-place behavior without penalizing decoder frames.
            if(&input==&output) {
                std::vector<uint32_t> separate;
                if(!scale(sw,sh,input,width,height,separate))return false;
                output=std::move(separate);return true;
            }
            if(sw==width&&sh==height) {
                output.assign(input.begin(),input.begin()+static_cast<ptrdiff_t>(size_t(sw)*sh));return true;
            }
            if(sw!=sourceWidth_||sh!=sourceHeight_||width!=width_||height!=height_) {
                axis(x_,sw,width);axis(y_,sh,height);
                first_.resize(static_cast<size_t>(width));second_.resize(static_cast<size_t>(width));
                sourceWidth_=sw;sourceHeight_=sh;width_=width;height_=height;
            }
            output.resize(static_cast<size_t>(width)*height);
            int previousFirst=-1,previousSecond=-1;
            const auto horizontal=[&](int y,std::vector<uint32_t>& row) {
                const auto* source=input.data()+static_cast<size_t>(y)*sw;
                for(int x=0;x<width;++x) {
                    const auto& p=x_[static_cast<size_t>(x)];
                    row[static_cast<size_t>(x)]=mix(source[p.first],source[p.second],p.weight);
                }
            };
            for(int y=0;y<height;++y) {
                const auto& p=y_[static_cast<size_t>(y)];
                if(p.first!=previousFirst) {
                    if(p.first==previousSecond)first_.swap(second_);else horizontal(p.first,first_);
                    previousFirst=p.first;previousSecond=-1;
                }
                if(p.second!=previousSecond){horizontal(p.second,second_);previousSecond=p.second;}
                auto* row=output.data()+static_cast<size_t>(y)*width;
                for(int x=0;x<width;++x)row[x]=mix(first_[static_cast<size_t>(x)],second_[static_cast<size_t>(x)],p.weight);
            }
            return true;
        }catch(...){return false;}
    }
};
