#include "PitchStretch.h"
#include <iostream>
#include <stdexcept>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
double frequency(const std::vector<int16_t>& pcm, int channel, uint32_t sampleRate) {
    size_t first = 0, last = 0, crossings = 0;
    const size_t trim=std::min<size_t>(sampleRate/5,pcm.size()/20);
    for (size_t i=std::max<size_t>(1,trim);i+trim<pcm.size()/2;++i) {
        if (pcm[(i-1)*2+channel] <= 0 && pcm[i*2+channel] > 0) {
            if (!crossings) first = i;
            last = i; ++crossings;
        }
    }
    require(crossings > 50 && last > first, "stretched sound lost its tone");
    return double(crossings - 1)*sampleRate/(last - first);
}
int main() {
    try {
        constexpr uint32_t sr = 48000;
        std::vector<int16_t> input(sr*2*2), output;
        for (size_t i = 0; i < input.size()/2; ++i) {
            input[2*i] = static_cast<int16_t>(12000*std::sin(6.283185307179586*440*i/sr));
            input[2*i+1] = static_cast<int16_t>(9000*std::sin(6.283185307179586*660*i/sr));
        }
        for (const double rate : {0.25,0.5,0.75,1.0,1.25,1.5,2.0,4.0}) {
            require(stretchPitchPreserving(input, sr, rate, output), "stretch failed");
            require(output.size()/2 == static_cast<size_t>(std::llround((input.size()/2)/rate)), "stretched duration is wrong");
            const double left = frequency(output, 0, sr), right = frequency(output, 1, sr);
            std::cout << rate << "x: " << output.size()/2 << " frames, " << left << " / " << right << " Hz\n";
            require(std::abs(left - 440) < 3 && std::abs(right - 660) < 3, "playback rate changed pitch");
            if (rate == 1.0) require(output == input, "normal-speed PCM changed");
        }
        std::vector<int16_t> silence(9600, 0);
        require(stretchPitchPreserving(silence, sr, 0.5, output) && std::all_of(output.begin(), output.end(), [](int16_t v) { return v == 0; }), "silence produced noise");
        std::atomic_bool cancelled{true};
        require(!stretchPitchPreserving(input, sr, 0.5, output, &cancelled), "stretch ignored cancellation");
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
