#include "PerformanceTrace.h"
#include "AudioEngine.h"
#include "ExperimentProfile.h"
#include "PitchStretch.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <mmdeviceapi.h>
#include <avrt.h>
#pragma comment(lib, "avrt.lib")

#define STB_VORBIS_IMPLEMENTATION
#define STB_VORBIS_NO_PUSHDATA_API
#pragma warning(push)
#pragma warning(disable: 4244 4456 4457 4701)
#include "../third_party/stb_vorbis.c"
#pragma warning(pop)

using Microsoft::WRL::ComPtr;

namespace {

bool decodeOggFile(const std::filesystem::path& path, std::vector<int16_t>& pcm,
    uint32_t outputRate, uint32_t outputChannels, std::wstring& error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) { error = L"OGG 음원 파일을 열 수 없습니다: " + path.wstring(); return false; }
    const auto fileSize = input.tellg();
    if (fileSize <= 0 || fileSize > static_cast<std::streamoff>(512ull * 1024ull * 1024ull) ||
        fileSize > static_cast<std::streamoff>(std::numeric_limits<int>::max())) {
        error = L"OGG 음원 파일 크기가 유효하지 않거나 디코딩 한도를 넘었습니다: " + path.wstring();
        return false;
    }
    std::vector<unsigned char> encoded(static_cast<size_t>(fileSize));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()))) {
        error = L"OGG 음원 파일을 읽지 못했습니다: " + path.wstring();
        return false;
    }

    int sourceChannels = 0, sourceRate = 0;
    short* decoded = nullptr;
    const int sourceFrames = stb_vorbis_decode_memory(encoded.data(), static_cast<int>(encoded.size()),
        &sourceChannels, &sourceRate, &decoded);
    if (sourceFrames <= 0 || !decoded || sourceChannels <= 0 || sourceRate <= 0) {
        if (decoded) free(decoded);
        error = L"OGG Vorbis 음원을 디코딩할 수 없습니다: " + path.wstring();
        return false;
    }

    const uint64_t outputFrames = (static_cast<uint64_t>(sourceFrames) * outputRate + static_cast<uint32_t>(sourceRate) / 2) /
        static_cast<uint32_t>(sourceRate);
    if (outputFrames == 0 || outputFrames > (512ull * 1024ull * 1024ull) / (outputChannels * sizeof(int16_t))) {
        free(decoded);
        error = L"디코딩된 OGG 음원이 메모리 한도를 넘었습니다: " + path.wstring();
        return false;
    }

    pcm.resize(static_cast<size_t>(outputFrames) * outputChannels);
    for (uint64_t frame = 0; frame < outputFrames; ++frame) {
        const double sourcePosition = static_cast<double>(frame) * sourceRate / outputRate;
        const uint64_t first = std::min<uint64_t>(static_cast<uint64_t>(sourcePosition), static_cast<uint64_t>(sourceFrames - 1));
        const uint64_t second = std::min<uint64_t>(first + 1, static_cast<uint64_t>(sourceFrames - 1));
        const float fraction = static_cast<float>(sourcePosition - first);
        for (uint32_t channel = 0; channel < outputChannels; ++channel) {
            const int sourceChannel = sourceChannels == 1 ? 0 : std::min<int>(static_cast<int>(channel), sourceChannels - 1);
            const size_t aIndex = static_cast<size_t>(first) * sourceChannels + sourceChannel;
            const size_t bIndex = static_cast<size_t>(second) * sourceChannels + sourceChannel;
            const float sample = decoded[aIndex] + (decoded[bIndex] - decoded[aIndex]) * fraction;
            pcm[static_cast<size_t>(frame) * outputChannels + channel] = static_cast<int16_t>(std::clamp(sample, -32768.0f, 32767.0f));
        }
    }
    free(decoded);
    return true;
}

} // namespace

AudioEngine::~AudioEngine() { stop(); }

