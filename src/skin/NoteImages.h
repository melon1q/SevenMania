#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <d2d1.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include "../SkinNoteGroupFiles.h"

namespace sevenmania::skin {
// HeldStart/HeldBody/HeldEnd are the long-note images shown while the key is held; they fall back to
// the normal long-note images when a skin has none.
enum class NotePart { Tap, LongStart, LongBody, LongEnd, Mine, HeldStart, HeldBody, HeldEnd };

class NoteImages {
public:
    struct Image {
        int width{}, height{};
        Microsoft::WRL::ComPtr<ID2D1Bitmap> gpuBitmap;
        HDC dc{};
        HBITMAP bitmap{};
        HGDIOBJ previous{};
        uint32_t* pixels{};
        bool opaque = true;

        Image() = default;
        Image(const Image&) = delete;
        Image& operator=(const Image&) = delete;
        ~Image() {
            if (previous) SelectObject(dc, previous);
            if (bitmap) DeleteObject(bitmap);
            if (dc) DeleteDC(dc);
        }

        void drawRaster(uint32_t* target, int stride, int rows, int left, double top,
            int right, double bottom, const RECT& clip, float opacity) const {
            const int x0 = std::max({left, 0, static_cast<int>(clip.left)});
            const int x1 = std::min({right, stride, static_cast<int>(clip.right)});
            const double y0 = std::max({top, 0.0, static_cast<double>(clip.top)});
            const double y1 = std::min({bottom, static_cast<double>(rows), static_cast<double>(clip.bottom)});
            if (x0 >= x1 || y0 >= y1) return;

            // Advance in source pixels without dividing per output pixel.
            constexpr uint64_t unit = uint64_t{1} << 24;
            const uint64_t step = width * unit / (right - left);
            const uint64_t first = (x0 - left) * step + step / 2;
            const double scaleY = height / (bottom - top);
            for (int y = static_cast<int>(std::floor(y0)); y < static_cast<int>(std::ceil(y1)); ++y) {
                const double start = std::max(y0, static_cast<double>(y));
                const double end = std::min(y1, y + 1.0);
                const auto sy = std::clamp(static_cast<int>(((start + end) * 0.5 - top) * scaleY), 0, height - 1);
                const auto* source = pixels + static_cast<size_t>(sy) * width;
                auto* output = target + static_cast<size_t>(y) * stride + x0;
                const auto alpha = static_cast<unsigned>(std::lround((end - start) * opacity * 255));
                uint64_t sx = first;
                if (opaque && alpha == 255) {
                    for (int x = x0; x < x1; ++x, sx += step) *output++ = source[sx / unit];
                } else {
                    for (int x = x0; x < x1; ++x, sx += step, ++output)
                        *output = blend(source[sx / unit], *output, alpha);
                }
            }
        }

    private:
        static uint32_t blend(uint32_t source, uint32_t target, unsigned opacity) {
            const unsigned alpha = source >> 24;
            if (alpha == 0) return target;
            if (alpha == 255 && opacity == 255) return source;
            const unsigned inverse = 255 - (alpha * opacity + 127) / 255;
            const auto channel = [&](int shift) {
                return (((source >> shift) & 255) * opacity + ((target >> shift) & 255) * inverse + 127) / 255;
            };
            return 0xff000000u | (channel(16) << 16) | (channel(8) << 8) | channel(0);
        }
    };

    void clear() {
        images_ = {};
        bodyExtra_ = {};
        bodyFps_ = {12, 12};
        gpuTarget_.Reset();
    }

