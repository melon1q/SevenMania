#pragma once

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>
#include "Mpeg1Video.h"

using Microsoft::WRL::ComPtr;

class VideoBga {
    static constexpr DWORD videoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
    static constexpr DWORD mediaSource = static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE);
    struct Frame { LONGLONG time{}; std::vector<uint32_t> pixels; };
    ComPtr<IMFSourceReader> reader_;
    Mpeg1Video mpeg1_;
    Frame current_, pending_;
    bool hasPending_{};
    UINT32 width_{}, height_{};
    int stride_{};
    LONGLONG duration_{};
    double frameRate_=30;
    LONGLONG timestampOrigin_{};
    bool hasTimestampOrigin_{};

    bool readFrame(Frame& frame, bool& endOfStream) {
        endOfStream = false;
        if(mpeg1_.ready()) {
            double time{};
            if(!mpeg1_.read(frame.pixels,time)){endOfStream=true;return false;}
            frame.time=static_cast<LONGLONG>(std::llround(time*10'000'000.0));
            hasTimestampOrigin_=true;timestampOrigin_=0;return true;
        }
        for (int attempt = 0; attempt < 8; ++attempt) {
            DWORD flags{};
            LONGLONG timestamp{};
            ComPtr<IMFSample> sample;
            const HRESULT hr = reader_->ReadSample(videoStream, 0, nullptr, &flags, &timestamp, &sample);
            if (FAILED(hr)) return false;
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { endOfStream = true; return false; }
            if (!sample) continue;
            ComPtr<IMFMediaBuffer> buffer;
            if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) return false;
            BYTE* bytes{}; DWORD maximum{}, length{};
            if (FAILED(buffer->Lock(&bytes, &maximum, &length))) return false;
            const size_t rowBytes = static_cast<size_t>(width_) * 4;
            const size_t pitch = static_cast<size_t>(std::abs(stride_));
            if (pitch < rowBytes || length < pitch * height_) { buffer->Unlock(); return false; }
            frame.pixels.resize(static_cast<size_t>(width_) * height_);
            for (UINT32 y = 0; y < height_; ++y) {
                const UINT32 sourceY = stride_ < 0 ? height_ - 1 - y : y;
                const auto* row = reinterpret_cast<const uint32_t*>(bytes + static_cast<size_t>(sourceY) * pitch);
                auto* output = frame.pixels.data() + static_cast<size_t>(y) * width_;
                for (UINT32 x = 0; x < width_; ++x) output[x] = row[x] | 0xff000000u;
            }
            buffer->Unlock();
            // MPEG/MP4 files can begin with a positive container timestamp.
            // That is not a delay authored into the BMS video event.
            if (!hasTimestampOrigin_) {
                timestampOrigin_ = std::max<LONGLONG>(0, timestamp);
                hasTimestampOrigin_ = true;
            }
            frame.time = timestamp - timestampOrigin_;
            return true;
        }
        return false;
    }

    bool seekStart(LONGLONG time = 0) {
        if(mpeg1_.ready())return mpeg1_.rewind();
        PROPVARIANT position{};
        PropVariantInit(&position);
        position.vt = VT_I8;
        position.hVal.QuadPart = time;
        const HRESULT hr = reader_->SetCurrentPosition(GUID_NULL, position);
        PropVariantClear(&position);
        return SUCCEEDED(hr);
    }

