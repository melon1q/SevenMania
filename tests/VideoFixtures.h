#pragma once
#include "VideoBga.h"
#include <vfw.h>
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

}
