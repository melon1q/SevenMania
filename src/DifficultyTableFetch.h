#pragma once
// Downloads a difficulty table with WinHTTP (built into Windows): the page or header the player
// entered, then the header's data_url. Runs on a worker thread; never touches UI state.
#include "DifficultyTable.h"
#include <winhttp.h>
#include <atomic>
#include <string>
#pragma comment(lib, "winhttp.lib")

namespace bms::table {
inline constexpr size_t maxDownloadBytes = 64u << 20; // the largest public tables are a few MB

// GET over http or https, following redirects. On failure returns false and a short reason.
inline bool httpGet(const std::wstring& url, std::string& body, std::wstring& error, const std::atomic_bool* cancel = nullptr) {
    body.clear();
    URL_COMPONENTS parts{sizeof(parts)};
    wchar_t host[256]{}, path[4096]{};
    parts.lpszHostName = host; parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path; parts.dwUrlPathLength = 4096;
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) || (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS)) {
        error = L"주소 형식이 올바르지 않습니다 (http:// 또는 https://)"; return false;
    }
    std::wstring object(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.lpszExtraInfo && parts.dwExtraInfoLength) object.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (object.empty()) object = L"/";
    struct Handle { HINTERNET h{}; ~Handle() { if (h) WinHttpCloseHandle(h); } };
    Handle session{WinHttpOpen(L"SevenMania-DifficultyTable/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) session.h = WinHttpOpen(L"SevenMania-DifficultyTable/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) { error = L"인터넷 연결을 준비하지 못했습니다"; return false; }
    WinHttpSetTimeouts(session.h, 10000, 10000, 15000, 30000);
    Handle connection{WinHttpConnect(session.h, std::wstring(parts.lpszHostName, parts.dwHostNameLength).c_str(), parts.nPort, 0)};
    if (!connection.h) { error = L"서버에 연결하지 못했습니다"; return false; }
    Handle request{WinHttpOpenRequest(connection.h, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)};
    if (!request.h) { error = L"요청을 만들지 못했습니다"; return false; }
    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.h, nullptr)) {
        error = L"응답을 받지 못했습니다 (오류 " + std::to_wstring(GetLastError()) + L")"; return false;
    }
    DWORD status{}, size = sizeof(status);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) { error = L"서버가 " + std::to_wstring(status) + L" 상태로 응답했습니다"; return false; }
    for (;;) {
        if (cancel && cancel->load()) { error = L"취소했습니다"; return false; }
        DWORD available{};
        if (!WinHttpQueryDataAvailable(request.h, &available)) { error = L"내려받는 중 연결이 끊겼습니다"; return false; }
        if (!available) break;
        if (body.size() + available > maxDownloadBytes) { error = L"파일이 너무 큽니다"; return false; }
        const size_t at = body.size();
        body.resize(at + available);
        DWORD read{};
        if (!WinHttpReadData(request.h, body.data() + at, available, &read)) { error = L"내려받는 중 연결이 끊겼습니다"; return false; }
        body.resize(at + read);
    }
    return true;
}

struct FetchResult { Table table; std::string header, data; std::wstring error; bool ok{}; };

// The entered URL may be the table page (with the bmstable meta tag) or the header JSON itself.
inline FetchResult fetchTable(const std::wstring& url, const std::atomic_bool* cancel = nullptr) {
    FetchResult result;
    result.table.url = trimmed(url);
    std::string first;
    if (!httpGet(result.table.url, first, result.error, cancel)) return result;
    const auto firstText = decodeText(first);
    const auto start = firstText.find_first_not_of(L" \t\r\n");
    if (start != std::wstring::npos && firstText[start] == L'{') {
        result.table.headerUrl = result.table.url;
        result.header = std::move(first);
    } else {
        const auto meta = findBmstableMeta(firstText);
        if (!meta) { result.error = L"난이도표 페이지가 아닙니다 (bmstable 메타 태그 없음)"; return result; }
        result.table.headerUrl = resolveUrl(result.table.url, *meta);
        if (!httpGet(result.table.headerUrl, result.header, result.error, cancel)) { result.error = L"헤더: " + result.error; return result; }
    }
    if (!parseHeader(decodeText(result.header), result.table)) { result.error = L"헤더 JSON을 읽지 못했습니다"; return result; }
    const auto dataUrl = resolveUrl(result.table.headerUrl, result.table.dataUrl);
    if (!httpGet(dataUrl, result.data, result.error, cancel)) { result.error = L"데이터: " + result.error; return result; }
    if (!parseData(decodeText(result.data), result.table)) { result.error = L"데이터 JSON을 읽지 못했습니다"; return result; }
    result.ok = true;
    return result;
}
} // namespace bms::table
