#pragma once
// A pre-rendered card, independent of audio and judgement clocks.
class SongIntroOverlay {
 HDC dc_{};HBITMAP bitmap_{};HGDIOBJ old_{};void* pixels_{};
 Microsoft::WRL::ComPtr<ID2D1Bitmap> gpu_;
 Microsoft::WRL::ComPtr<ID2D1RenderTarget> owner_;
 std::chrono::steady_clock::time_point started_{};double duration_=1.1;
 int x_{},y_{},w_{},h_{};
public:
 ~SongIntroOverlay(){reset();}
 void reset(){gpu_.Reset();owner_.Reset();if(dc_){SelectObject(dc_,old_);DeleteObject(bitmap_);DeleteDC(dc_);}dc_=nullptr;bitmap_=nullptr;pixels_=nullptr;started_={};}
 static double opacity(double age,double duration){return age<0||age>=duration?0:std::min(1.0,(duration-age)/.2);}
 void prepare(const bms::Chart& chart,RECT area,int milliseconds,ID2D1RenderTarget* target){
  reset();w_=area.right-area.left;h_=area.bottom-area.top;x_=area.left;y_=area.top;duration_=std::clamp(milliseconds,800,1500)/1000.;
  if(w_<180||h_<150)return;
  dc_=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w_;info.bmiHeader.biHeight=-h_;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
  bitmap_=CreateDIBSection(dc_,&info,DIB_RGB_COLORS,&pixels_,nullptr,0);if(!bitmap_){DeleteDC(dc_);dc_=nullptr;return;}old_=SelectObject(dc_,bitmap_);
  RECT all{0,0,w_,h_};auto brush=CreateSolidBrush(RGB(18,23,30));FillRect(dc_,&all,brush);DeleteObject(brush);
  int top=16;
  if(!chart.titleImage.empty()) {Gdiplus::Image image(chart.titleImage.c_str());if(image.GetLastStatus()==Gdiplus::Ok&&image.GetWidth()&&image.GetHeight()){
   const double scale=std::min(double(w_-24)/image.GetWidth(),double(h_/3)/image.GetHeight());int iw=int(image.GetWidth()*scale),ih=int(image.GetHeight()*scale);Gdiplus::Graphics graphics(dc_);graphics.DrawImage(&image,(w_-iw)/2,top,iw,ih);top+=ih+12;}}
  auto font=CreateFontW(-std::clamp(w_/16,15,26),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Malgun Gothic");auto previous=SelectObject(dc_,font);SetBkMode(dc_,TRANSPARENT);SetTextColor(dc_,RGB(240,242,247));
  const std::wstring level=chart.playLevel.empty()?L"":L"LEVEL "+chart.playLevel;
  for(const auto& line:{chart.genre,chart.title,chart.subtitle,chart.artist,level}){if(line.empty())continue;RECT r{12,top,w_-12,std::min(h_-12,top+64)};if(r.bottom<=r.top)break;DrawTextW(dc_,line.c_str(),-1,&r,DT_LEFT|DT_WORDBREAK|DT_END_ELLIPSIS|DT_NOPREFIX);top+=66;}
  SelectObject(dc_,previous);DeleteObject(font);GdiFlush();auto p=static_cast<uint32_t*>(pixels_);for(size_t i=0;i<size_t(w_)*h_;++i)p[i]|=0xff000000;
  owner_=target;if(target)target->CreateBitmap(D2D1::SizeU(w_,h_),pixels_,w_*4,D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),&gpu_);
 }
 void start(){started_=std::chrono::steady_clock::now();}
 bool active()const{return started_!=std::chrono::steady_clock::time_point{}&&std::chrono::duration<double>(std::chrono::steady_clock::now()-started_).count()<duration_;}
 double alpha()const{return started_==std::chrono::steady_clock::time_point{}?0:opacity(std::chrono::duration<double>(std::chrono::steady_clock::now()-started_).count(),duration_);}
 void draw(HDC target){const auto a=alpha();if(a>0)AlphaBlend(target,x_,y_,w_,h_,dc_,0,0,w_,h_,{AC_SRC_OVER,0,BYTE(a*255),AC_SRC_ALPHA});}
 void draw(ID2D1RenderTarget* target){const auto a=alpha();if(a<=0||!target||!pixels_)return;
  if(owner_.Get()!=target){gpu_.Reset();owner_=target;target->CreateBitmap(D2D1::SizeU(w_,h_),pixels_,w_*4,D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),&gpu_);}
  if(gpu_)target->DrawBitmap(gpu_.Get(),D2D1::RectF(float(x_),float(y_),float(x_+w_),float(y_+h_)),float(a));}
};
