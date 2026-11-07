#pragma once
#include "BmsParser.h"
#include <algorithm>
namespace bms {
inline std::wstring songResourceIdentity(const Chart& c){
    if(!c.catalogGroupKey.empty())return c.catalogGroupKey;
    std::vector<std::wstring> paths;for(const auto& [id,p]:c.samples)paths.push_back(toLower(p.lexically_normal().generic_wstring()));
    std::sort(paths.begin(),paths.end());paths.erase(std::unique(paths.begin(),paths.end()),paths.end());
    if(paths.empty())return L"chart:"+toLower(c.path.lexically_normal().generic_wstring());
    uint64_t hash=14695981039346656037ull;
    for(const auto& p:paths){for(auto ch:p)hash=(hash^uint16_t(ch))*1099511628211ull;hash=(hash^0xffff)*1099511628211ull;}
    return L"audio:"+std::to_wstring(paths.size())+L":"+std::to_wstring(hash);
}
}
