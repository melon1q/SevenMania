#pragma once
#include "Lr2Parser.h"
#include <algorithm>

// Finds the .lr2skin files under a folder so a settings page can offer them per screen.
namespace sevenmania::skin::lr2 {
struct SkinEntry {
    std::filesystem::path file;
    Header header;
};
struct SkinSearchLimits {
    int depth=8;
    std::size_t entries=50000, skins=512;
};

// Only headers are read: includes and images are not opened, and every screen type is listed.
inline std::vector<SkinEntry> findSkins(const std::filesystem::path& root,const SkinSearchLimits& limits={}) {
    std::vector<SkinEntry> skins;
    std::error_code error;
    std::filesystem::recursive_directory_iterator it(root,std::filesystem::directory_options::skip_permission_denied,error);
    std::size_t entries=0;
    for(;!error&&it!=std::filesystem::recursive_directory_iterator{};it.increment(error)) {
        if(++entries>limits.entries||skins.size()>=limits.skins)break;
        if(it.depth()>=limits.depth)it.disable_recursion_pending();
        const auto& path=it->path();
        if(_wcsicmp(path.extension().c_str(),L".lr2skin")!=0)continue;
        std::error_code status;
        if(!it->is_regular_file(status))continue;
        Options options;
        options.headerOnly=true;
        auto result=parse(path,options);
        if(result)skins.push_back({path,std::move(result.document->header)});
    }
    std::sort(skins.begin(),skins.end(),[](const SkinEntry& a,const SkinEntry& b) {
        return a.header.name!=b.header.name?a.header.name<b.header.name:a.file<b.file;
    });
    return skins;
}
}
