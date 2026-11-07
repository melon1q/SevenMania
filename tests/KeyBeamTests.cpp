#include "KeyBeams.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void expectNear(double a, double b, const char* message) { require(std::abs(a - b) < 0.00001, message); }
int wmain(int argc, wchar_t** argv) {
    try {
        KeyBeams beams;
        beams.setPressed(0, true, 1);
        expectNear(beams.widthFraction(0, 1), 0, "initial thin width");
        expectNear(beams.widthFraction(0, 1.01), 0.5, "expansion midpoint");
        expectNear(beams.widthFraction(0, 1.02), 1, "expansion 20 ms");
        expectNear(beams.widthFraction(0, 10), 1, "held width");
        beams.setPressed(0, false, 10);
        expectNear(beams.widthFraction(0, 10.01), 0.5, "reverse midpoint");
        expectNear(beams.widthFraction(0, 10.02), 0, "release 20 ms");
        beams.reset(); beams.setPressed(1, true, 0); beams.setPressed(1, false, 0.008);
        expectNear(beams.widthFraction(1, 0.008), 0.4, "release jumps");
        expectNear(beams.widthFraction(1, 0.012), 0.2, "release reverses expansion speed");
        beams.setPressed(1, true, 0.012);
        expectNear(beams.widthFraction(1, 0.012), 0.2, "repress jumps");
        expectNear(beams.widthFraction(1, 0.028), 1, "repress reaches full width");
        beams.reset(); expectNear(beams.widthFraction(1, 0.028), 0, "restart beams");
        constexpr uint32_t background = 0xff000000;
        constexpr int width = 200, height = 180;
        std::vector<uint32_t> pixels(width * height, background);
        beams.setPressed(0, true, 0); beams.setPressed(7, true, 0);
        beams.draw(pixels.data(), width, height, 10, 20, 150, 0.1);
        require(pixels[20] == background, "far edge is not transparent");
        require(pixels[74 * width + 20] == background && pixels[75 * width + 20] == background, "beam exceeds half height");
        require((pixels[150 * width + 20] & 255) > (pixels[112 * width + 20] & 255) && pixels[112 * width + 20] != background, "gradient not stronger near line");
        const LaneLayout testLayout{ 10, 20, 20 };
        const int lastLanePixel = testLayout.right(7) - 1;
        require(pixels[150 * width + lastLanePixel] != background && pixels[150 * width + 90] == background, "lane isolation");
        require(pixels[151 * width + 20] == background && pixels[150 * width + 9] == background, "beam bounds");
        std::vector<uint32_t> guarded(102, background); guarded.front() = guarded.back() = 0x12345678;
        beams.draw(guarded.data() + 1, 10, 10, -5, 20, 8, 0.1);
        require(guarded.front() == 0x12345678 && guarded.back() == 0x12345678, "clipping");
        beams.reset(); beams.setPressed(0, true, 0);
        std::fill(pixels.begin(), pixels.end(), background);
        beams.draw(pixels.data(), width, height, 0, 68, 150, 0.1, 104);
        require(pixels[150 * width + 103] != background && pixels[150 * width + 104] == background, "wide scratch beam alignment");
        beams.setPressed(1, true, 0);
        beams.draw(pixels.data(), width, height, 0, 68, 150, 0.1, 104);
        const LaneLayout wideLayout{ 0, 68, 104 };
        require(pixels[150 * width + wideLayout.left(1)] != background && pixels[150 * width + wideLayout.right(1) - 1] != background &&
            pixels[150 * width + wideLayout.right(1)] == background, "wide first-key beam alignment");
        if (argc > 1) {
            constexpr int previewWidth = 420, previewHeight = 220;
            std::vector<uint32_t> preview(previewWidth * previewHeight, 0xff101820);
            for (int i = 0; i < 3; ++i) {
                KeyBeams frame; frame.setPressed(0, true, 0);
                if (i == 2) frame.setPressed(0, false, 0.1);
                frame.draw(preview.data(), previewWidth, previewHeight, 30 + i * 140, 76, 200, i == 0 ? 0.05 : i == 1 ? 0.1 : 0.15);
            }
            BITMAPFILEHEADER file{}; file.bfType = 0x4d42; file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER); file.bfSize = file.bfOffBits + static_cast<DWORD>(preview.size() * 4);
            BITMAPINFOHEADER info{}; info.biSize = sizeof(info); info.biWidth = previewWidth; info.biHeight = -previewHeight; info.biPlanes = 1; info.biBitCount = 32;
            std::ofstream output(std::filesystem::path(argv[1]), std::ios::binary);
            output.write(reinterpret_cast<const char*>(&file), sizeof(file)); output.write(reinterpret_cast<const char*>(&info), sizeof(info)); output.write(reinterpret_cast<const char*>(preview.data()), static_cast<std::streamsize>(preview.size() * 4));
        }
        std::cout << "Key beams: 100 ms expansion/release, continuous reversal, hold/reset, gradient, lane isolation and clipping passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
