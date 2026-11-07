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
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
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

bool sameRowsMarkup(const std::vector<SongListVisualRow>& left, const std::vector<SongListVisualRow>& right) {
    if (left.size() != right.size()) return false;
    for (size_t i = 0; i < left.size(); ++i) {
        const auto& a = left[i];
        const auto& b = right[i];
        if (a.kind != b.kind || a.index != b.index || a.top != b.top || a.height != b.height || a.title != b.title || a.level != b.level ||
            a.variant != b.variant || a.banner != b.banner || a.difficultyColor != b.difficultyColor ||
            a.lamp != b.lamp || a.blocked != b.blocked || a.alternate != b.alternate) return false;
    }
    return true;
}

std::string cssPixels(float value) { return std::to_string(value) + "px"; }

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
    Rml::Element* rowTrack{};
    Rml::Element* selectionGuide{};
    std::unordered_set<std::string> bannerSources;
    int width{}, height{};
    std::vector<SongListVisualRow> rows;
    std::vector<Rml::Element*> rowElements, titleElements;
    Rml::Element* scrollingTitle{};
    int selectedTitleIndex{-1};
    double titleOverflow{};
    int titleOffset{};
    float trackTop{std::numeric_limits<float>::quiet_NaN()};
    float guideTop{std::numeric_limits<float>::quiet_NaN()};
    int guideHeight{};
    HDC bitmapDc{};
    HBITMAP bitmap{};
    HGDIOBJ originalBitmap{};
    std::uint32_t* pixels{};

#ifdef BMS_PERF_DIAGNOSTICS
    struct TimingSample { uint64_t count{}; double totalMs{}, maxMs{}; };
    std::array<TimingSample, 6> timings{};
    std::chrono::steady_clock::time_point timingReportAt{std::chrono::steady_clock::now()};
    void recordTiming(size_t index, std::chrono::steady_clock::time_point started) {
        const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        auto& sample = timings[index];
        ++sample.count; sample.totalMs += elapsed; sample.maxMs = std::max(sample.maxMs, elapsed);
    }
    void reportTimings() {
        const auto now = std::chrono::steady_clock::now();
        if (now - timingReportAt < std::chrono::seconds(1)) return;
        timingReportAt = now;
        constexpr std::array<const char*, 6> names{"RmlUpdate", "RmlRender", "CopyResource", "MapReadback", "DibCopy", "AlphaBlend"};
        char line[768]{};
        int used = std::snprintf(line, sizeof(line), "SongListPerf");
        for (size_t i = 0; i < timings.size() && used > 0 && static_cast<size_t>(used) < sizeof(line); ++i) {
            const auto& sample = timings[i];
            if (!sample.count) continue;
            used += std::snprintf(line + used, sizeof(line) - static_cast<size_t>(used), " %s=avg%.3f/max%.3fms(%llu)",
                names[i], sample.totalMs / static_cast<double>(sample.count), sample.maxMs,
                static_cast<unsigned long long>(sample.count));
        }
        if (used > 0) { line[std::min<size_t>(static_cast<size_t>(used), sizeof(line) - 2)] = '\n';
            line[std::min<size_t>(static_cast<size_t>(used) + 1, sizeof(line) - 1)] = '\0'; OutputDebugStringA(line); }
        timings = {};
    }
