// Shared by gameplay and the frozen preparation preview. No per-frame allocation.
void drawLaneSeparator(HDC dc,RECT r,int judgeY){
 r.top=std::max(0L,r.top);r.bottom=std::min(r.bottom,LONG(std::max(0,judgeY)));
 if(r.left>=r.right||r.top>=r.bottom)return;
 if(gSeparatorOpacity<=0)return;
 if(gGpuFrame){gCompositionPresenter.rectangle(float(r.left),float(r.top),float(r.right),float(r.bottom),RGB(255,255,255),gSeparatorOpacity/100.f);return;}
 if(dc==gBackBufferDc&&gBackBufferPixels){
  GdiFlush();const unsigned a=gSeparatorOpacity;
  for(int y=std::max(0L,r.top);y<std::min(LONG(gBackBufferHeight),r.bottom);++y)for(int x=std::max(0L,r.left);x<std::min(LONG(gBackBufferWidth),r.right);++x){auto& p=static_cast<uint32_t*>(gBackBufferPixels)[size_t(y)*gBackBufferWidth+x];unsigned red=(((p>>16)&255)*(100-a)+255*a)/100,green=(((p>>8)&255)*(100-a)+255*a)/100,blue=((p&255)*(100-a)+255*a)/100;p=0xff000000|(red<<16)|(green<<8)|blue;}
 }else for(int y=r.top;y<r.bottom;++y)for(int x=r.left;x<r.right;++x){auto c=GetPixel(dc,x,y);if(c!=CLR_INVALID)SetPixelV(dc,x,y,RGB((GetRValue(c)*(100-gSeparatorOpacity)+255*gSeparatorOpacity)/100,(GetGValue(c)*(100-gSeparatorOpacity)+255*gSeparatorOpacity)/100,(GetBValue(c)*(100-gSeparatorOpacity)+255*gSeparatorOpacity)/100));}
}
