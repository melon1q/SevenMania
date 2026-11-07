#pragma once
#include "BmsParser.h"
#include <atomic>
#include <functional>
#include <stdexcept>
#include <vector>

namespace bms {
inline bool libraryCacheStartAllowed(const std::vector<std::filesystem::path>& sources,const std::filesystem::path& root){
    return sources.empty()||(sources.size()==1&&toLower(std::filesystem::absolute(sources.front()).lexically_normal().wstring())==toLower(std::filesystem::absolute(root).lexically_normal().wstring()));
}
using LibraryProgress = std::function<void(size_t completed, size_t total)>;
class LibraryError : public std::runtime_error {
public:
    std::wstring message;
    explicit LibraryError(std::wstring text) : std::runtime_error("song library error"), message(std::move(text)) {}
};
struct ImportedFolder {
    std::filesystem::path folder;
    size_t copiedFiles{}, unchangedFiles{};
    std::vector<std::filesystem::path> skippedFiles;
};
ImportedFolder importSongFolder(const std::filesystem::path& source, const std::filesystem::path& libraryRoot,
    const std::atomic_bool* cancelled = nullptr, const LibraryProgress& progress = {});
struct ImportedSongs {
    size_t importedFolders{}, copiedFiles{}, unchangedFiles{};
    std::vector<std::filesystem::path> skippedFiles;
    std::vector<std::wstring> errors;
};
// Chart selections import their containing folder, including shared media.
ImportedSongs importSongSources(const std::vector<std::filesystem::path>& sources,
    const std::filesystem::path& libraryRoot, const std::atomic_bool* cancelled = nullptr,
    const LibraryProgress& progress = {});
std::vector<Chart> scanSongFolder(const std::filesystem::path& folder,
    const std::atomic_bool* cancelled = nullptr, const LibraryProgress& progress = {}, uint64_t seed = 0, bool metadataOnly = false,
    const std::filesystem::path& cacheDirectory = {});
} // namespace bms
