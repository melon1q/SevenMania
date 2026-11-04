#pragma once
#include "BmsParser.h"
#include <cstdint>
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

inline bool copyUserBackgroundImage(const std::filesystem::path& source, const std::filesystem::path& assets,
    std::wstring& savedName, std::wstring& message) {
    {
        Gdiplus::Bitmap image(source.c_str());
        if (image.GetLastStatus() != Gdiplus::Ok || image.GetWidth() == 0 || image.GetHeight() == 0 ||
            static_cast<uint64_t>(image.GetWidth()) * image.GetHeight() > 16ull * 1024 * 1024) {
            message = L"이미지 파일을 불러올 수 없습니다."; return false;
        }
    }
    const auto filename = L"browse-background" + bms::toLower(source.extension().wstring());
    const auto destination = assets / filename;
    std::error_code error;
    std::filesystem::create_directories(assets, error);
    if (!error && _wcsicmp(std::filesystem::absolute(source).lexically_normal().c_str(),
        std::filesystem::absolute(destination).lexically_normal().c_str()) != 0)
        std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, error);
    if (error) { message = L"Assets 폴더에 이미지를 복사하지 못했습니다."; return false; }
    savedName = filename; return true;
}
