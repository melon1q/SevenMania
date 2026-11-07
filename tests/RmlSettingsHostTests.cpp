#include "../src/RmlSettingsHost.h"
#include "../src/RmlSongListRenderer.h"
#include <objidl.h>
#include <gdiplus.h>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <map>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Surface {
    HDC dc = CreateCompatibleDC(nullptr); HBITMAP bitmap{}; HGDIOBJ previous{};
    Surface() {
        BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = 900; info.bmiHeader.biHeight = -700;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        void* pixels{}; bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        require(bitmap && pixels, "test surface failed"); previous = SelectObject(dc, bitmap);
    }
    ~Surface() { SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc); }
    void paint(bms::ui::RmlSettingsHost& host) { SendMessageW(host.window(), WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), 0); GdiFlush(); }
};
void key(HWND hwnd, unsigned key) { SendMessageW(hwnd, WM_KEYDOWN, key, 0); SendMessageW(hwnd, WM_KEYUP, key, 0); }
void click(bms::ui::RmlSettingsHost& host, const std::string& id) {
    RECT r = host.bounds(id); require(r.right > r.left && r.bottom > r.top, "control bounds missing");
    const LPARAM point = MAKELPARAM((r.left+r.right)/2, (r.top+r.bottom)/2);
    SendMessageW(host.window(), WM_MOUSEMOVE, 0, point);
    SendMessageW(host.window(), WM_LBUTTONDOWN, MK_LBUTTON, point);
    SendMessageW(host.window(), WM_LBUTTONUP, 0, point);
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    Gdiplus::GdiplusStartupInput input; ULONG_PTR token{}; Gdiplus::GdiplusStartup(&token, &input, nullptr);
    int code = 0;
    try {
        Surface surface;
        bms::ui::RmlSongListRenderer list;
        bms::ui::SongListVisualRow row; row.title=L"Simultaneous song list"; row.artist=L"Artist"; row.level=L"7"; row.height=58;
        const bool listEnabled = list.enabled();
        require(!listEnabled || list.paint(surface.dc, 400, 200, {row}), "initial list render failed");
        bms::ui::RmlSettingsHost host;
        unsigned events = 0;
        std::map<std::string,std::string> eventValues;
        require(host.open(nullptr, argv[1], [&](const auto& id, const auto& value) { ++events; eventValues[id] = value; }, false), "settings host open failed");
        bool rejectedThread = false;
        bool rejectedListThread = !listEnabled;
        std::thread worker([&] {
            try { host.focus("probe-check"); } catch (const std::exception&) { rejectedThread = true; }
            if (listEnabled) rejectedListThread = !list.paint(surface.dc,400,200,{row});
        });
        worker.join(); require(rejectedThread && rejectedListThread, "foreign-thread UI access was not rejected");
        surface.paint(host);
        click(host, "probe-check"); surface.paint(host);
        require(host.value("probe-check") == "1", "mouse click did not toggle checkbox");
        require(eventValues["probe-check"] == "1", "checkbox event value was lost");
        require(host.focus("probe-check"), "checkbox focus failed"); key(host.window(), VK_TAB);
        require(host.focused() == "probe-range", "Tab focus traversal failed");
        BYTE keyboard[256]{}; GetKeyboardState(keyboard);
        BYTE shifted[256]; std::memcpy(shifted, keyboard, 256); shifted[VK_SHIFT] |= 0x80; SetKeyboardState(shifted);
        key(host.window(), VK_TAB); SetKeyboardState(keyboard);
        require(host.focused() == "probe-check", "Shift+Tab focus traversal failed");
        require(host.focus("probe-range"), "slider focus failed");
        const int rangeBefore = std::stoi(host.value("probe-range")); key(host.window(), VK_RIGHT); surface.paint(host);
        require(std::stoi(host.value("probe-range")) > rangeBefore, "slider did not accept keyboard input");
        RECT range = host.bounds("probe-range");
        const RECT bar = host.bounds("probe-range/sliderbar");
        const int thumb = (bar.left+bar.right)/2, dragY = (bar.top+bar.bottom)/2;
        SendMessageW(host.window(), WM_MOUSEMOVE, 0, MAKELPARAM(thumb,dragY));
        SendMessageW(host.window(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(thumb,dragY));
        for (int x=thumb; x<range.right-10; x+=8) {
            SendMessageW(host.window(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x,dragY)); surface.paint(host);
        }
        SendMessageW(host.window(), WM_LBUTTONUP, 0, MAKELPARAM(range.right-10,(range.top+range.bottom)/2));
        require(std::stoi(host.value("probe-range")) > 50, "mouse drag did not move the slider");
        require(host.focus("probe-select"), "select focus failed"); key(host.window(), VK_DOWN); key(host.window(), VK_RETURN); surface.paint(host);
        require(host.value("probe-select") == "144", "select did not accept keyboard selection");
        require(eventValues["probe-select"] == "144", "selected option did not reach the value callback");
        require(host.setOptions("probe-select",{{"10","New first"},{"20","New last"}}),"replace options failed");
        require(host.setOptions("probe-select",{{"10","New first"},{"20","New last"}}),"repeat options failed");
        host.setValue("probe-select","20");require(host.focus("probe-select"),"replaced select focus failed");
        key(host.window(),VK_UP);key(host.window(),VK_RETURN);surface.paint(host);
        require(host.value("probe-select")=="10","replacing select options retained old/duplicate entries");
        require(host.focus("probe-text"), "text focus failed");
        SendMessageW(host.window(), WM_CHAR, L'한', 0); SendMessageW(host.window(), WM_CHAR, 0xd83d, 0); SendMessageW(host.window(), WM_CHAR, 0xde03, 0);
        require(host.value("probe-text") == "한😃", "Unicode text or surrogate input was lost");
        key(host.window(), VK_TAB); surface.paint(host);
        require(host.focus("probe-button"), "button focus failed");
        const auto beforeEnter = events; key(host.window(), VK_RETURN); surface.paint(host);
        require(events > beforeEnter, "Enter did not activate the focused button");
        click(host, "probe-button"); surface.paint(host); require(events > 3, "form events did not reach the callback");
        POINT wheel{40,100}; ClientToScreen(host.window(), &wheel);
        SendMessageW(host.window(), WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(wheel.x, wheel.y)); surface.paint(host);
        SendMessageW(host.window(), WM_CANCELMODE, 0, 0);
        require(GetCapture() != host.window(), "mouse capture remained stuck");
        SendMessageW(host.window(), WM_KILLFOCUS, 0, 0);
        SendMessageW(host.window(), WM_SIZE, SIZE_RESTORED, MAKELPARAM(500,400)); surface.paint(host);
        RECT suggested{0,0,700,550}; SendMessageW(host.window(), WM_DPICHANGED, MAKEWPARAM(144,144), reinterpret_cast<LPARAM>(&suggested)); surface.paint(host);
        std::filesystem::create_directories(argv[2]);
        {
            const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
            Gdiplus::Bitmap image(surface.bitmap,nullptr); require(image.Save((std::filesystem::path(argv[2])/L"settings-input-probe.png").c_str(),&png)==Gdiplus::Ok,"probe capture failed");
        }
        const auto metrics = host.metrics();
        std::cout << "Input/drag frames=" << metrics.frames << " total render+readback ms=" << metrics.renderMilliseconds << '\n';
        list.reset(); surface.paint(host); // Closing the list must not shut down the settings runtime.
        require(host.value("probe-check") == "1", "list reset destroyed settings document");
        require(!listEnabled || list.paint(surface.dc,400,200,{row}),"list could not rejoin the existing runtime");
        host.close(); require(!listEnabled || list.paint(surface.dc,400,200,{row}),"closing settings destroyed list context");
        for (int i=0;i<8;++i) { require(host.open(nullptr,argv[1],{},false),"settings reopen failed"); surface.paint(host); host.close(); }
        require(!host.open(nullptr,L"missing-settings-document.rml",{},false),"missing document did not fail safely");
        require(!listEnabled || list.paint(surface.dc,400,200,{row}),"failed settings initialization broke the list");
        list.reset(); require(host.open(nullptr,argv[1],{},false),"settings-only runtime initialization failed");
        HWND closed = host.window(); key(closed, VK_ESCAPE);
        MSG message{}; while (PeekMessageW(&message, closed, WM_CLOSE, WM_CLOSE, PM_REMOVE)) DispatchMessageW(&message);
        require(!host.window(), "Escape did not close the input host"); host.close();
        std::cout << "Rml settings host input, Unicode, resize/DPI, lifetime and failure recovery passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; code=1; }
    Gdiplus::GdiplusShutdown(token); return code;
}