public:
    void reset() {
        reader_.Reset();mpeg1_.reset(); current_ = {}; pending_ = {}; hasPending_ = false;
        width_ = height_ = 0; stride_ = 0; duration_ = 0;
        timestampOrigin_ = 0; hasTimestampOrigin_ = false;frameRate_=30;
    }

    bool open(const std::filesystem::path& path) {
        reset();
        const auto native=[&]() -> bool {
            ComPtr<IMFAttributes> attributes;
            if (FAILED(MFCreateAttributes(&attributes, 2)) || FAILED(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE)) ||
                FAILED(MFCreateSourceReaderFromURL(path.c_str(), attributes.Get(), &reader_))) { reset(); return false; }
            if (FAILED(reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE)) ||
                FAILED(reader_->SetStreamSelection(videoStream, TRUE))) { reset(); return false; }
            ComPtr<IMFMediaType> output;
            if (FAILED(MFCreateMediaType(&output)) || FAILED(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) ||
                FAILED(output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) ||
                FAILED(reader_->SetCurrentMediaType(videoStream, nullptr, output.Get()))) { reset(); return false; }
            ComPtr<IMFMediaType> actual;
            if (FAILED(reader_->GetCurrentMediaType(videoStream, &actual)) ||
                FAILED(MFGetAttributeSize(actual.Get(), MF_MT_FRAME_SIZE, &width_, &height_)) || width_ == 0 || height_ == 0 ||
                static_cast<uint64_t>(width_) * height_ * 4 > 256ull * 1024 * 1024) { reset(); return false; }
            UINT32 numerator{},denominator{};
            if(SUCCEEDED(MFGetAttributeRatio(actual.Get(),MF_MT_FRAME_RATE,&numerator,&denominator)) && numerator&&denominator)frameRate_=static_cast<double>(numerator)/denominator;
            UINT32 stride{};
            if (SUCCEEDED(actual->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))) stride_ = static_cast<int32_t>(stride);
            else stride_ = static_cast<int>(width_ * 4);
            PROPVARIANT duration{};
            PropVariantInit(&duration);
            if (SUCCEEDED(reader_->GetPresentationAttribute(mediaSource, MF_PD_DURATION, &duration)) && duration.vt == VT_UI8)
                duration_ = static_cast<LONGLONG>(duration.uhVal.QuadPart);
            PropVariantClear(&duration);
            return true;
        };
        if(native())return true;
        reset();
        if(!mpeg1_.open(path))return false;
        width_=mpeg1_.width();height_=mpeg1_.height();stride_=int(width_*4);frameRate_=mpeg1_.frameRate();
        return true;
    }

    bool update(double seconds, bool loop = true) {
        if (!reader_&&!mpeg1_.ready()) return false;
        bool changed = false;
        const bool firstUpdate = !hasTimestampOrigin_;
        // Establish the original stream timestamp before a preview/seek jumps
        // ahead, otherwise that seek's first sample would become time zero.
        if (firstUpdate) {
            bool eos{};
            if (!readFrame(current_, eos)) return false;
            changed = true;
        }
        const LONGLONG target = duration_ > 0
            ? static_cast<LONGLONG>((loop ? std::fmod(std::max(0.0, seconds),duration_/10'000'000.0) : std::clamp(seconds,0.0,std::max(0.0,duration_/10'000'000.0-0.000001))) * 10'000'000.0)
            : static_cast<LONGLONG>(std::max(0.0, seconds) * 10'000'000.0);
        if ((!current_.pixels.empty() && target < current_.time) ||
            (firstUpdate && target > 10'000'000)) {
            if (!seekStart(target)) return false;
            current_ = {};
            pending_ = {}; hasPending_ = false;
        }
        for (;;) {
            if (hasPending_) {
                if (pending_.time > target) break;
                current_ = std::move(pending_); pending_ = {}; hasPending_ = false; changed = true;
            }
            bool eos{};
            Frame next;
            if (readFrame(next, eos)) { pending_ = std::move(next); hasPending_ = true; continue; }
            if (eos && loop && duration_ <= 0 && !current_.pixels.empty()) {
                duration_ = current_.time + static_cast<LONGLONG>(std::llround(10'000'000.0/frameRate_));
                if(target>=duration_&&seekStart()) {
                    current_={};pending_={};hasPending_=false;
                    return update(seconds,loop);
                }
            }
            break; // The next clock update handles looping through the normal modulo/rewind path.
        }
        return changed;
    }

    const std::vector<uint32_t>& pixels() const { return current_.pixels; }
    double frameRate() const{return frameRate_;}
    double positionSeconds() const{return current_.time/10'000'000.0;}
    int width() const { return static_cast<int>(width_); }
    int height() const { return static_cast<int>(height_); }
    bool ready() const { return !current_.pixels.empty(); }
};
