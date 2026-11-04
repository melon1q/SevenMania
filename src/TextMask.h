#pragma once
#include <windows.h>
#include <algorithm>
#include <cstdint>
// Tint only glyph coverage; outlines are composed separately and retain their colors.
inline void blendTextMask(const uint32_t* source,int sw,int sh,uint32_t* target,int width,int height,int left,int top,int dw,int dh,COLORREF color) {
    if(!source||!target||sw<=0||sh<=0||dw<=0||dh<=0)return;
    const auto alpha=[&](int x,int y){return source[size_t(y)*sw+x]>>24;};
    for(int y=std::max(0,top);y<std::min(height,top+dh);++y)for(int x=std::max(0,left);x<std::min(width,left+dw);++x){
        const double px=std::clamp((x-left+.5)*sw/dw-.5,0.0,double(sw-1)),py=std::clamp((y-top+.5)*sh/dh-.5,0.0,double(sh-1));
        const int x0=int(px),y0=int(py),x1=std::min(x0+1,sw-1),y1=std::min(y0+1,sh-1);
        const unsigned wx=unsigned((px-x0)*256+.5),wy=unsigned((py-y0)*256+.5);
        const unsigned first=(alpha(x0,y0)*(256-wx)+alpha(x1,y0)*wx)>>8;
        const unsigned second=(alpha(x0,y1)*(256-wx)+alpha(x1,y1)*wx)>>8;
        const unsigned a=(first*(256-wy)+second*wy)>>8;
        auto& pixel=target[size_t(y)*width+x];const unsigned inverse=255-a;
        pixel=((a+((pixel>>24)*inverse+127)/255)<<24)|
            (((GetRValue(color)*a+127)/255+(((pixel>>16)&255)*inverse+127)/255)<<16)|
            (((GetGValue(color)*a+127)/255+(((pixel>>8)&255)*inverse+127)/255)<<8)|
            ((GetBValue(color)*a+127)/255+((pixel&255)*inverse+127)/255);
    }
}
