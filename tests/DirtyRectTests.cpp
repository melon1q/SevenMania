#include "DirtyRect.h"
#include "TextRasterCache.h"
#include <iostream>
#include <stdexcept>
#include <vector>
static void check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
static void drawBody(HDC dc, const std::wstring& text, RECT bounds, HFONT font, COLORREF color) {
    const auto old = SelectObject(dc, font); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), int(text.size()), &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
}
int main() { try {
    const int w = 64, h = 32;
    std::vector<uint32_t> a(w * h, 0xff000000u), b = a;
    check(dirtyrect::empty(dirtyrect::find(a.data(), w, b.data(), w, w, h)), "identical images must be clean");
    b[5 * w + 10] = 1; b[20 * w + 40] = 2;
    RECT r = dirtyrect::find(a.data(), w, b.data(), w, w, h);
    check(r.left == 10 && r.top == 5 && r.right == 41 && r.bottom == 21, "dirty rect bounds wrong");
    check(dirtyrect::bytes(r) == uint64_t(31) * 16 * 4, "dirty bytes wrong");
    b = a; b[0] = 7; b[h * w - 1] = 7;
    r = dirtyrect::find(a.data(), w, b.data(), w, w, h);
    check(r.left == 0 && r.top == 0 && r.right == w && r.bottom == h, "corner changes must span the image");
    // Different pitches (region inside a larger backbuffer).
    std::vector<uint32_t> wide(128 * h, 0xff000000u);
    std::vector<uint32_t> region(w * h, 0xff000000u);
    wide[3 * 128 + 63] = 9;
    r = dirtyrect::find(region.data(), w, wide.data(), 128, w, h);
    check(r.left == 63 && r.right == 64 && r.top == 3 && r.bottom == 4, "pitch handling wrong");

    b = a; b[7 * w + 30] = 3;
    r = dirtyrect::rows(a.data(), w, b.data(), w, w, h);
    check(r.left == 0 && r.right == w && r.top == 7 && r.bottom == 8, "row band must stay full width");
    check(dirtyrect::empty(dirtyrect::rows(a.data(), w, a.data(), w, w, h)), "identical rows must be clean");
    check(!dirtyrect::same(a.data(), w, b.data(), w, w, h) && dirtyrect::same(a.data(), w, a.data(), w, w, h), "same() wrong");
    check(dirtyrect::same(region.data(), w, wide.data() + 64, 128, 64, h), "same() pitch handling wrong");
    // Text cache: prewarmed text is a hit, and does not count as a gameplay miss.
    HFONT font = CreateFontW(-40, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
    TextRasterCache cache;
    const auto missesBefore = gTextRasterCacheMisses;
    check(cache.prewarm(L"GREAT 0123456789", font, RGB(255, 255, 255), drawBody), "prewarm failed");
    check(gTextRasterCacheMisses == missesBefore, "prewarm counted as miss");
    check(cache.contains(L"GREAT 0123456789", font, RGB(255, 255, 255)), "prewarmed text missing");
    // Grow-only surfaces: a narrower string after a wide one must rasterize identically
    // to a fresh cache that never saw the wide string.
    std::vector<uint32_t> first, second;
    const auto capture = [](std::vector<uint32_t>& out) {
        return [&out](const uint32_t* p, int sw, int sh, int, int, int, int, uint64_t) { out.assign(p, p + size_t(sw) * sh); return true; };
    };
    check(cache.draw(nullptr, 1, 1, L"GREAT 7", RECT{0, 0, 1, 1}, font, RGB(255, 255, 255), 1.0, drawBody, false, capture(first)), "draw after prewarm failed");
    TextRasterCache fresh;
    check(fresh.draw(nullptr, 1, 1, L"GREAT 7", RECT{0, 0, 1, 1}, font, RGB(255, 255, 255), 1.0, drawBody, false, capture(second)), "fresh draw failed");
    check(!first.empty() && first == second, "grow-only surface changed sprite pixels");
    check(gTextRasterCacheMisses == missesBefore + 2, "real misses not counted");
    DeleteObject(font);
    std::cout << "dirty rect / text prewarm tests passed\n"; return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