bool AudioEngine::decodeFile(const std::filesystem::path& path, AudioData& data, std::wstring& error) const {
    BMS_TRACE_ZONE("Audio decode");
    if (bms::toLower(path.extension().wstring()) == L".ogg") {
        if (decodeOggFile(path, data.pcm, sampleRate_, channels_, error)) return true;
        data.pcm.clear(); // Try installed Windows decoders for non-Vorbis Ogg or mislabeled files.
    }
    ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader);
    if (FAILED(hr)) {
        error = L"Windows Media Foundation에서 음원을 열지 못했습니다. WAV·MP3 또는 Windows Ogg 코덱 설치를 확인하세요: " + path.wstring();
        return false;
    }

    ComPtr<IMFMediaType> outputType;
    hr = MFCreateMediaType(&outputType);
    if (SUCCEEDED(hr)) hr = outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    if (SUCCEEDED(hr)) hr = outputType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    if (SUCCEEDED(hr)) hr = outputType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels_);
    if (SUCCEEDED(hr)) hr = outputType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate_);
    if (SUCCEEDED(hr)) hr = outputType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    if (SUCCEEDED(hr)) hr = outputType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, channels_ * sizeof(int16_t));
    if (SUCCEEDED(hr)) hr = outputType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, sampleRate_ * channels_ * sizeof(int16_t));
    if (SUCCEEDED(hr)) hr = reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    if (SUCCEEDED(hr)) hr = reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE);
    if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, outputType.Get());
    if (FAILED(hr)) {
        error = L"오디오 형식을 PCM으로 변환할 수 없습니다: " + path.wstring();
        return false;
    }

    constexpr size_t maxSamples = 512ull * 1024ull * 1024ull / sizeof(int16_t);
    for (;;) {
        if (prepareCancelled_ && prepareCancelled_->load()) { error = L"불러오기를 취소했습니다."; return false; }
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags, nullptr, &sample);
        if (FAILED(hr)) { error = L"음원 디코딩 중 오류가 발생했습니다: " + path.wstring(); return false; }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample) continue;
        ComPtr<IMFMediaBuffer> buffer;
        hr = sample->ConvertToContiguousBuffer(&buffer);
        if (FAILED(hr)) { error = L"음원 데이터를 읽을 수 없습니다: " + path.wstring(); return false; }
        BYTE* bytes = nullptr;
        DWORD maxLength = 0, currentLength = 0;
        hr = buffer->Lock(&bytes, &maxLength, &currentLength);
        if (FAILED(hr)) { error = L"음원 버퍼를 잠글 수 없습니다: " + path.wstring(); return false; }
        if (data.pcm.size() + currentLength / sizeof(int16_t) > maxSamples) {
            buffer->Unlock();
            error = L"음원 사전 디코딩 한도(512MB)를 넘었습니다: " + path.wstring();
            return false;
        }
        const auto* begin = reinterpret_cast<const int16_t*>(bytes);
        data.pcm.insert(data.pcm.end(), begin, begin + currentLength / sizeof(int16_t));
        buffer->Unlock();
    }
    if (data.pcm.empty()) { error = L"음원 파일에 재생할 오디오가 없습니다: " + path.wstring(); return false; }
    if (data.pcm.size() % channels_ != 0) data.pcm.resize(data.pcm.size() - data.pcm.size() % channels_);
    return true;
}

void AudioEngine::configureDecodeRate() {
    if (decodeRateReady_) return;
    decodeRateReady_ = true;
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    WAVEFORMATEX* format{};
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) &&
        SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) &&
        SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf()))) &&
        SUCCEEDED(client->GetMixFormat(&format))) {
        if (format->nSamplesPerSec >= 8000 && format->nSamplesPerSec <= 192000) sampleRate_ = format->nSamplesPerSec;
        CoTaskMemFree(format);
    }
}

bool AudioEngine::loadEffect(const std::wstring& effectId, const std::filesystem::path& path, std::wstring& error) {
    configureDecodeRate();
    auto audio = std::make_shared<AudioData>();
    if (!decodeFile(path, *audio, error)) return false;
    effects_[effectId] = std::move(audio);
    return true;
}

void AudioEngine::loadDiagnosticBeep() {
    configureDecodeRate();auto tone=std::make_shared<AudioData>();
    const size_t frames=static_cast<size_t>(sampleRate_*.045);tone->pcm.resize(frames*2);
    for(size_t i=0;i<frames;++i) {
        const double envelope=std::min({1.0,i/(sampleRate_*.003),(frames-1-i)/(sampleRate_*.005)});
        const auto value=static_cast<int16_t>(std::sin(i*880*6.283185307179586/sampleRate_)*5000*envelope);
        tone->pcm[i*2]=tone->pcm[i*2+1]=value;
    }
    effects_[L"fps-drop"]=std::move(tone);
}

