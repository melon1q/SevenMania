#include "../src/RmlSettingsHost.h"
#include "../src/RmlSongListRenderer.h"
#include <objidl.h>
#include <gdiplus.h>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <map>
#include <algorithm>
#include <filesystem>
#include <vector>

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
        {
            // Skin select screen: the document loads without RML/RCSS errors, shows a large preview picture,
            // and screen rows and arrows report their ids; unsupported screens have no id to click.
            const auto document = std::filesystem::path(argv[1]).parent_path() / L"skinselect.rml";
            std::vector<std::string> clicked;
            require(host.open(nullptr, document, [&](const auto& id, const auto&) { clicked.push_back(id); }, false), "skin select document did not load");
            const auto picture = std::filesystem::path(argv[2]) / L"skin-preview.png";
            std::filesystem::create_directories(picture.parent_path());
            {
                Gdiplus::Bitmap bitmap(320, 180, PixelFormat32bppARGB);
                { Gdiplus::Graphics graphics(&bitmap); graphics.Clear(Gdiplus::Color(255, 40, 120, 200)); }
                CLSID png{}; CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &png);
                require(bitmap.Save(picture.c_str(), &png) == Gdiplus::Ok, "preview fixture not written");
            }
            const auto utf8 = [](const std::wstring& text) {
                const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
                std::string out(size_t(size), '\0'); WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size, nullptr, nullptr); return out;
            };
            require(host.setImage("preview", utf8(picture.wstring()) + "#preview"), "preview image not set");
            require(host.setVisible("option-0", true) && host.setText("option-0-label", "MODE") && host.setText("option-0-value", "A"), "option row not filled");
            require(host.setClass("screen-2", "selected", true), "screen row missing");
            require(host.setVisible("option-1", true) && host.setText("option-1-label", "レーン明るさ(LANE BRIGHTNESS)") &&
                host.setText("option-1-value", "標準(DEFAULT)"), "second option row not filled");
            surface.paint(host);
            // A shown row keeps its label, arrows and value on one line, and rows do not overlap.
            const RECT label = host.bounds("option-0-label"), dec = host.bounds("option-0-dec"), value = host.bounds("option-0-value"),
                       inc = host.bounds("option-0-inc"), next = host.bounds("option-1-label"), longLabel = host.bounds("option-1-label");
            require(label.right <= dec.left + 1 && dec.right <= value.left + 1 && value.right <= inc.left + 1, "option row parts are not side by side");
            require(std::abs(label.top - dec.top) <= 12 && std::abs(dec.top - inc.top) <= 2, "option row parts are not on one line");
            require(next.top >= label.bottom - 1, "option rows overlap");
            require(longLabel.bottom - longLabel.top <= 48, "a long label wraps onto a second line");
            {
                // Real LR2 option names (LITONE5_PLAYAC) for a look at the finished screen: Validation/RmlSettings/skin-select.png.
                const char* names[][2] = {{"プレイサイド(PLAYSIDE)", "1P"}, {"レーン明るさ(LANE BRIGHTNESS)", "標準(DEFAULT)"},
                    {"グラフ位置(GRAPH POSITION)", "遠い(FAR)"}, {"判定タイミング表示(JUDGE TIMING)", "なし(OFF)"}, {"ボム(BOMB)", "金(GOLD)"},
                    {"LNボム(LN BOMB)", "金(GOLD)"}, {"ターンテーブル(TURNTABLE)", "小さい(SMALL)"}, {"鍵盤透過(TRANSPARENT KEYS)", "なし(OFF)"},
                    {"ローディング(LOADING)", "曲読み込み情報(MUSIC LOADING)"}, {"LR2 F/S(FAST/SLOW)", "使用していない(NO USE)"},
                    {"ノーツ(NOTES)", "標準(DEFAULT)"}, {"レーザー(LASER)", "標準(DEFAULT)"}};
                for (int i = 0; i < 12; ++i) {
                    const auto id = "option-" + std::to_string(i);
                    host.setVisible(id, true); host.setText(id + "-label", names[i][0]); host.setText(id + "-value", names[i][1]);
                }
                host.setClass("option-3", "focused", true);
                surface.paint(host); // lay the rows out first
                const RECT box = host.bounds("options");
                require(host.bounds("option-11").top >= box.bottom, "the last row should start below the visible list");
                require(host.scrollIntoView("option-11"), "rows below the list cannot be scrolled into view");
                const RECT shown = host.bounds("option-11");
                require(shown.top >= box.top && shown.bottom <= box.bottom + 1, "scrolling into view did not bring the row into the list");
                host.setText("skin-name", "LITONE5_PLAYAC"); host.setText("skin-author", "ネアラ"); host.setText("skin-value", "LITONE5_PLAYAC  (2/3)");
                surface.paint(host);
                Gdiplus::Bitmap shot(surface.bitmap, nullptr);
                CLSID png{}; CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &png);
                shot.Save((std::filesystem::path(argv[2]) / L"skin-select.png").c_str(), &png);
                host.scrollTop("options"); surface.paint(host); // back to the first rows for the clicks below
            }
            click(host, "skin-next"); click(host, "screen-4"); click(host, "option-0-inc"); click(host, "skin-inc");
            require(std::find(clicked.begin(), clicked.end(), "skin-inc") != clicked.end(), "the skin row arrow did not report its id");
            require(std::find(clicked.begin(), clicked.end(), "skin-next") != clicked.end() &&
                std::find(clicked.begin(), clicked.end(), "screen-4") != clicked.end() &&
                std::find(clicked.begin(), clicked.end(), "option-0-inc") != clicked.end(), "skin select clicks did not report their ids");
            host.close();
        }
        {
            // Key config screen: modes on the left, a row per lane with its key and controller button side by
            // side; the red texts and modes report their ids. Capture: Validation/RmlSettings/key-config.png.
            const auto document = std::filesystem::path(argv[1]).parent_path() / L"keyconfig.rml";
            std::vector<std::string> clicked;
            require(host.open(nullptr, document, [&](const auto& id, const auto&) { clicked.push_back(id); }, false), "key config document did not load");
            const char* lanes[][3] = {{"스크래치", "L-SHIFT", "BUTTON 8, 9 + AXIS X"}, {"1번 건반", "S", "BUTTON 1"}, {"2번 건반", "D", "BUTTON 2"},
                {"3번 건반", "F", "BUTTON 3"}, {"4번 건반", "SPACE", "BUTTON 4"}, {"5번 건반", "J", "BUTTON 5"}, {"6번 건반", "K", "BUTTON 6"}, {"7번 건반", "L", "---"}};
            for (int i = 0; i < 8; ++i) {
                const auto id = "slot-" + std::to_string(i);
                require(host.setVisible(id, true) && host.setText(id + "-label", lanes[i][0]) && host.setText(id + "-key", lanes[i][1]) &&
                    host.setText(id + "-pad", lanes[i][2]), "key config row missing");
            }
            host.setClass("slot-7-pad", "unset", true);
            host.setClass("mode-1", "selected", true); host.setClass("slot-2", "focused", true);
            host.setText("slot-3-key", "입력 대기"); host.setText("slot-3-pad", "..."); host.setClass("slot-3-key", "listening", true); host.setClass("slot-3-pad", "listening", true);
            host.setText("device", "컨트롤러: 연결됨");
            surface.paint(host);
            const RECT label = host.bounds("slot-0-label"), keyText = host.bounds("slot-0-key"), pad = host.bounds("slot-0-pad"), next = host.bounds("slot-1-key");
            require(label.right <= keyText.left + 1 && keyText.right <= pad.left + 1, "key config row parts are not side by side");
            require(std::abs(keyText.top - pad.top) <= 2 && next.top >= keyText.bottom - 1, "key config rows are not laid out in lines");
            {
                Gdiplus::Bitmap shot(surface.bitmap, nullptr);
                CLSID png{}; CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &png);
                shot.Save((std::filesystem::path(argv[2]) / L"key-config.png").c_str(), &png);
            }
            click(host, "slot-1-key"); click(host, "slot-2-pad"); click(host, "mode-4");
            for (const char* id : {"slot-1-key", "slot-2-pad", "mode-4"})
                require(std::find(clicked.begin(), clicked.end(), id) != clicked.end(), "a key config click did not report its id");
            host.close();
        }
        std::cout << "Rml settings host input, Unicode, resize/DPI, lifetime and failure recovery passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; code=1; }
    Gdiplus::GdiplusShutdown(token); return code;
}