    // Returns human-readable warnings (ignored sheet, bad manifest values) for the host to log.
    // "sheetName" picks Notes/<sheetName>-NoteGroup.png; empty means Notes/NoteGroup.png, or else the
    // first <name>-NoteGroup.png in name order.
    std::vector<std::wstring> load(const std::filesystem::path& skinFile, const std::wstring& sheetName = {},
                                   bool bundledSkin = false) {
        std::vector<std::wstring> warnings;
        constexpr std::array names{
            std::array<const wchar_t*, kPartCount>{L"normal.png", L"long-start.png", L"long-body.png", L"long-end.png", L"mine.png",
                L"held-long-start.png", L"held-long-body.png", L"held-long-end.png"},
            std::array<const wchar_t*, kPartCount>{L"scratch.png", L"scratch-long-start.png", L"scratch-long-body.png", L"scratch-long-end.png", nullptr,
                L"scratch-held-long-start.png", L"scratch-held-long-body.png", L"scratch-held-long-end.png"}
        };
        std::error_code ec;
        const auto notes = std::filesystem::absolute(skinFile, ec).parent_path() / L"Notes";
        decltype(images_) next;
        decltype(bodyExtra_) nextExtra;
        for (size_t row = 0; row < names.size(); ++row) {
            for (size_t part = 0; part < names[row].size(); ++part) {
                if (const auto* name = names[row][part]) next[row][part] = decode(notes / name);
            }
        }
        // Without Notes/NoteGroup.ini the sheet is the 324x360 ModernChic layout. The ini can
        // declare any other layout; see docs/SkinAssetFolders.md.
        auto sheetPath = notes / L"NoteGroup.png";
        // An update can leave the former bundled sheet next to its named replacement.
        if (bundledSkin && std::filesystem::is_regular_file(notes / L"Default-NoteGroup.png", ec))
            sheetPath = notes / L"Default-NoteGroup.png";
        const auto namedSheets = skinNoteGroupFiles(notes.parent_path());
        bool chosen = false;
        if (!sheetName.empty()) {
            const auto found = std::find_if(namedSheets.begin(), namedSheets.end(),
                [&](const auto& sheet) { return _wcsicmp(sheet.first.c_str(), sheetName.c_str()) == 0; });
            if (found != namedSheets.end()) { sheetPath = found->second; chosen = true; }
            else warnings.push_back(L"Notes/" + sheetName + L"-NoteGroup.png was selected but does not exist; using the automatic sheet.");
        }
        if (!chosen && !std::filesystem::is_regular_file(sheetPath, ec) && !namedSheets.empty())
            sheetPath = namedSheets.front().second;
        const auto sheetLabel = L"Notes/" + sheetPath.filename().wstring();
        auto iniPath = sheetPath;
        iniPath.replace_extension(L".ini");
        const auto layout = readSheetLayout(iniPath);
        const bool hasSheet = std::filesystem::is_regular_file(sheetPath, ec);
        std::unique_ptr<Gdiplus::Bitmap> group;
        bool validGroup = false;
        if (hasSheet) {
            group = std::make_unique<Gdiplus::Bitmap>(sheetPath.c_str(), FALSE);
            const int width = static_cast<int>(group->GetWidth()), height = static_cast<int>(group->GetHeight());
            if (group->GetLastStatus() != Gdiplus::Ok) {
                warnings.push_back(sheetLabel + L" could not be decoded and was ignored.");
            } else if (layout.width > 0 && (width != layout.width || height != layout.height)) {
                warnings.push_back(sheetLabel + L" is " + std::to_wstring(width) + L"x" + std::to_wstring(height) +
                    L" but the layout expects " + std::to_wstring(layout.width) + L"x" + std::to_wstring(layout.height) +
                    (layout.declared ? L" (" + iniPath.filename().wstring() + L")." :
                        L". Add " + iniPath.filename().wstring() + L" next to the sheet to declare a different layout."));
            } else {
                validGroup = true;
            }
        }
        for (size_t column = 0; column < layout.columns.size(); ++column) {
            const auto& slot = layout.columns[column];
            for (size_t part = 0; part < layout.partY.size(); ++part) {
                if (part == static_cast<size_t>(NotePart::Mine)) continue;   // the mine has no sheet cell
                const auto name = std::wstring(slot.prefix) + kSuffixes[part] + L"-" + slot.color + L".png";
                auto& image = next[column + 2][part];
                image = decode(notes / name);
                if (image || !validGroup || slot.width <= 0) continue;
                const int sheetWidth = static_cast<int>(group->GetWidth()), sheetHeight = static_cast<int>(group->GetHeight());
                const int y = layout.partY[part];
                if (y < 0 && part >= static_cast<size_t>(NotePart::HeldStart)) continue;   // held cell not declared
                if (slot.x < 0 || y < 0 || slot.x + slot.width > sheetWidth || y + layout.cellHeight > sheetHeight) {
                    warnings.push_back(L"NoteGroup cell " + std::wstring(slot.prefix) + kSuffixes[part] + L"-" + slot.color +
                        L" lies outside the sheet and was skipped.");
                    continue;
                }
                image = decodePixels(*group, Gdiplus::Rect(slot.x, y, slot.width, layout.cellHeight));
                // Long-note body animation: further frames follow the first one downwards.
                const bool heldBody = part == static_cast<size_t>(NotePart::HeldBody);
                if ((part == static_cast<size_t>(NotePart::LongBody) || heldBody) && image) {
                    for (int frame = 1; frame < (heldBody ? layout.heldBodyFrames : layout.bodyFrames); ++frame) {
                        const int frameY = y + frame * layout.cellHeight;
                        if (frameY + layout.cellHeight > sheetHeight) {
                            warnings.push_back(L"NoteGroup " + std::wstring(heldBody ? L"held " : L"") + L"body frame " + std::to_wstring(frame + 1) + L" of " +
                                std::wstring(slot.prefix) + L"-" + slot.color + L" lies outside the sheet; only " +
                                std::to_wstring(frame) + L" frame(s) are used.");
                            break;
                        }
                        if (auto extra = decodePixels(*group, Gdiplus::Rect(slot.x, frameY, slot.width, layout.cellHeight)))
                            nextExtra[heldBody][column + 2].push_back(std::move(extra));
                        else break;
                    }
                }
            }
        }
        bodyFps_ = {static_cast<double>(layout.bodyFps), static_cast<double>(layout.heldBodyFps)};
        bodyExtra_ = std::move(nextExtra);
        images_ = std::move(next);
        gpuTarget_.Reset();
        return warnings;
    }

