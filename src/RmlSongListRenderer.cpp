#include "RmlSongListRenderer.h"
#include "TextMarquee.h"
#include "RmlUiRuntime.h"

#ifdef BMS_ENABLE_RMLUI_SONG_LIST
#include <RmlUi/Core.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/TextShapingContext.h>
#include "RmlUi_Renderer_DX11.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <unordered_set>

namespace bms::ui {
namespace {

std::string utf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
    return result;
}

std::string escaped(std::wstring_view text) {
    std::string output;
    for (const char c : utf8(text)) {
        switch (c) {
        case '&': output += "&amp;"; break;
        case '<': output += "&lt;"; break;
        case '>': output += "&gt;"; break;
        case '"': output += "&quot;"; break;
        case '\r': case '\n': output += ' '; break;
        default: output += c; break;
        }
    }
    return output;
}

void requireHr(HRESULT result, const char* message) {
    if (FAILED(result)) throw std::runtime_error(message);
}

} // namespace

struct RmlSongListRenderer::Impl {
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    std::shared_ptr<RmlUiRuntime> runtime;
    Ptr<ID3D11Device> device;
    Ptr<ID3D11DeviceContext> deviceContext;
    Ptr<ID3D11Texture2D> target, staging;
    Ptr<ID3D11RenderTargetView> targetView;
    RenderInterface_DX11* renderer{};
    Rml::Context* context{};
    Rml::Element* rowsElement{};
    std::unordered_set<std::string> bannerSources;
    int width{}, height{};
    std::vector<SongListVisualRow> rows;
    Rml::Element* scrollingTitle{};
    double titleOverflow{};
    int titleOffset{};
    HDC bitmapDc{};
    HBITMAP bitmap{};
    HGDIOBJ originalBitmap{};
    std::uint32_t* pixels{};

    ~Impl() {
        if (context) { runtime->requireThread(); Rml::RemoveContext(context->GetName()); }
        GdiFlush();
        if (bitmapDc && originalBitmap) SelectObject(bitmapDc, originalBitmap);
        if (bitmap) DeleteObject(bitmap);
        if (bitmapDc) DeleteDC(bitmapDc);
    }

