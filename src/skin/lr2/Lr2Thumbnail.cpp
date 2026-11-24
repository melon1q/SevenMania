#include "Lr2Thumbnail.h"
#include "Lr2Preview.h"
#include "Lr2ScreenState.h"
#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cwctype>
#include <vector>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace sevenmania::skin::lr2 {
using Microsoft::WRL::ComPtr;

Options representativeOptions(ScreenType screen, const SkinChoices& choices) {
    Options options;
    options.screen = screen;
    options.choices = choices;
    if (screen == ScreenType::Select)
        options.initialOptions = {{0,false},{3,false},{21,false},{22,false},{23,false},{24,false},{25,false},{26,false},
            {30,true},{31,false},{40,false},{41,true},{46,false},{50,true},{51,false},{52,true},{53,false}};
    else if (screen == ScreenType::Decide) {
        options.initialOptions = {{52,true},{53,false},{190,false},{191,true}};
        for (int i = 0; i <= 5; ++i) options.initialOptions[150 + i] = i == 3;
    } else if (screen == ScreenType::Result) {
        ResultState state;
        state.cleared = true; state.exScore = 190; state.noteCount = 100; state.maxCombo = 100;
        state.played = true; state.bestExScore = 150; state.bestMaxCombo = 80; state.bestMissCount = 5;
        options.initialOptions = resultOptions(state);
    } else if (playLaneCount(static_cast<int>(screen))) {
        options.condition = [screen](int id) -> std::optional<bool> {
            PlayFrame state;
            state.keys = playKeys(static_cast<int>(screen));
            if (const auto enabled = state.option(std::abs(id))) return *enabled == (id > 0);
            return std::nullopt;
        };
    }
    return options;
}

namespace {
std::wstring lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return text;
}
bool picture(const std::filesystem::path& path) {
    const auto extension = lower(path.extension().wstring());
    return extension == L".png" || extension == L".jpg" || extension == L".jpeg" || extension == L".bmp";
}
bool regularFile(const std::filesystem::path& path) { std::error_code ec; return std::filesystem::is_regular_file(path, ec); }
} // namespace

// Beside the skin, by name: "<stem>", "ss_<stem>", "<stem>_preview", "preview_<stem>", "<stem>_thumbnail",
// "thumbnail_<stem>", also with a play skin's "play_" prefix dropped (LR2's play_7.lr2skin -> ss_7.png).
// Then a lone preview / thumbnail / screenshot picture in the skin's folder or its parent, when the folder
// holds a single skin; several skins sharing one folder never borrow each other's picture.
std::filesystem::path findThumbnail(const std::filesystem::path& skin) {
    Options header;
    header.headerOnly = true;
    try {
        if (auto result = parse(skin, header); result && !result.document->header.thumbnail.empty()) {
            // Throws when the named file is missing or outside the theme; then the folder is searched.
            const auto named = resolveAssetPath(result.document->root, skin, result.document->header.thumbnail);
            if (!named.empty() && regularFile(named)) return named;
        }
    } catch (const std::exception&) {
    }
    const auto folder = skin.parent_path();
    const auto stem = lower(skin.stem().wstring());
    std::vector<std::wstring> stems{stem};
    if (stem.starts_with(L"play_")) stems.push_back(stem.substr(5));
    if (stem.starts_with(L"play")) stems.push_back(stem.substr(4));
    std::vector<std::filesystem::path> pictures;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && picture(it->path())) pictures.push_back(it->path());
    for (const auto& name : stems)
        for (const auto& pattern : {name, L"ss_" + name, name + L"_preview", L"preview_" + name, name + L"_thumbnail", L"thumbnail_" + name, name + L"_ss"})
            for (const auto& candidate : pictures)
                if (lower(candidate.stem().wstring()) == pattern) return candidate;
    // A folder with one skin may keep a generically named picture, here or one level up.
    std::size_t skins = 0;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
        if (lower(it->path().extension().wstring()) == L".lr2skin") ++skins;
    if (skins != 1) return {};
    for (const auto& directory : {folder, folder.parent_path()}) {
        std::vector<std::filesystem::path> generic;
        for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec) || !picture(it->path())) continue;
            const auto name = lower(it->path().stem().wstring());
            if (name.starts_with(L"preview") || name.starts_with(L"thumbnail") || name.starts_with(L"thumb") ||
                name.starts_with(L"screenshot") || name.starts_with(L"ss_")) generic.push_back(it->path());
        }
        if (generic.size() == 1) return generic.front();
        if (!generic.empty()) return {};
    }
    return {};
}