    void prepareGpu(ID2D1RenderTarget* target) {
        if (target == gpuTarget_.Get() || !target || empty()) return;
        releaseGpu();
        gpuTarget_ = target;
        const auto properties = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        for (const auto& group : images_) {
            for (const auto& image : group) {
                if (!image) continue;
                target->CreateBitmap(D2D1::SizeU(image->width, image->height), image->pixels, image->width * 4,
                    properties, &image->gpuBitmap);
            }
        }
        for (const auto& kind : bodyExtra_)
            for (const auto& frames : kind)
                for (const auto& image : frames)
                    target->CreateBitmap(D2D1::SizeU(image->width, image->height), image->pixels, image->width * 4,
                        properties, &image->gpuBitmap);
    }

    void releaseGpu() {
        if (!gpuTarget_) return;
        for (const auto& group : images_)
            for (const auto& image : group)
                if (image) image->gpuBitmap.Reset();
        for (const auto& kind : bodyExtra_)
            for (const auto& frames : kind)
                for (const auto& image : frames) image->gpuBitmap.Reset();
        gpuTarget_.Reset();
    }

    double bodyFps(bool held = false) const { return bodyFps_[held ? 1 : 0]; }

    bool empty() const {
        for (const auto& group : images_)
            for (const auto& image : group)
                if (image) return false;
        return true;
    }

    // "tick" selects the animation frame of a long-note body (frame = tick modulo frame count).
    const Image* find(NotePart part, bool scratch, int color = -1, long long tick = 0) const {
        if (part >= NotePart::HeldStart) {
            if (const auto* held = findPart(part, scratch, color, tick)) return held;
            // Fall back to the matching normal part (HeldStart -> LongStart, HeldBody -> LongBody, HeldEnd -> LongEnd).
            part = static_cast<NotePart>(static_cast<int>(part) - static_cast<int>(NotePart::HeldStart) + 1);
        }
        return findPart(part, scratch, color, tick);
    }

private:
    const Image* findPart(NotePart part, bool scratch, int color, long long tick) const {
        const auto index = static_cast<size_t>(part);
        size_t row = 0;
        if (color >= 0 && images_[scratch ? (color == 3 ? 2 : 3) : (color == 2 ? 5 : 4)][index])
            row = scratch ? (color == 3 ? 2 : 3) : (color == 2 ? 5 : 4);
        else if (scratch && images_[1][index]) row = 1;
        if (part == NotePart::LongBody || part == NotePart::HeldBody) {
            const auto& extra = bodyExtra_[part == NotePart::HeldBody][row];
            if (!extra.empty()) {
                const auto count = static_cast<long long>(extra.size()) + 1;
                const auto frame = ((tick % count) + count) % count;
                if (frame > 0) return extra[static_cast<size_t>(frame - 1)].get();
            }
        }
        return images_[row][index].get();
    }

    static constexpr size_t kPartCount = 8;
    struct SheetColumn { const wchar_t* prefix; const wchar_t* color; int x, width; };
    struct SheetLayout {
        int width = 324, height = 360, cellHeight = 36;   // width 0 = do not check the sheet size
        // Tap, LongStart, LongBody, LongEnd, (mine: not in the sheet), HeldStart, HeldBody, HeldEnd
        // ModernChic: the idle body is the empty y108 cell; the held body alternates the glowing y144 / y180
        // cells. The sheet's lower blocks (y216 and below) are not used. Held head/tail use the normal ones.
        std::array<int, kPartCount> partY{0, 72, 108, 36, -1, -1, 144, -1};
        int heldBodyFrames = 2;
        int heldBodyFps = 12;
        int bodyFrames = 1;
        int bodyFps = 12;
        std::array<SheetColumn, 4> columns{{{L"scratch", L"green", 0, 108}, {L"scratch", L"red", 108, 108},
            {L"normal", L"white", 216, 60}, {L"normal", L"blue", 276, 48}}};
        bool declared = false;
    };
    static constexpr std::array<const wchar_t*, kPartCount> kSuffixes{L"", L"-long-start", L"-long-body", L"-long-end", L"",
        L"-held-long-start", L"-held-long-body", L"-held-long-end"};

