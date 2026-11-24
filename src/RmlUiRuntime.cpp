#include "RmlUiRuntime.h"
#ifdef BMS_ENABLE_RMLUI
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <mutex>
namespace bms::ui {
std::string rmlUtf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}
namespace {
class WindowsFiles final : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String& path) override {
        const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), nullptr, 0);
        if (size <= 0) return 0;
        std::wstring wide(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), wide.data(), size);
        FILE* file = nullptr;
        return _wfopen_s(&file, wide.c_str(), L"rb") == 0 ? reinterpret_cast<Rml::FileHandle>(file) : 0;
    }
    void Close(Rml::FileHandle file) override { std::fclose(reinterpret_cast<FILE*>(file)); }
    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override { return std::fread(buffer, 1, size, reinterpret_cast<FILE*>(file)); }
    bool Seek(Rml::FileHandle file, long offset, int origin) override { return std::fseek(reinterpret_cast<FILE*>(file), offset, origin) == 0; }
    size_t Tell(Rml::FileHandle file) override { return static_cast<size_t>(std::ftell(reinterpret_cast<FILE*>(file))); }
};

class UiSystem final : public SystemInterface_Win32 {
    const std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
public:
    unsigned issues{};
    double GetElapsedTime() override { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count(); }
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT || type == Rml::Log::LT_WARNING) ++issues;
        OutputDebugStringA(("RmlUi: " + message + "\n").c_str());
        return true;
    }
};

void requireHr(HRESULT result, const char* message) {
    if (FAILED(result)) throw std::runtime_error(message);
}

