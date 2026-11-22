#pragma once
// MD5 and SHA-256 of a chart file's raw bytes, as lowercase hex.
// These identify a chart independently of where it is stored: play records are keyed by
// SHA-256, and BMS difficulty tables / courses look charts up by MD5 or SHA-256.
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <string>
#include <vector>
#pragma comment(lib, "bcrypt.lib")

namespace bms {
struct ChartHashes { std::wstring md5, sha256; };

namespace detail {
inline std::wstring bcryptHexDigest(LPCWSTR algorithm, const void* data, size_t size) {
    BCRYPT_ALG_HANDLE provider{};
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&provider, algorithm, nullptr, 0))) return {};
    std::wstring hex;
    DWORD length{}, written{};
    if (BCRYPT_SUCCESS(BCryptGetProperty(provider, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&length), sizeof(length), &written, 0))
        && length > 0 && length <= 64) {
        std::vector<UCHAR> digest(length);
        BCRYPT_HASH_HANDLE hash{};
        bool ok = BCRYPT_SUCCESS(BCryptCreateHash(provider, &hash, nullptr, 0, nullptr, 0, 0));
        // BCryptHashData takes a ULONG length; feed large inputs in pieces.
        const auto* bytes = static_cast<const UCHAR*>(data);
        for (size_t offset = 0; ok && offset < size;) {
            const ULONG chunk = static_cast<ULONG>(std::min<size_t>(size - offset, 1u << 30));
            ok = BCRYPT_SUCCESS(BCryptHashData(hash, const_cast<PUCHAR>(bytes + offset), chunk, 0));
            offset += chunk;
        }
        ok = ok && BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(), length, 0));
        if (hash) BCryptDestroyHash(hash);
        if (ok) {
            static constexpr wchar_t digits[] = L"0123456789abcdef";
            hex.reserve(length * 2);
            for (UCHAR byte : digest) { hex.push_back(digits[byte >> 4]); hex.push_back(digits[byte & 15]); }
        }
    }
    BCryptCloseAlgorithmProvider(provider, 0);
    return hex;
}
} // namespace detail

inline ChartHashes chartHashes(const void* data, size_t size) {
    return { detail::bcryptHexDigest(BCRYPT_MD5_ALGORITHM, data, size), detail::bcryptHexDigest(BCRYPT_SHA256_ALGORITHM, data, size) };
}
inline ChartHashes chartHashes(const std::vector<char>& bytes) { return chartHashes(bytes.data(), bytes.size()); }
} // namespace bms
