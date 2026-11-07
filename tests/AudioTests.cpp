#include "AudioEngine.h"
#include "SongPreview.h"
#include "SelectionAudio.h"
#include "PracticePreview.h"
#include "PreviewTiming.h"

#include <mfapi.h>
#include <chrono>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <windows.h>

namespace {

void writeWave(const std::filesystem::path& path) {
    constexpr uint32_t rate = 44100;
    constexpr uint16_t channels = 2, bits = 16;
    constexpr uint32_t frames = 44100;
    constexpr uint32_t dataSize = frames * channels * sizeof(int16_t);
    std::ofstream out(path, std::ios::binary);
    auto write = [&out](const auto& value) { out.write(reinterpret_cast<const char*>(&value), sizeof(value)); };
    out.write("RIFF", 4); uint32_t riff = 36 + dataSize; write(riff); out.write("WAVEfmt ", 8);
    uint32_t fmtSize = 16; write(fmtSize); uint16_t format = 1; write(format); write(channels); write(rate);
    uint32_t byteRate = rate * channels * bits / 8; write(byteRate); uint16_t align = channels * bits / 8; write(align); write(bits);
    out.write("data", 4); write(dataSize);
    for (uint32_t i = 0; i < frames; ++i) {
        const double phase = 6.283185307179586 * 523.25 * i / rate;
        const int16_t sample = static_cast<int16_t>(std::sin(phase) * 4500.0);
        write(sample); write(sample);
    }
}

} // namespace

struct AudioEngineTestAccess {
    static void verifyPitchPreservingMix() {
        AudioEngine mixer;
        mixer.sampleRate_ = 48000;
        auto tone = std::make_shared<AudioEngine::AudioData>();
        tone->pcm.resize(48000*2*2);
        for (size_t i = 0; i < tone->pcm.size()/2; ++i)
            tone->pcm[2*i] = tone->pcm[2*i+1] = static_cast<int16_t>(9000*std::sin(6.283185307179586*523.25*i/48000));
        mixer.samples_[L"tone"] = tone; mixer.samples_[L"alias"] = tone;
        const auto original = tone->pcm;
        for (double rate : {0.5, 0.75, 1.25, 1.5}) {
            mixer.stop();
            std::wstring error;
            if (!mixer.preparePlaybackRate(rate, error)) throw std::runtime_error("pitch cache preparation failed");
            const auto cached = mixer.playbackSamples_.at(L"tone");
            if (cached != mixer.playbackSamples_.at(L"alias") || !mixer.preparePlaybackRate(rate, error) ||
                mixer.playbackSamples_.at(L"tone") != cached) throw std::runtime_error("pitch cache did not reuse identical samples/rate");
            mixer.playbackRate_ = rate;
            mixer.bgm_ = {{12000, L"tone"}}; mixer.nextBgm_ = 0; mixer.nextFrame_ = 0;
            constexpr uint32_t frames = 72000;
            mixer.mixBuffer_.resize(frames*2);
            std::vector<int16_t> output(frames*2);
            mixer.fillBuffer(output.data(), frames);
            const auto onset = static_cast<size_t>(std::llround(12000/rate));
            if (std::any_of(output.begin(), output.begin()+onset*2, [](int16_t v) { return v != 0; }))
                throw std::runtime_error("practice BGM started before its scaled chart event");
            size_t first = 0, last = 0, crossings = 0;
            for (size_t i = onset+4800; i < onset+28800; ++i) {
                if (output[(i-1)*2] <= 0 && output[i*2] > 0) {
                    if (!crossings) first = i;
                    last = i; ++crossings;
                }
            }
            if (crossings < 100 || last <= first || std::abs(double(crossings-1)*48000/(last-first)-523.25) > 3)
                throw std::runtime_error("practice mixer changed pitch or sample timing");
        }
        mixer.stop();
        std::wstring error;
        if (!mixer.preparePlaybackRate(1.0, error) || !mixer.playbackSamples_.empty() || tone->pcm != original)
            throw std::runtime_error("normal rate retained pitch cache or modified original audio");
        mixer.releaseSong();
        if (mixer.decodedBytes() != 0) throw std::runtime_error("pitch cache retained song memory after release");
    }
    static void verifyMixing() {
        AudioEngine mixer;
        AudioEngine::AudioData tone;
        tone.pcm = {1000, -1000, 2000, -2000, 3000, -3000, 4000, -4000};
        mixer.mixBuffer_.resize(8);
        mixer.setVolumes(100, 50, 25, 100);
        mixer.voices_ = {{&tone, 0, false, false}, {&tone, 1, true, false}};
        std::array<int16_t, 8> result{};
        mixer.fillBuffer(result.data(), 4);
        const std::array<int16_t, 8> expected{500, -500, 1250, -1250, 2000, -2000, 2750, -2750};
        if (result != expected) throw std::runtime_error("block mixer changed voice timing or category gains");
    }
    static void pauseMixer(AudioEngine& audio) {
        audio.running_ = false;
        SetEvent(audio.audioEvent_);
        if (audio.worker_.joinable()) audio.worker_.join();
    }
    static void resumeMixer(AudioEngine& audio) {
        audio.running_ = true;
        audio.worker_ = std::thread(&AudioEngine::workerLoop, &audio);
    }
    static uint64_t nextFrame(const AudioEngine& audio) { return audio.nextFrame_.load(); }
};

