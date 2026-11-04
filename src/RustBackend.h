#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
namespace bms { struct JsonValue; namespace rust_backend {
bool enabled() noexcept;
// Validation only: change before starting workers, never during an active scan.
void setEnabledForValidation(bool value) noexcept;
uint64_t successes(unsigned operation) noexcept;
bool splitLines(std::wstring_view text, std::vector<std::wstring>& out);
bool readJson(std::wstring_view text, JsonValue& out);
bool discoverCharts(const std::filesystem::path& root, const std::atomic_bool* cancel, std::vector<std::filesystem::path>& out);
bool normalizeTitle(std::wstring_view title, std::wstring& out);
}} // namespace bms::rust_backend
