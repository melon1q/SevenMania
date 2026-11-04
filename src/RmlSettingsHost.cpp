#include "RmlSettingsHost.h"
#ifdef BMS_ENABLE_RMLUI_SETTINGS
#include "RmlUiRuntime.h"
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <chrono>
#include <cstring>
#include <set>
#include <map>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <stdexcept>

namespace bms::ui {
bool RmlSettingsHost::available() noexcept { return true; }
namespace {
void checked(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("RmlUi settings graphics operation failed"); }
}
struct RmlSettingsHost::Impl : Rml::EventListener {
    std::shared_ptr<RmlUiRuntime> runtime;
    TextInputMethodEditor_Win32 ime;
    Rml::Context* context{};
    Rml::ElementDocument* document{};
    HWND hwnd{};
    Change change;
    Failure failure;
    std::wstring* errorOut{};
    bool embedded{}, synchronizing{}, faulted{};
    Metrics stats;
    std::set<int> keys;
    std::map<std::string,std::vector<std::pair<std::string,std::string>>> optionSets;
    bool dirty = true;
    bool releasingCapture{};
    unsigned surrogate{};
    int width{}, height{};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> target, staging;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> view;
    HDC dc{}; HBITMAP bitmap{}; HGDIOBJ original{}; unsigned char* pixels{};
    template<class Edit> bool edit(const std::string& id, Edit edit) {
        if (!document) return false;
        runtime->requireThread(); auto* element=document->GetElementById(id); if(!element)return false;
        struct Reset { bool& flag; bool previous; ~Reset(){flag=previous;} } reset{synchronizing,synchronizing};
        synchronizing=true; edit(element); dirty=true; InvalidateRect(hwnd,nullptr,FALSE); return true;
    }
    ~Impl() {
        if (hwnd) DestroyWindow(hwnd);
        if (context) Rml::RemoveContext(context->GetName());
        if (dc && original) SelectObject(dc, original);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
    }
    void blur() {
        if (!context) return;
        context->ProcessMouseLeave();
        for (const int key : keys) context->ProcessKeyUp(RmlWin32::ConvertKey(key), 0);
        keys.clear(); surrogate = 0;
        for (int button = 0; button < 3; ++button) context->ProcessMouseButtonUp(button, 0);
        if (ime.IsComposing()) ime.CancelComposition();
        if (auto* element = context->GetFocusElement()) element->Blur();
        if (GetCapture() == hwnd) { releasingCapture = true; ReleaseCapture(); releasingCapture = false; }
        dirty = true;
    }
    void ProcessEvent(Rml::Event& event) override {
        if (synchronizing) return;
        auto* element = event.GetTargetElement();
        while (element && element->GetId().empty()) element = element->GetParentNode();
        if (!element) return;
        std::string value = element->GetAttribute<Rml::String>("value", "");
        if (auto* control = dynamic_cast<Rml::ElementFormControl*>(element)) {
            const auto type = element->GetAttribute<Rml::String>("type", "");
            if (event.GetType() == "click" && type != "button" && type != "submit") return;
            value = control->GetValue();
        }
        if (element->GetAttribute<Rml::String>("type", "") == "checkbox") value = element->HasAttribute("checked") ? "1" : "0";
        if (auto* label = document->GetElementById("probe-status")) label->SetInnerRML("Input received");
        dirty = true;
        if (change) change(element->GetId(), value); // Callbacks enqueue expensive application commands.
    }
    void resize(int w, int h) {
        if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return;
        if (width == w && height == h) return;
        GdiFlush(); view.Reset(); target.Reset(); staging.Reset();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = w; desc.Height = h; desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        checked(runtime->device->CreateTexture2D(&desc, nullptr, &target));
        checked(runtime->device->CreateRenderTargetView(target.Get(), nullptr, &view));
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        checked(runtime->device->CreateTexture2D(&desc, nullptr, &staging));
        if (!dc) dc = CreateCompatibleDC(nullptr);
        if (bitmap) { SelectObject(dc, original); DeleteObject(bitmap); bitmap = nullptr; }
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
        if (!dc || !bitmap || !pixels) throw std::runtime_error("Unable to allocate settings back buffer");
        original = SelectObject(dc, bitmap); width = w; height = h;
        context->SetDimensions({w, h}); dirty = true;
    }
    void paint(HDC destination) {
        runtime->requireThread();
        RECT client{}; GetClientRect(hwnd, &client); resize(client.right, client.bottom);
        if (!bitmap) return;
        if (dirty || context->GetNextUpdateDelay() <= 0) {
            const auto start = std::chrono::steady_clock::now();
            if (!context->Update()) throw std::runtime_error("Settings layout failed");
            const float clear[]{0.04f, 0.06f, 0.08f, 1};
            runtime->deviceContext->ClearRenderTargetView(view.Get(), clear);
            runtime->renderer->SetViewport(width, height); runtime->renderer->BeginFrame();
            const bool rendered = context->Render(); runtime->renderer->EndFrame(view.Get());
            if (!rendered) throw std::runtime_error("Settings render failed");
            runtime->deviceContext->CopyResource(staging.Get(), target.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            checked(runtime->deviceContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
            GdiFlush();
            for (int y = 0; y < height; ++y) std::memcpy(pixels + size_t(y) * width * 4,
                static_cast<const unsigned char*>(mapped.pData) + size_t(y) * mapped.RowPitch, size_t(width) * 4);
            runtime->deviceContext->Unmap(staging.Get(), 0);
            stats.frames++; stats.renderMilliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            dirty = false;
        }
        BitBlt(destination, 0, 0, width, height, dc, 0, 0, SRCCOPY);
    }
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            self->hwnd = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, message, w, l);
        try {
            if (message == WM_CLOSE) {
                if (self->embedded) PostMessageW(GetParent(window), WM_CLOSE, 0, 0);
                else DestroyWindow(window);
                return 0;
            }
            if (message == WM_APP + 1) { DestroyWindow(window); return 0; }
            if (message == WM_DESTROY) {
                KillTimer(window, 1); self->blur();
                if (self->context) { Rml::RemoveContext(self->context->GetName()); self->context = nullptr; self->document = nullptr; }
                GdiFlush();
                if (self->dc && self->original) SelectObject(self->dc, self->original);
                if (self->bitmap) DeleteObject(self->bitmap);
                if (self->dc) DeleteDC(self->dc);
                self->dc = nullptr; self->bitmap = nullptr; self->original = nullptr; self->pixels = nullptr;
                self->target.Reset(); self->staging.Reset(); self->view.Reset();
                if (self->runtime) self->runtime->setInputWindow(nullptr);
                self->runtime.reset(); self->hwnd = nullptr; return 0;
            }
            if (message == WM_ERASEBKGND) return 1;
            if (!self->context) return DefWindowProcW(window, message, w, l);
            self->runtime->requireThread(); self->runtime->setInputWindow(window);
            if (message == WM_SIZE) { self->resize(LOWORD(l), HIWORD(l)); InvalidateRect(window, nullptr, FALSE); return 0; }
            if (message == WM_DPICHANGED) {
                self->context->SetDensityIndependentPixelRatio(float(HIWORD(w)) / 96);
                const auto* r = reinterpret_cast<RECT*>(l);
                if (r) SetWindowPos(window, nullptr, r->left, r->top, r->right-r->left, r->bottom-r->top, SWP_NOZORDER | SWP_NOACTIVATE);
                self->dirty = true; InvalidateRect(window, nullptr, FALSE); return 0;
            }
            if (message == WM_TIMER) {
                if (IsWindowVisible(window) && (self->dirty || self->context->GetNextUpdateDelay() <= 0)) InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            if (message == WM_PAINT) {
                PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint);
                try { self->paint(dc); } catch (...) { EndPaint(window, &paint); throw; }
                EndPaint(window, &paint); return 0;
            }
            if (message == WM_PRINTCLIENT) { self->paint(reinterpret_cast<HDC>(w)); return 0; }
            if (message == WM_CAPTURECHANGED && self->releasingCapture) return 0;
            if (message == WM_KILLFOCUS || message == WM_CANCELMODE || message == WM_CAPTURECHANGED) self->blur();
            if (message == WM_KEYDOWN && w == VK_ESCAPE) {
                if (self->ime.IsComposing()) { self->ime.CancelComposition(); self->dirty = true; InvalidateRect(window, nullptr, FALSE); }
                else {
                    Rml::ElementList selects; self->document->QuerySelectorAll(selects,"select");
                    bool closed = false;
                    for(auto* select:selects) for(int i=0;i<select->GetNumChildren(true);++i) {
                        auto* popup=select->GetChild(i);
                        if(popup->GetTagName()=="selectbox" && popup->IsVisible()) {
                            select->Blur(); select->Focus(); closed=true;
                        }
                    }
                    if(closed) { self->dirty=true; InvalidateRect(window,nullptr,FALSE); }
                    else PostMessageW(window, WM_CLOSE, 0, 0);
                }
                return 0;
            }
            if (message == WM_KEYDOWN && w == VK_F9 && self->embedded) { PostMessageW(GetParent(window), WM_KEYDOWN, w, l); return 0; }
            if (message == WM_SYSKEYDOWN && w == VK_F4 && (l & (1LL << 29))) { PostMessageW(window, WM_CLOSE, 0, 0); return 0; }
            if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) self->keys.insert(int(w));
            if (message == WM_KEYUP || message == WM_SYSKEYUP) self->keys.erase(int(w));
            if (message == WM_LBUTTONDOWN && IsWindowVisible(window)) SetFocus(window);
            // Per-host UTF-16 state avoids the official helper's process-static surrogate state.
            bool propagate = true;
            if (message == WM_CHAR) {
                unsigned c = unsigned(w);
                if (c >= 0xd800 && c <= 0xdbff) self->surrogate = c;
                else {
                    if (c >= 0xdc00 && c <= 0xdfff && !self->surrogate) return 0;
                    if (c >= 0xdc00 && c <= 0xdfff && self->surrogate) c = 0x10000 + ((self->surrogate - 0xd800) << 10) + c - 0xdc00;
                    self->surrogate = 0;
                    if (c >= 32 && c != 127) self->context->ProcessTextInput(static_cast<Rml::Character>(c));
                }
            } else if (message == WM_MOUSEWHEEL) {
                POINT point{short(LOWORD(l)), short(HIWORD(l))}; ScreenToClient(window, &point);
                self->context->ProcessMouseMove(point.x, point.y, RmlWin32::GetKeyModifierState());
                propagate = RmlWin32::WindowProcedure(self->context, self->ime, window, message, w, l);
            } else {
                self->releasingCapture = message == WM_LBUTTONUP;
                propagate = RmlWin32::WindowProcedure(self->context, self->ime, window, message, w, l);
                self->releasingCapture = false;
            }
            if (!propagate) { self->dirty = true; InvalidateRect(window, nullptr, FALSE); return 0; }
            if ((message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
                message == WM_KILLFOCUS || message == WM_CAPTURECHANGED || message == WM_IME_COMPOSITION) {
                self->dirty = true; InvalidateRect(window, nullptr, FALSE);
                return 0; // RmlUi alone owns navigation; do not call IsDialogMessage for this HWND.
            }
        } catch (const std::exception& e) {
            OutputDebugStringA(e.what());
            if (self->errorOut) { const std::string text = e.what(); self->errorOut->assign(text.begin(), text.end()); }
            if (!self->faulted) {
                self->faulted = true;
                if (self->failure) self->failure(); // Owner posts fallback outside this RmlUi dispatch.
                PostMessageW(window, WM_APP + 1, 0, 0);
            }
        }
        return DefWindowProcW(window, message, w, l);
    }
};
RmlSettingsHost::RmlSettingsHost() = default;
RmlSettingsHost::~RmlSettingsHost() = default;
bool RmlSettingsHost::open(HWND owner, const std::filesystem::path& path, Change change, bool visible) {
    return openImpl(owner, path, std::move(change), {}, visible, false);
}
bool RmlSettingsHost::openEmbedded(HWND owner, const std::filesystem::path& path, Change change, Failure failure, bool visible) {
    return openImpl(owner, path, std::move(change), std::move(failure), visible, true);
}
bool RmlSettingsHost::openImpl(HWND owner, const std::filesystem::path& path, Change change, Failure failure, bool visible, bool embedded) {
    close(); error_.clear();
    try {
        impl_ = std::make_unique<Impl>(); impl_->runtime = RmlUiRuntime::acquire(); impl_->change = std::move(change);
        impl_->embedded = embedded; impl_->failure = std::move(failure); impl_->errorOut = &error_;
        WNDCLASSW type{}; type.lpfnWndProc = Impl::procedure; type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"BmsRmlSettingsHost"; type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Settings class registration failed");
        RECT ownerClient{0,0,680,520}; if (embedded) GetClientRect(owner, &ownerClient);
        HWND window = CreateWindowExW(0, type.lpszClassName, L"RmlUi settings", embedded ? WS_CHILD | WS_CLIPSIBLINGS : WS_OVERLAPPEDWINDOW,
            embedded ? 0 : CW_USEDEFAULT, embedded ? 0 : CW_USEDEFAULT,
            ownerClient.right, ownerClient.bottom, owner, nullptr, type.hInstance, impl_.get());
        if (!window) throw std::runtime_error("Settings host window failed");
        impl_->context = impl_->runtime->createContext("bms-settings", {680, 520}, &impl_->ime);
        if (!impl_->context) throw std::runtime_error("Settings context failed");
        impl_->context->SetDensityIndependentPixelRatio(float(GetDpiForWindow(window)) / 96);
        // Re-parse external CSS on each embedded settings session so a cached
        // malformed sheet cannot bypass validation on the next open.
        if (embedded) Rml::Factory::ClearStyleSheetCache();
        const unsigned issues = impl_->runtime->diagnostics();
        impl_->document = impl_->context->LoadDocument(rmlUtf8(path.generic_wstring()));
        if (!impl_->document) throw std::runtime_error("Settings test document unavailable; native settings remain active");
        impl_->document->AddEventListener("click", impl_.get()); impl_->document->AddEventListener("change", impl_.get());
        impl_->document->Show();
        impl_->context->Update();
        if (impl_->runtime->diagnostics() != issues) throw std::runtime_error("Settings RML/RCSS contains loading or parsing errors");
        SetTimer(window, 1, 33, nullptr);
        if (visible) ShowWindow(window, SW_SHOW);
        return true;
    } catch (const std::exception& e) {
        const std::string message = e.what(); error_.assign(message.begin(), message.end()); close(); return false;
    }
}
bool RmlSettingsHost::setChecked(const std::string& id, bool checked) {
    if (!impl_ || !impl_->document) return false;
    impl_->runtime->requireThread();
    auto* element = impl_->document->GetElementById(id); if (!element) return false;
    struct Reset { bool& flag; ~Reset() { flag = false; } } reset{impl_->synchronizing};
    impl_->synchronizing = true;
    if (checked) element->SetAttribute("checked", ""); else element->RemoveAttribute("checked");
    impl_->dirty = true; InvalidateRect(impl_->hwnd, nullptr, FALSE); return true;
}
bool RmlSettingsHost::setText(const std::string& id, const std::string& text) {
    if (!impl_ || !impl_->document) return false;
    impl_->runtime->requireThread();
    auto* element = impl_->document->GetElementById(id); if (!element) return false;
    element->SetInnerRML(Rml::StringUtilities::EncodeRml(text)); impl_->dirty = true;
    InvalidateRect(impl_->hwnd, nullptr, FALSE); return true;
}
bool RmlSettingsHost::setValue(const std::string& id,const std::string& value) {
    return impl_ && impl_->edit(id,[&](Rml::Element* e){
        if(auto* control=dynamic_cast<Rml::ElementFormControl*>(e))
            if(control->GetValue()!=value)control->SetValue(value);
    });
}
bool RmlSettingsHost::setRange(const std::string& id,int minimum,int maximum) {
    return impl_ && impl_->edit(id,[&](Rml::Element* e){e->SetAttribute("min",minimum);e->SetAttribute("max",maximum);});
}
bool RmlSettingsHost::setOptions(const std::string& id,const std::vector<std::pair<std::string,std::string>>& options) {
    if(!impl_)return false;
    impl_->runtime->requireThread();
    if(const auto it=impl_->optionSets.find(id);it!=impl_->optionSets.end()&&it->second==options)return true;
    const bool applied=impl_->edit(id,[&](Rml::Element* e){
        // Select options live inside the widget, not the element's normal child
        // list. Replacing inner RML alone appends duplicate options.
        if(auto* select=dynamic_cast<Rml::ElementFormControlSelect*>(e))select->RemoveAll();
        std::string markup;
        for(const auto& [value,label]:options)markup+="<option value=\""+Rml::StringUtilities::EncodeRml(value)+"\">"+Rml::StringUtilities::EncodeRml(label)+"</option>";
        e->SetInnerRML(markup);
    });
    if(applied)impl_->optionSets[id]=options;
    return applied;
}
bool RmlSettingsHost::setVisible(const std::string& id,bool visible) {
    return impl_ && impl_->edit(id,[&](Rml::Element* e){e->SetProperty("display",visible?"block":"none");});
}
bool RmlSettingsHost::setEnabled(const std::string& id,bool enabled) {
    return impl_ && impl_->edit(id,[&](Rml::Element* e){if(auto* c=dynamic_cast<Rml::ElementFormControl*>(e))c->SetDisabled(!enabled);});
}
bool RmlSettingsHost::setClass(const std::string& id,const std::string& name,bool active) {
    return impl_ && impl_->edit(id,[&](Rml::Element* e){e->SetClass(name,active);});
}
bool RmlSettingsHost::scrollTop(const std::string& id) {
    return impl_ && impl_->edit(id,[&](Rml::Element* e){e->SetScrollTop(0);});
}
void RmlSettingsHost::close() {
    if (impl_ && impl_->runtime) impl_->runtime->requireThread();
    impl_.reset();
}
HWND RmlSettingsHost::window() const noexcept { return impl_ ? impl_->hwnd : nullptr; }
bool RmlSettingsHost::owns(HWND hwnd) const noexcept { return window() && (hwnd == window() || IsChild(window(), hwnd)); }
bool RmlSettingsHost::focus(const std::string& id) {
    if (!impl_ || !impl_->document) return false;
    impl_->runtime->requireThread();
    auto* e = impl_->document->GetElementById(id);
    if(!e || !e->Focus())return false;
    e->ScrollIntoView(Rml::ScrollIntoViewOptions{Rml::ScrollAlignment::Nearest});
    impl_->dirty=true; InvalidateRect(impl_->hwnd,nullptr,FALSE); return true;
}
std::string RmlSettingsHost::focused() const {
    if (!impl_ || !impl_->context) return {};
    impl_->runtime->requireThread();
    auto* e = impl_->context->GetFocusElement(); return e ? e->GetId() : "";
}
RECT RmlSettingsHost::bounds(const std::string& id) const {
    if (!impl_ || !impl_->document) return {};
    impl_->runtime->requireThread();
    auto* e = impl_->document->GetElementById(id);
    if (!e && id.find('/') != std::string::npos) {
        const auto slash = id.find('/');
        auto* parent = impl_->document->GetElementById(id.substr(0, slash));
        if (parent) for (int i = 0; i < parent->GetNumChildren(true); ++i) {
            auto* child = parent->GetChild(i);
            if (child->GetTagName() == id.substr(slash + 1)) { e = child; break; }
        }
    }
    if (!e) e = impl_->document->QuerySelector(id);
    if (!e) return {};
    const auto p = e->GetAbsoluteOffset(); const auto size = e->GetBox().GetSize();
    return {LONG(p.x), LONG(p.y), LONG(p.x + size.x), LONG(p.y + size.y)};
}
std::string RmlSettingsHost::value(const std::string& id) const {
    if (!impl_ || !impl_->document) return {};
    impl_->runtime->requireThread();
    auto* e = impl_->document->GetElementById(id); if (!e) return {};
    if (e->GetAttribute<Rml::String>("type", "") == "checkbox") return e->HasAttribute("checked") ? "1" : "0";
    auto* control = dynamic_cast<Rml::ElementFormControl*>(e); return control ? control->GetValue() : "";
}
RmlSettingsHost::Metrics RmlSettingsHost::metrics() const noexcept { return impl_ ? impl_->stats : Metrics{}; }
const std::wstring& RmlSettingsHost::error() const noexcept { return error_; }
}
#else
namespace bms::ui {
bool RmlSettingsHost::available() noexcept { return false; }
struct RmlSettingsHost::Impl {};
RmlSettingsHost::RmlSettingsHost() = default;
RmlSettingsHost::~RmlSettingsHost() = default;
bool RmlSettingsHost::open(HWND, const std::filesystem::path&, Change, bool) { error_ = L"RmlUi settings host is disabled in this build"; return false; }
bool RmlSettingsHost::openEmbedded(HWND, const std::filesystem::path&, Change, Failure, bool) { error_ = L"RmlUi settings host is disabled in this build"; return false; }
bool RmlSettingsHost::setChecked(const std::string&, bool) { return false; }
bool RmlSettingsHost::setText(const std::string&, const std::string&) { return false; }
bool RmlSettingsHost::setValue(const std::string&,const std::string&) { return false; }
bool RmlSettingsHost::setRange(const std::string&,int,int) { return false; }
bool RmlSettingsHost::setOptions(const std::string&,const std::vector<std::pair<std::string,std::string>>&) { return false; }
bool RmlSettingsHost::setVisible(const std::string&,bool) { return false; }
bool RmlSettingsHost::setEnabled(const std::string&,bool) { return false; }
bool RmlSettingsHost::setClass(const std::string&,const std::string&,bool) { return false; }
bool RmlSettingsHost::scrollTop(const std::string&) { return false; }
void RmlSettingsHost::close() {}
HWND RmlSettingsHost::window() const noexcept { return nullptr; }
bool RmlSettingsHost::owns(HWND) const noexcept { return false; }
bool RmlSettingsHost::focus(const std::string&) { return false; }
std::string RmlSettingsHost::focused() const { return {}; }
RECT RmlSettingsHost::bounds(const std::string&) const { return {}; }
std::string RmlSettingsHost::value(const std::string&) const { return {}; }
RmlSettingsHost::Metrics RmlSettingsHost::metrics() const noexcept { return {}; }
const std::wstring& RmlSettingsHost::error() const noexcept { return error_; }
}
#endif
