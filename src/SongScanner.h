#pragma once
#include "SongLibrary.h"
#include "SongLibrarySources.h"
#include "SongMetadataCache.h"
#include <set>

namespace bms {
struct SongIndexUpdate {
    std::vector<Chart> charts;
    std::vector<std::filesystem::path> folders;
    std::vector<std::wstring> warnings;
    bool complete{true};
};
inline void appendUniqueCharts(std::vector<Chart>& out,std::vector<Chart> input,std::set<std::wstring>& seen) {
    for(auto& chart:input)if(seen.insert(libraryPathKey(chart.path)).second)out.push_back(std::move(chart));
}
inline std::vector<Chart> snapshotSongLibraries(const SongLibrarySources& sources,const std::atomic_bool* cancelled=nullptr) {
    std::vector<Chart> out;std::set<std::wstring> seen;
    for(const auto& root:sources.scanRoots()) {
        if(cancelled&&*cancelled)break;
        SongMetadataCache cache(root,0,sources.cacheDirectory(root));
        appendUniqueCharts(out,cache.snapshot(cancelled),seen);
    }
    return out;
}
// A targeted refresh retains immutable metadata for all other sources. Both
// workers publish an owned result; they never touch UI or selection objects.
inline SongIndexUpdate scanSongLibraries(const SongLibrarySources& sources,const std::atomic_bool* cancelled=nullptr,
    const LibraryProgress& progress={},const std::filesystem::path& target={},std::vector<Chart> previous={},
    std::vector<std::filesystem::path> previousFolders={}) {
    SongIndexUpdate update;std::set<std::wstring> seen;
    if(!target.empty())for(auto& c:previous)if(!libraryContains(target,c.path)&&seen.insert(libraryPathKey(c.path)).second)update.charts.push_back(std::move(c));
    if(!target.empty())for(auto& folder:previousFolders)if(!libraryContains(target,folder))update.folders.push_back(std::move(folder));
    const auto roots=target.empty()?sources.scanRoots():std::vector<std::filesystem::path>{target};
    for(size_t i=0;i<roots.size();++i) {
        if(cancelled&&*cancelled)return {};
        const auto& root=roots[i];std::error_code ec;
        if(!std::filesystem::is_directory(root,ec)) {update.warnings.push_back(L"연결되지 않은 라이브러리: "+root.wstring());continue;}
        try {
            auto charts=scanSongFolder(root,cancelled,[&](size_t done,size_t total){if(progress)progress(i*1000+(total?done*1000/total:0),roots.size()*1000);},0,true,sources.cacheDirectory(root));
            appendUniqueCharts(update.charts,std::move(charts),seen);
        } catch(const LibraryError& error) {if(cancelled&&*cancelled)return {};update.warnings.push_back(error.message);}
        for(std::filesystem::recursive_directory_iterator it(root,std::filesystem::directory_options::skip_permission_denied,ec),end;it!=end;it.increment(ec)) {
            if(cancelled&&*cancelled)return {};
            if(ec){ec.clear();continue;}
            if(!it->is_directory(ec))continue;
            const auto attributes=GetFileAttributesW(it->path().c_str());
            if(it->path().filename()==L".sevenmania-index"||(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))){it.disable_recursion_pending();continue;}
            update.folders.push_back(it->path());
        }
        if(progress)progress((i+1)*1000,roots.size()*1000);
    }
    return update;
}
}
