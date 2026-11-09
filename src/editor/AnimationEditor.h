#pragma once
// Modal animation track editor for one element. Edits a working copy of the Document through
// AnimationEditModel; "적용" hands the copy to the editor (one undo step) and replays the trigger.
#include "AnimationEditModel.h"
#include <functional>
#include <cmath>
#include <chrono>
namespace sevenmania::editor {
struct AnimationDialog {
    enum : int { TrackList=1,AddTrack,DuplicateTrack,DeleteTrack,MoveUp,MoveDown,IdEdit,PropertyBox,TriggerBox,EasingBox,FinishBox,PolicyBox,DurationEdit,LoopCheck,
        KeyList,TimeEdit,ValueEdit,AddKey,SetKey,DeleteKey,Play,Apply,Close,Status };
    Document working;int element{};std::vector<int> targets;HWND window{};std::function<void(const Document&)> commit;std::function<void(const std::wstring&)> play;
    int selectedTrack=-1;bool updating{},playing{};std::chrono::steady_clock::time_point playStart;
    static constexpr RECT graph{15,330,885,530};
    J& elem(){return working.elements().at(element);}
    HWND item(int id)const{return GetDlgItem(window,id);}
    HWND control(const wchar_t* cls,const wchar_t* text,int id,int x,int y,int w,int h,DWORD style=0){
        return CreateWindowExW(cls==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,x,y,w,h,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandle(nullptr),nullptr);
    }
    void label(const wchar_t* text,int x,int y,int w=200){control(L"STATIC",text,0,x,y,w,20);}
    void fill(int id,const std::vector<std::wstring>& values){for(auto& v:values)SendMessageW(item(id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(v.c_str()));}
    static std::wstring text(HWND h){const int n=GetWindowTextLengthW(h);std::wstring s(size_t(n)+1,L'\0');GetWindowTextW(h,s.data(),n+1);s.resize(size_t(n));return s;}
    static double number(HWND h,const char* what){
        const auto s=text(h);size_t used{};double v{};try{v=std::stod(s,&used);}catch(...){used=0;}
        if(s.empty()||used!=s.size()||!std::isfinite(v))throw std::runtime_error(std::string("Invalid number: ")+what);return v;
    }
    void setCombo(int id,const std::wstring& value){
        const auto at=SendMessageW(item(id),CB_FINDSTRINGEXACT,size_t(-1),reinterpret_cast<LPARAM>(value.c_str()));
        if(at!=CB_ERR)SendMessageW(item(id),CB_SETCURSEL,at,0);else SetWindowTextW(item(id),value.c_str());
    }
    std::wstring comboText(int id){const auto at=SendMessageW(item(id),CB_GETCURSEL,0,0);
        if(at!=CB_ERR){const auto n=SendMessageW(item(id),CB_GETLBTEXTLEN,at,0);std::wstring s(size_t(n)+1,L'\0');SendMessageW(item(id),CB_GETLBTEXT,at,reinterpret_cast<LPARAM>(s.data()));s.resize(size_t(n));return s;}
        return text(item(id));}
    void create(){
        label(targets.size()>1?(L"트랙 — 적용 시 선택한 "+std::to_wstring(targets.size())+L"개 요소 전체의 트랙을 이 목록으로 교체").c_str():L"트랙 (위에서 아래로 적용, 같은 속성은 뒤쪽이 우선)",15,8,640);control(L"LISTBOX",L"",TrackList,15,30,240,170,LBS_NOTIFY|WS_VSCROLL);
        control(L"BUTTON",L"추가",AddTrack,15,205,75,26);control(L"BUTTON",L"복제",DuplicateTrack,95,205,75,26);control(L"BUTTON",L"삭제",DeleteTrack,175,205,80,26);
        control(L"BUTTON",L"▲ 위로",MoveUp,15,235,120,26);control(L"BUTTON",L"▼ 아래로",MoveDown,135,235,120,26);
        label(L"트랙 이름",275,8,200);control(L"EDIT",L"",IdEdit,275,30,200,24,ES_AUTOHSCROLL);
        label(L"속성",495,8,150);control(L"COMBOBOX",L"",PropertyBox,495,30,160,150,CBS_DROPDOWNLIST|WS_VSCROLL);fill(PropertyBox,anim::properties);
        label(L"시작 이벤트(직접 입력 가능)",275,66,220);control(L"COMBOBOX",L"",TriggerBox,275,88,200,150,CBS_DROPDOWN|WS_VSCROLL|CBS_AUTOHSCROLL);fill(TriggerBox,anim::triggers);
        label(L"보간",495,66,150);control(L"COMBOBOX",L"",EasingBox,495,88,160,150,CBS_DROPDOWNLIST|WS_VSCROLL);fill(EasingBox,anim::easings);
        label(L"완료 후",275,124,150);control(L"COMBOBOX",L"",FinishBox,275,146,200,150,CBS_DROPDOWNLIST);fill(FinishBox,anim::finishes);
        label(L"다시 발생 시",495,124,150);control(L"COMBOBOX",L"",PolicyBox,495,146,160,150,CBS_DROPDOWNLIST);fill(PolicyBox,anim::policies);
        label(L"길이 (초)",275,182,150);control(L"EDIT",L"",DurationEdit,275,204,200,24,ES_AUTOHSCROLL);
        control(L"BUTTON",L"반복",LoopCheck,495,206,160,24,BS_AUTOCHECKBOX);
        label(L"키프레임 (시간 → 값)",675,8,210);control(L"LISTBOX",L"",KeyList,675,30,210,170,LBS_NOTIFY|WS_VSCROLL);
        label(L"시간(초)",675,206,90);label(L"값",785,206,90);
        control(L"EDIT",L"",TimeEdit,675,228,100,24,ES_AUTOHSCROLL);control(L"EDIT",L"",ValueEdit,785,228,100,24,ES_AUTOHSCROLL);
        control(L"BUTTON",L"추가",AddKey,675,258,65,26);control(L"BUTTON",L"수정",SetKey,745,258,65,26);control(L"BUTTON",L"삭제",DeleteKey,815,258,70,26);
        label(L"값 곡선 — 게임과 같은 평가기로 계산합니다 (x/y는 절대 좌표, scale 배율, rotation 도, opacity 0~1)",15,306,860);
        control(L"BUTTON",L"▶ 곡선 재생",Play,15,542,140,30);control(L"BUTTON",L"적용 (캔버스에서 재생)",Apply,675,542,210,30);control(L"BUTTON",L"닫기",Close,565,542,100,30);
        control(L"STATIC",L"",Status,165,548,390,20);
        refreshTracks();
    }
    skin::AnimationTrack parsedTrack()const{
        if(selectedTrack<0)throw std::runtime_error("No track");
        return anim::parsed(working.elements().at(element)).at(size_t(selectedTrack));
    }
    void refreshTracks(int select=-2){
        updating=true;const auto& e=elem();const size_t count=anim::trackCount(e);
        SendMessageW(item(TrackList),LB_RESETCONTENT,0,0);
        for(size_t i=0;i<count;++i){const auto s=anim::settings(e,i);const auto label=s.id+L"  ["+s.property+L" / "+s.trigger+L"]";SendMessageW(item(TrackList),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
        if(select!=-2)selectedTrack=select;
        if(selectedTrack>=int(count))selectedTrack=int(count)-1;if(selectedTrack<0&&count)selectedTrack=0;
        if(selectedTrack>=0)SendMessageW(item(TrackList),LB_SETCURSEL,selectedTrack,0);
        updating=false;refreshFields();
    }
    void refreshFields(int keySelect=-1){
        updating=true;const bool has=selectedTrack>=0;
        for(int id:{IdEdit,PropertyBox,TriggerBox,EasingBox,FinishBox,PolicyBox,DurationEdit,LoopCheck,KeyList,TimeEdit,ValueEdit,AddKey,SetKey,DeleteKey,Play,DuplicateTrack,DeleteTrack,MoveUp,MoveDown})EnableWindow(item(id),has);
        SendMessageW(item(KeyList),LB_RESETCONTENT,0,0);
        if(has){
            const auto& e=elem();const auto s=anim::settings(e,size_t(selectedTrack));
            SetWindowTextW(item(IdEdit),s.id.c_str());setCombo(PropertyBox,s.property);setCombo(TriggerBox,s.trigger);setCombo(EasingBox,s.easing);setCombo(FinishBox,s.finish);setCombo(PolicyBox,s.policy);
            SetWindowTextW(item(DurationEdit),std::to_wstring(s.duration).c_str());SendMessageW(item(LoopCheck),BM_SETCHECK,s.loop?BST_CHECKED:BST_UNCHECKED,0);
            const size_t n=anim::keyCount(e,size_t(selectedTrack));
            for(size_t i=0;i<n;++i){const auto [time,value]=anim::key(e,size_t(selectedTrack),i);const auto row=std::to_wstring(time)+L"  →  "+std::to_wstring(value);SendMessageW(item(KeyList),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(row.c_str()));}
            if(keySelect>=0&&size_t(keySelect)<n){SendMessageW(item(KeyList),LB_SETCURSEL,keySelect,0);fillKeyEdits();}
        }
        updating=false;InvalidateRect(window,&graph,FALSE);
    }
    void fillKeyEdits(){
        const int at=int(SendMessageW(item(KeyList),LB_GETCURSEL,0,0));if(at<0||selectedTrack<0)return;
        const auto [time,value]=anim::key(elem(),size_t(selectedTrack),size_t(at));
        SetWindowTextW(item(TimeEdit),std::to_wstring(time).c_str());SetWindowTextW(item(ValueEdit),std::to_wstring(value).c_str());
    }
    void say(const std::wstring& s){SetWindowTextW(item(Status),s.c_str());}
    // Reads the settings controls and stores them; on failure the controls are reverted to the stored values.
    void applyFields(){
        if(updating||selectedTrack<0)return;
        try{
            anim::TrackSettings s;s.id=text(item(IdEdit));s.property=comboText(PropertyBox);s.trigger=comboText(TriggerBox);s.easing=comboText(EasingBox);s.finish=comboText(FinishBox);s.policy=comboText(PolicyBox);
            s.duration=number(item(DurationEdit),"duration");s.loop=SendMessageW(item(LoopCheck),BM_GETCHECK,0,0)==BST_CHECKED;
            anim::updateTrack(elem(),size_t(selectedTrack),s);say(L"");refreshTracks();
        }catch(const detail::Failure& error){say(detail::wide(error.error.message,""));refreshTracks();}
        catch(const std::exception& error){say(detail::wide(error.what(),""));refreshTracks();}
    }
    void command(int id,int code){
        if(updating)return;
        try{
            switch(id){
            case TrackList:if(code==LBN_SELCHANGE){selectedTrack=int(SendMessageW(item(TrackList),LB_GETCURSEL,0,0));refreshFields();}break;
            case AddTrack:{const auto at=anim::addTrack(elem(),L"opacity",L"screen.enter");refreshTracks(int(at));}break;
            case DuplicateTrack:if(selectedTrack>=0)refreshTracks(int(anim::duplicateTrack(elem(),size_t(selectedTrack))));break;
            case DeleteTrack:if(selectedTrack>=0){anim::removeTrack(elem(),size_t(selectedTrack));refreshTracks();}break;
            case MoveUp:case MoveDown:if(selectedTrack>=0){const int to=selectedTrack+(id==MoveUp?-1:1);anim::moveTrack(elem(),size_t(selectedTrack),id==MoveUp?-1:1);refreshTracks(std::clamp(to,0,int(anim::trackCount(elem()))-1));}break;
            case PropertyBox:case EasingBox:case FinishBox:case PolicyBox:if(code==CBN_SELCHANGE)PostMessageW(window,WM_APP,0,0);break; // CB_GETCURSEL is not final until the notification returns
            case TriggerBox:if(code==CBN_SELCHANGE)PostMessageW(window,WM_APP,0,0);else if(code==CBN_KILLFOCUS)applyFields();break;
            case IdEdit:case DurationEdit:if(code==EN_KILLFOCUS)applyFields();break;
            case LoopCheck:if(code==BN_CLICKED)applyFields();break;
            case KeyList:if(code==LBN_SELCHANGE)fillKeyEdits();break;
            case AddKey:if(selectedTrack>=0){const auto at=anim::addKey(elem(),size_t(selectedTrack),number(item(TimeEdit),"time"),number(item(ValueEdit),"value"));say(L"");refreshFields(int(at));}break;
            case SetKey:{const int at=int(SendMessageW(item(KeyList),LB_GETCURSEL,0,0));if(selectedTrack>=0&&at>=0){const auto to=anim::setKey(elem(),size_t(selectedTrack),size_t(at),number(item(TimeEdit),"time"),number(item(ValueEdit),"value"));say(L"");refreshFields(int(to));}}break;
            case DeleteKey:{const int at=int(SendMessageW(item(KeyList),LB_GETCURSEL,0,0));if(selectedTrack>=0&&at>=0){anim::removeKey(elem(),size_t(selectedTrack),size_t(at));say(L"");refreshFields(std::max(0,at-1));}}break;
            case Play:startPlayback();break;
            case Apply:{
                applyFields();size_t others=0;
                for(int target:targets)if(target!=element&&target>=0&&target<int(working.elements().size())){anim::copyTracks(elem(),working.elements().at(target));++others;}
                commit(working);if(selectedTrack>=0&&play)play(anim::settings(elem(),size_t(selectedTrack)).trigger);
                say(others?L"선택한 "+std::to_wstring(others+1)+L"개 요소 모두에 적용했습니다.":L"적용했습니다. (편집기 실행 취소로 되돌릴 수 있습니다)");
            }break;
            case Close:DestroyWindow(window);break;
            }
        }catch(const detail::Failure& error){say(detail::wide(error.error.message,""));}
        catch(const std::exception& error){say(detail::wide(error.what(),""));}
    }
    void startPlayback(){applyFields();playing=true;playStart=std::chrono::steady_clock::now();SetTimer(window,1,16,nullptr);}
    double cursor(const skin::AnimationTrack& t)const{
        const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-playStart).count();return t.loop?std::fmod(elapsed,t.duration):std::min(elapsed,t.duration);
    }
    void tick(){
        if(!playing)return;InvalidateRect(window,&graph,FALSE);
        try{const auto t=parsedTrack();if(!t.loop&&std::chrono::duration<double>(std::chrono::steady_clock::now()-playStart).count()>=t.duration+.25){playing=false;KillTimer(window,1);}}catch(...){playing=false;KillTimer(window,1);}
    }
    void paint(HDC dc){
        const int w=graph.right-graph.left,h=graph.bottom-graph.top;HDC mem=CreateCompatibleDC(dc);HBITMAP bmp=CreateCompatibleBitmap(dc,w,h);auto old=SelectObject(mem,bmp);
        RECT all{0,0,w,h};HBRUSH bg=CreateSolidBrush(RGB(250,250,252));FillRect(mem,&all,bg);DeleteObject(bg);SetBkMode(mem,TRANSPARENT);
        HGDIOBJ font=GetStockObject(DEFAULT_GUI_FONT);SelectObject(mem,font);
        try{
            const auto t=parsedTrack();double lo=t.keys.front().second,hi=lo;for(auto& k:t.keys){lo=std::min(lo,k.second);hi=std::max(hi,k.second);}
            if(hi-lo<1e-9){lo-=1;hi+=1;}const double pad=(hi-lo)*.12;lo-=pad;hi+=pad;
            const int left=60,right=w-20,top=14,bottom=h-26;auto X=[&](double time){return left+int((right-left)*time/t.duration);};auto Y=[&](double v){return bottom-int((bottom-top)*(v-lo)/(hi-lo));};
            HPEN grid=CreatePen(PS_SOLID,1,RGB(222,224,230));auto prev=SelectObject(mem,grid);
            for(int i=0;i<=4;++i){const int y=top+(bottom-top)*i/4;MoveToEx(mem,left,y,nullptr);LineTo(mem,right,y);const auto v=std::to_wstring(hi-(hi-lo)*i/4);const auto s=v.substr(0,v.find(L'.')+3);TextOutW(mem,4,y-7,s.c_str(),int(s.size()));}
            for(int i=0;i<=4;++i){const int x=left+(right-left)*i/4;MoveToEx(mem,x,top,nullptr);LineTo(mem,x,bottom);const auto v=std::to_wstring(t.duration*i/4);const auto s=v.substr(0,v.find(L'.')+3)+L"s";TextOutW(mem,x-12,bottom+5,s.c_str(),int(s.size()));}
            SelectObject(mem,prev);DeleteObject(grid);
            HPEN curve=CreatePen(PS_SOLID,2,RGB(30,110,230));prev=SelectObject(mem,curve);
            for(int i=0;i<=400;++i){const double time=t.duration*i/400;const int x=X(time),y=Y(t.sample(time));if(i==0)MoveToEx(mem,x,y,nullptr);else LineTo(mem,x,y);}
            SelectObject(mem,prev);DeleteObject(curve);
            HBRUSH dot=CreateSolidBrush(RGB(230,80,40));prev=SelectObject(mem,dot);
            for(auto& k:t.keys)Rectangle(mem,X(k.first)-4,Y(k.second)-4,X(k.first)+5,Y(k.second)+5);SelectObject(mem,prev);DeleteObject(dot);
            if(playing){const double now=cursor(t);HPEN line=CreatePen(PS_SOLID,1,RGB(40,160,80));prev=SelectObject(mem,line);MoveToEx(mem,X(now),top,nullptr);LineTo(mem,X(now),bottom);SelectObject(mem,prev);DeleteObject(line);
                const auto s=L"t="+std::to_wstring(now).substr(0,5)+L"s  값="+std::to_wstring(t.sample(now)).substr(0,8);TextOutW(mem,left+8,top+2,s.c_str(),int(s.size()));}
        }catch(...){const wchar_t* s=L"트랙을 선택하세요";TextOutW(mem,20,20,s,int(wcslen(s)));}
        BitBlt(dc,graph.left,graph.top,w,h,mem,0,0,SRCCOPY);SelectObject(mem,old);DeleteObject(bmp);DeleteDC(mem);
        FrameRect(dc,&graph,static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    }
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wParam,LPARAM lParam){
        auto* self=reinterpret_cast<AnimationDialog*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<AnimationDialog*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);self->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(window,message,wParam,lParam);
        try{
            if(message==WM_CREATE){self->create();return 0;}
            if(message==WM_COMMAND){self->command(LOWORD(wParam),HIWORD(wParam));return 0;}
            if(message==WM_APP){self->applyFields();return 0;}
            if(message==WM_TIMER&&wParam==1){self->tick();return 0;}
            if(message==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);self->paint(dc);EndPaint(window,&ps);return 0;}
            if(message==WM_CLOSE){DestroyWindow(window);return 0;}
            if(message==WM_DESTROY){KillTimer(window,1);}
        }catch(const std::exception& error){MessageBoxA(window,error.what(),"Animation editor",MB_ICONERROR);}
        return DefWindowProcW(window,message,wParam,lParam);
    }
};
inline void editAnimations(HWND owner,const Document& document,int element,std::vector<int> targets,std::function<void(const Document&)> commit,std::function<void(const std::wstring&)> play){
    if(element<0||element>=int(document.screens.at(document.screen)[L"elements"].array.size()))throw std::runtime_error("애니메이션을 편집할 요소를 먼저 선택하세요.");
    AnimationDialog dialog;dialog.working=document;dialog.element=element;dialog.targets=std::move(targets);dialog.commit=std::move(commit);dialog.play=std::move(play);
    WNDCLASSW c{};c.lpfnWndProc=AnimationDialog::proc;c.hInstance=GetModuleHandle(nullptr);c.lpszClassName=L"SevenManiaAnimationEditor";c.hCursor=LoadCursor(nullptr,IDC_ARROW);c.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClassW(&c);
    HWND window=CreateWindowExW(WS_EX_DLGMODALFRAME,c.lpszClassName,L"애니메이션 트랙 편집",WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,140,100,915,625,owner,nullptr,c.hInstance,&dialog);
    if(!window)throw std::runtime_error("Cannot open animation editor");
    EnableWindow(owner,FALSE);MSG message{};
    while(IsWindow(window)&&GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    EnableWindow(owner,TRUE);SetForegroundWindow(owner);
}
}