bool AudioEngine::prepare(const bms::Chart& chart, std::wstring& error, const std::atomic_bool* cancelled,
    bool tolerateDecodeFailures, size_t* decodeFailureCount) {
    configureDecodeRate();
    if (decodeFailureCount) *decodeFailureCount = 0;
    prepareCancelled_ = cancelled;
    releaseSong();
    durationSeconds_ = chart.durationSeconds;
    slicedChart_=chart.bmson;triggeredSlices_.clear();
    nextBgm_ = 0;
    triggeredSlices_.clear();
    size_t decodedBytes = 0;
    constexpr size_t totalLimit = 512ull * 1024ull * 1024ull;
    BMS_TRACE_ZONE("Audio prepare");
    std::unordered_map<std::wstring, std::shared_ptr<AudioData>> decodedFiles;
    for (const auto& [id, path] : chart.samples) {
        if (cancelled && cancelled->load()) { error = L"불러오기를 취소했습니다."; return false; }
        const auto pathKey = bms::toLower(path.lexically_normal().wstring());
        auto existing = decodedFiles.find(pathKey);
        if (existing != decodedFiles.end()) {
            samples_[id] = existing->second;
            continue;
        }
        auto audio = std::make_shared<AudioData>();
        if (!decodeFile(path, *audio, error)) {
            if (cancelled && cancelled->load()) return false;
            if (tolerateDecodeFailures) {
                if (decodeFailureCount) ++*decodeFailureCount;
                continue;
            }
            return false;
        }
        const size_t nextBytes = audio->pcm.size() * sizeof(int16_t);
        if (nextBytes > totalLimit - decodedBytes) {
            error = L"채보의 디코딩 음원이 전체 메모리 한도(512MB)를 넘었습니다.";
            return false;
        }
        decodedBytes += nextBytes;
        samples_[id] = audio;
        decodedFiles[pathKey] = std::move(audio);
    }
    for(auto& [id,audio]:samples_) {
        const auto key=chart.objectBase==62?id:bms::toLower(id);
        const auto fx=chart.sampleEffects.find(key);
        if(fx==chart.sampleEffects.end() && chart.wavVolume==100) continue;
        auto transformed=std::make_shared<AudioData>();
        const double pitch=fx==chart.sampleEffects.end()?1.0:std::pow(2.0,(fx->second.pitch-60)/12.0);
        const double gain=chart.wavVolume/100.0*(fx==chart.sampleEffects.end()?1.0:fx->second.volume/100.0);
        const size_t sourceFrames=audio->pcm.size()/channels_;
        size_t outputFrames=static_cast<size_t>(sourceFrames/pitch);
        if(fx!=chart.sampleEffects.end() && fx->second.durationMilliseconds>0)
            outputFrames=std::min(outputFrames,static_cast<size_t>(fx->second.durationMilliseconds*sampleRate_/1000));
        transformed->pcm.resize(outputFrames*channels_);
        for(size_t frame=0;frame<outputFrames;++frame) {
            const double at=frame*pitch;const auto first=std::min(sourceFrames-1,static_cast<size_t>(at));const auto second=std::min(first+1,sourceFrames-1);
            for(size_t c=0;c<channels_;++c) transformed->pcm[frame*channels_+c]=static_cast<int16_t>(std::clamp(
                (audio->pcm[first*channels_+c]+(audio->pcm[second*channels_+c]-audio->pcm[first*channels_+c])*(at-first))*gain,-32768.0,32767.0));
        }
        audio=std::move(transformed);
    }
    // bmson stems are decoded once, then sliced at authored pulse boundaries.
    for(const auto& [id,slice]:chart.sampleSlices) {
        const auto source=samples_.find(slice.source);if(source==samples_.end())continue;
        const auto& pcm=source->second->pcm;const size_t frames=pcm.size()/channels_;
        const size_t first=std::min(frames,static_cast<size_t>(std::max(0.0,slice.offset)*sampleRate_));
        const size_t last=slice.duration<0?frames:std::min(frames,first+static_cast<size_t>(std::max(0.0,slice.duration)*sampleRate_));
        auto sliced=std::make_shared<AudioData>();sliced->pcm.assign(pcm.begin()+first*channels_,pcm.begin()+last*channels_);samples_[id]=std::move(sliced);
    }
    for(const auto& [id,group]:chart.sampleGroups) {
        auto combined=std::make_shared<AudioData>();size_t size=0;
        for(const auto& member:group)if(samples_.contains(member))size=std::max(size,samples_[member]->pcm.size());
        combined->pcm.resize(size);std::set<std::wstring> seen;
        for(const auto& member:group)if(seen.insert(member).second && samples_.contains(member)) {
            const auto& pcm=samples_[member]->pcm;
            for(size_t i=0;i<pcm.size();++i)combined->pcm[i]=static_cast<int16_t>(std::clamp(int(combined->pcm[i])+int(pcm[i]),-32768,32767));
        }
        samples_[id]=std::move(combined);
    }
    for(const auto& note:chart.notes)if(samples_.contains(note.sampleId))
        durationSeconds_=std::max(durationSeconds_,note.timeSeconds+samples_[note.sampleId]->pcm.size()/double(sampleRate_*channels_));
    for (const auto& event : chart.bgm)
        bgm_.push_back({ static_cast<uint64_t>(std::max(0.0, event.timeSeconds) * sampleRate_), event.sampleId });
    std::stable_sort(bgm_.begin(), bgm_.end(), [](const BgmFrame& a, const BgmFrame& b) { return a.frame < b.frame; });
    for (const auto& event : bgm_) {
        if (!samples_.contains(event.id)) continue;
        const double sampleLength = static_cast<double>(samples_.at(event.id)->pcm.size()) / (sampleRate_ * channels_);
        durationSeconds_ = std::max(durationSeconds_, static_cast<double>(event.frame) / sampleRate_ + sampleLength);
    }
    error.clear();
    return true;
}

