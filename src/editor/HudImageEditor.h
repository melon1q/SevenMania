#pragma once
#include "EditorDocument.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <commdlg.h>
#include <functional>
#include <cmath>
namespace sevenmania::editor {
inline const wchar_t* hudJudgementNames[]={L"pgreat",L"great",L"good",L"bad",L"poor",L"mine"};
inline void importHudImage(Document& doc,int element,int glyph,const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);const auto size=file.tellg();
    if(size<=0||size>16*1024*1024)throw std::runtime_error("Image read/size limit");
    auto data=std::make_shared<Bytes>(static_cast<std::size_t>(size));file.seekg(0);file.read(reinterpret_cast<char*>(data->data()),size);if(!file)throw std::runtime_error("Image read failed");
    static unsigned sequence=0;const auto id=L"hud-image-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++sequence);
    auto extension=path.extension().wstring();for(auto& c:extension)if(c>=L'A'&&c<=L'Z')c+=L'a'-L'A';
    SkinResource resource;resource.path="assets/images/"+detail::utf8(id)+detail::utf8(extension);resource.bytes=data;detail::imageInfo(resource,{});
    auto& h=doc.elements().at(element).object[L"hud"];h.type=T::Object;
    if(glyph<6){h.object[L"judgements"].type=T::Object;h.object[L"judgements"].object[hudJudgementNames[glyph]]=str(id);}
    else {auto& digits=h.object[L"digits"];digits.type=T::Array;digits.array.resize(10,str(L""));digits.array.at(glyph-6)=str(id);}
    doc.files[resource.path]=data;doc.manifest.object[L"resourcePaths"].object[id]=str(detail::wide(resource.path,""));
}
struct HudImageDialog {
    Document working;int element{};HWND window{},list{},judgement{},combo{},animation{},preview{};HBITMAP previewBitmap{};
    std::vector<HWND> values;std::function<void(const Document&)> commit;
    const std::vector<std::wstring> fields{L"x",L"y",L"width",L"height",L"opacity",L"judgementWidth",L"judgementHeight",L"digitWidth",L"digitHeight",L"spacing",L"gap",L"displayMs"};
    const double defaults[12]{660,520,600,100,1,320,80,48,80,0,16,850};
    ~HudImageDialog(){if(previewBitmap)DeleteObject(previewBitmap);}
    HWND control(const wchar_t* cls,const wchar_t* text,int id,int x,int y,int w,int h,DWORD style=0){return CreateWindowExW(cls==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,x,y,w,h,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandle(nullptr),nullptr);}
    std::wstring reference(int glyph)const{const auto& h=working.elements().at(element)[L"hud"];return glyph<6?h[L"judgements"][hudJudgementNames[glyph]].str():h[L"digits"].type==T::Array&&h[L"digits"].array.size()==10?h[L"digits"].array[glyph-6].str():L"";}
    void refreshPreview(){
        const int glyph=int(SendMessageW(list,LB_GETCURSEL,0,0));if(glyph<0)return;const auto id=reference(glyph);
        const auto& resource=working.manifest[L"resourcePaths"][id];const auto path=resource.type==T::String?resource.str():resource[L"path"].str();
        SendMessageW(preview,STM_SETIMAGE,IMAGE_BITMAP,0);if(previewBitmap){DeleteObject(previewBitmap);previewBitmap=nullptr;}
        auto found=working.files.find(detail::utf8(path));if(found==working.files.end())return;
        using Microsoft::WRL::ComPtr;ComPtr<IWICImagingFactory> f;ComPtr<IWICStream> stream;ComPtr<IWICBitmapDecoder> decoder;ComPtr<IWICBitmapFrameDecode> frame;ComPtr<IWICFormatConverter> converter;ComPtr<IWICBitmapScaler> scaler;
        auto check=[](HRESULT result){if(FAILED(result))throw std::runtime_error("Cannot preview image");};
        check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f)));check(f->CreateStream(&stream));check(stream->InitializeFromMemory(const_cast<BYTE*>(found->second->data()),DWORD(found->second->size())));
        check(f->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnDemand,&decoder));check(decoder->GetFrame(0,&frame));UINT w{},h{};check(frame->GetSize(&w,&h));
        const double scale=std::min(300.0/w,100.0/h);w=std::max(1u,UINT(w*scale));h=std::max(1u,UINT(h*scale));check(f->CreateBitmapScaler(&scaler));check(scaler->Initialize(frame.Get(),w,h,WICBitmapInterpolationModeFant));check(f->CreateFormatConverter(&converter));check(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppBGR,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
        BITMAPINFO b{};b.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);b.bmiHeader.biWidth=w;b.bmiHeader.biHeight=-int(h);b.bmiHeader.biPlanes=1;b.bmiHeader.biBitCount=32;void* pixels{};
        previewBitmap=CreateDIBSection(nullptr,&b,DIB_RGB_COLORS,&pixels,nullptr,0);if(!previewBitmap)throw std::runtime_error("Preview allocation failed");check(converter->CopyPixels(nullptr,w*4,w*h*4,static_cast<BYTE*>(pixels)));SendMessageW(preview,STM_SETIMAGE,IMAGE_BITMAP,reinterpret_cast<LPARAM>(previewBitmap));
    }
    void create(){
        control(L"STATIC",L"판정 방식",0,15,15,95,24);judgement=control(L"COMBOBOX",L"",1,115,12,200,100,CBS_DROPDOWNLIST);
        control(L"STATIC",L"콤보 방식",0,340,15,95,24);combo=control(L"COMBOBOX",L"",2,440,12,200,100,CBS_DROPDOWNLIST);
        const auto& e=working.elements().at(element);for(HWND box:{judgement,combo})for(const auto* label:{L"font",L"image"})SendMessageW(box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        SendMessageW(judgement,CB_SETCURSEL,e[L"hud"][L"judgementMode"].str()==L"image",0);SendMessageW(combo,CB_SETCURSEL,e[L"hud"][L"comboMode"].str()==L"image",0);
        list=control(L"LISTBOX",L"",3,15,55,300,220,LBS_NOTIFY|WS_VSCROLL);for(int i=0;i<16;++i){const auto label=i<6?std::wstring(hudJudgementNames[i]):L"combo "+std::to_wstring(i-6);SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}SendMessageW(list,LB_SETCURSEL,0,0);
        control(L"BUTTON",L"선택 이미지 가져오기...",4,15,282,300,30);preview=control(L"STATIC",L"",0,15,320,300,110,SS_BITMAP|SS_CENTERIMAGE);
        const wchar_t* labels[]={L"X",L"Y",L"영역 너비",L"영역 높이",L"불투명도 (0~1)",L"판정 너비",L"판정 높이",L"숫자 너비",L"숫자 높이",L"숫자 간격",L"판정/콤보 간격",L"표시 시간 (ms)"};
        for(int i=0;i<12;++i){const int x=340+(i%2)*205,y=55+(i/2)*58;control(L"STATIC",labels[i],0,x,y,190,20);const auto& v=i<5?e[fields[i]]:e[L"hud"][fields[i]];auto value=std::to_wstring(v.num(defaults[i]));values.push_back(control(L"EDIT",value.c_str(),100+i,x,y+23,180,24,ES_AUTOHSCROLL));}
        control(L"STATIC",L"애니메이션 트랙 JSON (기존 extensions.animations 형식)",0,15,443,720,24);
        const auto& tracks=e[L"extensions"][L"animations"];const auto json=tracks.type==T::Null?std::string("[]"):serialize(tracks);animation=control(L"EDIT",detail::wide(json,"").c_str(),5,15,470,725,125,ES_MULTILINE|ES_WANTRETURN|WS_VSCROLL|ES_AUTOVSCROLL);
        control(L"BUTTON",L"페이드 예제",6,15,607,140,30);control(L"BUTTON",L"적용 및 미리보기",7,340,607,195,30);control(L"BUTTON",L"취소",8,550,607,190,30);refreshPreview();
    }
    void apply(){auto& e=working.elements().at(element);auto& h=e.object[L"hud"];h.type=T::Object;h.object[L"judgementMode"]=str(SendMessageW(judgement,CB_GETCURSEL,0,0)==1?L"image":L"font");h.object[L"comboMode"]=str(SendMessageW(combo,CB_GETCURSEL,0,0)==1?L"image":L"font");
        for(int i=0;i<12;++i){wchar_t text[128]{};GetWindowTextW(values[i],text,128);size_t used{};const double value=std::stod(text,&used);if(used!=wcslen(text)||!std::isfinite(value))throw std::runtime_error("Invalid numeric property");(i<5?e.object:h.object)[fields[i]]=num(value);}
        const auto length=GetWindowTextLengthW(animation);if(length>65536)throw std::runtime_error("Animation JSON exceeds 64KiB");std::wstring text(length+1,L'\0');GetWindowTextW(animation,text.data(),length+1);text.resize(length);auto tracks=detail::json(bytes(detail::utf8(text)),"HUD animations",{});if(tracks.type!=T::Array)throw std::runtime_error("Animations must be an array");auto& ext=e.object[L"extensions"];ext.type=T::Object;ext.object[L"animations"]=std::move(tracks);
        commit(working);DestroyWindow(window);
    }
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wParam,LPARAM lParam){auto* self=reinterpret_cast<HudImageDialog*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<HudImageDialog*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);self->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(window,message,wParam,lParam);
        try{if(message==WM_CREATE){self->create();return 0;}if(message==WM_CLOSE){DestroyWindow(window);return 0;}
            if(message==WM_COMMAND){const int id=LOWORD(wParam);if(id==3&&HIWORD(wParam)==LBN_SELCHANGE)self->refreshPreview();
                if(id==4){wchar_t path[32768]{};OPENFILENAMEW o{sizeof(o)};o.hwndOwner=window;o.lpstrFilter=L"Images\0*.png;*.jpg;*.jpeg\0\0";o.lpstrFile=path;o.nMaxFile=32768;o.Flags=OFN_FILEMUSTEXIST|OFN_NOCHANGEDIR;if(GetOpenFileNameW(&o)){importHudImage(self->working,self->element,int(SendMessageW(self->list,LB_GETCURSEL,0,0)),path);self->refreshPreview();}}
                if(id==6)SetWindowTextW(self->animation,LR"([{"id":"hud-fade","property":"opacity","trigger":"judgement","duration":0.85,"finish":"hold","keys":[{"time":0,"value":1},{"time":0.6,"value":1},{"time":0.85,"value":0}]}])");
                if(id==7)self->apply();if(id==8)DestroyWindow(window);return 0;}
        }catch(const detail::Failure& error){MessageBoxA(window,error.error.message.c_str(),"HUD images",MB_ICONERROR);}catch(const std::exception& error){MessageBoxA(window,error.what(),"HUD images",MB_ICONERROR);}return DefWindowProcW(window,message,wParam,lParam);
    }
};
inline void editHudImages(HWND owner,const Document& document,int element,std::function<void(const Document&)> commit){
    if(element<0||document.screen!="play"||document.screens.at(document.screen)[L"elements"].array.at(element)[L"binding"].str()!=L"play.judgementCombo")throw std::runtime_error("Select a play.judgementCombo element first");
    HudImageDialog dialog;dialog.working=document;dialog.element=element;dialog.commit=std::move(commit);WNDCLASSW c{};c.lpfnWndProc=HudImageDialog::proc;c.hInstance=GetModuleHandle(nullptr);c.lpszClassName=L"SevenManiaHudImages";c.hCursor=LoadCursor(nullptr,IDC_ARROW);c.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClassW(&c);
    HWND window=CreateWindowExW(WS_EX_DLGMODALFRAME,c.lpszClassName,L"판정 / 콤보 이미지 설정",WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,160,120,775,690,owner,nullptr,c.hInstance,&dialog);if(!window)throw std::runtime_error("Cannot open HUD image settings");
    EnableWindow(owner,FALSE);MSG message{};while(IsWindow(window)&&GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}EnableWindow(owner,TRUE);SetForegroundWindow(owner);
}
}
