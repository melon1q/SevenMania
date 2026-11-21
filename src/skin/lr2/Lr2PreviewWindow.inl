#include "Lr2Preview.h"
#include "Lr2Diagnostics.h"
#include "Lr2SkinConfig.h"
#include "CompositionPresenter.h"
#include "skin/SkinInternal.h"
#include <chrono>

namespace sevenmania::skin::lr2 {
namespace {
constexpr wchar_t windowClass[]=L"SevenManiaLr2Preview";
constexpr wchar_t windowTitle[]=L"SevenMania LR2 Preview";
constexpr UINT_PTR animationTimer=1;
using Clock=std::chrono::steady_clock;

std::wstring diagnosticText(const std::vector<Diagnostic>& diagnostics) {
    constexpr std::size_t maxLines=30;
    // The grouped summary comes first: it stays readable when a skin emits hundreds of warnings.
    std::wstring text=detail::wide(formatDiagnosticSummary(summarizeDiagnostics(diagnostics)),"LR2 diagnostic");
    if(!text.empty())text+=L"\n";
    for(std::size_t i=0;i<std::min(maxLines,diagnostics.size());++i) {
        const auto& d=diagnostics[i];
        if(!d.source.empty())text+=d.source.filename().wstring()+L":"+std::to_wstring(d.line)+L" ";
        text+=detail::wide(d.message,"LR2 diagnostic")+L"\n";
    }
    if(diagnostics.size()>maxLines)
        text+=L"Additional diagnostics: "+std::to_wstring(diagnostics.size()-maxLines);
    return text;
}

struct PreviewWindow {
    CompositionPresenter presenter{true};
    PreviewScene scene;
    std::vector<Diagnostic> diagnostics;
    Clock::time_point start=Clock::now();
    double time{};
    int width{},height{};
    bool retry{},failed{};

    bool paint(HWND window) {
        RECT client{};
        GetClientRect(window,&client);
        if(client.right<=0||client.bottom<=0||IsIconic(window))return true;
        if(width!=client.right||height!=client.bottom) {
            scene.releaseDevice();
            width=client.right;
            height=client.bottom;
        }
        Microsoft::WRL::ComPtr<ID2D1DeviceContext> context;
        auto prepare=[&] {
            return presenter.prepare(window,width,height)&&
                SUCCEEDED(presenter.renderTarget()->QueryInterface(IID_PPV_ARGS(&context)))&&
                scene.prepare(context.Get());
        };
        time=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        const CompositionPresenter::OverlayDraw overlay{
            [](void* data,ID2D1RenderTarget* target,int w,int h) {
                auto& self=*static_cast<PreviewWindow*>(data);
                // Overlay-only presenter creates an ID2D1DeviceContext.
                self.scene.draw(static_cast<ID2D1DeviceContext*>(target),self.time,w,h);
            }, this};
        if(prepare()&&presenter.present(window,nullptr,width,height,nullptr,0,0,0,overlay)) {
            retry=false;
            return true;
        }
        scene.releaseDevice();
        presenter.reset();
        if(retry)return false;
        retry=true;
        return true;
    }
};

LRESULT CALLBACK previewProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    if(message==WM_NCCREATE) {
        const auto* create=reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return DefWindowProcW(window,message,wParam,lParam);
    }
    auto* preview=reinterpret_cast<PreviewWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(!preview)return DefWindowProcW(window,message,wParam,lParam);
    switch(message) {
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(window,&paint);
            const bool rendered=preview->paint(window);
            EndPaint(window,&paint);
            if(!rendered) {
                preview->failed=true;
                DestroyWindow(window);
            }
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_TIMER:
            if(!IsIconic(window))InvalidateRect(window,nullptr,FALSE);
            return 0;
        case WM_KEYDOWN:
            if(wParam==VK_ESCAPE) {DestroyWindow(window); return 0;}
            if(wParam==VK_SPACE) {preview->start=Clock::now(); return 0;}
            if(wParam==VK_F1) {
                const auto text=diagnosticText(preview->diagnostics);
                MessageBoxW(window,text.empty()?L"No diagnostics.":text.c_str(),windowTitle,MB_OK);
                return 0;
            }
            break;
        case WM_DESTROY:
            KillTimer(window,animationTimer);
            preview->scene.releaseDevice();
            preview->presenter.reset();
            PostQuitMessage(preview->failed?1:0);
            return 0;
    }
    return DefWindowProcW(window,message,wParam,lParam);
}
}

int runPreview(const std::filesystem::path& file,unsigned designWidth,unsigned designHeight) try {
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(initialized)&&initialized!=RPC_E_CHANGED_MODE) {
        MessageBoxW(nullptr,L"Cannot initialize the preview COM apartment.",windowTitle,MB_ICONERROR);
        return 1;
    }
    struct Apartment {bool owned; ~Apartment(){if(owned)CoUninitialize();}} apartment{SUCCEEDED(initialized)};
    PreviewWindow preview;
    Options options;
    options.previewIncludes=true;
    auto result=parse(file,options);
    if(result&&designWidth&&designHeight) {
        result.document->header.width=designWidth;
        result.document->header.height=designHeight;
    }
    preview.diagnostics=std::move(result.diagnostics);
    if(!result||!preview.scene.load(*result.document,preview.diagnostics)) {
        // Show fatal diagnostics first, even if a skin produces many warnings.
        std::stable_partition(preview.diagnostics.begin(),preview.diagnostics.end(),
                              [](const Diagnostic& d){return d.error;});
        const auto text=diagnosticText(preview.diagnostics);
        MessageBoxW(nullptr,text.empty()?L"Cannot load the LR2 skin.":text.c_str(),windowTitle,MB_ICONERROR);
        return 1;
    }
    const auto& header=result.document->header;
    const auto title=std::wstring(windowTitle)+L" - "+detail::wide(header.name,"LR2 skin name")+
        L"  [Space: restart, Esc: close, F1: diagnostics]";
    const auto instance=GetModuleHandleW(nullptr);
    WNDCLASSW type{};
    type.lpfnWndProc=previewProc;
    type.hInstance=instance;
    type.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    type.lpszClassName=windowClass;
    if(!RegisterClassW(&type))return 1;
    const int width=std::min(static_cast<int>(header.width),960);
    const int height=width*static_cast<int>(header.height)/static_cast<int>(header.width);
    RECT bounds{0,0,width,height};
    AdjustWindowRect(&bounds,WS_OVERLAPPEDWINDOW,FALSE);
    const HWND window=CreateWindowExW(0,windowClass,title.c_str(),WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,bounds.right-bounds.left,bounds.bottom-bounds.top,
        nullptr,nullptr,instance,&preview);
    if(!window)return 1;
    if(!SetTimer(window,animationTimer,16,nullptr)) {
        DestroyWindow(window);
        return 1;
    }
    preview.start=Clock::now();
    ShowWindow(window,SW_SHOWNORMAL);
    MSG message{};
    BOOL received;
    while((received=GetMessageW(&message,nullptr,0,0))>0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if(IsWindow(window))DestroyWindow(window);
    if(preview.failed)
        MessageBoxW(nullptr,L"The Direct3D/Direct2D preview could not render.",windowTitle,MB_ICONERROR);
    return received<0?1:static_cast<int>(message.wParam);
} catch(...) {
    MessageBoxW(nullptr,L"The LR2 preview could not be opened.",windowTitle,MB_ICONERROR);
    return 1;
}
}