#endif

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

    void render(const std::vector<SongListVisualRow>& newRows, double textSeconds, bool rebuild,
        float newTrackTop, float newGuideTop, int newRowHeight) {
        if (rebuild) {
            std::string markup;
            std::unordered_set<std::string> nextBanners;
            int trackHeight = 1;
            for (const auto& row : newRows) trackHeight = std::max(trackHeight, static_cast<int>(std::ceil(row.top + row.height)));
            markup = "<div id=\"row-track\" style=\"height:" + std::to_string(trackHeight) + "px\">";
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
                markup += "\" id=\"song-row-" + std::to_string(row.index) + "\" style=\"top:" + cssPixels(row.top) + ";height:" + std::to_string(row.height) + "px\">";
                markup += "<div class=\"lamp" + std::string(row.lamp == 1 ? " clear" : row.lamp == 2 ? " played" : "") + "\"></div>";
                markup += "<div class=\"level\"";
                if(row.kind==SongEntryKind::Song)markup+=" style=\"color:"+std::string(color)+"\"";
                markup+=">"+escaped(row.level)+"</div>";
                markup += "<div class=\"labels\"><div class=\"title\"><span class=\"title-text\" id=\"song-title-" +
                    std::to_string(row.index) + "\">" + escaped(row.title) + "</span></div></div>";
                markup += "<div class=\"variant\">" + escaped(row.variant) + "</div>";
                if (!row.banner.empty()) {
                    markup += "<img class=\"banner\" src=\"" + escaped(row.banner) + "\" />";
                    nextBanners.insert(utf8(row.banner));
                }
                markup += "</div>";
            }
            markup += "</div>";
            if (newRows.empty()) markup = "<div class=\"empty\">표시할 곡이 없습니다.</div>";
            else markup += "<div id=\"selection-guide\"></div>";
            rowsElement->SetInnerRML(markup);
            for (const auto& source : bannerSources)
                if (!nextBanners.contains(source)) Rml::ReleaseTexture(source, renderer);
            bannerSources = std::move(nextBanners);
#ifdef BMS_PERF_DIAGNOSTICS
            const auto updateStarted = std::chrono::steady_clock::now();
#endif
            if (!context->Update()) throw std::runtime_error("Unable to update RmlUi song-list layout");
#ifdef BMS_PERF_DIAGNOSTICS
            recordTiming(0, updateStarted);
#endif
            rowTrack = rowsElement->GetElementById("row-track");
            selectionGuide = rowsElement->GetElementById("selection-guide");
            rowElements.clear(); titleElements.clear(); rowElements.reserve(newRows.size()); titleElements.reserve(newRows.size());
            for (const auto& row : newRows) {
                rowElements.push_back(rowsElement->GetElementById("song-row-" + std::to_string(row.index)));
                titleElements.push_back(rowsElement->GetElementById("song-title-" + std::to_string(row.index)));
            }
        }

        bool needsUpdate = false;
        int nextSelectedTitleIndex = -1;
        for (size_t i = 0; i < newRows.size(); ++i) {
            if (newRows[i].selected) nextSelectedTitleIndex = static_cast<int>(i);
            if (i < rows.size() && i < rowElements.size() && rows[i].selected != newRows[i].selected && rowElements[i]) {
                rowElements[i]->SetClass("selected", newRows[i].selected);
                needsUpdate = true;
            }
        }
        if (nextSelectedTitleIndex != selectedTitleIndex || rebuild) {
            if (scrollingTitle && selectedTitleIndex >= 0 && !rebuild) {
                scrollingTitle->SetProperty("width", "100%");
                scrollingTitle->SetProperty("left", "0px");
                scrollingTitle->SetProperty("overflow", "hidden");
                needsUpdate = true;
            }
            selectedTitleIndex = nextSelectedTitleIndex;
            scrollingTitle = selectedTitleIndex >= 0 && selectedTitleIndex < static_cast<int>(titleElements.size()) ?
                titleElements[static_cast<size_t>(selectedTitleIndex)] : nullptr;
            titleOverflow = 0;
            if (scrollingTitle) {
                const auto& selectedRow = newRows[static_cast<size_t>(selectedTitleIndex)];
                const Rml::String language;
                const int textWidth = Rml::GetFontEngineInterface()->GetStringWidth(
                    scrollingTitle->GetFontFaceHandle(), utf8(selectedRow.title), {language});
                scrollingTitle->SetProperty("width", std::to_string(textWidth) + "px");
                scrollingTitle->SetProperty("overflow", "visible");
                titleOverflow = std::max(0.0f, textWidth - scrollingTitle->GetParentNode()->GetClientWidth());
            }
            titleOffset = -1;
            needsUpdate = true;
        }

        if (rowTrack && (rebuild || !std::isfinite(trackTop) || std::abs(newTrackTop - trackTop) >= 0.01f)) {
            rowTrack->SetProperty("top", cssPixels(newTrackTop));
            trackTop = newTrackTop; needsUpdate = true;
        }
        if (selectionGuide && (rebuild || newGuideTop != guideTop || newRowHeight != guideHeight)) {
            selectionGuide->SetProperty("top", cssPixels(newGuideTop));
            selectionGuide->SetProperty("height", std::to_string(newRowHeight) + "px");
            guideTop = newGuideTop; guideHeight = newRowHeight; needsUpdate = true;
        }
        const int offset = textMarqueeOffset(titleOverflow, textSeconds);
        if (offset != titleOffset) {
            titleOffset = offset;
            if (scrollingTitle) {
            scrollingTitle->SetProperty("left", std::to_string(-offset) + "px");
                needsUpdate = true;
            }
        }
        if (!rebuild && !needsUpdate && sameRowsMarkup(newRows, rows)) return;
        if (needsUpdate) {
#ifdef BMS_PERF_DIAGNOSTICS
            const auto updateStarted = std::chrono::steady_clock::now();
#endif
            if (!context->Update()) throw std::runtime_error("Unable to update RmlUi song-list state");
#ifdef BMS_PERF_DIAGNOSTICS
            recordTiming(0, updateStarted);
#endif
        }
        rows = newRows;
        const float transparent[4]{};
        deviceContext->ClearRenderTargetView(targetView.Get(), transparent);
        runtime->requireThread();
        renderer->SetViewport(width, height);
        renderer->BeginFrame();