namespace {
// Sample state for each screen: something on every bar, a selected song and a stage that has settled.
ScreenData sampleScreen() {
    ScreenData data;
    data.songText = {L"SevenMania", L"Preview", L"Skin Preview Song", L"[ANOTHER]", L"Artist", L"Sub Artist", L"Genre"};
    auto& n = data.numbers;
    n.keys = 7; n.noteCount = 1000; n.exScore = 1820; n.points = 2000; n.combo = 1000; n.maxCombo = 1000; n.judgedNotes = 1000;
    n.judgementCounts = {820, 160, 20, 0, 0}; n.gauge = 100; n.bpm = n.minBpm = n.maxBpm = 150; n.difficulty = 4;
    n.bestExScore = 1600; n.bestMaxCombo = 800; n.targetExScore = 1600; n.result = true;
    n.gaugeHistory.assign(100, 0.f);
    for (std::size_t i = 0; i < n.gaugeHistory.size(); ++i) n.gaugeHistory[i] = std::min(100.f, 20.f + float(i));
    return data;
}
SelectFrame sampleSelect() {
    SelectFrame frame;
    frame.song = sampleScreen();
    frame.song.numbers.result = false;
    frame.songSelected = true; frame.keys = 7; frame.level = 12; frame.canScroll = true; frame.selectionAge = 5000;
    static const wchar_t* titles[] = {L"Preview Song", L"SevenMania", L"Skin Select", L"Another Song", L"Hyper Song", L"Normal Song", L"Folder"};
    for (std::size_t i = 0; i < frame.rows.size(); ++i) {
        auto& row = frame.rows[i];
        row.title = titles[i % std::size(titles)];
        row.level = std::to_wstring(5 + i % 8);
        row.levelValue = 5 + static_cast<int>(i % 8);
        row.difficulty = 1 + static_cast<int>(i % 5);
        row.lamp = static_cast<int>(i % 6);
        row.rank = static_cast<int>(i % 9);
        row.row = static_cast<int>(i);
    }
    return frame;
}
PlayFrame samplePlay(ScreenType screen) {
    PlayFrame frame = sampleScreen().numbers;
    frame.result = false;
    frame.keys = playKeys(static_cast<int>(screen));
    frame.gauge = 80; frame.combo = 321; frame.judge = 5; // PGREAT (LR2 counts judgements down from 5)
    frame.judgementAge = 100; frame.readyAge = 4000; frame.playAge = 3000;
    frame.judgedNotes = 400; frame.exScore = 720; frame.elapsed = 60; frame.remaining = 60; frame.hiSpeed = 3;
    frame.keyOn.fill(-1); frame.keyOff.fill(-1); frame.hold.fill(-1); frame.bomb.fill(-1);
    return frame;
}
// One tap note per lane, rising lane by lane from left to right like stairs. A double play skin climbs
// once per side. Lanes the key mode lacks have zero width and are skipped.
void drawStaircase(ID2D1DeviceContext* target, const PreviewScene& scene, const std::array<PlayLane,16>& lanes, double milliseconds) {
    std::vector<int> order;
    for (int lane = 0; lane < 16; ++lane)
        if (lanes[lane].right > lanes[lane].left && lanes[lane].noteHeight > 0) order.push_back(lane);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return lanes[a].left < lanes[b].left; });
    // Split the two sides where the gap between neighbouring lanes is clearly wider than a lane.
    std::vector<std::vector<int>> sides(1);
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (i > 0) {
            const auto& previous = lanes[order[i - 1]];
            const int gap = lanes[order[i]].left - previous.right;
            if (gap > previous.right - previous.left) sides.emplace_back();
        }
        sides.back().push_back(order[i]);
    }
    for (const auto& side : sides) {
        if (side.empty()) continue;
        const auto& first = lanes[side.front()];
        // Climb about two thirds of the way up the lane, keeping notes at least one note height apart.
        const float step = std::max(first.noteHeight * 1.5f, first.noteBottom * 0.66f / static_cast<float>(side.size()));
        for (std::size_t i = 0; i < side.size(); ++i) {
            const auto& lane = lanes[side[i]];
            const auto sprite = scene.noteSprite(side[i], milliseconds);
            const float bottom = lane.noteBottom - step * static_cast<float>(i + 1);
            const D2D1_RECT_F place = D2D1::RectF(float(lane.left), bottom - lane.noteHeight, float(lane.right), bottom);
            if (sprite.bitmap)
                target->DrawBitmap(sprite.bitmap, place, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                                   D2D1::RectF(sprite.x, sprite.y, sprite.x + sprite.width, sprite.y + sprite.height));
        }
    }
}
bool writePng(const std::filesystem::path& output,const BYTE* pixels, UINT pitch, UINT width, UINT height, std::string& error) {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    std::error_code ec;
    std::filesystem::create_directories(output.parent_path(), ec);
    auto temporary = output; temporary += L".tmp";
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(temporary.c_str(), GENERIC_WRITE)) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) || FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) || FAILED(frame->SetSize(width, height))) {
        error = "PNG encoder unavailable"; return false;
    }
    // PNG takes 32bppBGRA as is (it would quietly turn 32bppBGR into 24-bit). The target ignores alpha,
    // so every pixel is made opaque.
    auto format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppBGRA) { error = "PNG pixel format unavailable"; return false; }
    std::vector<BYTE> opaque(static_cast<size_t>(width) * height * 4);
    for (UINT y = 0; y < height; ++y) {
        std::copy_n(pixels + static_cast<size_t>(y) * pitch, static_cast<size_t>(width) * 4, opaque.data() + static_cast<size_t>(y) * width * 4);
        for (UINT x = 0; x < width; ++x) opaque[(static_cast<size_t>(y) * width + x) * 4 + 3] = 255;
    }
    if (FAILED(frame->WritePixels(height, width * 4, static_cast<UINT>(opaque.size()), opaque.data())) ||
        FAILED(frame->Commit()) || FAILED(encoder->Commit())) { error = "PNG write failed"; return false; }
    stream.Reset(); frame.Reset(); encoder.Reset();
    if (!MoveFileExW(temporary.c_str(), output.c_str(), MOVEFILE_REPLACE_EXISTING)) { std::filesystem::remove(temporary, ec); error = "PNG replace failed"; return false; }
    return true;
}
} // namespace

