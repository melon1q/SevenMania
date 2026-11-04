#pragma once
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <string>

class EmbeddedHudFont {
    HANDLE handles_[3]{};
public:
    EmbeddedHudFont() = default;
    EmbeddedHudFont(const EmbeddedHudFont&) = delete;
    EmbeddedHudFont& operator=(const EmbeddedHudFont&) = delete;
    ~EmbeddedHudFont() { reset(); }
    bool load(HMODULE module = GetModuleHandleW(nullptr)) {
        if(handles_[0])return true;
        for(int i=0;i<3;++i){const auto resource=FindResourceW(module,MAKEINTRESOURCEW(2001+i),RT_RCDATA);
            const auto loaded=resource?LoadResource(module,resource):nullptr;auto* bytes=loaded?LockResource(loaded):nullptr;DWORD count{};
            if(bytes)handles_[i]=AddFontMemResourceEx(bytes,SizeofResource(module,resource),nullptr,&count);
            if(!handles_[i]||!count){reset();return false;}}
        return true;
    }
    void reset() {for(auto& handle:handles_)if(handle){RemoveFontMemResourceEx(handle);handle=nullptr;}}

};

inline COLORREF hudRainbowColor(int phase) {
    const int hue = (phase * 60) % 360;
    const int segment = hue / 60, fraction = (hue % 60) * 255 / 60;
    const int high = 255, low = 32;
    const int rise = low + (high-low)*fraction/255, fall = high-(high-low)*fraction/255;
    switch (segment) {
    case 0: return RGB(high,rise,low); case 1: return RGB(fall,high,low);
    case 2: return RGB(low,high,rise); case 3: return RGB(low,fall,high);
    case 4: return RGB(rise,low,high); default: return RGB(high,low,fall);
    }
}
// Draw the outline once per cached sprite, not once per gameplay frame.
inline void drawHudTextBody(HDC dc, const std::wstring& text, RECT bounds, HFONT font, COLORREF color, int rainbowPhase = -1) {
    const int saved = SaveDC(dc);
    if (font) SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SIZE size{}; GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&size);
    const int x=(bounds.left+bounds.right-size.cx)/2, y=(bounds.top+bounds.bottom-size.cy)/2;
    auto stroke = [&](int width, COLORREF edge) {
        LOGBRUSH brush{BS_SOLID,edge,0};
        HPEN pen=ExtCreatePen(PS_GEOMETRIC|PS_SOLID|PS_JOIN_ROUND|PS_ENDCAP_ROUND,width,&brush,0,nullptr);
        const auto old=SelectObject(dc,pen);
        BeginPath(dc); TextOutW(dc,x,y,text.c_str(),static_cast<int>(text.size())); EndPath(dc); StrokePath(dc);
        SelectObject(dc,old); DeleteObject(pen);
    };
    stroke(12,RGB(0,0,0)); stroke(6,RGB(255,255,255));
    if(rainbowPhase < 0) {
        SetTextColor(dc,color);TextOutW(dc,x,y,text.c_str(),static_cast<int>(text.size()));
    } else {
        // Spatial rainbow is baked into the cached sprite; no time-based recoloring.
        for(int band=0;band<6;++band){
            const RECT clip{x+size.cx*band/6,y,x+size.cx*(band+1)/6,y+size.cy};
            SetTextColor(dc,hudRainbowColor(band));
            ExtTextOutW(dc,x,y,ETO_CLIPPED,&clip,text.c_str(),static_cast<UINT>(text.size()),nullptr);
        }
    }
    RestoreDC(dc,saved);
}

inline void drawHudGlyphMask(HDC dc,const std::wstring& text,RECT bounds,HFONT font,COLORREF color) {
    const int saved=SaveDC(dc);if(font)SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);
    SIZE size{};GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&size);
    TextOutW(dc,(bounds.left+bounds.right-size.cx)/2,(bounds.top+bounds.bottom-size.cy)/2,text.c_str(),static_cast<int>(text.size()));RestoreDC(dc,saved);
}

inline std::wstring combinedJudgementText(const std::wstring& judgement, unsigned combo) {
    return judgement.empty() ? L"" : judgement + L" " + std::to_wstring(combo);
}
