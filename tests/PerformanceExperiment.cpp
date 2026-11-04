#include "SongLibrary.h"
#include "ExperimentProfile.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#ifdef BMS_USE_MIMALLOC
#include <mimalloc.h>
#endif

int wmain(int argc, wchar_t** argv) {
    try {
        using Clock = std::chrono::steady_clock;
#ifdef BMS_USE_MIMALLOC
        const auto allocation = std::make_unique<int>(42);
        if (!mi_is_in_heap_region(allocation.get())) throw std::runtime_error("C++ new did not use mimalloc");
        std::cout << "allocator=mimalloc ";
#else
        std::cout << "allocator=standard ";
#endif
#ifdef BMS_USE_ONETBB
        std::cout << "scheduler=oneTBB\n";
#else
        std::cout << "scheduler=jthread\n";
#endif
        // Repeated temporary allocation workload, independent of rendering.
        const auto started = Clock::now();
        size_t sum = 0;
        for (int pass = 0; pass < 50; ++pass) {
            std::vector<std::unique_ptr<unsigned char[]>> buffers;
            for (size_t i = 0; i < 4000; ++i) {
                auto data = std::make_unique<unsigned char[]>(64 + i % 2048);
                data[0] = static_cast<unsigned char>(i); sum += data[0]; buffers.push_back(std::move(data));
            }
        }
        std::cout << "temporary_alloc_ms=" << std::chrono::duration<double, std::milli>(Clock::now() - started).count()
            << " checksum=" << sum << '\n';
        if (argc > 1) {
            const auto start = Clock::now();
            auto charts = bms::scanSongFolder(argv[1]);
            const double milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            std::sort(charts.begin(), charts.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
            uint64_t fingerprint = 14695981039346656037ull;
            for (const auto& chart : charts) {
                const auto text = chart.path.generic_wstring() + chart.title + chart.playLevel +
                    std::to_wstring(chart.notes.size()) + std::to_wstring(chart.durationSeconds) + chart.blockReason;
                for (auto character : text) { fingerprint ^= static_cast<uint64_t>(character); fingerprint *= 1099511628211ull; }
            }
            std::cout << "charts=" << charts.size() << " scan_ms=" << milliseconds << " fingerprint=" << fingerprint << '\n';
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
