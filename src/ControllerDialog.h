#pragma once
#include "ControllerDevice.h"
#include <commctrl.h>
#include <string_view>

namespace bms::controller {
class SettingsDialog {
    Device& device_;
    Config original_;
    std::wstring originalId_,ini_;
    HWND window_{},owner_{};
    bool done_=false,saved_=false;
    std::vector<std::pair<std::wstring,std::wstring>> choices_;
    HWND control(const wchar_t* type,const wchar_t* text,int id,int x,int y,int w,int h,DWORD style=0){
        const auto result=CreateWindowExW(type==std::wstring_view(L"COMBOBOX")?WS_EX_CLIENTEDGE:0,type,text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,x,y,w,h,window_,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(result,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return result;
    }
    void label(const wchar_t* text,int x,int y,int w=300){control(L"STATIC",text,0,x,y,w,22);}
    HWND combo(int id,int x,int y,int width){return control(L"COMBOBOX",L"",id,x,y,width,240,CBS_DROPDOWNLIST|WS_VSCROLL);}
    void add(HWND box,const std::wstring& text){SendMessageW(box,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
    int selected(int id){return int(SendDlgItemMessageW(window_,id,CB_GETCURSEL,0,0));}
    void select(int id,int value){SendDlgItemMessageW(window_,id,CB_SETCURSEL,value,0);}
    void scan(){
        const auto prior=selected(101);const auto id=prior>=0&&prior<int(choices_.size())?choices_[prior].second:device_.deviceId;
        device_.scan();choices_.clear();choices_.push_back({L"자동: 첫 번째 연결 장치",L""});
        for(const auto& item:device_.devices)choices_.push_back({item.name,item.id});
        int index=0;bool found=id.empty();for(int i=1;i<int(choices_.size());++i)if(choices_[i].second==id){index=i;found=true;}
        if(!found){index=int(choices_.size());choices_.push_back({L"저장된 장치 (현재 연결 안 됨)",id});}
        SendDlgItemMessageW(window_,101,CB_RESETCONTENT,0,0);for(const auto& item:choices_)add(GetDlgItem(window_,101),item.first);select(101,index);
        device_.deviceId=choices_[index].second;device_.reconnect(window_);
    }
    void create(){
        control(L"BUTTON",L"게임 컨트롤러 입력 사용",100,20,16,300,26,BS_AUTOCHECKBOX);
        SendDlgItemMessageW(window_,100,BM_SETCHECK,original_.enabled?BST_CHECKED:BST_UNCHECKED,0);
        label(L"장치",20,55);combo(101,20,78,500);control(L"BUTTON",L"다시 검색",102,535,76,120,28);
        for(int i=0;i<10;++i){const int x=i<7?20:350,y=125+(i<7?i:i-7)*39;
            const std::wstring name=i<7?L"건반 "+std::to_wstring(i+1):i==7?L"스크래치 버튼 1":i==8?L"스크래치 버튼 2":L"시작 / 일시정지";
            label(name.c_str(),x,y,155);auto box=combo(110+i,x+155,y-3,130);add(box,L"사용 안 함");for(int b=1;b<=128;++b)add(box,L"버튼 "+std::to_wstring(b));
            select(110+i,(i<7?original_.keys[i]:i==7?original_.scratchCw:i==8?original_.scratchCcw:original_.menu)+1);
        }
        label(L"턴테이블 축",350,255);auto axes=combo(130,505,252,150);for(auto name:{L"사용 안 함",L"X",L"Y",L"Z",L"Rx",L"Ry",L"Rz",L"Slider 1",L"Slider 2"})add(axes,name);select(130,original_.axis+1);
        label(L"축 동작",350,294);auto mode=combo(131,505,291,150);for(auto name:{L"버튼만 사용",L"턴테이블 회전축",L"중앙 복귀 축"})add(mode,name);select(131,original_.mode);
        label(L"축 감도 (1~8192)",350,333);control(L"EDIT",std::to_wstring(original_.threshold).c_str(),132,540,330,115,24,WS_BORDER|ES_NUMBER|ES_AUTOHSCROLL);
        label(L"스크래치 유지 (20~250ms)",350,372);control(L"EDIT",std::to_wstring(original_.holdMs).c_str(),133,540,369,115,24,WS_BORDER|ES_NUMBER|ES_AUTOHSCROLL);
        label(L"입력 확인: 아래에서 연결 상태, 버튼 번호, 축 값을 확인하세요.",20,420,640);
        control(L"STATIC",L"장치를 확인하는 중...",140,20,449,635,72);
        label(L"IIDX 컨트롤러: 게임패드/HID 모드에서 건반과 축을 맞춰 주세요.",20,530,640);
        label(L"키보드 모드는 기존 키 배치 설정을 사용합니다. 1P 7+1키 지원.",20,552,640);
        control(L"BUTTON",L"저장",IDOK,425,587,110,32,BS_DEFPUSHBUTTON);control(L"BUTTON",L"취소",IDCANCEL,545,587,110,32);
        scan();SetTimer(window_,1,16,nullptr);
    }
    void save(){
        Config draft=original_;draft.enabled=SendDlgItemMessageW(window_,100,BM_GETCHECK,0,0)==BST_CHECKED;
        for(int i=0;i<7;++i)draft.keys[i]=selected(110+i)-1;
        draft.scratchCw=selected(117)-1;draft.scratchCcw=selected(118)-1;draft.menu=selected(119)-1;draft.axis=selected(130)-1;draft.mode=selected(131);
        BOOL validThreshold{},validHold{};draft.threshold=int(GetDlgItemInt(window_,132,&validThreshold,FALSE));draft.holdMs=int(GetDlgItemInt(window_,133,&validHold,FALSE));
        if(!validThreshold||draft.threshold<1||draft.threshold>8192||!validHold||draft.holdMs<20||draft.holdMs>250){MessageBoxW(window_,L"축 감도와 유지시간을 표시된 범위로 입력해 주세요.",L"컨트롤러 설정",MB_OK|MB_ICONINFORMATION);return;}
        device_.config=draft;
        device_.disableIfUnavailable();
        if(!device_.save(ini_)){device_.config=original_;MessageBoxW(window_,L"컨트롤러 설정을 저장하지 못했습니다.",L"컨트롤러 설정",MB_OK|MB_ICONERROR);return;}
        saved_=true;DestroyWindow(window_);
    }
    void monitor(){
        const auto sample=device_.poll(false,[](const State&){},true);
        std::wstring text=sample.connected?L"연결됨  |  눌린 버튼: ":L"연결된 장치를 찾지 못했습니다. 케이블과 컨트롤러 모드를 확인하세요.";
        if(sample.connected){for(int i=0;i<128;++i)if(sample.buttons[i])text+=std::to_wstring(i+1)+L" ";text+=L"\r\n축: ";const wchar_t* axes[]={L"X",L"Y",L"Z",L"Rx",L"Ry",L"Rz",L"S1",L"S2"};for(int i=0;i<8;++i)text+=std::wstring(axes[i])+L"="+std::to_wstring(sample.axes[i])+L" ";}
        SetDlgItemTextW(window_,140,text.c_str());
    }
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM w,LPARAM l){
        auto* self=reinterpret_cast<SettingsDialog*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<SettingsDialog*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window_=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(self){
            if(message==WM_CREATE){self->create();return 0;}
            if(message==WM_TIMER){self->monitor();return 0;}
            if(message==WM_COMMAND){const int id=LOWORD(w);if(id==IDOK)self->save();else if(id==IDCANCEL)DestroyWindow(window);else if(id==102)self->scan();else if(id==101&&HIWORD(w)==CBN_SELCHANGE){const int index=self->selected(101);if(index>=0&&index<int(self->choices_.size())){self->device_.deviceId=self->choices_[index].second;self->device_.reconnect(window);}}return 0;}
            if(message==WM_DESTROY){KillTimer(window,1);self->done_=true;return 0;}
        }
        return DefWindowProcW(window,message,w,l);
    }
public:
    SettingsDialog(Device& device,HWND owner,const std::wstring& ini):device_(device),original_(device.config),originalId_(device.deviceId),ini_(ini),owner_(owner){}
    bool show(HWND gameWindow){
        WNDCLASSW type{};type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"SevenManiaControllerSettings";type.lpfnWndProc=proc;type.hCursor=LoadCursor(nullptr,IDC_ARROW);type.hbrBackground=HBRUSH(COLOR_BTNFACE+1);RegisterClassW(&type);
        const auto window=CreateWindowExW(WS_EX_DLGMODALFRAME|WS_EX_CONTROLPARENT,type.lpszClassName,L"컨트롤러 설정",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,700,670,owner_,nullptr,type.hInstance,this);
        if(!window)return false;
        SetWindowTextW(window,L"컨트롤러 설정");EnableWindow(owner_,FALSE);ShowWindow(window,SW_SHOW);SetFocus(GetDlgItem(window,100));
        MSG message{};while(!done_){const auto read=GetMessageW(&message,nullptr,0,0);if(read<=0){if(read==0)PostQuitMessage(int(message.wParam));break;}if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
        if(IsWindow(window))DestroyWindow(window);
        if(!saved_){device_.config=original_;device_.deviceId=originalId_;}
        device_.reconnect(gameWindow);EnableWindow(owner_,TRUE);SetForegroundWindow(owner_);return saved_;
    }
};
}