class SongListRenderInterface final : public RenderInterface_DX11 {
public:
    using RenderInterface_DX11::RenderInterface_DX11;
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override {
        const auto empty = [&] {
            const std::array<Rml::byte, 4> transparent{};
            dimensions = {1, 1};
            return GenerateTexture({transparent.data(), transparent.size()}, dimensions);
        };
        // A source ending in "#preview" (the skin select screen's picture) keeps its own aspect at up to
        // 1280x720; anything else becomes a small banner.
        constexpr std::string_view previewMark = "#preview";
        const bool preview = source.size() > previewMark.size() && std::string_view(source).ends_with(previewMark);
        const auto file = preview ? source.substr(0, source.size() - previewMark.size()) : source;
        const auto path = std::filesystem::u8path(file);
        Gdiplus::Bitmap original(path.c_str());
        if (original.GetLastStatus() != Gdiplus::Ok) {
            const auto texture = RenderInterface_DX11::LoadTexture(dimensions, file);
            return texture ? texture : empty();
        }
        const UINT width = original.GetWidth(), height = original.GetHeight();
        if (!width || !height || width > 8192 || height > 8192) return empty();
        int thumbWidth = 128, thumbHeight = 40;
        if (preview) {
            const float fit = std::min({1.f, 1280.f / width, 720.f / height});
            thumbWidth = std::max(1, static_cast<int>(width * fit));
            thumbHeight = std::max(1, static_cast<int>(height * fit));
        }
        Gdiplus::Bitmap thumbnail(thumbWidth, thumbHeight, PixelFormat32bppPARGB);
        {
            Gdiplus::Graphics graphics(&thumbnail);
            graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            const float scale = std::min(float(thumbWidth) / width, float(thumbHeight) / height);
            const float w = width * scale, h = height * scale;
            if (graphics.DrawImage(&original, Gdiplus::RectF((thumbWidth - w) / 2, (thumbHeight - h) / 2, w, h)) != Gdiplus::Ok) return empty();
        }
        Gdiplus::BitmapData data{};
        Gdiplus::Rect bounds(0, 0, thumbWidth, thumbHeight);
        if (thumbnail.LockBits(&bounds, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &data) != Gdiplus::Ok) return empty();
        std::vector<Rml::byte> rgba(static_cast<size_t>(thumbWidth) * thumbHeight * 4);
        for (int y = 0; y < thumbHeight; ++y) {
            const auto* bgra = static_cast<const unsigned char*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride;
            for (int x = 0; x < thumbWidth; ++x) {
                const size_t offset = static_cast<size_t>(y * thumbWidth + x) * 4;
                rgba[offset] = bgra[x * 4 + 2]; rgba[offset + 1] = bgra[x * 4 + 1];
                rgba[offset + 2] = bgra[x * 4]; rgba[offset + 3] = bgra[x * 4 + 3];
            }
        }
        thumbnail.UnlockBits(&data);
        dimensions = {thumbWidth, thumbHeight};
        return GenerateTexture({rgba.data(), rgba.size()}, dimensions);
    }
};


}
struct RmlUiRuntime::Services { UiSystem system; WindowsFiles files; ULONG_PTR imageToken{}; };
RmlUiRuntime::RmlUiRuntime() : services_(std::make_unique<Services>()) {}
void RmlUiRuntime::requireThread() const {
    if (thread_ != GetCurrentThreadId()) throw std::runtime_error("RmlUi must remain on its owning UI thread");
}
std::shared_ptr<RmlUiRuntime> RmlUiRuntime::acquire() {
    static std::weak_ptr<RmlUiRuntime> shared;
    static std::mutex lock;
    std::lock_guard guard(lock);
    if (auto live = shared.lock()) { live->requireThread(); return live; }
    auto live = std::make_shared<RmlUiRuntime>();
    live->initialize(); shared = live; return live;
}
void RmlUiRuntime::initialize() {
        Gdiplus::GdiplusStartupInput imageInput;
        if (Gdiplus::GdiplusStartup(&services_->imageToken, &imageInput, nullptr) != Gdiplus::Ok)
            throw std::runtime_error("Unable to initialize banner image decoding");
        constexpr D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_0};
        HRESULT created = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels, 1, D3D11_SDK_VERSION, &device, nullptr, &deviceContext);
        if (FAILED(created)) created = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels, 1, D3D11_SDK_VERSION, &device, nullptr, &deviceContext);
        requireHr(created, "Unable to create song-list D3D11 device");
        renderer = std::make_unique<SongListRenderInterface>(device.Get());
        Rml::SetRenderInterface(renderer.get());
        Rml::SetSystemInterface(&services_->system);
        Rml::SetFileInterface(&services_->files);
        if (!Rml::Initialise()) throw std::runtime_error("Unable to initialize RmlUi");
        initialized_ = true;

        wchar_t windowsDirectory[MAX_PATH]{};
        GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
        const auto fonts = std::filesystem::path(windowsDirectory) / L"Fonts";
        bool fontLoaded = false;
        for (const auto* name : {L"malgun.ttf", L"segoeui.ttf"}) {
            if (Rml::LoadFontFace(rmlUtf8((fonts / name).generic_wstring()), "Bms UI", Rml::Style::FontStyle::Normal,
                Rml::Style::FontWeight::Normal)) { fontLoaded = true; break; }
        }
        if (!fontLoaded) throw std::runtime_error("No supported Windows UI font found");
        // Fallbacks for characters Malgun Gothic lacks: Japanese (Meiryo, or Yu Gothic / MS Gothic where
        // Meiryo is not installed) and symbols.
        for (const auto* name : {L"meiryo.ttc", L"YuGothR.ttc", L"YuGothM.ttc", L"msgothic.ttc", L"seguisym.ttf"})
            if (std::filesystem::exists(fonts / name)) Rml::LoadFontFace(rmlUtf8((fonts / name).generic_wstring()), true);


}
RmlUiRuntime::~RmlUiRuntime() {
    if (initialized_) Rml::Shutdown();
    Rml::SetRenderInterface(nullptr); Rml::SetSystemInterface(nullptr); Rml::SetFileInterface(nullptr);
    renderer.reset();
    if (deviceContext) { deviceContext->ClearState(); deviceContext->Flush(); }
    if (services_->imageToken) Gdiplus::GdiplusShutdown(services_->imageToken);
}
Rml::Context* RmlUiRuntime::createContext(const std::string& prefix, Rml::Vector2i size, Rml::TextInputHandler* input) {
    requireThread();
    return Rml::CreateContext(prefix + "-" + std::to_string(++nextContext_), size, renderer.get(), input);
}
void RmlUiRuntime::setInputWindow(HWND window) { requireThread(); services_->system.SetWindow(window); }
unsigned RmlUiRuntime::diagnostics() const { requireThread(); return services_->system.issues; }
}
#endif
