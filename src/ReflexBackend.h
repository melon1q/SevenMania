#pragma once
#include "LowLatency.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <cstring>
#ifndef BMS_DISABLE_NVIDIA_REFLEX
#pragma warning(push)
#pragma warning(disable: 4201 4324)
#include "../third_party/nvapi/nvapi.h"
#include "../third_party/nvapi/nvapi_interface.h"
#pragma warning(pop)

namespace latency {
// Only loads the NVIDIA driver runtime from System32; no bundled driver DLL or import library.
class ReflexBackend final : public Backend {
    IUnknown* device_{}; // Borrowed until release(), before the D3D device is destroyed.
    bool enabled_{};
    decltype(&NvAPI_D3D_GetSleepStatus) status_{};
    decltype(&NvAPI_D3D_SetSleepMode) configure_{};
    decltype(&NvAPI_D3D_Sleep) sleep_{};
    decltype(&NvAPI_D3D_SetLatencyMarker) marker_{};
    using Query = void* (__cdecl*)(unsigned int);
    template<class T> static T resolve(Query query, const char* name) noexcept {
        for (const auto& entry : nvapi_interface_table)
            if (std::strcmp(entry.func, name) == 0) return reinterpret_cast<T>(query(entry.id));
        return nullptr;
    }
    bool okay(NvAPI_Status result, const wchar_t* operation) noexcept {
        if (result == NVAPI_OK) return true;
        wchar_t line[160]{};
        swprintf_s(line, L"BMS Reflex: %s failed (%d); disabling for this session.\n", operation, static_cast<int>(result));
        OutputDebugStringW(line); return false;
    }
public:
    ~ReflexBackend() override { release(); }
    Capability probe(Device device) noexcept override {
        release();
        if (device.vendorId != 0x10de || !device.nativeDevice) return {true, false};
        // Keep one initialized driver runtime for the entire process. Unloading
        // nvapi64.dll during a D3D resize can invalidate live driver callbacks.
        struct Runtime {
            HMODULE library{};
            Query query{};
            bool ready{};
            Runtime() noexcept {
#ifdef _WIN64
                library = LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
                library = LoadLibraryExW(L"nvapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
#endif
                if (!library) return;
                query = reinterpret_cast<Query>(GetProcAddress(library, "nvapi_QueryInterface"));
                if (!query) return;
                const auto initialize = resolve<decltype(&NvAPI_Initialize)>(query, "NvAPI_Initialize");
                ready = initialize && initialize() == NVAPI_OK;
            }
            // Deliberately no Unload/FreeLibrary. The OS owns teardown at exit.
        };
        static const Runtime runtime;
        if (!runtime.library) return {false, false};
        if (!runtime.ready) return {true, false};
        const auto query = runtime.query;
        status_ = resolve<decltype(status_)>(query, "NvAPI_D3D_GetSleepStatus");
        configure_ = resolve<decltype(configure_)>(query, "NvAPI_D3D_SetSleepMode");
        sleep_ = resolve<decltype(sleep_)>(query, "NvAPI_D3D_Sleep");
        marker_ = resolve<decltype(marker_)>(query, "NvAPI_D3D_SetLatencyMarker");
        if (!status_ || !configure_ || !sleep_ || !marker_) return {true, false};
        device_ = static_cast<IUnknown*>(device.nativeDevice);
        NV_GET_SLEEP_STATUS_PARAMS state{}; state.version = NV_GET_SLEEP_STATUS_PARAMS_VER;
        if (status_(device_, &state) != NVAPI_OK) { release(); return {true, false}; }
        return {true, true};
    }
    bool configure(bool enabled, uint32_t intervalUs) noexcept override {
        if (!device_ || !configure_) return false;
        NV_SET_SLEEP_MODE_PARAMS mode{}; mode.version = NV_SET_SLEEP_MODE_PARAMS_VER;
        mode.bLowLatencyMode = enabled; mode.bLowLatencyBoost = false;
        mode.minimumIntervalUs = enabled ? intervalUs : 0;
        mode.bUseMarkersToOptimize = true;
        enabled_ = enabled_ || enabled; // Best-effort OFF even if enabling fails partway.
        if (!okay(configure_(device_, &mode), L"SetSleepMode")) return false;
        enabled_ = enabled;
        NV_GET_SLEEP_STATUS_PARAMS state{}; state.version = NV_GET_SLEEP_STATUS_PARAMS_VER;
        return okay(status_(device_, &state), L"GetSleepStatus");
    }
    bool sleep() noexcept override { return device_ && sleep_ && okay(sleep_(device_), L"Sleep"); }
    bool enabledInDriver() noexcept override {
        NV_GET_SLEEP_STATUS_PARAMS state{}; state.version = NV_GET_SLEEP_STATUS_PARAMS_VER;
        return device_ && status_ && status_(device_, &state) == NVAPI_OK && state.bLowLatencyMode;
    }
    bool marker(uint64_t frame, Marker marker) noexcept override {
        if (!device_ || !marker_) return false;
        NV_LATENCY_MARKER_PARAMS params{}; params.version = NV_LATENCY_MARKER_PARAMS_VER;
        params.frameID = frame; params.markerType = static_cast<NV_LATENCY_MARKER_TYPE>(marker);
        return okay(marker_(device_, &params), L"SetLatencyMarker");
    }
    void release() noexcept override {
        if (device_ && configure_ && enabled_) {
            NV_SET_SLEEP_MODE_PARAMS mode{}; mode.version = NV_SET_SLEEP_MODE_PARAMS_VER;
            configure_(device_, &mode);
        }
        enabled_ = false; device_ = nullptr;
        status_ = nullptr;
        configure_ = nullptr; sleep_ = nullptr; marker_ = nullptr;
    }
};
inline Capability probeReflex() noexcept {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, nullptr))) return {true, false};
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi; Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC desc{};
    if (FAILED(device.As(&dxgi)) || FAILED(dxgi->GetAdapter(&adapter)) || FAILED(adapter->GetDesc(&desc))) return {true, false};
    ReflexBackend backend; return backend.probe({device.Get(), desc.VendorId});
}
}
#else
namespace latency {
class ReflexBackend final : public Backend {
public:
    Capability probe(Device) noexcept override { return {}; }
    bool configure(bool, uint32_t) noexcept override { return false; }
    bool sleep() noexcept override { return false; }
    bool marker(uint64_t, Marker) noexcept override { return false; }
    bool enabledInDriver() noexcept override { return false; }
    void release() noexcept override {}
};
inline Capability probeReflex() noexcept { return {}; }
}
#endif