bool renderThumbnail(const std::filesystem::path& skin, ScreenType screen, const SkinChoices& choices,
                     const std::filesystem::path& output, std::string& error, unsigned maxWidth, unsigned maxHeight) {
    try {
        auto result = parse(skin, representativeOptions(screen, choices));
        if (!result) {
            error = "skin did not parse";
            for (const auto& diagnostic : result.diagnostics) if (diagnostic.error) { error = diagnostic.message; break; }
            return false;
        }
        const auto& header = result.document->header;
        const double scale = std::min({1.0, double(maxWidth) / std::max(1u, header.width), double(maxHeight) / std::max(1u, header.height)});
        const UINT width = std::max(1u, static_cast<UINT>(header.width * scale)), height = std::max(1u, static_cast<UINT>(header.height * scale));
        bool play = playLaneCount(static_cast<int>(screen)) > 0;
        const ScreenData screenData = sampleScreen();
        PreviewScene scene;
        scene.setStretch(true);
        // A play skin the game could not use (no lanes, say) still has pictures worth showing.
        if (!scene.load(*result.document, result.diagnostics, play, &screenData)) {
            if (!play || !scene.load(*result.document, result.diagnostics, false, &screenData)) { error = "skin scene did not build"; return false; }
            play = false;
        }

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                     D3D11_SDK_VERSION, &device, nullptr, &context))) { error = "no Direct3D device"; return false; }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> texture, staging;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture))) { error = "no render texture"; return false; }
        desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) { error = "no staging texture"; return false; }
        ComPtr<IDXGIDevice> dxgi;
        ComPtr<IDXGISurface> surface;
        ComPtr<ID2D1Device> d2d;
        ComPtr<ID2D1DeviceContext> drawing;
        ComPtr<ID2D1Bitmap1> target;
        if (FAILED(device.As(&dxgi)) || FAILED(texture.As(&surface)) || FAILED(D2D1CreateDevice(dxgi.Get(), nullptr, &d2d)) ||
            FAILED(d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &drawing))) { error = "no Direct2D device"; return false; }
        const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96);
        if (FAILED(drawing->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &target))) { error = "no Direct2D target"; return false; }
        drawing->SetTarget(target.Get());
        drawing->SetDpi(96, 96);
        if (!scene.prepare(drawing.Get())) { error = "skin images did not load"; return false; }

        const SelectFrame select = sampleSelect();
        const PlayFrame playFrame = samplePlay(screen);
        // Late enough that opening animations have settled, before any closing fade.
        const double milliseconds = screen == ScreenType::Select ? 3000 : screen == ScreenType::Decide ? 1500 : play ? 8000 : 4000;
        drawing->BeginDraw();
        drawing->Clear(D2D1::ColorF(0, 0, 0));
        const auto lanes = play ? scene.playLanes(static_cast<int>(width), static_cast<int>(height)) : std::nullopt;
        if (lanes) {
            scene.draw(drawing.Get(), milliseconds, static_cast<int>(width), static_cast<int>(height), &playFrame, DrawPass::BeforeNotes);
            drawStaircase(drawing.Get(), scene, *lanes, milliseconds);
            scene.draw(drawing.Get(), milliseconds, static_cast<int>(width), static_cast<int>(height), &playFrame, DrawPass::AfterNotes);
        } else {
            scene.draw(drawing.Get(), milliseconds, static_cast<int>(width), static_cast<int>(height), play ? &playFrame : nullptr,
                       DrawPass::All, screen == ScreenType::Select ? &select : nullptr, ScreenTimers{});
        }
        if (FAILED(drawing->EndDraw())) { error = "drawing failed"; return false; }
        context->CopyResource(staging.Get(), texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) { error = "readback failed"; return false; }
        const bool written = writePng(output, static_cast<const BYTE*>(mapped.pData), mapped.RowPitch, width, height, error);
        context->Unmap(staging.Get(), 0);
        scene.releaseDevice();
        return written;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