bool AudioEngine::start(std::wstring& error) { return startOutput(error, false); }
bool AudioEngine::preparePlaybackRate(double rate, std::wstring& error, const std::atomic_bool* cancelled) {
    if (!std::isfinite(rate) || rate < 0.1 || rate > 10.0 || running_) {
        error = L"배속 음원을 준비할 수 없는 상태입니다."; return false;
    }
    if (rate == preparedRate_) return true;
    playbackSamples_.clear(); preparedRate_ = 1.0;
    if (rate == 1.0) return true;
    try {
        std::unordered_map<const AudioData*, std::shared_ptr<AudioData>> unique;
        size_t total = 0;
        for (const auto& [id, sample] : samples_) {
            if (cancelled && cancelled->load()) { playbackSamples_.clear(); return false; }
            auto& stretched = unique[sample.get()];
            if (!stretched) {
                const double expectedBytes = sample->pcm.size()*sizeof(int16_t)/rate;
                if (expectedBytes > 1024.0*1024*1024 - total) throw std::length_error("stretched samples exceed memory budget");
                stretched = std::make_shared<AudioData>();
                if (!stretchPitchPreserving(sample->pcm, sampleRate_, rate, stretched->pcm, cancelled)) {
                    playbackSamples_.clear(); return false;
                }
                total += stretched->pcm.size()*sizeof(int16_t);
            }
            playbackSamples_[id] = stretched;
        }
        preparedRate_ = rate;
        return true;
    } catch (...) {
        playbackSamples_.clear();
        error = L"피치를 유지하는 배속 음원 준비에 실패했습니다.";
        return false;
    }
}
bool AudioEngine::startAt(double seconds, std::wstring& error, double playbackRate) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > durationSeconds_ ||
        !std::isfinite(playbackRate) || playbackRate < 0.1 || playbackRate > 10.0) {
        error = L"재생 시작 위치 또는 재생 속도가 올바르지 않습니다.";
        return false;
    }
    return startOutput(error, false, seconds, playbackRate);
}

bool AudioEngine::startEffectsOnly(std::wstring& error) { return startOutput(error, true); }

bool AudioEngine::pause() noexcept {
    if (!running_ || !audioClient_) return false;
    if (paused_) return true;
    pausedPositionSeconds_ = positionSeconds();
    pausedAt_ = std::chrono::steady_clock::now();
    paused_ = true;
    std::lock_guard lock(deviceMutex_);
    if (!audioClient_ || FAILED(audioClient_->Stop())) {
        paused_ = false;
        return false;
    }
    return true;
}

bool AudioEngine::resume() noexcept {
    if (!running_ || !audioClient_) return false;
    if (!paused_) return true;
    std::lock_guard lock(deviceMutex_);
    if (!audioClient_ || FAILED(audioClient_->Start())) return false;
    if (pausedAt_ != std::chrono::steady_clock::time_point{})
        playbackEpoch_ += std::chrono::steady_clock::now() - pausedAt_;
    pausedAt_ = {};
    paused_ = false;
    return true;
}

