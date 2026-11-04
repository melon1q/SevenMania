#pragma once
#ifdef BMS_ENABLE_RMLUI
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <RmlUi/Core.h>
#pragma warning(push)
#pragma warning(disable: 4005) // Official Windows backend repeats our Unicode defines.
#include "RmlUi_Renderer_DX11.h"
#include "RmlUi_Platform_Win32.h"
#pragma warning(pop)
#include <memory>
#include <string_view>

namespace bms::ui {
std::string rmlUtf8(std::wstring_view text);
// All clients are confined to one UI thread. Contexts own their documents;
// the last client releases the global runtime, after all contexts are removed.
class RmlUiRuntime {
    struct Services;
    std::unique_ptr<Services> services_;
    bool initialized_{};
    DWORD thread_ = GetCurrentThreadId();
    unsigned nextContext_{};
    void initialize();
public:
    RmlUiRuntime();
    ~RmlUiRuntime();
    RmlUiRuntime(const RmlUiRuntime&) = delete;
    RmlUiRuntime& operator=(const RmlUiRuntime&) = delete;
    static std::shared_ptr<RmlUiRuntime> acquire();
    void requireThread() const;
    bool ownsThread() const noexcept { return thread_ == GetCurrentThreadId(); }
    Rml::Context* createContext(const std::string& prefix, Rml::Vector2i size, Rml::TextInputHandler* input = nullptr);
    void setInputWindow(HWND window);
    unsigned diagnostics() const;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext;
    std::unique_ptr<RenderInterface_DX11> renderer;
};
}
#endif
