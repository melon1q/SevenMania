#include "../src/TextRasterCache.h"
#include "../src/KeyDeck.h"
#include "../src/HudTextStyle.h"
#include "reference/FrameStudyTextRaster.h"
#include "reference/FrameStudyKeyDeck.h"
#include <iostream>

int main() {
    EmbeddedHudFont embedded;
    if (!embedded.load()) { std::cerr << "Embedded gameplay font unavailable\n"; return 1; }
    HFONT font=CreateFontW(-80,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Michroma");
    TextRasterCache candidate; ReferenceTextRasterCache reference;
    constexpr int width=1100,height=180;
    std::vector<uint32_t> a(width*height),b(width*height);
    for(int i=0;i<96;++i) {
        const std::wstring text=i%3==0?L"GREAT "+std::to_wstring(i):i%3==1?L"GOOD 1000":L"POOR 0";
        const COLORREF color=hudRainbowColor(i%12);
        auto draw=[&](HDC dc,const std::wstring&t,RECT rect,HFONT f,COLORREF c){drawHudTextBody(dc,t,rect,f,c);};
        std::fill(a.begin(),a.end(),0xff19394du);b=a;
        const RECT rect{i%2?-40:0,0,width-i%7*40,height};
        const double scale=i%2?.87:1.03;
        if(!candidate.draw(a.data(),width,height,text,rect,font,color,scale,draw,i%4==0) ||
           !reference.draw(b.data(),width,height,text,rect,font,color,scale,draw,i%4==0) || a!=b) {
            std::cerr<<"Raster mismatch at "<<i<<'\n';return 1;
        }
        if(i%17==0){candidate.clear();reference.clear();}
    }
    candidate.clear();reference.clear();DeleteObject(font);
    TurntableImage disc,black,white;
    std::vector<uint32_t> image(32*32);
    for(size_t i=0;i<image.size();++i)image[i]=0xff000000u|static_cast<uint32_t>(i*1033)&0xffffffu;
    disc.setPixels(32,32,image.data());black.setPixels(32,32,image.data());white.setPixels(32,32,image.data());
    for(int i=0;i<120;++i) {
        std::array<bool,8> pressed{}; for(int k=0;k<8;++k)pressed[k]=(i/9>>k)&1;
        std::fill(a.begin(),a.end(),0xff19394du);b=a;
        int lane=60+i/40,scratch=i%50==0?44:90;
        const TurntableImage* turntable=i<60?&disc:nullptr;
        KeyDeck::drawCached(a.data(),width,height,i%5-2,lane,25,pressed,i*.027,scratch,turntable,&black,&white);
        ReferenceKeyDeck::drawCached(b.data(),width,height,i%5-2,lane,25,pressed,i*.027,scratch,turntable,&black,&white);
        if(a!=b){std::cerr<<"Deck mismatch at "<<i<<'\n';return 1;}
        if(i%23==0)disc.setPixels(32,32,image.data());
    }
    std::cout<<"96 outlined/scaled/colored HUD and 120 deck frames match baseline pixels exactly\n";
}