bool AudioEngine::startOutput(std::wstring& error, bool effectsOnly, double startSeconds, double playbackRate) {
    stop();
    if (!preparePlaybackRate(playbackRate, error)) return false;
    framesPerBuffer_ = requestedBufferFrames_.load();
    if ((effectsOnly && effects_.empty()) || (!effectsOnly && samples_.empty() && effects_.empty())) {
        error = L"재생할 음원이 없습니다."; return false;
    }
    effectsOnly_ = effectsOnly;
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (SUCCEEDED(hr)) hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (SUCCEEDED(hr)) hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&audioClient_));
    if (FAILED(hr)) { error = L"WASAPI 오디오 출력 장치를 열지 못했습니다."; stop(); return false; }
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = static_cast<WORD>(channels_);
    format.nSamplesPerSec = sampleRate_;
    playbackRate_ = playbackRate;
    triggeredSlices_.clear();
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(channels_ * sizeof(int16_t));
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    WAVEFORMATEX* deviceFormat{};
    audioClient_->GetMixFormat(&deviceFormat);
    const bool floatFormat = deviceFormat && deviceFormat->wBitsPerSample == 32 &&
        (deviceFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (deviceFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && deviceFormat->cbSize >= 22 &&
        IsEqualGUID(reinterpret_cast<WAVEFORMATEXTENSIBLE*>(deviceFormat)->SubFormat, MFAudioFormat_Float)));
    outputFloat_ = floatFormat && deviceFormat->nSamplesPerSec == format.nSamplesPerSec && deviceFormat->nChannels >= 1 && deviceFormat->nChannels <= 8;
    outputChannels_ = outputFloat_ ? deviceFormat->nChannels : channels_;
    WAVEFORMATEX* streamFormat = outputFloat_ ? deviceFormat : &format;
    const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | (outputFloat_ ? 0 : AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY);
    ComPtr<IAudioClient3> lowLatency;
    bool initialized = false;
    if (SUCCEEDED(audioClient_->QueryInterface(IID_PPV_ARGS(&lowLatency)))) {
        UINT32 defaultPeriod{}, fundamental{}, minimum{}, maximum{};
        hr = lowLatency->GetSharedModeEnginePeriod(streamFormat, &defaultPeriod, &fundamental, &minimum, &maximum);
        if (SUCCEEDED(hr) && fundamental > 0) {
            const UINT32 period = std::clamp(((framesPerBuffer_ + fundamental - 1) / fundamental) * fundamental, minimum, maximum);
            initialized = SUCCEEDED(lowLatency->InitializeSharedAudioStream(flags, period, streamFormat, nullptr));
        }
    }
    if (!initialized) hr = audioClient_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, streamFormat, nullptr);
    if (deviceFormat) CoTaskMemFree(deviceFormat);
    if (!initialized && FAILED(hr)) { error = L"WASAPI 오디오 버퍼를 준비하지 못했습니다."; stop(); return false; }
    audioEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    hr = audioEvent_ && stopEvent_ ? audioClient_->SetEventHandle(audioEvent_) : E_OUTOFMEMORY;
    if (SUCCEEDED(hr)) hr = audioClient_->GetBufferSize(&deviceBufferFrames_);
    if (SUCCEEDED(hr)) hr = audioClient_->GetService(IID_PPV_ARGS(&renderClient_));
    if (SUCCEEDED(hr)) hr = audioClient_->GetService(IID_PPV_ARGS(&audioClock_));
    if (SUCCEEDED(hr)) hr = audioClock_->GetFrequency(&clockFrequency_);
    if (FAILED(hr)) { error = L"WASAPI 출력 서비스를 준비하지 못했습니다."; stop(); return false; }
    LARGE_INTEGER qpcFrequency{};
    if (QueryPerformanceFrequency(&qpcFrequency) && qpcFrequency.QuadPart > 0)
        qpcFrequency_ = static_cast<uint64_t>(qpcFrequency.QuadPart);
    mixBuffer_.resize(static_cast<size_t>(deviceBufferFrames_) * channels_);
    voices_.reserve(512);
    transportStartFrame_ = static_cast<uint64_t>(startSeconds * sampleRate_);
    lastRawPositionSeconds_ = lastPositionSeconds_ = static_cast<double>(transportStartFrame_) / sampleRate_;
    useWallClock_ = false;
    nextFrame_ = static_cast<uint64_t>(std::llround(transportStartFrame_ / playbackRate_)); nextBgm_ = 0; voices_.clear();
    { std::lock_guard lock(triggerMutex_); pendingTriggers_.clear(); pendingEffect_.clear(); }
    running_ = true;
    if (!renderAvailable() || FAILED(audioClient_->Start())) {
        error = L"WASAPI 오디오 출력을 시작하지 못했습니다."; stop(); return false;
    }
    playbackEpoch_ = std::chrono::steady_clock::now();
    worker_ = std::thread(&AudioEngine::workerLoop, this);
    return true;
}

