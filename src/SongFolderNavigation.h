#pragma once
#include "SongSelection.h"
#include "SongGrouping.h"
#include "SongLibrarySources.h"
#include <map>
#include <set>
#include <fstream>
#include <sstream>
#include <windows.h>
namespace bms::ui {
class SongFolderNavigation {
    std::filesystem::path root_,current_;
    const Chart* indexedData_{};
    size_t indexedSize_=size_t(-1),indexedKnown_=size_t(-1);
    std::map<std::filesystem::path,std::set<std::filesystem::path>> children_,directories_;
    std::set<std::filesystem::path> indexedPreserved_;
    std::map<std::filesystem::path,std::filesystem::path> aliases_;
    std::wstring lastTrace_;
    bool all_{};
    bool configuredEmpty_{};
    bool rootSongs_{};
    bool original_{};
    std::vector<std::filesystem::path> roots_;
    std::map<std::wstring,std::filesystem::path> displayParents_;
    std::filesystem::path owner(const std::filesystem::path& path)const {
        if(libraryContains(root_,path))return root_;
        for(const auto& r:roots_)if(libraryContains(r,path))return r;
        return {};
    }
    std::set<std::filesystem::path> visibleDirectories_;
    std::map<std::wstring,int> cursors_;
    std::wstring key()const{return rootSongs_?L"ROOT SONGS":all_?L"ALL SONGS":current_.wstring();}
public:
    // Call after in-place path edits; normal worker publication replaces the vector.
    void invalidate(){indexedSize_=size_t(-1);lastTrace_.clear();}
    std::set<std::filesystem::path> preservedFolders;
    std::vector<std::filesystem::path> folders;
    std::vector<std::filesystem::path> knownFolders;
    std::filesystem::path location()const{return rootSongs_?std::filesystem::path(L"ROOT SONGS"):all_?std::filesystem::path(L"ALL SONGS"):current_;}
    void restore(const std::filesystem::path& p,const std::vector<Chart>& charts){rootSongs_=false;if(p==L"ROOT SONGS"){current_=root_;all_=false;rootSongs_=true;return;}if(p==L"ALL SONGS"){all_=true;return;}if(p==root_){current_=root_;all_=false;return;}
        bool known=isLibraryFolder(p)||std::find(knownFolders.begin(),knownFolders.end(),p)!=knownFolders.end();
        for(const auto& c:charts){auto d=c.path.parent_path();while(!d.empty()&&d!=root_){if(d==p)known=true;auto parent=d.parent_path();if(parent==d)break;d=parent;}}
        if(known&&!owner(p).empty()){current_=p;all_=false;}}
    void root(const std::filesystem::path& p){configuredEmpty_=false;if(root_!=p){invalidate();root_=current_=p;all_=false;rootSongs_=false;cursors_.clear();knownFolders.clear();}}
    void remember(int row){cursors_[key()]=row;}
    int cursor()const{auto i=cursors_.find(key());return i==cursors_.end()?0:i->second;}
    void save(const std::filesystem::path& selected)const{if(root_.empty())return;try{
        auto file=root_/L".sevenmania-index"/L"navigation.bin",temp=file;temp+=L".tmp";std::filesystem::create_directories(file.parent_path());
        {std::ofstream f(temp,std::ios::binary|std::ios::trunc);for(const auto& p:{location(),selected}){auto value=p.wstring();uint32_t n=uint32_t(value.size());if(n>32768)return;f.write(reinterpret_cast<const char*>(&n),4);f.write(reinterpret_cast<const char*>(value.data()),n*sizeof(wchar_t));}f.flush();if(!f)return;}
        MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    }catch(...){}}
    std::filesystem::path restoreSaved(const std::vector<Chart>& charts){if(root_.empty())return {};try{
        std::ifstream f(root_/L".sevenmania-index"/L"navigation.bin",std::ios::binary);std::filesystem::path values[2];
        for(auto& p:values){uint32_t n{};f.read(reinterpret_cast<char*>(&n),4);if(!f||n>32768)return {};std::wstring value(n,L'\0');f.read(reinterpret_cast<char*>(value.data()),n*sizeof(wchar_t));if(!f)return {};p=value;}
        restore(values[0],charts);return values[1];
    }catch(...){return {};}}
    void configure(const std::vector<std::filesystem::path>& roots,bool original) {
        if(roots.empty()){root_.clear();current_.clear();roots_.clear();folders.clear();knownFolders.clear();visibleDirectories_.clear();displayParents_.clear();all_=rootSongs_=false;configuredEmpty_=true;return;}
        configuredEmpty_=false;
        std::vector<std::filesystem::path> previousRoots=roots_;
        if(original_!=original)invalidate();
        root(roots.front());original_=original;roots_.clear();
        for(size_t i=1;i<roots.size();++i) {
            if(libraryContains(root_,roots[i]))continue;
            bool covered=false;
            for(size_t j=1;j<roots.size();++j)if(i!=j&&libraryPathKey(roots[i])!=libraryPathKey(roots[j])&&libraryContains(roots[j],roots[i]))covered=true;
            if(!covered)roots_.push_back(roots[i]);
        }
        if(previousRoots!=roots_)invalidate();
        if(!all_&&owner(current_).empty())current_=root_;
    }
    bool isLibraryFolder(const std::filesystem::path& p)const{return std::find(roots_.begin(),roots_.end(),p)!=roots_.end();}
    std::filesystem::path displayParent(const std::filesystem::path& chart)const {
        auto it=displayParents_.find(libraryPathKey(chart.parent_path()));
        return it==displayParents_.end()?std::filesystem::path{}:it->second;
    }
    bool accepts(const std::filesystem::path& p)const{return !configuredEmpty_&&(all_||root_.empty()||(!isRoot()&&visibleDirectories_.contains(p.parent_path())));}
    void build(const std::vector<Chart>& charts,const std::vector<SongGroup>* =nullptr){
        if(root_.empty()){folders.clear();return;}
        if(indexedData_!=charts.data()||indexedSize_!=charts.size()||indexedKnown_!=knownFolders.size()||indexedPreserved_!=preservedFolders){
            std::set<std::filesystem::path> chartDirectories;
            std::map<std::filesystem::path,size_t> descendants;
            for(const auto& c:charts)if(!owner(c.path).empty())chartDirectories.insert(c.path.parent_path());
            for(const auto& directory:chartDirectories){const auto source=owner(directory);auto d=directory;
                while(!d.empty()){++descendants[d];if(d==source)break;auto parent=d.parent_path();if(parent==d)break;d=parent;}}
            // Collapse empty unary wrappers, independently of descendant song count.
            std::map<std::filesystem::path,std::set<std::filesystem::path>> physicalChildren;
            for(const auto& [d,count]:descendants)if(d!=owner(d))physicalChildren[d.parent_path()].insert(d);
            aliases_.clear();
            for(const auto& [d,count]:descendants){const auto parent=d.parent_path();
                const bool wrapper=!original_&&parent!=owner(d)&&!parent.empty()&&!chartDirectories.contains(parent)&&physicalChildren[parent].size()==1&&!preservedFolders.contains(parent)&&!preservedFolders.contains(d);
                aliases_[d]=wrapper&&aliases_.contains(parent)?aliases_.at(parent):d;
            }
            const auto logical=[&](const std::filesystem::path& d){auto it=aliases_.find(d);return it==aliases_.end()?d:it->second;};
            displayParents_.clear();directories_.clear();children_.clear();lastTrace_.clear();
            for(const auto& directory:chartDirectories){const auto source=owner(directory);auto anchor=directory;
                if(!original_&&descendants[directory]==1){
                    while(anchor!=source&&!preservedFolders.contains(anchor)){
                        auto parent=anchor.parent_path();
                        if(parent==source||parent.empty()||chartDirectories.contains(parent)||descendants[parent]!=1||preservedFolders.contains(parent))break;
                        anchor=parent;
                    }
                }
                auto display=directory;
                if(directory==root_)display=L"ROOT SONGS";
                else if(!original_&&directory!=source&&descendants[directory]==1&&!preservedFolders.contains(directory))
                    display=anchor.parent_path()==root_?anchor:anchor.parent_path();
                display=logical(display);
                displayParents_[libraryPathKey(directory)]=display;
                directories_[display].insert(directory);
                if(display==L"ROOT SONGS"){children_[root_].insert(display);continue;}
                auto d=display;
                while(d!=source&&!d.empty()){auto parent=d.parent_path();if(parent==d)break;if(logical(parent)!=logical(d))children_[logical(parent)].insert(logical(d));d=parent;}
            }
            // Original view retains the previous explicit empty-directory support.
            if(original_)for(const auto& d:knownFolders)if(d!=root_&&!isLibraryFolder(d)&&!owner(d).empty()&&owner(d)==owner(d.parent_path()))children_[d.parent_path()].insert(d);
            for(const auto& r:roots_)children_[root_].insert(r);
            indexedData_=charts.data();indexedSize_=charts.size();indexedKnown_=knownFolders.size();indexedPreserved_=preservedFolders;
        }
        if(auto alias=aliases_.find(current_);!original_&&alias!=aliases_.end())current_=alias->second;
        if(!all_&&!rootSongs_&&!isRoot()&&!isLibraryFolder(current_)&&!directories_.contains(current_)&&!children_.contains(current_)&&!(original_&&std::find(knownFolders.begin(),knownFolders.end(),current_)!=knownFolders.end())){
            auto it=displayParents_.find(libraryPathKey(current_));
            current_=it==displayParents_.end()?root_:it->second;
            if(current_==L"ROOT SONGS"){current_=root_;rootSongs_=true;}
        }
        visibleDirectories_.clear();
        const auto location=rootSongs_?std::filesystem::path(L"ROOT SONGS"):current_;
        if(auto it=directories_.find(location);it!=directories_.end())visibleDirectories_=it->second;
        folders.clear();
        if(!all_&&!rootSongs_)if(auto it=children_.find(current_);it!=children_.end())folders.assign(it->second.begin(),it->second.end());
        wchar_t tracePath[32768]{};
        if(GetEnvironmentVariableW(L"SEVENMANIA_NAV_TRACE",tracePath,32768)>0&&lastTrace_!=key()){
            lastTrace_=key();std::wostringstream text;trace(text,charts);const auto wide=text.str();
            const int count=WideCharToMultiByte(CP_UTF8,0,wide.data(),int(wide.size()),nullptr,0,nullptr,nullptr);std::string utf8(count,0);
            WideCharToMultiByte(CP_UTF8,0,wide.data(),int(wide.size()),utf8.data(),count,nullptr,nullptr);
            std::ofstream log(std::filesystem::path(tracePath),std::ios::app|std::ios::binary);if(log)log<<utf8;
        }
    }
    // Opt-in navigation diagnostics; called only when the location/index changes.
    void trace(std::wostream& out,const std::vector<Chart>& charts)const {
        out<<L"mode="<<(original_?L"original":L"smart")<<L" ui="<<location().wstring()<<L"\n";
        std::map<std::filesystem::path,size_t> direct;
        std::map<std::filesystem::path,std::set<std::filesystem::path>> child;
        std::map<std::filesystem::path,std::set<std::wstring>> groups;
        const auto titles=canonicalSongTitles(charts);
        for(size_t i=0;i<charts.size();++i){auto d=charts[i].path.parent_path();++direct[d];const auto id=songGroupIdentity(charts[i],titles[i]);
            while(!d.empty()&&!owner(d).empty()){groups[d].insert(id);if(d==owner(d))break;child[d.parent_path()].insert(d);d=d.parent_path();}}
        for(const auto& [d,ids]:groups){const auto a=aliases_.find(d);const bool aliased=a!=aliases_.end()&&a->second!=d;
            const auto parent=displayParent(d/L"diagnostic.bms");const bool compressed=aliased||(!parent.empty()&&parent!=d);
            out<<L"physical="<<d.wstring()<<L" direct="<<direct[d]<<L" children="<<child[d].size()<<L" groups="<<ids.size()<<L" display="<<parent.wstring()<<L" compressed="<<compressed
               <<L" reason="<<(original_?L"원본 모드":d==owner(d)?L"등록 루트":preservedFolders.contains(d)?L"명시적 보존":aliased?L"직접 채보 없는 단일 자식 래퍼 압축":compressed?L"말단 곡 디렉터리의 표시 부모 생략":direct[d]?L"직접 채보/혼합 폴더 유지":child[d].size()>1?L"여러 자식 패키지 유지":L"루트/보존 경계 또는 단일 자식 래퍼의 대표 위치 유지")<<L"\n";}
        for(const auto& f:folders)out<<L"UI folder="<<f.wstring()<<L"\n";
        for(const auto& c:charts)if(accepts(c.path))out<<L"UI chart="<<c.path.wstring()<<L"\n";
    }
    bool isRoot()const{return !all_&&!rootSongs_&&current_==root_;}
    bool back(){if(isRoot())return false;if(rootSongs_){rootSongs_=false;return true;}if(all_)all_=false;else if(isLibraryFolder(current_))current_=root_;else {current_=current_.parent_path();if(!original_)if(auto it=aliases_.find(current_);it!=aliases_.end())current_=it->second;}return true;}
    // Negative list IDs: -1 parent, -2 ALL SONGS, -3 and below real folders.
    void enter(int id){if(id==-1)back();else if(id==-2){all_=true;rootSongs_=false;}else if(id<=-3&&size_t(-id-3)<folders.size()){const auto destination=folders[size_t(-id-3)];rootSongs_=destination==L"ROOT SONGS";current_=rootSongs_?root_:destination;all_=false;}}
};
}