    void initialize() {
        runtime = RmlUiRuntime::acquire();
        device = runtime->device; deviceContext = runtime->deviceContext; renderer = runtime->renderer.get();
        context = runtime->createContext("bms-song-list", {1, 1});
        if (!context) throw std::runtime_error("Unable to create RmlUi song-list context");
        std::wstring executable(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (length == 0 || length >= executable.size()) throw std::runtime_error("Unable to locate song-list assets");
        executable.resize(length);
        const auto fontPath=std::filesystem::path(executable).parent_path()/L"Assets/Fonts/Michroma-Regular.ttf";
        if(!Rml::LoadFontFace(utf8(fontPath.generic_wstring()),"Michroma",Rml::Style::FontStyle::Normal,Rml::Style::FontWeight::Normal))throw std::runtime_error("Unable to load Michroma song difficulty font");
        for(const auto bold:{false,true}){const auto path=std::filesystem::path(executable).parent_path()/(bold?L"Assets/Fonts/Oxanium-Bold.ttf":L"Assets/Fonts/Oxanium-Regular.ttf");
            if(!Rml::LoadFontFace(utf8(path.generic_wstring()),"Oxanium",Rml::Style::FontStyle::Normal,bold?Rml::Style::FontWeight::Bold:Rml::Style::FontWeight::Normal))throw std::runtime_error("Unable to load Oxanium font");}
        const auto documentPath = std::filesystem::path(executable).parent_path() / L"Assets/UI/song-list.rml";
        auto* document = context->LoadDocument(utf8(documentPath.generic_wstring()));
        if (!document) throw std::runtime_error("Unable to load Assets/UI/song-list.rml");
        rowsElement = document->GetElementById("rows");
        if (!rowsElement) throw std::runtime_error("Song-list document has no rows element");
        document->Show();
    }

    void resize(int newWidth, int newHeight) {
        GdiFlush();
        targetView.Reset(); target.Reset(); staging.Reset();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = newWidth; desc.Height = newHeight; desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        requireHr(device->CreateTexture2D(&desc, nullptr, &target), "Unable to allocate RmlUi render target");
        requireHr(device->CreateRenderTargetView(target.Get(), nullptr, &targetView), "Unable to create RmlUi target view");
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        requireHr(device->CreateTexture2D(&desc, nullptr, &staging), "Unable to allocate RmlUi readback texture");
        if (!bitmapDc) bitmapDc = CreateCompatibleDC(nullptr);
        if (bitmap && bitmapDc) { SelectObject(bitmapDc, originalBitmap); DeleteObject(bitmap); bitmap = nullptr; }
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = newWidth; info.bmiHeader.biHeight = -newHeight;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        bitmap = CreateDIBSection(bitmapDc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
        if (!bitmapDc || !bitmap || !pixels) throw std::runtime_error("Unable to allocate transparent song-list bitmap");
        originalBitmap = SelectObject(bitmapDc, bitmap);
        width = newWidth; height = newHeight;
        renderer->SetViewport(width, height);
        context->SetDimensions({width, height});
    }

    void render(const std::vector<SongListVisualRow>& newRows, double textSeconds, bool rebuild) {
        if (rebuild) {
            std::string markup;
            std::unordered_set<std::string> nextBanners;
            for (const auto& row : newRows) {
                char color[8]{};
                std::snprintf(color, sizeof(color), "#%02x%02x%02x", GetRValue(row.difficultyColor), GetGValue(row.difficultyColor), GetBValue(row.difficultyColor));
                markup += "<div class=\"row";
                if(row.kind==SongEntryKind::ParentFolder)markup+=" parent-folder";
                else if(row.kind==SongEntryKind::Folder)markup+=" folder-entry";
                else if(row.kind==SongEntryKind::AllSongs)markup+=" all-songs";
                else if(row.kind==SongEntryKind::LibraryRoot)markup+=" library-root";
                if (row.selected) markup += " selected";
                if (row.blocked) markup += " blocked";
                if (row.alternate) markup += " alternate";
                markup += "\" style=\"top:" + std::to_string(row.top) + "px;height:" + std::to_string(row.height) + "px\">";
                markup += "<div class=\"lamp" + std::string(row.lamp == 1 ? " clear" : row.lamp == 2 ? " played" : "") + "\"></div>";
                markup += "<div class=\"level\"";
                if(row.kind==SongEntryKind::Song)markup+=" style=\"color:"+std::string(color)+"\"";
                markup+=">"+escaped(row.level)+"</div>";
                markup += "<div class=\"labels\"><div class=\"title\">";
                if (row.selected) markup += "<span class=\"title-text\" id=\"scrolling-title\">" + escaped(row.title) + "</span>";
                else markup += escaped(row.title);
                markup += "</div><div class=\"artist\">" + escaped(row.artist) + "</div></div>";
                markup += "<div class=\"variant\">" + escaped(row.variant) + "</div>";
                if (!row.banner.empty()) {
                    markup += "<img class=\"banner\" src=\"" + escaped(row.banner) + "\" />";
                    nextBanners.insert(utf8(row.banner));
                }
                markup += "</div>";
            }
            if (newRows.empty()) markup = "<div class=\"empty\">표시할 곡이 없습니다.</div>";
            rowsElement->SetInnerRML(markup);
            for (const auto& source : bannerSources)
                if (!nextBanners.contains(source)) Rml::ReleaseTexture(source, renderer);
            bannerSources = std::move(nextBanners);
            if (!context->Update()) throw std::runtime_error("Unable to update RmlUi song-list layout");
            scrollingTitle = rowsElement->GetElementById("scrolling-title");
            titleOverflow = 0;
            if (scrollingTitle) {
                const auto selected = std::find_if(newRows.begin(), newRows.end(), [](const auto& row) { return row.selected; });
                const Rml::String language;
                const int textWidth = Rml::GetFontEngineInterface()->GetStringWidth(scrollingTitle->GetFontFaceHandle(), utf8(selected->title), {language});
                scrollingTitle->SetProperty("width", std::to_string(textWidth) + "px");
                titleOverflow = std::max(0.0f, textWidth - scrollingTitle->GetParentNode()->GetClientWidth());
            }
            titleOffset = -1;
        }
        const int offset = textMarqueeOffset(titleOverflow, textSeconds);
        if (!rebuild && offset == titleOffset) return;
        titleOffset = offset;
        if (scrollingTitle) {
            scrollingTitle->SetProperty("left", std::to_string(-offset) + "px");
            if (!context->Update()) throw std::runtime_error("Unable to update song-title scrolling");
        }
        const float transparent[4]{};
        deviceContext->ClearRenderTargetView(targetView.Get(), transparent);
        runtime->requireThread();
        renderer->SetViewport(width, height);
        renderer->BeginFrame();
        const bool rendered = context->Render();
        renderer->EndFrame(targetView.Get());
        if (!rendered) throw std::runtime_error("Unable to render RmlUi song list");
        deviceContext->CopyResource(staging.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        requireHr(deviceContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Unable to read RmlUi song list");
        GdiFlush(); // Finish previous GDI reads before replacing the cached DIB.
        for (int y = 0; y < height; ++y)
            std::memcpy(pixels + static_cast<size_t>(y) * width,
                static_cast<const unsigned char*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch, static_cast<size_t>(width) * 4);
        deviceContext->Unmap(staging.Get(), 0);
        rows = newRows;
    }
};

RmlSongListRenderer::RmlSongListRenderer() = default;
RmlSongListRenderer::~RmlSongListRenderer() = default;
bool RmlSongListRenderer::enabled() const noexcept { return !failed_; }
bool RmlSongListRenderer::needsAnimation() const noexcept { return impl_ && impl_->titleOverflow > 0; }

bool RmlSongListRenderer::paint(HDC dc, int width, int height, const std::vector<SongListVisualRow>& rows, double textSeconds) {
    if (failed_ || !dc || width <= 0 || height <= 0 || width > 16384 || height > 16384) return false;
    if (impl_ && !impl_->runtime->ownsThread()) return false;
    try {
        if (!impl_) {
            impl_ = std::make_unique<Impl>();
            impl_->initialize();
        }
        const bool resized = width != impl_->width || height != impl_->height;
        if (resized) impl_->resize(width, height);
        impl_->render(rows, textSeconds, resized || rows != impl_->rows);
        const BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        return AlphaBlend(dc, 0, 0, width, height, impl_->bitmapDc, 0, 0, width, height, blend) != FALSE;
    } catch (const std::exception& error) {
        OutputDebugStringA((std::string("RmlUi song list disabled: ") + error.what() + "\n").c_str());
        reset(); failed_ = true;
        return false;
    }
}

void RmlSongListRenderer::reset() { impl_.reset(); }

} // namespace bms::ui

#else
namespace bms::ui {
struct RmlSongListRenderer::Impl {};
RmlSongListRenderer::RmlSongListRenderer() = default;
RmlSongListRenderer::~RmlSongListRenderer() = default;
bool RmlSongListRenderer::enabled() const noexcept { return false; }
bool RmlSongListRenderer::paint(HDC, int, int, const std::vector<SongListVisualRow>&, double) { return false; }
bool RmlSongListRenderer::needsAnimation() const noexcept { return false; }
void RmlSongListRenderer::reset() {}
} // namespace bms::ui
#endif