void AudioEngine::stop() {
    running_ = false;
    paused_ = false;
    pausedAt_ = {};
    pausedPositionSeconds_ = 0;
    if (stopEvent_) SetEvent(stopEvent_);
    if (worker_.joinable()) worker_.join();
    if (audioClient_) { audioClient_->Stop(); audioClient_->Reset(); }
    if (audioClock_) { audioClock_->Release(); audioClock_ = nullptr; }
    if (renderClient_) { renderClient_->Release(); renderClient_ = nullptr; }
    if (audioClient_) { audioClient_->Release(); audioClient_ = nullptr; }
    deviceBufferFrames_ = 0; clockFrequency_ = 0;
    if (audioEvent_) { CloseHandle(audioEvent_); audioEvent_ = nullptr; }
    if (stopEvent_) { CloseHandle(stopEvent_); stopEvent_ = nullptr; }
    mixBuffer_.clear(); voices_.clear();
    playbackEpoch_ = {};
    qpcFrequency_ = 0;
    lastRawPositionSeconds_ = lastPositionSeconds_ = 0;
    useWallClock_ = false;
    { std::lock_guard lock(triggerMutex_); pendingTriggers_.clear(); pendingEffect_.clear(); }
}

void AudioEngine::releaseSong() {
    stop();triggeredSlices_.clear();slicedChart_=false;
    playbackSamples_.clear(); playbackSamples_.rehash(0); preparedRate_ = 1.0;
    samples_.clear(); samples_.rehash(0);
    std::vector<BgmFrame>().swap(bgm_);
    std::vector<Voice>().swap(voices_);
    std::vector<float>().swap(mixBuffer_);
    durationSeconds_ = 0;
}

void AudioEngine::trigger(const std::wstring& sampleId) {
    if (!running_) return;
    std::unique_lock lock(triggerMutex_,std::defer_lock);
    {BMS_PERF_SCOPE(triggerLockMs);lock.lock();}
    pendingTriggers_.push_back(sampleId);
}

bool AudioEngine::triggerEffect(const std::wstring& effectId) {
    if (!running_) return false;
    const auto found = effects_.find(effectId);
    if (found == effects_.end()) return false;
    std::lock_guard lock(triggerMutex_);
    pendingEffect_.submit(effectId);
    return true;
}

void AudioEngine::setVolumes(int master, int music, int keys, int effects) noexcept {
    masterVolume_.store(std::clamp(master, 0, 100) / 100.0f, std::memory_order_relaxed);
    musicVolume_.store(std::clamp(music, 0, 100) / 100.0f, std::memory_order_relaxed);
    keyVolume_.store(std::clamp(keys, 0, 100) / 100.0f, std::memory_order_relaxed);
    effectVolume_.store(std::clamp(effects, 0, 100) / 100.0f, std::memory_order_relaxed);
}

bool AudioEngine::setBufferFrames(uint32_t frames) noexcept {
    switch (frames) {
    case 32: case 64: case 128: case 256: case 512: case 1024:
        requestedBufferFrames_.store(frames);
        return true;
    default: return false;
    }
}