    static SheetLayout readSheetLayout(const std::filesystem::path& ini) {
        SheetLayout layout;
        std::error_code ec;
        if (!std::filesystem::is_regular_file(ini, ec)) return layout;
        layout.declared = true;
        const auto file = ini.c_str();
        const auto read = [&](const wchar_t* section, const wchar_t* key, int fallback) {
            return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, file));
        };
        layout.width = read(L"Sheet", L"Width", 0);
        layout.height = read(L"Sheet", L"Height", 0);
        layout.cellHeight = std::max(1, read(L"Sheet", L"CellHeight", layout.cellHeight));
        constexpr std::array<const wchar_t*, kPartCount> partKeys{L"Tap", L"LongStart", L"LongBody", L"LongEnd", nullptr,
            L"HeldLongStart", L"HeldLongBody", L"HeldLongEnd"};
        for (size_t part = 0; part < partKeys.size(); ++part) {
            if (!partKeys[part]) continue;
            // With a manifest the held cells are off unless declared (the defaults belong to the ModernChic sheet).
            const int fallback = part >= static_cast<size_t>(NotePart::HeldStart) ? -1 : layout.partY[part];
            layout.partY[part] = read(L"Parts", partKeys[part], fallback);
        }
        // With a manifest the body is a single frame unless declared.
        layout.bodyFrames = std::clamp(read(L"Parts", L"LongBodyFrames", 1), 1, 64);
        layout.bodyFps = std::clamp(read(L"Parts", L"LongBodyFps", layout.bodyFps), 1, 120);
        layout.heldBodyFrames = std::clamp(read(L"Parts", L"HeldLongBodyFrames", 1), 1, 64);
        layout.heldBodyFps = std::clamp(read(L"Parts", L"HeldLongBodyFps", layout.heldBodyFps), 1, 120);
        for (auto& column : layout.columns) {
            const auto section = std::wstring(column.prefix) + L"-" + column.color;
            // With a manifest, a column that is not declared is off (no ModernChic defaults).
            column.x = read(section.c_str(), L"X", 0);
            column.width = read(section.c_str(), L"Width", 0);
        }
        return layout;
    }

    static std::unique_ptr<Image> decode(const std::filesystem::path& path) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) return {};
        Gdiplus::Bitmap file(path.c_str(), FALSE);
        constexpr UINT maxSide = 1024;
        const UINT width = file.GetWidth(), height = file.GetHeight();
        if (file.GetLastStatus() != Gdiplus::Ok || !width || !height || width > maxSide || height > maxSide) return {};

        return decodePixels(file, Gdiplus::Rect(0, 0, static_cast<int>(width), static_cast<int>(height)));
    }

    static std::unique_ptr<Image> decodePixels(Gdiplus::Bitmap& file, const Gdiplus::Rect& bounds) {
        auto image = std::make_unique<Image>();
        image->width = bounds.Width;
        image->height = bounds.Height;
        image->dc = CreateCompatibleDC(nullptr);
        if (!image->dc) return {};

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = image->width;
        info.bmiHeader.biHeight = -image->height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* pixels{};
        image->bitmap = CreateDIBSection(image->dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!image->bitmap) return {};
        image->pixels = static_cast<uint32_t*>(pixels);
        image->previous = SelectObject(image->dc, image->bitmap);
        if (!image->previous) return {};

        Gdiplus::BitmapData data{};
        if (file.LockBits(&bounds, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &data) != Gdiplus::Ok) return {};
        for (int y = 0; y < image->height; ++y) {
            const auto* row = reinterpret_cast<const uint32_t*>(static_cast<const BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride);
            std::copy_n(row, image->width, image->pixels + static_cast<size_t>(y) * image->width);
        }
        file.UnlockBits(&data);
        image->opaque = std::all_of(image->pixels, image->pixels + static_cast<size_t>(image->width) * image->height,
            [](uint32_t pixel) { return (pixel >> 24) == 255; });
        return image;
    }

    std::array<std::array<std::unique_ptr<Image>, kPartCount>, 6> images_{};
    std::array<std::array<std::vector<std::unique_ptr<Image>>, 6>, 2> bodyExtra_{};   // [held][row]: body frames 2..n
    std::array<double, 2> bodyFps_{12, 12};
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> gpuTarget_;
};
} // namespace sevenmania::skin
