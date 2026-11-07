#pragma once
#include "SkinPackage.h"
#include <stdexcept>

namespace sevenmania::skin::detail {
struct Failure { SkinError error; };
[[noreturn]] inline void fail(ErrorCode code, const std::string& path, const std::string& message) {
    throw Failure{{code, path, message}};
}
std::wstring wide(const std::string& text, const std::string& path);
std::string utf8(const std::wstring& text);
std::string safePath(const std::string& path);
bms::JsonValue json(const Bytes& data, const std::string& path, const Limits& limits);
using Archive = std::map<std::string, std::shared_ptr<const Bytes>>;
Archive readZip(const std::filesystem::path& file, const Limits& limits);
void imageInfo(SkinResource& resource, const Limits& limits);
}