double AudioEngine::positionSeconds() const {
    if (paused_) return pausedPositionSeconds_;
    if (!audioClient_ || playbackEpoch_ == std::chrono::steady_clock::time_point{}) return 0;
    const double startSeconds = static_cast<double>(transportStartFrame_) / sampleRate_;
    const double wallPositionSeconds = startSeconds + std::max(0.0,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - playbackEpoch_).count()) * playbackRate_;
    double positionSeconds = wallPositionSeconds;
    UINT64 position{}, qpcPosition{};
    if (audioClock_ && clockFrequency_ && SUCCEEDED(audioClock_->GetPosition(&position, &qpcPosition))) {
        // Use the stream clock for the sample currently reaching the output device;
        // wall time alone can run ahead of buffered audio and make BGA lead the song.
        lastRawPositionSeconds_ = std::max(lastRawPositionSeconds_, startSeconds + static_cast<double>(position) / clockFrequency_ * playbackRate_);
        double audioPositionSeconds = lastRawPositionSeconds_;

        // GetPosition returns a QPC timestamp paired with its stream position.
        // Interpolate only from that timestamp to keep the rendered playhead smooth.
        LARGE_INTEGER qpcNow{};
        if (qpcPosition && qpcFrequency_ && QueryPerformanceCounter(&qpcNow)) {
            const double now100ns = static_cast<double>(qpcNow.QuadPart) * 10'000'000.0 / qpcFrequency_;
            const double elapsedSinceSample = (now100ns - static_cast<double>(qpcPosition)) / 10'000'000.0;
            audioPositionSeconds += std::clamp(elapsedSinceSample, 0.0, 0.05) * playbackRate_;
        }
        // An output underrun can stop the stream clock while the device emits silence.
        // Preserve the game's real-time transport, then keep it on wall time for this run.
        if (!useWallClock_ && wallPositionSeconds - audioPositionSeconds > 0.10 * playbackRate_)
            useWallClock_ = true;
        if (!useWallClock_) positionSeconds = audioPositionSeconds;
    }
    // Keep transport monotonic if the endpoint briefly reports an older sample position.
    lastPositionSeconds_ = std::max(lastPositionSeconds_, positionSeconds);
    return lastPositionSeconds_;
}

bool AudioEngine::renderAvailable() {
    BMS_TRACE_ZONE("WASAPI render buffer");
    UINT32 padding{};
    if (FAILED(audioClient_->GetCurrentPadding(&padding)) || padding > deviceBufferFrames_) return false;
#ifdef BMS_PERF_DIAGNOSTICS
    if (padding == 0 && running_.load() && !paused_.load()) ++emptyPaddingEvents_;
#endif
    const UINT32 available = deviceBufferFrames_ - padding;
    if (!available) return true;
    if (playbackEpoch_ != std::chrono::steady_clock::time_point{}) {
        const auto elapsed = static_cast<uint64_t>(std::chrono::duration<double>(std::chrono::steady_clock::now() - playbackEpoch_).count() * sampleRate_);
        const auto outputStart = static_cast<uint64_t>(std::llround(transportStartFrame_ / playbackRate_));
        if (outputStart + elapsed + padding > nextFrame_.load()) nextFrame_ = outputStart + elapsed + padding;
    }
    BYTE* output{};
    if (FAILED(renderClient_->GetBuffer(available, &output))) return false;
    for (uint32_t offset = 0; offset < available;) {
        const uint32_t frames = std::min(framesPerBuffer_, available - offset);
        if (outputFloat_) {
            fillBuffer(nullptr, frames);
            auto* destination = reinterpret_cast<float*>(output) + static_cast<size_t>(offset) * outputChannels_;
            std::fill_n(destination, static_cast<size_t>(frames) * outputChannels_, 0.0f);
            for (uint32_t i = 0; i < frames; ++i) {
                const float left = std::clamp(mixBuffer_[i * 2] / 32768.0f, -1.0f, 1.0f);
                const float right = std::clamp(mixBuffer_[i * 2 + 1] / 32768.0f, -1.0f, 1.0f);
                destination[i * outputChannels_] = outputChannels_ == 1 ? (left + right) * 0.5f : left;
                if (outputChannels_ > 1) destination[i * outputChannels_ + 1] = right;
            }
        } else fillBuffer(reinterpret_cast<int16_t*>(output) + static_cast<size_t>(offset) * channels_, frames);
        offset += frames;
    }
    return SUCCEEDED(renderClient_->ReleaseBuffer(available, 0));
}

void AudioEngine::fillBuffer(int16_t* target, uint32_t frames) {
#ifdef BMS_PERF_DIAGNOSTICS
    const auto audioStart=perf::Clock::now();
    struct AudioCost {perf::Clock::time_point start;~AudioCost(){if(perf::detailed){perf::audioMixNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(perf::Clock::now()-start).count(),std::memory_order_relaxed);perf::audioCalls.fetch_add(1,std::memory_order_relaxed);}}} audioCost{audioStart};
