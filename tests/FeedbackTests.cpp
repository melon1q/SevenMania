#include "PlayFeedback.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int wmain(int argc, wchar_t** argv) {
    try {
        require(std::abs(PlayFeedback::scale(0) - 0.12) < 0.00001, "initial size");
        require(PlayFeedback::scale(0.12) > 1.0, "easeOutBack overshoot missing");
        require(std::abs(PlayFeedback::scale(0.18) - 1) < 0.00001, "growth endpoint");
        require(PlayFeedback::scale(0.23) == 1 && PlayFeedback::scale(0.27) == 1, "circle shrinks while fading");
        require(std::abs(PlayFeedback::opacity(0.23) - 0.5) < 0.00001, "fast fade midpoint");
        require(PlayFeedback::scale(0.28) == 0 && PlayFeedback::opacity(0.28) == 0 && PlayFeedback::scale(-1) == 0, "lifetime");
        constexpr int width = 640, height = 180;
        constexpr uint32_t background = 0xff121a26u;
        PlayFeedback feedback;
        feedback.addCombo(10); feedback.addCombo(5);
        require(feedback.combo() == 15 && feedback.maxCombo() == 15, "batched hold combo was lost");
        feedback.miss();
        require(feedback.combo() == 0 && feedback.maxCombo() == 15, "hold combo miss/reset changed the record");
        feedback.reset();
        require(feedback.combo() == 0 && feedback.maxCombo() == 0, "new song retained hold combo");
        feedback.hit(0, 10); feedback.hit(7, 10);
        require(feedback.combo() == 2, "simultaneous combo");
        require(feedback.maxCombo() == 2, "maximum combo tracking");
        const std::array<uint32_t, 6> spriteFrames{
            0xffff0000u, 0xffff8000u, 0xffffff00u, 0xff00ff00u, 0xff0080ffu, 0xff0000ffu
        };
        PlayFeedback animated;
        require(animated.setSpriteSheet(6, 1, spriteFrames.data()), "six-frame splash sprite sheet setup");
        animated.hit(3, 0.0);
        std::vector<uint32_t> firstFrame(640 * 180, background), lastFrame(firstFrame);
        animated.draw(firstFrame.data(), 640, 180, 16, 76, 90, 0.01);
        animated.draw(lastFrame.data(), 640, 180, 16, 76, 90, 0.16);
        require(firstFrame != lastFrame, "video splash frames do not animate");
        feedback.miss(); require(feedback.combo() == 0, "miss reset");
        require(feedback.maxCombo() == 2, "miss preserves maximum combo");
        std::vector<uint32_t> canvas(width * height, background);
        feedback.draw(canvas.data(), width, height, 16, 76, 90, 10.12);
        size_t changed = 0;
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            if (canvas[static_cast<size_t>(y) * width + x] == background) continue;
            ++changed;
            require(x < 128 || x > 480, "wrong lane position");
            require(std::abs(y - 91) < 55, "effect away from judgement line");
        }
        require(changed > 500, "circle invisible");
        std::vector<uint32_t> clipped(102, background);
        clipped.front() = clipped.back() = 0xabcdef01;
        feedback.draw(clipped.data() + 1, 10, 10, -50, 76, 0, 10.12);
        require(clipped.front() == 0xabcdef01 && clipped.back() == 0xabcdef01, "out of bounds");
        feedback.reset(); feedback.draw(canvas.data(), width, height, 16, 76, 90, 10.12);
        require(feedback.combo() == 0 && feedback.maxCombo() == 0, "restart combo records");
        std::fill(canvas.begin(), canvas.end(), background);
        feedback.draw(canvas.data(), width, height, 16, 76, 90, 10.12);
        require(std::all_of(canvas.begin(), canvas.end(), [](uint32_t p) { return p == background; }), "restart effect");
        // Render four animation ages side-by-side for visual inspection.
        for (int i = 0; i < 4; ++i) {
            PlayFeedback frame; frame.hit(0, 0);
            constexpr double ages[]{ 0.0, 0.12, 0.18, 0.23 };
            frame.draw(canvas.data(), width, height, 40 + i * 150, 76, 90, ages[i]);
        }
        if (argc > 1) {
            const std::filesystem::path path(argv[1]);
            BITMAPFILEHEADER file{}; file.bfType = 0x4d42; file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
            file.bfSize = file.bfOffBits + static_cast<DWORD>(canvas.size() * 4);
            BITMAPINFOHEADER info{}; info.biSize = sizeof(info); info.biWidth = width; info.biHeight = -height;
            info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB;
            std::ofstream output(path, std::ios::binary);
            output.write(reinterpret_cast<const char*>(&file), sizeof(file)); output.write(reinterpret_cast<const char*>(&info), sizeof(info));
            output.write(reinterpret_cast<const char*>(canvas.data()), static_cast<std::streamsize>(canvas.size() * 4));
        }
        std::cout << "Feedback: easing, lifetime, simultaneous combo, miss/reset, lane anchor, clipping passed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
