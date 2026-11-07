#include "VideoFrameScaler.h"
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <tuple>
static bool tracking=false;
static size_t allocations=0;
void* operator new(size_t size){if(tracking)++allocations;if(void* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete(void* p,size_t) noexcept{std::free(p);}
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
// Independent, slow four-tap reference with the original two-stage integer rounding.
static std::vector<uint32_t> reference(const std::vector<uint32_t>& input,int sw,int sh,int w,int h){
    std::vector<uint32_t> result(size_t(w)*h);
    const auto blend=[](uint32_t a,uint32_t b,unsigned t){uint32_t out=0;for(unsigned shift=0;shift<32;shift+=8)out|=(( ((a>>shift)&255)*(256-t)+((b>>shift)&255)*t)>>8)<<shift;return out;};
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        const double px=std::clamp((x+.5)*sw/w-.5,0.0,double(sw-1)),py=std::clamp((y+.5)*sh/h-.5,0.0,double(sh-1));
        const int x0=int(px),y0=int(py),x1=std::min(x0+1,sw-1),y1=std::min(y0+1,sh-1);
        const auto wx=unsigned(std::lround((px-x0)*256)),wy=unsigned(std::lround((py-y0)*256));
        result[size_t(y)*w+x]=blend(blend(input[size_t(y0)*sw+x0],input[size_t(y0)*sw+x1],wx),blend(input[size_t(y1)*sw+x0],input[size_t(y1)*sw+x1],wx),wy);
    }return result;
}
int main(){try{
    VideoFrameScaler scaler;std::vector<uint32_t> output;
    for(const auto [sw,sh,w,h]:{std::tuple{1,1,9,7},{9,7,1,1},{3,5,17,11},{17,11,3,5},{31,23,31,23},{31,23,1,59},{1,23,51,1}}){
        std::vector<uint32_t> input(size_t(sw)*sh);uint32_t state=17;
        for(auto& pixel:input){state=state*1664525u+1013904223u;pixel=state;}
        const auto expected=reference(input,sw,sh,w,h);
        for(int repeat=0;repeat<3;++repeat){require(scaler.scale(sw,sh,input,w,h,output),"scale failed");require(output==expected,"pixel mismatch");}
        auto inplace=input;require(scaler.scale(sw,sh,inplace,w,h,inplace)&&inplace==expected,"in-place mismatch");
    }
    std::vector<uint32_t> input(1920*1080,0xff987654u);
    for(const auto [w,h]:{std::pair{1280,720},{1920,1080}}){
        require(scaler.scale(1920,1080,input,w,h,output),"warmup failed");
        allocations=0;tracking=true;
        const bool ok=scaler.scale(1920,1080,input,w,h,output);tracking=false;
        require(ok&&allocations==0,"warmed scaler allocated scratch memory");
    }
    const auto saved=output;require(!scaler.scale(0,1,input,1,1,output),"accepted zero source");
    require(!scaler.scale(9999,9999,input,1,1,output),"accepted short input");
    require(!scaler.scale(1,1,input,100000,100000,output),"accepted oversized output");require(output==saved,"invalid call changed output");
    std::cout<<"Video scaler: exact pixels, resize, aliasing, limits and zero warmed allocations passed\n";
}catch(const std::exception& e){tracking=false;std::cerr<<e.what()<<'\n';return 1;}return 0;}
