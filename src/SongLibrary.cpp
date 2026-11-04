#include "SongLibrary.h"
#include "SongMetadataCache.h"
#include "RustBackend.h"
#include "ExperimentProfile.h"
#ifdef BMS_USE_ONETBB
#include <oneapi/tbb/task_arena.h>
#include <oneapi/tbb/parallel_for.h>
#endif
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <optional>

namespace bms {
namespace {
namespace fs = std::filesystem;
bool isCancelled(const std::atomic_bool* flag) { return flag && flag->load(); }
void checkCancelled(const std::atomic_bool* flag) {
    if (isCancelled(flag)) throw LibraryError(L"불러오기를 취소했습니다.");
}
bool within(const fs::path& path, const fs::path& root) {
    auto p = path.begin();
    for (auto r = root.begin(); r != root.end(); ++r, ++p)
        if (p == path.end() || _wcsicmp(p->c_str(), r->c_str()) != 0) return false;
    return true;
}
fs::path resolvedDirectory(const fs::path& requested) {
    // Resolve an existing directory by handle. weakly_canonical probes every
    // ancestor, which fails in restricted Windows folders despite child access.
    const auto absolute = fs::absolute(requested).lexically_normal();
    auto existing = absolute;
    const auto fallback = [&] {
        for (auto parent = existing; !parent.empty(); parent = parent.parent_path()) {
            const DWORD attributes = GetFileAttributesW(parent.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
                throw LibraryError(L"링크 폴더의 실제 경로를 확인할 수 없습니다: " + parent.wstring());
            if (parent == parent.root_path()) break;
        }
        return absolute;
    };
    std::vector<fs::path> missing;
    while (!fs::exists(existing)) {
        if (existing == existing.root_path()) throw LibraryError(L"폴더 경로를 확인할 수 없습니다: " + requested.wstring());
        missing.push_back(existing.filename()); existing = existing.parent_path();
    }
    HANDLE handle = CreateFileW(existing.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return fallback();
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()), FILE_NAME_OPENED);
    CloseHandle(handle);
    if (!length || length >= buffer.size()) return fallback();
    buffer.resize(length);
    if (buffer.rfind(L"\\\\?\\UNC\\", 0) == 0) buffer = L"\\\\" + buffer.substr(8);
    else if (buffer.rfind(L"\\\\?\\", 0) == 0) buffer.erase(0, 4);
    fs::path result(buffer);
    for (auto it = missing.rbegin(); it != missing.rend(); ++it) result /= *it;
    return result.lexically_normal();
}
std::wstring sourceKey(const fs::path& source) {
    uint64_t hash = 14695981039346656037ull;
    for (wchar_t c : toLower(source.generic_wstring())) {
        hash = (hash ^ (static_cast<unsigned>(c) & 255)) * 1099511628211ull;
        hash = (hash ^ (static_cast<unsigned>(c) >> 8)) * 1099511628211ull;
    }
    std::wostringstream text;
    text << std::hex << std::setfill(L'0') << std::setw(16) << hash;
    return text.str();
}
DWORD CALLBACK copyProgress(LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD,
    HANDLE, HANDLE, LPVOID data) {
    return isCancelled(static_cast<const std::atomic_bool*>(data)) ? PROGRESS_CANCEL : PROGRESS_CONTINUE;
}
bool sourceReadFailure(DWORD error) {
    return error == ERROR_READ_FAULT || error == ERROR_CRC || error == ERROR_HANDLE_EOF;
}
bool chartExtension(const fs::path& path) {
    const auto extension = toLower(path.extension().wstring());
    return extension == L".bms" || extension == L".bme" || extension == L".bml" || (extension == L".pms" || extension == L".bmson" || extension == L".mbm");
}
} // namespace

ImportedFolder importSongFolder(const fs::path& sourcePath, const fs::path& libraryPath,
    const std::atomic_bool* cancelled, const LibraryProgress& progress) {
    checkCancelled(cancelled);
    const auto source = resolvedDirectory(sourcePath);
    const auto library = resolvedDirectory(libraryPath);
    if (!fs::is_directory(source)) throw LibraryError(L"곡 폴더를 찾을 수 없습니다: " + source.wstring());
    if (within(source, library)) return {source};
    if (within(library, source)) throw LibraryError(L"게임의 Songs 폴더를 포함한 상위 폴더는 복사할 수 없습니다. 곡 폴더를 선택하세요.");

    fs::create_directories(library);
    auto name = source.filename().wstring();
    if (name.empty()) name = L"Songs";
    name.resize(std::min<size_t>(name.size(), 80));
    const auto destination = library / (name + L"-" + sourceKey(source));
    const bool existing = fs::exists(destination);
    if (existing && !fs::is_directory(destination)) throw LibraryError(L"곡 저장 경로가 폴더가 아닙니다: " + destination.wstring());
    if (existing && (GetFileAttributesW(destination.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT))
        throw LibraryError(L"곡 저장 경로에 링크가 있습니다: " + destination.wstring());
    const std::wstring unique = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    const auto working = existing ? destination : library / (L"." + destination.filename().wstring() + L".importing-" + unique);
    ImportedFolder result{destination};
    struct File { fs::path relative; uintmax_t size; fs::file_time_type modified; };
    std::vector<File> files;
    std::vector<fs::path> directories;
    bool created = false;
    try {
        for (fs::recursive_directory_iterator it(source), end; it != end; ++it) {
            checkCancelled(cancelled);
            const auto& entry = *it;
            const DWORD attributes = GetFileAttributesW(entry.path().c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
                throw LibraryError(L"복사할 수 없는 파일 또는 링크가 있습니다: " + entry.path().wstring());
            const auto relative = entry.path().lexically_relative(source);
            if (entry.is_directory()) directories.push_back(relative);
            else if (entry.is_regular_file()) files.push_back({relative, entry.file_size(), entry.last_write_time()});
        }
        checkCancelled(cancelled);
        if (!existing) {
            created = fs::create_directory(working);
            if (!created) throw LibraryError(L"곡 복사 임시 폴더를 만들 수 없습니다: " + working.wstring());
        }
        for (const auto& relative : directories) {
            checkCancelled(cancelled); fs::create_directories(working / relative);
            if (GetFileAttributesW((working / relative).c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)
                throw LibraryError(L"곡 저장 경로에 링크가 있습니다: " + (working / relative).wstring());
        }
        if (progress) progress(0, files.size());
        for (const auto& file : files) {
            checkCancelled(cancelled);
            const auto target = working / file.relative;
            const DWORD targetAttributes = GetFileAttributesW(target.c_str());
            if (targetAttributes != INVALID_FILE_ATTRIBUTES && (targetAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                throw LibraryError(L"곡 저장 경로에 링크가 있습니다: " + target.wstring());
            std::error_code ec;
            if (fs::is_regular_file(target, ec) && !ec && fs::file_size(target) == file.size && fs::last_write_time(target) == file.modified) {
                ++result.unchangedFiles;
            } else {
                HANDLE sourceHandle = CreateFileW((source / file.relative).c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
                if (sourceHandle == INVALID_HANDLE_VALUE) {
                    // A chart can still run when an individual optional asset
                    // is locked or unreadable. Skip that source file and let
                    // parsing mark affected charts as partially loaded.
                    result.skippedFiles.push_back(destination / file.relative);
                    if (progress) progress(result.copiedFiles + result.unchangedFiles + result.skippedFiles.size(), files.size());
                    continue;
                }
                CloseHandle(sourceHandle);
                const auto temporary = target.parent_path() / (L"." + target.filename().wstring() + L".copying-" + unique);
                BOOL abortCopy = FALSE;
                const bool copied = CopyFileExW((source / file.relative).c_str(), temporary.c_str(), copyProgress,
                    const_cast<std::atomic_bool*>(cancelled), &abortCopy, COPY_FILE_FAIL_IF_EXISTS) != FALSE;
                const DWORD error = copied ? ERROR_SUCCESS : GetLastError();
                if (!copied) {
                    if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) fs::remove(temporary, ec);
                    checkCancelled(cancelled);
                    if (sourceReadFailure(error)) {
                        result.skippedFiles.push_back(destination / file.relative);
                        if (progress) progress(result.copiedFiles + result.unchangedFiles + result.skippedFiles.size(), files.size());
                        continue;
                    }
                    throw fs::filesystem_error("song copy failed", source / file.relative, target, std::error_code(error, std::system_category()));
                }
                if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    const DWORD moveError = GetLastError(); fs::remove(temporary, ec);
                    throw fs::filesystem_error("song copy replace failed", target, std::error_code(moveError, std::system_category()));
                }
                fs::last_write_time(target, file.modified);
                ++result.copiedFiles;
            }
            if (progress) progress(result.copiedFiles + result.unchangedFiles + result.skippedFiles.size(), files.size());
        }
        checkCancelled(cancelled);
        if (!existing) {
            // Windows indexers can briefly hold a just-written directory. Retry
            // publication only for transient sharing/access errors, then report it.
            std::error_code publishError;
            for (unsigned attempt=0;attempt<5;++attempt) {
                fs::rename(working,destination,publishError);
                if (!publishError) break;
                if (publishError.value()!=ERROR_ACCESS_DENIED && publishError.value()!=ERROR_SHARING_VIOLATION) break;
                if (attempt<4) Sleep(10u<<attempt);
            }
            if (publishError) throw fs::filesystem_error("publish imported song folder",working,destination,publishError);
        }
        return result;
    } catch (...) {
        // Only a newly created, verified child of Songs is removed. An existing
        // imported folder keeps its completed files when a refresh is cancelled.
        if (created && within(working, library) && working != library && working != destination) {
            std::error_code cleanup; fs::remove_all(working, cleanup);
        }
        throw;
    }
}

ImportedSongs importSongSources(const std::vector<fs::path>& sources, const fs::path& libraryRoot,
    const std::atomic_bool* cancelled, const LibraryProgress& progress) {
    ImportedSongs result;
    const auto library=resolvedDirectory(libraryRoot);
    std::vector<fs::path> folders;
    for(const auto& source:sources) {
        checkCancelled(cancelled);
        try {
            const bool directory=fs::is_directory(source);
            if(!directory&&(!fs::is_regular_file(source)||!chartExtension(source)))
                throw LibraryError(L"지원하는 채보 파일 또는 곡 폴더가 아닙니다: "+source.wstring());
            const auto folder=resolvedDirectory(directory?source:fs::absolute(source).parent_path());
            if(within(library,folder)&&!within(folder,library))
                throw LibraryError(L"Songs 폴더를 포함한 상위 폴더는 추가할 수 없습니다: "+source.wstring());
            if(std::none_of(folders.begin(),folders.end(),[&](const auto& f){return within(f,folder)&&within(folder,f);}))folders.push_back(folder);
        } catch(const LibraryError& e){result.errors.push_back(e.message);}
        catch(const fs::filesystem_error&){result.errors.push_back(L"곡 경로를 확인할 수 없습니다: "+source.wstring());}
    }
    // A selected parent already contains its selected child; copy it once.
    std::vector<fs::path> roots;
    for(size_t i=0;i<folders.size();++i) {
        bool covered=false;
        for(size_t j=0;j<folders.size();++j)if(i!=j&&within(folders[i],folders[j])){covered=true;break;}
        if(!covered)roots.push_back(folders[i]);
    }
    const size_t total=std::max<size_t>(1,roots.size())*1000;
    if(progress)progress(0,total);
    for(size_t i=0;i<roots.size();++i) {
        checkCancelled(cancelled);
        try {
            auto imported=importSongFolder(roots[i],library,cancelled,[&](size_t done,size_t count){
                if(progress)progress(i*1000+(count?done*1000/count:0),total);
            });
            ++result.importedFolders;result.copiedFiles+=imported.copiedFiles;result.unchangedFiles+=imported.unchangedFiles;
            result.skippedFiles.insert(result.skippedFiles.end(),imported.skippedFiles.begin(),imported.skippedFiles.end());
        } catch(const LibraryError& e){checkCancelled(cancelled);result.errors.push_back(e.message);}
        catch(const fs::filesystem_error&){result.errors.push_back(L"곡 폴더를 복사하지 못했습니다: "+roots[i].wstring());}
        if(progress)progress((i+1)*1000,total);
    }
    checkCancelled(cancelled);
    if(progress)progress(total,total);
    return result;
}

std::vector<Chart> scanSongFolder(const fs::path& folder, const std::atomic_bool* cancelled,
    const LibraryProgress& progress, uint64_t seed, bool metadataOnly, const fs::path& cacheDirectory) {
    BMS_TRACE_ZONE("Song library scan");
    if (!fs::is_directory(folder)) throw LibraryError(L"곡 폴더를 찾을 수 없습니다: " + folder.wstring());
    std::vector<fs::path> paths;
    if (!rust_backend::discoverCharts(folder, cancelled, paths)) {
        std::error_code ec;
        for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
             it != end; it.increment(ec)) {
            checkCancelled(cancelled);
            if (ec) { ec.clear(); continue; }
            if(it->is_directory(ec)) {
                const auto attributes=GetFileAttributesW(it->path().c_str());
                if(it->path().filename()==L".sevenmania-index" || (attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT)))it.disable_recursion_pending();
            } else if(chartExtension(it->path())) {
                const auto attributes=GetFileAttributesW(it->path().c_str());
                if(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT)&&it->is_regular_file(ec)&&!ec)paths.push_back(it->path());
            }
            ec.clear();
        }
    }
    checkCancelled(cancelled);
    std::map<fs::path, std::vector<size_t>> grouped;
    for (size_t i = 0; i < paths.size(); ++i) grouped[paths[i].parent_path()].push_back(i);
    std::vector<std::vector<size_t>> groups;
    for (auto& [directory, indices] : grouped) groups.push_back(std::move(indices));
    std::vector<Chart> charts(paths.size());
    std::optional<SongMetadataCache> cache;if(metadataOnly)cache.emplace(folder,seed,cacheDirectory);
    std::atomic_size_t nextGroup{}, completed{};
    std::atomic_bool failed{};
    std::mutex mutex;
    std::exception_ptr failure;
    if (progress) progress(0, paths.size());
    const size_t workerCount = std::min<size_t>(groups.size(), std::clamp(std::thread::hardware_concurrency(), 1u, 4u));
    const auto analyzeGroups = [&] {
        BMS_TRACE_ZONE("Analyze chart groups");
        try {
            while (!isCancelled(cancelled) && !failed) {
                const size_t group = nextGroup.fetch_add(1);
                if (group >= groups.size()) break;
                BmsParser parser(true, seed);
                for (size_t index : groups[group]) {
                    if (isCancelled(cancelled) || failed) break;
                    try { if(!cache||!cache->load(paths[index],charts[index])) { charts[index] = parser.parse(paths[index]).chart; if(cache){cache->save(charts[index]);charts[index]=SongMetadataCache::metadata(charts[index]);} } }
                    catch (...) {
                        charts[index].path = paths[index]; charts[index].title = paths[index].stem().wstring();
                        charts[index].blockReason = L"채보 분석 중 오류가 발생했습니다.";
                    }
                    std::lock_guard lock(mutex);
                    const size_t done = completed.fetch_add(1) + 1;
                    if (progress) progress(done, paths.size());
                }
            }
        } catch (...) { std::lock_guard lock(mutex); failure = std::current_exception(); failed = true; }
    };
#ifdef BMS_USE_ONETBB
    // Keep the original concurrency cap and one parser per worker. Audio and
    // judgement threads never submit jobs to this background arena.
    if (workerCount) {
        oneapi::tbb::task_arena arena(static_cast<int>(workerCount));
        arena.execute([&] {
            oneapi::tbb::parallel_for(size_t{0}, workerCount, [&](size_t) { analyzeGroups(); });
        });
    }
#else
    std::vector<std::jthread> workers;
    for (size_t worker = 0; worker < workerCount; ++worker) workers.emplace_back(analyzeGroups);
    for (auto& worker : workers) worker.join();
#endif
    if (failure) std::rethrow_exception(failure);
    checkCancelled(cancelled);
    if(cache)cache->prune(charts);
    return charts;
}
} // namespace bms