#endif

    BMS_TRACE_ZONE("Audio mix");
    const uint64_t start = nextFrame_.load();
    const uint64_t end = start + frames;
    std::fill_n(mixBuffer_.data(), static_cast<size_t>(frames) * channels_, 0.0f);

    const auto& songSamples = playbackRate_ == 1.0 ? samples_ : playbackSamples_;
    while (!effectsOnly_ && nextBgm_ < bgm_.size() && bgm_[nextBgm_].frame / playbackRate_ < end) {
        const auto& event = bgm_[nextBgm_++];
        if (songSamples.contains(event.id) && (!slicedChart_ || triggeredSlices_.insert(event.id).second)) voices_.push_back({ songSamples.at(event.id).get(), static_cast<uint64_t>(std::llround(event.frame / playbackRate_)), false, false });
    }
    {
#ifdef BMS_PERF_DIAGNOSTICS
        const auto lockStart=perf::Clock::now();
#endif
        std::lock_guard lock(triggerMutex_);
#ifdef BMS_PERF_DIAGNOSTICS
        if(perf::detailed)perf::audioLockNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(perf::Clock::now()-lockStart).count(),std::memory_order_relaxed);
#endif
        if (!effectsOnly_) for (const auto& id : pendingTriggers_) if (songSamples.contains(id) && (!slicedChart_ || triggeredSlices_.insert(id).second)) voices_.push_back({ songSamples.at(id).get(), start, true, false });
        pendingTriggers_.clear();
        const auto latestEffect = pendingEffect_.take();
        if (!latestEffect.empty()) {
            // Stop the previous effect voice as soon as the next buffer is mixed.
            voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& voice) { return voice.effectSound; }), voices_.end());
            const auto effect = effects_.find(latestEffect);
            if (effect != effects_.end()) voices_.push_back({ effect->second.get(), start, false, true });
        }
    }

    const float master = masterVolume_.load(std::memory_order_relaxed);
    const float music = master * musicVolume_.load(std::memory_order_relaxed);
    const float keys = master * keyVolume_.load(std::memory_order_relaxed);
    const float effects = master * effectVolume_.load(std::memory_order_relaxed);
    // Mix contiguous spans per voice, with one gain snapshot per block.
    for (const auto& voice : voices_) {
        if (!voice.data || voice.startFrame >= end) continue;
        const uint64_t first = std::max(start, voice.startFrame);
        const uint64_t last = std::min(end, voice.startFrame + voice.data->pcm.size() / channels_);
        if (first >= last) continue;
        const float gain = voice.effectSound ? effects : voice.keySound ? keys : music;
        const auto* source = voice.data->pcm.data() + static_cast<size_t>(first - voice.startFrame) * channels_;
        auto* destination = mixBuffer_.data() + static_cast<size_t>(first - start) * channels_;
        const size_t count = static_cast<size_t>(last - first) * channels_;
        for (size_t i = 0; i < count; ++i) destination[i] += source[i] * gain;
    }
    if (target) for (size_t i = 0, count = static_cast<size_t>(frames) * channels_; i < count; ++i)
        target[i] = static_cast<int16_t>(std::clamp(mixBuffer_[i], -32768.0f, 32767.0f));
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [end](const Voice& voice) {
        return !voice.data || voice.startFrame + voice.data->pcm.size() / channels_ <= end;
    }), voices_.end());
    nextFrame_ = end;
}

void AudioEngine::workerLoop() {
    BMS_TRACE_THREAD("WASAPI audio");
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD taskIndex{};
    HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    if (task) AvSetMmThreadPriority(task, AVRT_PRIORITY_CRITICAL);
    HANDLE waitHandles[] = {stopEvent_, audioEvent_};
    while (running_) {
        const DWORD result = WaitForMultipleObjects(2, waitHandles, FALSE, 100);
        if (result == WAIT_OBJECT_0 || !running_) break;
        if (result != WAIT_OBJECT_0 + 1 && result != WAIT_TIMEOUT) continue;
        std::lock_guard lock(deviceMutex_);
        if (paused_) continue;
        if (!renderAvailable()) {
#ifdef BMS_PERF_DIAGNOSTICS
            ++renderFailures_;
#endif
            running_ = false; break;
        }
    }
    if (task) AvRevertMmThreadCharacteristics(task);
    if (SUCCEEDED(com)) CoUninitialize();
}
