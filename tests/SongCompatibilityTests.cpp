#include "AudioEngine.h"
#include "VideoBga.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    if (FAILED(MFStartup(MF_VERSION))) { CoUninitialize(); return 1; }
    int result = 0;
    try {
        const std::filesystem::path folder(argv[1]);
        unsigned playable = 0;
        for (const auto* name : { L"_slic_hertz_s1.bme", L"_slic_hertz_s2.bme", L"_slic_hertz_s3.bme", L"_slic_hertz_s4.bme" }) {
            const auto parsed = bms::BmsParser().parse(folder / name);
            const auto& chart = parsed.chart;
            if (!chart.playable || chart.mode != bms::ChartMode::SevenKey || !parsed.errors.empty()) throw std::runtime_error("SP parsing failed");
            if (chart.sharedSampleDefinitions) throw std::runtime_error("silent ID incorrectly inherited");
            AudioEngine audio;
            std::wstring error;
            if (!audio.prepare(chart, error)) { std::wcerr << error << L"\n"; throw std::runtime_error("audio preparation failed"); }
            std::wcout << name << L": playable, notes=" << chart.notes.size() << L", decoded samples=" << chart.samples.size() << L", silent IDs=" << chart.silentSampleIds.size() << L"\n";
            ++playable;
        }
        if (playable != 4 || !bms::BmsParser().parse(folder / L"_slic_hertz_d3.bme").chart.playable) throw std::runtime_error("mode gate failed");
        VideoBga video;
        if (!video.open(folder / L"_bga.mp4")) throw std::runtime_error("MP4 open failed");
        unsigned frames = 0;
        for (int i = 0; i < 120; ++i) {
            if (video.update(i / 30.0)) ++frames;
            if (i > 1 && !video.ready()) throw std::runtime_error("MP4 frame disappeared");
        }
        if (frames < 60) throw std::runtime_error("MP4 stalled");
        std::wcout << L"DP playable; MP4 frame updates in first 4 seconds=" << frames << L"\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; result = 1;
    }
    MFShutdown(); CoUninitialize();
    return result;
}
