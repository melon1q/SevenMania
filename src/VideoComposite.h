#pragma once
#include "BmsParser.h"
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace videoComposite {
// Both buffers use the canvas stride. The overlay already contains premultiplied BGRA.
inline void over(const uint32_t* overlay, uint32_t* dest, int stride, const RECT& area) {
    for (int y=area.top;y<area.bottom;++y) for (int x=area.left;x<area.right;++x) {
        const size_t i=static_cast<size_t>(y)*stride+x;
        const uint32_t src=overlay[i], alpha=src>>24;
        if (alpha==255) { dest[i]=src; continue; }
        if (!alpha) continue;
        const uint32_t old=dest[i], inverse=255-alpha;
        const auto channel=[&](int shift) {
            return ((src>>shift)&255)+((((old>>shift)&255)*inverse+127)/255);
        };
        dest[i]=0xff000000u | (channel(16)<<16) | (channel(8)<<8) | channel(0);
    }
}
// VideoBga produces opaque ARGB. Asset lookup and effective opacity belong to
// the caller, once per layer, never to this pixel loop.
inline void blend(const uint32_t* source,int sw,int sh,uint32_t* target,int tw,int th,
                  int left,unsigned alpha,const bms::BgaColor* key=nullptr) {
    if(!source || !target || sw<=0 || sh<=0 || tw<=0 || th<=0 || alpha==0)return;
    alpha=std::min(alpha,255u);
    const int begin=std::max(0,left),end=std::min(tw,left+sw),rows=std::min(sh,th);
    if(begin>=end)return;
    const unsigned inverse=255-alpha;
    for(int y=0;y<rows;++y) {
        const auto* input=source+static_cast<size_t>(y)*sw+begin-left;
        auto* output=target+static_cast<size_t>(y)*tw+begin;
        if(alpha==255 && !key) {
            std::memcpy(output,input,static_cast<size_t>(end-begin)*sizeof(uint32_t));
            continue;
        }
        for(int x=0;x<end-begin;++x) {
            const auto c=input[x];
            if(key && int((c>>16)&255)<=key->red && int((c>>8)&255)<=key->green && int(c&255)<=key->blue)continue;
            if(alpha==255){output[x]=c;continue;}
            const auto old=output[x];
            output[x]=0xff000000u|((((c>>16)&255)*alpha+((old>>16)&255)*inverse)/255<<16)|
                ((((c>>8)&255)*alpha+((old>>8)&255)*inverse)/255<<8)|(((c&255)*alpha+(old&255)*inverse)/255);
        }
    }
}
} // namespace videoComposite
