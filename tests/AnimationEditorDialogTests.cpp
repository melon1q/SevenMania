#include "editor/AnimationEditor.h"
#include <iostream>
using namespace sevenmania::editor;
namespace {
void require(bool ok,const char* what){if(!ok){std::cerr<<"FAILED: "<<what<<"\n";std::exit(1);}}
}
int main(){
    Document doc;doc.fresh();
    {J el;el.type=T::Object;el.object={{L"id",str(L"box")},{L"type",str(L"text")},{L"x",num(10)},{L"y",num(20)},{L"width",num(100)},{L"height",num(40)},{L"text",str(L"hi")}};
     doc.screens.at(doc.screen).object[L"elements"].array.push_back(el);
     el.object[L"id"]=str(L"second");el.object[L"x"]=num(300);doc.screens.at(doc.screen).object[L"elements"].array.push_back(el);}
    AnimationDialog dialog;dialog.working=doc;dialog.element=0;dialog.targets={0,1};int commits=0;std::wstring played;
    dialog.commit=[&](const Document& next){++commits;doc=next;};dialog.play=[&](const std::wstring& t){played=t;};
    WNDCLASSW c{};c.lpfnWndProc=AnimationDialog::proc;c.hInstance=GetModuleHandle(nullptr);c.lpszClassName=L"SevenManiaAnimationEditorTest";RegisterClassW(&c);
    HWND window=CreateWindowExW(0,c.lpszClassName,L"t",WS_POPUP,0,0,915,625,nullptr,nullptr,c.hInstance,&dialog);
    require(window!=nullptr,"dialog window");
    dialog.command(AnimationDialog::AddTrack,BN_CLICKED);
    require(anim::trackCount(dialog.elem())==1&&dialog.selectedTrack==0,"add track through the dialog");
    // edit settings through the controls, as a user would
    SetWindowTextW(dialog.item(AnimationDialog::IdEdit),L"fade-in");SetWindowTextW(dialog.item(AnimationDialog::DurationEdit),L"2.0");
    SendMessageW(dialog.item(AnimationDialog::EasingBox),CB_SETCURSEL,2,0);dialog.applyFields();
    auto s=anim::settings(dialog.elem(),0);require(s.id==L"fade-in"&&s.duration==2.0&&s.easing==L"ease-out","settings applied");
    // invalid input is refused and the controls are reverted
    SetWindowTextW(dialog.item(AnimationDialog::DurationEdit),L"abc");dialog.applyFields();
    require(anim::settings(dialog.elem(),0).duration==2.0,"invalid duration refused");
    require(GetWindowTextLengthW(dialog.item(AnimationDialog::Status))>0,"error is reported in status");
    SetWindowTextW(dialog.item(AnimationDialog::TimeEdit),L"0.5");SetWindowTextW(dialog.item(AnimationDialog::ValueEdit),L"0.5");
    dialog.command(AnimationDialog::AddKey,BN_CLICKED);require(anim::keyCount(dialog.elem(),0)==3,"key added");
    dialog.startPlayback();dialog.tick();
    HDC dc=CreateCompatibleDC(nullptr);HBITMAP bmp=CreateCompatibleBitmap(GetDC(nullptr),915,625);SelectObject(dc,bmp);dialog.paint(dc);DeleteDC(dc);DeleteObject(bmp);
    dialog.command(AnimationDialog::Apply,BN_CLICKED);
    require(commits==1&&played==L"screen.enter","apply commits and replays the trigger");
    require(anim::trackCount(doc.elements()[0])==1,"committed document has the track");
    require(anim::trackCount(doc.elements()[1])==1&&anim::settings(doc.elements()[1],0).id==L"fade-in","multi-select apply reaches the other selected object");
    dialog.command(AnimationDialog::DeleteTrack,BN_CLICKED);require(anim::trackCount(dialog.elem())==0&&dialog.selectedTrack==-1,"delete");
    DestroyWindow(window);
    std::cout<<"animation editor dialog: add/edit/refuse/key/play/paint/apply/delete passed\n";return 0;
}
