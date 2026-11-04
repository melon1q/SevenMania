#include "SongCatalog.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>

#include <windows.h>

namespace bms {
namespace {

std::string toUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) throw std::runtime_error("could not encode catalog text as UTF-8");
    std::string result(static_cast<size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), length, nullptr, nullptr) != length)
        throw std::runtime_error("could not encode catalog text as UTF-8");
    return result;
}

void appendString(std::string& output, const std::wstring& value) {
    output.push_back('"');
    for (const unsigned char byte : toUtf8(value)) {
        switch (byte) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (byte < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                output += "\\u00";
                output.push_back(hex[byte >> 4]);
                output.push_back(hex[byte & 0xf]);
            } else output.push_back(static_cast<char>(byte));
        }
    }
    output.push_back('"');
}

void appendNumber(std::string& output, double value) {
    if (!std::isfinite(value)) {
        output += "null";
        return;
    }
    char buffer[64]{};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
        std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (result.ec != std::errc{}) throw std::runtime_error("could not encode catalog number");
    output.append(buffer, result.ptr);
}

void appendBool(std::string& output, bool value) {
    output += value ? "true" : "false";
}

std::wstring modeName(ChartMode mode) {
    switch (mode) {
    case ChartMode::FiveKey: return L"FiveKey";
    case ChartMode::SevenKey: return L"SevenKey";
    case ChartMode::TenKey: return L"TenKey";
    case ChartMode::FourteenKey: return L"FourteenKey";
    case ChartMode::PopnNine: return L"PopnNine";
    case ChartMode::Other: return L"Other";
    default: return L"Unknown";
    }
}

void appendChart(std::string& output, const Chart& chart) {
    output += "{\"id\":";
    appendString(output, chart.path.generic_wstring());
    output += ",\"path\":";
    appendString(output, chart.path.generic_wstring());
    output += ",\"title\":";
    appendString(output, chart.title);
    output += ",\"subtitle\":";
    appendString(output, chart.subtitle);
    output += ",\"artist\":";
    appendString(output, chart.artist);
    output += ",\"genre\":";
    appendString(output, chart.genre);
    output += ",\"playLevel\":";
    appendString(output, chart.playLevel);
    output += ",\"mode\":";
    appendString(output, modeName(chart.mode));
    output += ",\"modeLabel\":";
    appendString(output, BmsParser::modeLabel(chart.mode));
    output += ",\"modeOrder\":" + std::to_string(static_cast<int>(chart.mode));
    output += ",\"playable\":";
    appendBool(output, chart.playable);
    output += ",\"blockReason\":";
    appendString(output, chart.blockReason);
    output += ",\"partialLoad\":";
    appendBool(output, chart.partialLoad);
    output += ",\"partialLoadReason\":";
    appendString(output, chart.partialLoadReason);
    output += ",\"initialBpm\":";
    appendNumber(output, chart.initialBpm);
    output += ",\"durationSeconds\":";
    appendNumber(output, chart.durationSeconds);
    output += ",\"previewStartDefined\":";
    appendBool(output, chart.previewStartDefined);
    output += ",\"previewStartSeconds\":";
    appendNumber(output, chart.previewStartSeconds);
    output += ",\"previewDurationSeconds\":";
    appendNumber(output, chart.previewDurationSeconds);
    output += ",\"bannerPath\":";
    appendString(output, chart.banner.generic_wstring());
    output += ",\"titleImagePath\":";
    appendString(output, chart.titleImage.generic_wstring());
    output.push_back('}');
}

} // namespace

std::string serializeSongCatalog(const std::vector<Chart>& charts,
    const std::filesystem::path& libraryRoot, uint64_t parseSeed) {
    std::string output;
    output.reserve(charts.size() * 384 + 128);
    output += "{\"schemaVersion\":1,\"libraryRoot\":";
    appendString(output, libraryRoot.generic_wstring());
    output += ",\"parseSeed\":" + std::to_string(parseSeed) + ",\"charts\":[";
    bool first = true;
    for (const auto& chart : charts) {
        if (!first) output.push_back(',');
        first = false;
        appendChart(output, chart);
    }
    output += "]}";
    return output;
}

} // namespace bms
