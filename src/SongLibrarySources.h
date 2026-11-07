#pragma once
#include "BmsParser.h"
#include <windows.h>
#include <algorithm>
#include <set>

namespace bms {
// Identity is separate from the path used by the parser and play records.
inline std::wstring libraryPathKey(const std::filesystem::path& path) {
    if(path.empty())return {};
    return toLower(std::filesystem::absolute(path).lexically_normal().generic_wstring());
}
inline bool libraryContains(const std::filesystem::path& root, const std::filesystem::path& path) {
    if(root.empty()||path.empty())return false;
    const auto r=libraryPathKey(root), p=libraryPathKey(path);
    return p==r || (p.starts_with(r) && (r.back()==L'/' || p[r.size()]==L'/'));
}
inline std::filesystem::path libraryResolvedPath(const std::filesystem::path& path) {
    auto result=std::filesystem::absolute(path).lexically_normal();
    HANDLE handle=CreateFileW(result.c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(handle==INVALID_HANDLE_VALUE)return result; // Retain disconnected sources.
    std::wstring name(32768,L'\0');
    const DWORD n=GetFinalPathNameByHandleW(handle,name.data(),DWORD(name.size()),FILE_NAME_NORMALIZED);
    CloseHandle(handle);
    if(n&&n<name.size()) {
        name.resize(n);
        if(name.starts_with(L"\\\\?\\UNC\\"))name=L"\\\\"+name.substr(8);
        else if(name.starts_with(L"\\\\?\\"))name.erase(0,4);
        result=name;
    }
    return result;
}
class SongLibrarySources {
    std::filesystem::path settings_,cacheRoot_;
    bool initialized_{};
    // INI files may already be ANSI. ASCII encoding preserves every UTF-16
    // path without changing the user's existing settings-file encoding.
    static std::wstring encodePath(const std::filesystem::path& path) {
        constexpr wchar_t hex[]=L"0123456789abcdef";std::wstring out=L"utf16:";
        for(auto c:path.wstring())for(int shift:{12,8,4,0})out+=hex[(uint16_t(c)>>shift)&15];
        return out;
    }
    static std::wstring decodePath(std::wstring value) {
        if(!value.starts_with(L"utf16:"))return value;
        if((value.size()-6)%4)return {};
        std::wstring out;
        for(size_t i=6;i<value.size();i+=4){unsigned c=0;
            for(size_t j=0;j<4;++j){auto digit=value[i+j];unsigned n=digit>=L'0'&&digit<=L'9'?digit-L'0':digit>=L'a'&&digit<=L'f'?digit-L'a'+10:16;if(n>15)return {};c=c*16+n;}
            if(!c)return {};out+=wchar_t(c);
        }return out;
    }
public:
    std::vector<std::filesystem::path> roots;
    bool originalFolders{}, individualCharts{}, collectVariants{};
    void initialize(const std::filesystem::path& primary,const std::filesystem::path& settings) {
        settings_=settings;initialized_=true;roots.clear();
        cacheRoot_=settings.empty()?libraryResolvedPath(primary).parent_path()/L".sevenmania-index"/L"libraries":std::filesystem::absolute(settings).parent_path()/L".sevenmania-index"/L"libraries";
        originalFolders=GetPrivateProfileIntW(L"SongLibrary",L"OriginalFolders",0,settings.c_str())!=0;
        collectVariants=GetPrivateProfileIntW(L"SongLibrary",L"CollectVariants",0,settings.c_str())!=0;
        individualCharts=GetPrivateProfileIntW(L"SongLibrary",L"IndividualCharts",0,settings.c_str())!=0;
        // Enumerate a variable-size INI section; no fixed source-count limit.
        std::vector<wchar_t> section(4096);
        while(GetPrivateProfileSectionW(L"SongLibrarySources",section.data(),DWORD(section.size()),settings.c_str())>=section.size()-2) {
            if(section.size()>16*1024*1024)return;
            section.resize(section.size()*2);
        }
        for(const wchar_t* p=section.data();*p;p+=wcslen(p)+1) {
            const auto* equal=wcschr(p,L'=');if(equal&&equal[1])add(decodePath(equal+1));
        }
    }
    bool add(const std::filesystem::path& path) {
        if(path.empty())return false;
        try {
        auto value=libraryResolvedPath(path);const auto key=libraryPathKey(value);
        if(std::any_of(roots.begin(),roots.end(),[&](const auto& p){return libraryPathKey(p)==key;}))return false;
        roots.push_back(std::move(value));return true;
        }catch(const std::filesystem::filesystem_error&){return false;}
    }
    bool remove(size_t index) {if(index>=roots.size())return false;roots.erase(roots.begin()+index);return true;}
    bool save()const {
        if(settings_.empty())return false;
        std::wstring section;
        for(size_t i=0;i<roots.size();++i){section+=L"Root"+std::to_wstring(i+1)+L"="+encodePath(roots[i]);section.push_back(0);}
        section.push_back(0);if(section.size()==1)section.push_back(0);
        return WritePrivateProfileSectionW(L"SongLibrarySources",section.data(),settings_.c_str()) &&
            WritePrivateProfileStringW(L"SongLibrary",L"OriginalFolders",originalFolders?L"1":L"0",settings_.c_str()) &&
            WritePrivateProfileStringW(L"SongLibrary",L"IndividualCharts",individualCharts?L"1":L"0",settings_.c_str()) &&
            WritePrivateProfileStringW(L"SongLibrary",L"CollectVariants",collectVariants?L"1":L"0",settings_.c_str());
    }
    bool configured()const{return initialized_;}
    std::vector<std::filesystem::path> navigationRoots()const{
        if(roots.empty())return {};
        std::vector<std::filesystem::path> out{cacheRoot_/L"Navigation"};out.insert(out.end(),roots.begin(),roots.end());return out;
    }
    std::filesystem::path cacheDirectory(const std::filesystem::path& root)const {
        if(cacheRoot_.empty())return root/L".sevenmania-index"; // Explicit low-level scanner compatibility.
        uint64_t hash=14695981039346656037ull;
        for(auto c:libraryPathKey(root))hash=(hash^uint16_t(c))*1099511628211ull;
        return cacheRoot_/std::to_wstring(hash);
    }
    std::vector<std::filesystem::path> scanRoots()const {
        std::vector<std::filesystem::path> out;
        for(const auto& root:roots) {
            bool covered=false;
            for(const auto& other:roots)if(libraryPathKey(root)!=libraryPathKey(other)&&libraryContains(other,root)){covered=true;break;}
            if(!covered)out.push_back(root);
        }
        return out;
    }
};
}
