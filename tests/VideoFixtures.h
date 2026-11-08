#pragma once
#include "VideoBga.h"
#include <vfw.h>
#include <mfapi.h>
#include <mfreadwrite.h>
#include <stdexcept>
namespace fixtures {
inline void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
// A tiny uncompressed clip keeps the test independent of external encoders,
// personal song libraries, and lossy color conversion.
inline void writeVideo(const std::filesystem::path& path, LONG startFrame = 0) {
    ComPtr<IAVIFile> file;
    check(SUCCEEDED(AVIFileOpenW(&file, path.c_str(), OF_CREATE | OF_WRITE, nullptr)), "create AVI fixture");
    AVISTREAMINFOW streamInfo{};
    streamInfo.fccType = streamtypeVIDEO;
    streamInfo.dwScale = 1; streamInfo.dwRate = 10;
    streamInfo.dwStart = startFrame;
    streamInfo.rcFrame = {0, 0, 16, 16};
    ComPtr<IAVIStream> stream;
    check(SUCCEEDED(AVIFileCreateStreamW(file.Get(), &stream, &streamInfo)), "create video stream");
    BITMAPINFOHEADER format{};
    format.biSize = sizeof(format); format.biWidth = 16; format.biHeight = 16;
    format.biPlanes = 1; format.biBitCount = 32; format.biCompression = BI_RGB;
    format.biSizeImage = 16 * 16 * 4;
    check(SUCCEEDED(AVIStreamSetFormat(stream.Get(), 0, &format, sizeof(format))), "set video format");
    for (LONG index = 0; index < 30; ++index) {
        const uint32_t color = index < 10 ? 0xffff0000u : index < 20 ? 0xff00ff00u : 0xff0000ffu;
        std::vector<uint32_t> pixels(16 * 16, color);
        check(SUCCEEDED(AVIStreamWrite(stream.Get(), index + startFrame, 1, pixels.data(), format.biSizeImage,
            AVIIF_KEYFRAME, nullptr, nullptr)), "write video frame");
    }
}

// H.264 MP4 through the OS encoder (decoders output NV12). Same colors and timing as
// writeVideo(); returns false when this Windows has no H.264 encoder.
inline bool writeH264(const std::filesystem::path& path) {
    constexpr UINT32 width = 64, height = 48;
    ComPtr<IMFSinkWriter> writer;
    if (FAILED(MFCreateSinkWriterFromURL(path.c_str(), nullptr, nullptr, &writer))) return false;
    ComPtr<IMFMediaType> output, input;
    MFCreateMediaType(&output); MFCreateMediaType(&input);
    output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video); output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    output->SetUINT32(MF_MT_AVG_BITRATE, 2'000'000); output->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(output.Get(), MF_MT_FRAME_SIZE, width, height); MFSetAttributeRatio(output.Get(), MF_MT_FRAME_RATE, 10, 1);
    MFSetAttributeRatio(output.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video); input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    input->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(input.Get(), MF_MT_FRAME_SIZE, width, height); MFSetAttributeRatio(input.Get(), MF_MT_FRAME_RATE, 10, 1);
    MFSetAttributeRatio(input.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    DWORD stream{};
    if (FAILED(writer->AddStream(output.Get(), &stream)) || FAILED(writer->SetInputMediaType(stream, input.Get(), nullptr)) ||
        FAILED(writer->BeginWriting())) return false;
    for (LONGLONG index = 0; index < 30; ++index) {
        const uint32_t color = index < 10 ? 0xffff0000u : index < 20 ? 0xff00ff00u : 0xff0000ffu;
        ComPtr<IMFMediaBuffer> buffer; ComPtr<IMFSample> sample;
        if (FAILED(MFCreateMemoryBuffer(width * height * 4, &buffer))) return false;
        BYTE* bytes{};
        buffer->Lock(&bytes, nullptr, nullptr);
        std::fill_n(reinterpret_cast<uint32_t*>(bytes), width * height, color);
        buffer->Unlock(); buffer->SetCurrentLength(width * height * 4);
        MFCreateSample(&sample); sample->AddBuffer(buffer.Get());
        sample->SetSampleTime(index * 1'000'000); sample->SetSampleDuration(1'000'000);
        if (FAILED(writer->WriteSample(stream, sample.Get()))) return false;
    }
    return SUCCEEDED(writer->Finalize());
}

}
