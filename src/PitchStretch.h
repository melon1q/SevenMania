#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
#pragma warning(push)
#pragma warning(disable: 4244 4267 4305 4456 4457 4458)
#include "../third_party/signalsmith-stretch/signalsmith-stretch.h"
#pragma warning(pop)

// Offline, stereo-linked preparation. No FFT, extra lookahead or allocations are
// added to the WASAPI callback; the original 1x PCM is copied exactly.
inline bool stretchPitchPreserving(const std::vector<int16_t>& input, uint32_t sampleRate,
    double rate, std::vector<int16_t>& output, const std::atomic_bool* cancelled = nullptr) {
    if (!std::isfinite(rate) || rate < 0.1 || rate > 10.0 || !sampleRate || input.size() % 2)
        throw std::invalid_argument("invalid pitch-preserving stretch parameters");
    if (cancelled && cancelled->load()) return false;
    if (rate == 1.0 || input.empty()) { output = input; return true; }
    const size_t frames = input.size()/2;
    const size_t outFrames = static_cast<size_t>(std::llround(frames/rate));
    if (outFrames > std::numeric_limits<size_t>::max()/2) throw std::length_error("stretch output too large");
    output.assign(outFrames*2, 0);
    signalsmith::stretch::SignalsmithStretch<float> stretch(0);
    stretch.presetDefault(2, static_cast<float>(sampleRate));
    struct Input {
        const std::vector<int16_t>& pcm;
        size_t offset{};
        struct Channel {
            const std::vector<int16_t>& pcm;
            size_t offset{}; int channel{};
            float operator[](int index) const {
                const int64_t frame = static_cast<int64_t>(offset) + index;
                return frame >= 0 && static_cast<uint64_t>(frame) < pcm.size()/2 ? pcm[static_cast<size_t>(frame)*2 + channel]/32768.0f : 0.0f;
            }
        };
        Channel operator[](int channel) const { return {pcm, offset, channel}; }
    } source{input};
    stretch.seek(source, stretch.inputLatency(), rate);
    const int latency = stretch.outputLatency();
    std::array<std::vector<float>, 2> scratch;
    for (auto& channel : scratch) channel.resize(std::max(1024, latency));
    float* destinations[]{scratch[0].data(), scratch[1].data()};
    const auto store = [&](size_t offset, int count) {
        for (int i = 0; i < count; ++i) {
            const auto frame = static_cast<int64_t>(offset) + i - latency;
            if (frame < 0 || static_cast<uint64_t>(frame) >= outFrames) continue;
            for (int c = 0; c < 2; ++c) {
                const float value = destinations[c][i];
                output[static_cast<size_t>(frame)*2 + c] = std::isfinite(value) ?
                    static_cast<int16_t>(std::lround(std::clamp(value*32768.0f, -32768.0f, 32767.0f))) : 0;
            }
        }
    };
    size_t consumed = 0;
    for (size_t produced = 0; produced < outFrames;) {
        if (cancelled && cancelled->load()) { output.clear(); return false; }
        const int count = static_cast<int>(std::min<size_t>(1024, outFrames - produced));
        const size_t next = std::min(frames, static_cast<size_t>(std::llround((produced + count)*rate)));
        source.offset = static_cast<size_t>(stretch.inputLatency()) + consumed;
        stretch.process(source, static_cast<int>(next - consumed), destinations, count);
        store(produced, count);
        consumed = next; produced += count;
    }
    stretch.flush(destinations, latency, static_cast<float>(rate));
    store(outFrames, latency);
    return true;
}