std::wstring thumbnailCacheName(const std::filesystem::path& skin, const SkinChoices& choices) {
    uint64_t hash = 14695981039346656037ull;
    const auto mix = [&](const std::wstring& text) { for (wchar_t c : text) { hash ^= static_cast<uint16_t>(c); hash *= 1099511628211ull; } hash ^= 0xff; hash *= 1099511628211ull; };
    std::error_code ec;
    mix(L"render-2"); // Bumped when the rendered sample changes (2: staircase notes), so old cached pictures are redrawn.
    mix(lower(std::filesystem::absolute(skin, ec).lexically_normal().wstring()));
    mix(std::to_wstring(std::filesystem::last_write_time(skin, ec).time_since_epoch().count()));
    const auto wide = [](const std::string& text) { return std::wstring(text.begin(), text.end()); };
    for (const auto& [name, value] : choices.options) { mix(wide(name)); mix(wide(value)); }
    for (const auto& [name, value] : choices.files) { mix(wide(name)); mix(wide(value)); }
    for (const auto& [name, values] : choices.offsets) { mix(wide(name)); for (int v : values) mix(std::to_wstring(v)); }
    wchar_t text[17]{};
    swprintf_s(text, L"%016llx", static_cast<unsigned long long>(hash));
    return std::wstring(text) + L".png";
}
}
