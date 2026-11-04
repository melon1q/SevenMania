#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace noteEffects {
struct Sprite {int size{};uint64_t id{};std::vector<uint32_t> pixels;};
inline const Sprite& rotated(int width,int height,double time,COLORREF color) {
    const int phase=static_cast<int>(std::floor(time*24))%64;
    const uint64_t id=0x6000000000000000ull | (uint64_t(color&0xffffff)<<24) | (uint64_t(width&255)<<16) | (uint64_t(height&255)<<8) | phase;
    static thread_local std::map<uint64_t,Sprite> cache;
    auto found=cache.find(id);if(found!=cache.end())return found->second;
    if(cache.size()>=128)cache.clear();
    Sprite result;result.size=std::max(2,static_cast<int>(std::ceil(std::hypot(width,height)))+2);result.id=id;
    result.pixels.resize(static_cast<size_t>(result.size)*result.size);
    const double angle=phase*6.283185307179586/64,cs=std::cos(angle),sn=std::sin(angle);
    const uint32_t pixel=0xff000000u|(GetRValue(color)<<16)|(GetGValue(color)<<8)|GetBValue(color);
    for(int y=0;y<result.size;++y)for(int x=0;x<result.size;++x){
        const double dx=x+.5-result.size/2.0,dy=y+.5-result.size/2.0;
        if(std::abs(dx*cs+dy*sn)<=width/2.0 && std::abs(-dx*sn+dy*cs)<=height/2.0)result.pixels[y*result.size+x]=pixel;
    }
    return cache.emplace(id,std::move(result)).first->second;
}
inline void draw(uint32_t* pixels,int width,int height,const Sprite& sprite,int left,int top,const RECT& clip) {
    for(int y=std::max({0,static_cast<int>(clip.top)-top});y<std::min({sprite.size,height-top,static_cast<int>(clip.bottom)-top});++y)
        for(int x=std::max({0,static_cast<int>(clip.left)-left});x<std::min({sprite.size,width-left,static_cast<int>(clip.right)-left});++x){
            const auto color=sprite.pixels[y*sprite.size+x];if(color>>24)pixels[(y+top)*width+x+left]=color;
        }
}
} // namespace noteEffects
