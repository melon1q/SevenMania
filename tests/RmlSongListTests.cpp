#include "RmlSongListRenderer.h"
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Surface {
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap{};
    HGDIOBJ previous{};
    std::uint32_t* pixels{};
    int width, height;
    Surface(int w, int h) : width(w), height(h) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w; info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
        require(dc && bitmap && pixels, "unable to create test surface");
        previous = SelectObject(dc, bitmap);
    }
    ~Surface() { SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc); }
    void clear(std::uint32_t background) { GdiFlush(); std::fill_n(pixels, static_cast<size_t>(width) * height, background); }
    std::vector<std::uint32_t> snapshot() const { GdiFlush(); return {pixels, pixels + static_cast<size_t>(width) * height}; }
};

void saveImage(Surface& surface, const std::filesystem::path& output, const wchar_t* mime = L"image/png") {
    UINT count{}, size{};
    Gdiplus::GetImageEncodersSize(&count, &size);
    std::vector<unsigned char> storage(size);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
    Gdiplus::GetImageEncoders(count, size, encoders);
    for (UINT index = 0; index < count; ++index) {
        if (std::wstring(encoders[index].MimeType) == mime) {
            Gdiplus::Bitmap image(surface.bitmap, nullptr);
            require(image.Save(output.c_str(), &encoders[index].Clsid) == Gdiplus::Ok, "unable to save rendered fixture");
            return;
        }
    }
    throw std::runtime_error("PNG encoder unavailable");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    ULONG_PTR gdiplus{};
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&gdiplus, &input, nullptr);
    int result = 0;
    try {
        std::vector<bms::ui::SongListVisualRow> rows;
        for (int index = 0; index < 8; ++index) {
            bms::ui::SongListVisualRow row;
            row.top = index * 58 - 15; row.height = 58;
            row.title = index == 0 ? L"한글 제목 <A & B> [ANOTHER]" : index == 1 ? L"日本語のタイトル [HYPER]" :
                L"A long song title that should fit the available viewport without overflowing";
            row.artist = L"Composer / 작곡가 · Artist"; row.level = std::to_wstring(index + 4);
            row.variant = L"2 / 4"; row.selected = index == 2; row.alternate = index % 2 != 0;
            row.lamp = index % 3; row.blocked = index == 5;
            row.difficultyColor = RGB(96 + index * 18, 190, 235 - index * 12);
            rows.push_back(std::move(row));
        }
        if (argc == 2) {
            const std::filesystem::path output(argv[1]);
            std::filesystem::create_directories(output);
            Surface banner(320, 100); banner.clear(0xffe04020);
            saveImage(banner, output / L"배너 & PNG.png");
            saveImage(banner, output / L"banner-jpeg.jpg", L"image/jpeg");
            rows[2].banner = (output / L"배너 & PNG.png").generic_wstring();
            rows[3].banner = (output / L"banner-jpeg.jpg").generic_wstring();
            rows[4].banner = (output / L"missing-banner.png").generic_wstring();
        }
        {
            bms::ui::RmlSongListRenderer renderer;
            Surface surface(640, 500);
            constexpr std::uint32_t background = 0xff305020;
            surface.clear(background);
            require(renderer.paint(surface.dc, surface.width, surface.height, rows), "RmlUi did not render the song list");
            const auto first = surface.snapshot();
            require(first.back() == background, "empty viewport area covered the underlying background");
            if (argc == 2) {
                for (int row : {2, 3}) {
                    size_t redPixels = 0;
                    for (int y = rows[row].top + 10; y < rows[row].top + 45; ++y)
                        for (int x = 510; x < 620; ++x) {
                            const auto pixel = first[static_cast<size_t>(y) * 640 + x];
                            if (((pixel >> 16) & 255) > 180 && ((pixel >> 8) & 255) < 100 && (pixel & 255) < 80) ++redPixels;
                        }
                    require(redPixels > 1500, "PNG/JPEG banner was not rendered inside the right side of its row");
                }
                require((first[static_cast<size_t>(rows[4].top + 20) * 640 + 600] & 0xFFFFFFu) != 0xFFFFFFu,
                    "a missing banner painted an opaque white placeholder");
            }
            require(static_cast<size_t>(std::count_if(first.begin(), first.end(), [](auto pixel) { return (pixel & 0xFFFFFF) != 0; })) > first.size() / 2,
                "rendered song list is blank");
            if (argc == 2) {
                std::filesystem::create_directories(argv[1]);
                saveImage(surface, std::filesystem::path(argv[1]) / L"song-list-rmlui.png");
            }
            surface.clear(background);
            require(renderer.paint(surface.dc, 640, 500, rows) && surface.snapshot() == first, "cached bitmap changed or was not repainted");
            require(renderer.needsAnimation(), "long selected song title did not enable scrolling");
            surface.clear(background);
            require(renderer.paint(surface.dc, 640, 500, rows, 0.8) && surface.snapshot() == first,
                "song title moved during the initial hold");
            surface.clear(background);
            require(renderer.paint(surface.dc, 640, 500, rows, 3.0) && surface.snapshot() != first,
                "overflowing song title did not scroll");
            surface.clear(background);
            require(renderer.paint(surface.dc, 640, 500, rows, 0) && surface.snapshot() == first,
                "song title did not return to its original position");
            surface.clear(0xff203060);
            require(renderer.paint(surface.dc, 640, 500, rows) && surface.snapshot().back() == 0xff203060,
                "cached overlay did not preserve a changed background");
            rows[2].selected = false; rows[3].selected = true;
            surface.clear(background);
            require(renderer.paint(surface.dc, 640, 500, rows) && surface.snapshot() != first, "selection change did not invalidate the bitmap");
            Surface narrow(320, 220), wide(900, 680);
            require(renderer.paint(narrow.dc, 320, 220, rows), "narrow viewport render failed");
            require(renderer.paint(wide.dc, 900, 680, rows), "wide viewport render failed");
            wide.clear(background);
            require(renderer.paint(wide.dc, 900, 680, {}), "removing visible banners failed");
        }
        {
            bms::ui::RmlSongListRenderer reopened;
            Surface empty(480, 240);
            empty.clear(0xff503020);
            require(reopened.paint(empty.dc, 480, 240, {}), "empty list or repeated RmlUi lifecycle failed");
            require(empty.snapshot().back() == 0xff503020, "empty list painted an opaque viewport");
        }
        {
            const auto output=argc>1?std::filesystem::path(argv[1]):std::filesystem::path(L"Validation/RmlUi");std::filesystem::create_directories(output);
            bms::ui::RmlSongListRenderer types;Surface surface(520,100);bms::ui::SongListVisualRow row;row.height=58;row.title=L"Folder navigation";row.level=L"DIR";
            std::vector<std::uint32_t> ordinary,parent;
            for(auto kind:{bms::ui::SongEntryKind::Song,bms::ui::SongEntryKind::Folder,bms::ui::SongEntryKind::ParentFolder})for(bool selected:{false,true}){
                row.kind=kind;row.selected=selected;surface.clear(0xff202020);require(types.paint(surface.dc,520,100,{row}),"entry state rendering failed");auto pixels=surface.snapshot();
                const int left=selected?4:20;
                require((pixels[30*520+left-1]&0xffffffu)==0x202020u,"song bar extends past its specified edge");
                require((pixels[left]&0xffffffu)!=0x202020u,"song bar corner is still rounded");
                const unsigned expected=kind==bms::ui::SongEntryKind::ParentFolder?(selected?0x527995u:0x456780u):(selected?0xdce2e8u:0xbcc2c9u);
                for(int y=2;y<56;++y)require((pixels[y*520+500]&0xffffffu)==expected,"song bar background is not the expected flat color");
                const auto name=std::to_wstring(int(kind))+(selected?L"-selected.png":L"-normal.png");saveImage(surface,output/(L"entry-"+name));
                if(kind==bms::ui::SongEntryKind::Song&&!selected)ordinary=pixels;
                if(kind==bms::ui::SongEntryKind::ParentFolder&&!selected){parent=pixels;require(parent!=ordinary,"parent folder used ordinary song styling");}
                if(kind==bms::ui::SongEntryKind::ParentFolder&&selected)require(pixels!=parent&&pixels!=ordinary,"parent selection lost its distinct styling");
            }
        }
        std::cout << "RmlUi song-list rendering, caching, selection, resizing and lifetime checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; result = 1;
    }
    Gdiplus::GdiplusShutdown(gdiplus);
    return result;
}