#ifdef BMS_PERF_DIAGNOSTICS
        const auto renderStarted = std::chrono::steady_clock::now();
#endif
        const bool rendered = context->Render();
        renderer->EndFrame(targetView.Get());
#ifdef BMS_PERF_DIAGNOSTICS
        recordTiming(1, renderStarted);
#endif
        if (!rendered) throw std::runtime_error("Unable to render RmlUi song list");
#ifdef BMS_PERF_DIAGNOSTICS
        const auto copyStarted = std::chrono::steady_clock::now();
#endif
        deviceContext->CopyResource(staging.Get(), target.Get());
#ifdef BMS_PERF_DIAGNOSTICS
        recordTiming(2, copyStarted);
        const auto mapStarted = std::chrono::steady_clock::now();
#endif
        D3D11_MAPPED_SUBRESOURCE mapped{};
        requireHr(deviceContext->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Unable to read RmlUi song list");
#ifdef BMS_PERF_DIAGNOSTICS
        recordTiming(3, mapStarted);
        const auto dibCopyStarted = std::chrono::steady_clock::now();
#endif
        GdiFlush(); // Finish previous GDI reads before replacing the cached DIB.
        for (int y = 0; y < height; ++y)
            std::memcpy(pixels + static_cast<size_t>(y) * width,
                static_cast<const unsigned char*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch, static_cast<size_t>(width) * 4);
        deviceContext->Unmap(staging.Get(), 0);
#ifdef BMS_PERF_DIAGNOSTICS
        recordTiming(4, dibCopyStarted);
        reportTimings();
#endif
    }
};

RmlSongListRenderer::RmlSongListRenderer() = default;
RmlSongListRenderer::~RmlSongListRenderer() = default;
bool RmlSongListRenderer::enabled() const noexcept { return !failed_; }
bool RmlSongListRenderer::needsAnimation() const noexcept { return impl_ && impl_->titleOverflow > 0; }

bool RmlSongListRenderer::paint(HDC dc, int width, int height, const std::vector<SongListVisualRow>& rows,
    double textSeconds, float trackTop, float guideTop, int rowHeight) {
    if (failed_ || !dc || width <= 0 || height <= 0 || width > 16384 || height > 16384) return false;
    if (impl_ && !impl_->runtime->ownsThread()) return false;
    try {
        if (!impl_) {
            impl_ = std::make_unique<Impl>();
            impl_->initialize();
        }
        const bool resized = width != impl_->width || height != impl_->height;
        if (resized) impl_->resize(width, height);
        impl_->render(rows, textSeconds, resized || !sameRowsMarkup(rows, impl_->rows), trackTop, guideTop, rowHeight);
        const BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
#ifdef BMS_PERF_DIAGNOSTICS
        const auto blendStarted = std::chrono::steady_clock::now();
#endif
        const bool blended = AlphaBlend(dc, 0, 0, width, height, impl_->bitmapDc, 0, 0, width, height, blend) != FALSE;
#ifdef BMS_PERF_DIAGNOSTICS
        impl_->recordTiming(5, blendStarted);
        impl_->reportTimings();
#endif
        return blended;
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
bool RmlSongListRenderer::paint(HDC, int, int, const std::vector<SongListVisualRow>&, double, float, float, int) { return false; }
bool RmlSongListRenderer::needsAnimation() const noexcept { return false; }
void RmlSongListRenderer::reset() {}
} // namespace bms::ui
#endif
