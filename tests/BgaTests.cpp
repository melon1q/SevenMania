#include "AsyncBgaRaster.h"
#include "VideoComposite.h"
#include "BgaRaster.h"
#include "UserBackground.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

void require(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
void savePng(const std::filesystem::path& path, int width, int height, const std::vector<uint32_t>& pixels) {
    UINT count{}, bytes{}; Gdiplus::GetImageEncodersSize(&count, &bytes);
    std::vector<BYTE> storage(bytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(storage.data());
    Gdiplus::GetImageEncoders(count, bytes, encoders);
    CLSID png{};
    for (UINT i = 0; i < count; ++i) if (wcscmp(encoders[i].MimeType, L"image/png") == 0) png = encoders[i].Clsid;
    Gdiplus::Bitmap image(width, height, width * 4, PixelFormat32bppARGB,
        reinterpret_cast<BYTE*>(const_cast<uint32_t*>(pixels.data())));
    require(image.Save(path.c_str(), &png) == Gdiplus::Ok, "PNG fixture could not be saved");
}

int wmain(int argc, wchar_t** argv) {
    ULONG_PTR token{}; Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) return 1;
    int result = 0;
    try {
        const auto root = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path(L"Validation/BgaTests");
        std::filesystem::create_directories(root);
        const auto colors = root / L"colors.png", alpha = root / L"alpha.png";
        savePng(colors, 2, 2, { 0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff });
        savePng(alpha, 2, 1, { 0x80ff0000, 0x00000000 });
        bms::Chart chart; chart.bitmaps[L"01"] = colors; chart.bitmaps[L"02"] = alpha;
        BgaRaster raster; raster.canvas(2, 2);
        std::vector<uint32_t> target(4, 0xff000000);
        require(raster.draw(chart, L"01", target.data()), "opaque fixture did not draw");
        require(target == std::vector<uint32_t>({ 0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff }), "original colors changed");
        {
            const auto imported = root / L"import-source.png", assets = root / L"Assets";
            std::filesystem::copy_file(colors, imported, std::filesystem::copy_options::overwrite_existing);
            std::wstring savedName, error;
            require(copyUserBackgroundImage(imported, assets, savedName, error), "custom background import failed");
            require(savedName == L"browse-background.png", "custom background kept an external source path");
            std::filesystem::remove(imported);
            BgaRaster restored; restored.canvas(2, 2);
            bms::Chart restoredChart; restoredChart.bitmaps[L"01"] = assets / savedName;
            std::vector<uint32_t> restoredPixels(4);
            require(restored.draw(restoredChart, L"01", restoredPixels.data()) && restoredPixels == target,
                "copied background could not be used after original removal");
            require(copyUserBackgroundImage(assets / savedName, assets, savedName, error), "reimporting the managed copy failed");
            require(!copyUserBackgroundImage(root / L"missing.png", assets, savedName, error) && savedName == L"browse-background.png",
                "invalid custom image replaced the working setting");
            std::filesystem::remove(assets / savedName);
            std::filesystem::remove(assets);
        }
        target.assign(4, 0xff000000);
        require(raster.draw(chart, L"01", target.data(), 128), "faded title fixture did not draw");
        require(target[0] == 0xff800000 && target[1] == 0xff008000 && target[2] == 0xff000080,
            "image opacity did not blend smoothly over the loading backdrop");
        require(raster.coversCanvas(chart, L"01"), "opaque full canvas was not detected");
        require(!raster.coversCanvas(chart, L"02"), "transparent miss image must retain lower layers");
        raster.canvas(4, 4); target.assign(16, 0xff000000);
        require(raster.draw(chart, L"01", target.data()), "resized fixture did not draw");
        const uint32_t center = target[5];
        require(std::abs(static_cast<int>((center >> 16) & 255) - 159) <= 1 &&
            std::abs(static_cast<int>((center >> 8) & 255) - 64) <= 1 &&
            std::abs(static_cast<int>(center & 255) - 64) <= 1, "bilinear center has incorrect weights");
        const auto resized = target;
        target.assign(16, 0xff000000); raster.draw(chart, L"01", target.data());
        require(target == resized, "cached resize changed pixels");
        raster.canvas(2, 1); target.assign(2, 0xff0000ff);
        require(raster.draw(chart, L"02", target.data()), "alpha fixture did not draw");
        require(target[0] == 0xff80007f && target[1] == 0xff0000ff, "premultiplied alpha blend is incorrect");
        raster.canvas(640, 480); target.assign(640 * 480, 0xff112233);
        chart.bgaClips[L"03"] = { L"01", 1, 0, 1, 1, 10, 20, true };
        require(raster.draw(chart, L"03", target.data()), "crop fixture did not draw");
        require(!raster.coversCanvas(chart, L"03"), "cropped image must retain lower layers");
        require(target[38 * 640 + 25] == 0xff00ff00 && target[38 * 640 + 24] == 0xff112233 &&
            target[40 * 640 + 25] == 0xff112233, "crop destination or extent is incorrect");
        chart.bgaClips[L"04"] = { L"01", 0, 0, 1, 1, 800, 800, true };
        const auto previous = target;
        require(raster.draw(chart, L"04", target.data()) && target == previous, "off-screen crop wrote outside canvas");
        require(!raster.draw(chart, L"missing", target.data()) && target == previous, "missing image changed canvas");
        chart.bitmaps[L"bad"] = root / L"missing.png";
        require(!raster.draw(chart, L"bad", target.data()) && target == previous, "missing file changed canvas");
        BgaRaster::tint(target.data(), 640, 480, RECT{ -5, -5, 1, 1 }, 0xffffffff, 255);
        require(target[0] == 0xffffffff && target[1] == 0xff112233, "tint clipping is incorrect");
        for (unsigned opacity : {0u, 1u, 51u, 128u, 204u, 254u, 255u}) for (uint32_t black : {0xff000000u, 0x00000000u}) {
            std::vector<uint32_t> actual(37 * 3);
            for (size_t i = 0; i < actual.size(); ++i) actual[i] = static_cast<uint32_t>(i * 39097183u + 0x10203040u);
            auto expected = actual;
            const unsigned weight = (opacity * 256 + 127) / 255;
            for (int y = 0; y < 3; ++y) for (int x = 3; x < 29; ++x) {
                const uint32_t original = expected[y * 37 + x]; uint32_t resultPixel = 0;
                for (unsigned channel = 0; channel < 4; ++channel) {
                    const unsigned shift = channel * 8;
                    const unsigned value = (((original >> shift) & 255) * (256 - weight) + ((black >> shift) & 255) * weight) >> 8;
                    resultPixel |= value << shift;
                }
                expected[y * 37 + x] = resultPixel;
            }
            BgaRaster::tint(actual.data(), 37, 3, {3, -1, 29, 4}, black, opacity);
            require(actual == expected, "SIMD black tint differs from independent scalar result");
            BgaRaster::tint(actual.data(), 37, 3, {29, 0, 3, 2}, black, opacity);
            require(actual == expected, "inverted tint bounds modified pixels");
        }
        // Compare opaque-copy, keyed-copy and alpha paths to an independent
        // scalar reference, including negative origins and clipped rows.
        for(unsigned alphaValue:{0u,1u,128u,254u,255u})for(int origin:{-30,-3,0,4,15,30})for(int keyMode:{0,1,2}) {
            constexpr int sw=23,sh=6,tw=19,th=4;
            std::vector<uint32_t> source(sw*sh),actual(tw*th,0xff123456),expected=actual;
            for(size_t i=0;i<source.size();++i)source[i]=i%3?0xff000000u|(static_cast<uint32_t>(i*39097183u)&0xffffffu):0xff000000u;
            const bms::BgaColor key{255,keyMode==2?90:0,keyMode==2?110:0,keyMode==2?130:0};
            for(int y=0;y<th;++y)for(int x=0;x<tw;++x) {
                const int sx=x-origin;if(sx<0 || sx>=sw)continue;const auto c=source[y*sw+sx];
                if(keyMode && int((c>>16)&255)<=key.red && int((c>>8)&255)<=key.green && int(c&255)<=key.blue)continue;
                uint32_t pixel=0xff000000u;for(unsigned shift:{0u,8u,16u})pixel|=(((c>>shift&255)*alphaValue+(expected[y*tw+x]>>shift&255)*(255-alphaValue))/255)<<shift;
                expected[y*tw+x]=pixel;
            }
            videoComposite::blend(source.data(),sw,sh,actual.data(),tw,th,origin,alphaValue,keyMode?&key:nullptr);
            require(actual==expected,"video fast paths changed alpha, color key or clipping");
        }
        raster.reset(); raster.canvas(2, 1);
        chart.bitmaps[L"01"] = alpha; target.assign(2, 0xff0000ff);
        require(raster.draw(chart, L"01", target.data()) && target[0] == 0xff80007f, "chart reset kept stale image");
        {
            bms::Chart asyncChart;asyncChart.bitmaps[L"01"]=colors;asyncChart.bitmaps[L"02"]=alpha;
            asyncChart.bga={{0,bms::BgaLayer::Base,L"01"},{1,bms::BgaLayer::Layer,L"02"}};
            AsyncBgaRaster background;background.start(asyncChart,7,5,0);
            BgaRaster baseline;baseline.canvas(7,5);
            std::vector<uint32_t> expected(35,0xff123456u),actual=expected;
            require(baseline.draw(asyncChart,L"01",expected.data())&&background.draw(asyncChart,L"01",actual.data())&&actual==expected,"async first image pixels changed");
            auto retained = background.canvasFrame(asyncChart,L"01",false,true);
            require(retained && std::equal(expected.begin(),expected.end(),retained.get()),"direct image differs from CPU composition");
            require(!background.canvasFrame(asyncChart,L"02",true,true),"transparent layer incorrectly occludes the backdrop");
            const auto oldPixels = expected;
            require(baseline.draw(asyncChart,L"02",expected.data(),128,true)&&background.draw(asyncChart,L"02",actual.data(),128,true)&&actual==expected,"async keyed layer pixels changed");
            // A cache hit must survive source removal; resize must obtain a new worker result.
            background.canvas(9,7);background.update(0);
            for(int attempt=0;attempt<1000;++attempt){background.update(0);actual.assign(63,0xff000000);if(background.draw(asyncChart,L"01",actual.data()))break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            baseline.canvas(9,7);expected.assign(63,0xff000000);require(baseline.draw(asyncChart,L"01",expected.data())&&actual==expected,"async resize failed");
            require(std::equal(oldPixels.begin(),oldPixels.end(),retained.get()),"resize invalidated retained backdrop");
            background.reset();background.start(asyncChart,2,2,1);background.reset();
        }
        std::cout << "BGA colors, bilinear resize, cached frames, alpha layers, crop, bounds, missing assets, tint, reset: passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    Gdiplus::GdiplusShutdown(token);
    return result;
}