int main(int argc, char** argv) {
    if(argc==2&&std::string(argv[1])=="--selection-policy"){
        for(int configured=0;configured<=100;++configured){if(SelectionAudio::musicVolume(configured,false)!=configured||SelectionAudio::musicVolume(configured,true)!=std::min(configured,5)||SelectionAudio::musicVolume(configured,false)!=configured)return 30;}
        const auto missing=std::filesystem::current_path()/L"Validation"/(L"MissingSelectionAudio-"+std::to_wstring(GetCurrentProcessId()));std::filesystem::create_directories(missing);
        SelectionAudio audio;audio.initialize(missing);audio.tickMusic(100,80,false,32);audio.tickMusic(100,80,true,32);audio.decide(100,100,32);audio.tickDecision();audio.stopMusic();audio.shutdown();
        std::cout<<"PASS: 101 volume/duck/restore policies and absent optional files (no playback)\n";return 0;
    }

    const HRESULT comStartup = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const HRESULT startup = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(startup)) {
        std::cerr << "Media Foundation startup failed.\n";
        return 1;
    }
    int result = 0;
    const auto root = std::filesystem::temp_directory_path() /
        (L"BmsPlayerAudioTests_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
            std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(root);
        const auto wavePath = root / L"generated-tone.wav";
        writeWave(wavePath);
        bms::Chart chart;
        chart.path = root / L"tone.bms";
        chart.samples.emplace(L"01", wavePath);
        {
            auto sectionChart = chart;
            sectionChart.durationSeconds = 3;
            sectionChart.bgm.push_back({0, L"01"});
            PracticePreview section;
            section.prepare(sectionChart);
            section.select(0, 2, 1);
            section.select(0.2, 0.5, 0.5);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!section.playing() && std::chrono::steady_clock::now() < deadline) {
                section.tick(0, 0, 256); Sleep(10);
            }
            if (!section.playing()) throw std::runtime_error("practice section preview did not start");
            Sleep(150); section.tick(0, 0, 256);
            if (!section.playing()) throw std::runtime_error("practice preview ignored selected slow rate");
            Sleep(550); section.tick(0, 0, 256);
            if (section.playing()) throw std::runtime_error("practice preview did not stop at latest selected end");
            std::filesystem::remove(wavePath);
            section.select(0, 0.2, 1.5);
            const auto changedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!section.playing() && std::chrono::steady_clock::now() < changedDeadline) {
                section.tick(0, 0, 256); Sleep(10);
            }
            if (!section.playing()) throw std::runtime_error("practice range change decoded audio again");
            section.stop();
            if (section.playing()) throw std::runtime_error("practice preview remained audible after close");
            section.reset(); writeWave(wavePath);
        }
        AudioEngine audio;
        std::wstring error;
        if (!audio.prepare(chart, error)) {
            std::wcerr << L"Generated WAV decode test failed: " << error << L"\n";
            result = 1;
        } else {
            std::cout << "AudioTests: generated WAV decoded with Windows Media Foundation.\n";
        }
        if (result == 0) {
            const auto corruptSample = root / L"corrupt-sample.wav";
            std::ofstream(corruptSample, std::ios::binary).put('x');
            auto partialChart = chart;
            partialChart.samples.emplace(L"02", corruptSample);
            size_t decodeFailures{};
            if (!audio.prepare(partialChart, error, nullptr, true, &decodeFailures) || decodeFailures != 1 || audio.decodedBytes() == 0)
                throw std::runtime_error("recoverable sample decode failure did not preserve the playable audio set");
            if (!audio.prepare(chart, error)) throw std::runtime_error("full sample reload after partial decode failed");
        }
        if (result == 0) {
            if (audio.decodedBytes() == 0) throw std::runtime_error("decoded song memory was not allocated");
            audio.releaseSong();
            if (audio.decodedBytes() != 0 || audio.durationSeconds() != 0 || audio.isRunning())
                throw std::runtime_error("song memory was retained after release");
            if (!audio.prepare(chart, error)) throw std::runtime_error("song reload after release failed");
            AudioEngineTestAccess::verifyMixing();
            AudioEngineTestAccess::verifyPitchPreservingMix();
            audio.setVolumes(0, 0, 0, 0);
            for (uint32_t frames : {32u, 64u, 128u, 256u, 512u, 1024u}) {
                if (!audio.setBufferFrames(frames) || audio.bufferFrames() != frames || !audio.start(error))
                    throw std::runtime_error("configured audio buffer could not start output");
                std::cout << "Requested block " << frames << ", device buffer " << audio.deviceBufferFrames() << " frames at " << audio.sampleRate() << " Hz\n";
                for (int voice = 0; voice < 256; ++voice) audio.trigger(L"01");
                const double before = audio.positionSeconds();
                Sleep(120);
                if (!audio.isRunning()) throw std::runtime_error("configured audio buffer stopped unexpectedly");
                const double elapsed = audio.positionSeconds() - before;
                if (elapsed < 0.10 || elapsed > 0.25) throw std::runtime_error("dense polyphony altered transport speed");
                audio.stop();
            }
            if (audio.setBufferFrames(441) || audio.bufferFrames() != 1024)
                throw std::runtime_error("unsupported buffer value altered the setting");
            audio.setBufferFrames(32);
            if (!audio.start(error)) throw std::runtime_error("underrun regression stream could not start");
            AudioEngineTestAccess::pauseMixer(audio);
            const double beforeStall = audio.positionSeconds();
            Sleep(220);
            const double afterStall = audio.positionSeconds();
            if (afterStall - beforeStall < 0.20) throw std::runtime_error("underrun slowed game time");
            AudioEngineTestAccess::resumeMixer(audio);
            Sleep(40);
            if (AudioEngineTestAccess::nextFrame(audio) + 1024 < static_cast<uint64_t>(audio.positionSeconds() * audio.sampleRate()))
                throw std::runtime_error("underrun recovery replayed overdue audio");
            audio.stop();
            std::cout << "AudioTests: WASAPI six sizes, 256 voices, forced underrun and real-time recovery passed.\n";
            chart.bgm.push_back({0, L"01"});
            if (!audio.prepare(chart, error) || !audio.startAt(0.5, error)) throw std::runtime_error("preview seek failed");
            Sleep(60);
            const double previewPosition = audio.positionSeconds();
            Sleep(120);
            const double previewAdvance = audio.positionSeconds() - previewPosition;
            if (previewPosition < 0.5 || previewPosition > 0.75 || previewAdvance < 0.10 || previewAdvance > 0.25 || !audio.isRunning())
                throw std::runtime_error("preview seek offset or playback speed failed");
            audio.stop();
            for (double rate : {0.5, 0.75, 1.25, 1.5}) {
                if (!audio.startAt(0.25, error, rate)) throw std::runtime_error("practice rate output failed");
                Sleep(80);
                const double position = audio.positionSeconds();
                const auto started = std::chrono::steady_clock::now();
                Sleep(160);
                const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
                if (std::abs(audio.positionSeconds() - position - elapsed * rate) > 0.045 || std::abs(audio.playbackRate() - rate) > 0.0001)
                    throw std::runtime_error("practice audio and chart transport rates diverged");
                if (!audio.pause()) throw std::runtime_error("practice pause failed");
                const double paused = audio.positionSeconds();
                Sleep(40);
                if (audio.positionSeconds() != paused || !audio.resume()) throw std::runtime_error("practice pause changed chart position");
                audio.stop();
            }
            if (audio.startAt(0, error, 0.09) || audio.startAt(0, error, 10.01)) throw std::runtime_error("out-of-range practice rate accepted");
            if (!audio.startAt(0, error) || audio.playbackRate() != 1.0) throw std::runtime_error("practice speed leaked into normal playback");
            audio.stop();
            std::cout << "AudioTests: practice 0.5x-1.5x, seek offset, pause and normal-rate restoration passed.\n";
            SongPreview preview;
            bms::Chart broken; broken.path = root / L"broken.bms"; broken.previewAudio = root / L"missing.wav";
            preview.select(broken);
            chart.previewAudio = wavePath; chart.previewStartSeconds = 42;
            const auto corrupt = root / L"preview-corrupt.wav";
            std::ofstream(corrupt, std::ios::binary).put('x');
            chart.previewAudioCandidates = {corrupt, wavePath};
            preview.select(chart);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!preview.playing() && std::chrono::steady_clock::now() < deadline) {
                preview.tick(0, 0, 256); Sleep(10);
            }
            if (!preview.playing() || preview.chartTime(0) < 42 || preview.chartTime(0) >= 43)
                throw std::runtime_error("latest preview or BGA timeline offset failed");
            preview.reset();
            if (preview.playing()) throw std::runtime_error("preview did not stop on cancellation");
            bms::Chart fallback;
            fallback.path = root / L"fallback.bms";
            fallback.previewAudio = corrupt; fallback.samples[L"01"] = wavePath;
            fallback.samples[L"02"] = root / L"missing-key.wav";
            fallback.bgm = {{0, L"01"}, {0, L"02"}};
            preview.select(fallback);
            const auto fallbackDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!preview.playing() && std::chrono::steady_clock::now() < fallbackDeadline) {
                preview.tick(0, 0, 256); Sleep(10);
            }
            if (!preview.playing() || !preview.lastError().empty()) throw std::runtime_error("unreadable preview did not fall back to usable chart audio");
            preview.shutdown();
            SongPreview warm;
            warm.pauseWarmup(true);
            warm.prewarm({chart, fallback});
            const auto warmDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while ((!warm.cached(chart.path) || !warm.cached(fallback.path)) && std::chrono::steady_clock::now() < warmDeadline) Sleep(10);
            if (!warm.cached(chart.path) || !warm.cached(fallback.path) || warm.playing())
                throw std::runtime_error("sequential background preview preparation failed or played without selection");
            std::filesystem::remove(wavePath);
            warm.select(chart); warm.tick(0, 0, 256);
            if (!warm.playing() || warm.chartTime(0) < 42) throw std::runtime_error("warm preview was decoded again on selection");
            warm.reset(); warm.select(fallback); warm.tick(0, 0, 256);
            if (!warm.playing()) throw std::runtime_error("second sequentially prepared preview could not be reused");
            warm.reset(); warm.select(chart); warm.tick(0, 0, 256);
            if (!warm.playing()) throw std::runtime_error("returning to a cached preview lost prepared audio");
            warm.clearCache();
            if (warm.cached(chart.path) || warm.cached(fallback.path) || warm.playing())
                throw std::runtime_error("library reload retained old prepared preview audio");
            warm.shutdown(); writeWave(wavePath);
        }
        if (result == 0 && argc > 1) {
            if ((std::string(argv[1]) == "--preview-chart" || std::string(argv[1]) == "--preview-group") && argc == 3) {
                const bool grouped = std::string(argv[1]) == "--preview-group";
                bms::Chart actual;
                if (grouped) {
                    std::vector<bms::Chart> charts;
                    for (const auto* file : {L"dive_l7.bme", L"dive_n7.bme", L"dive_a7.bme"})
                        charts.push_back(bms::BmsParser().parse(std::filesystem::path(argv[2]) / file).chart);
                    const auto representative = bms::sharePreviewTiming(charts, {0, 1, 2});
                    actual = charts[representative];
                    for (const auto& variant : charts) if (variant.previewStartSeconds != actual.previewStartSeconds)
                        throw std::runtime_error("actual grouped variants use different highlights");
                } else actual = bms::BmsParser().parse(argv[2]).chart;
                if (!actual.playable || actual.notes.empty() || actual.previewStartSeconds <= 0)
                    throw std::runtime_error("actual chart did not select a playable highlight");
                SongPreview actualPreview;
                if (grouped) {
                    actualPreview.prewarm({actual});
                    const auto cacheDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                    while (!actualPreview.cached(actual.path) && std::chrono::steady_clock::now() < cacheDeadline) Sleep(10);
                    if (!actualPreview.cached(actual.path)) throw std::runtime_error("actual group did not prewarm");
                }
                const auto selectedAt = std::chrono::steady_clock::now();
                actualPreview.select(actual);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                while (!actualPreview.playing() && std::chrono::steady_clock::now() < deadline) {
                    actualPreview.tick(0, 0, 256); Sleep(10);
                }
                if (!actualPreview.playing()) {
                    std::wcerr << actualPreview.lastError() << L'\n';
                    throw std::runtime_error("actual chart preview did not start");
                }
                const double selectionMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - selectedAt).count();
                Sleep(250); actualPreview.tick(0, 0, 256);
                const double time = actualPreview.chartTime(0);
                if (actual.previewAudio.empty() && (time < actual.previewStartSeconds + 0.2 || time > actual.previewStartSeconds + 1))
                    throw std::runtime_error("actual highlight audio position or rate is wrong");
                std::wcout << L"Actual preview: " << actual.title << L", start=" << actual.previewStartSeconds
                    << L" sec, audio timeline=" << time << L" sec, dedicated preview=" << !actual.previewAudio.empty() << L'\n';
                if (grouped) std::wcout << L"Three grouped charts share this section; cached selection startup=" << selectionMs << L" ms\n";
                actualPreview.shutdown();
            } else if (std::string(argv[1]) == "--effects" && argc == 4) {
                AudioEngine effects;
                std::wstring effectError;
                if (!effects.loadEffect(L"levelchange", argv[2], effectError) || !effects.loadEffect(L"click", argv[3], effectError)) {
                    std::wcerr << L"UI effect MP3 decode test failed: " << effectError << L"\n";
                    result = 1;
                } else std::cout << "AudioTests: both UI effect MP3 files decoded.\n";
            } else {
                const std::filesystem::path externalPath(argv[1]);
                bms::Chart encodedChart;
                if (bms::toLower(externalPath.extension().wstring()) == L".ogg")
                    encodedChart.samples.emplace(L"01", externalPath);
                else {
                    auto externalParsed = bms::BmsParser().parse(externalPath);
                    if (!externalParsed.chart.playable) {
                        std::wcerr << L"External chart is not playable: " << externalParsed.chart.blockReason << L"\n";
                        result = 1;
                    } else encodedChart = std::move(externalParsed.chart);
                }
                AudioEngine encodedAudio;
                std::wstring encodedError;
                if (result == 0 && !encodedAudio.prepare(encodedChart, encodedError)) {
                    std::wcerr << L"Encoded audio decode test failed: " << encodedError << L"\n";
                    result = 1;
                } else if (result == 0) std::cout << "AudioTests: external encoded audio decode passed.\n";
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "AudioTests failed: " << error.what() << "\n";
        result = 1;
    }
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    MFShutdown();
    if (SUCCEEDED(comStartup)) CoUninitialize();
    return result;
}
