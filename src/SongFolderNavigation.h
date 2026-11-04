#pragma once
#include "SongSelection.h"
#include "SongLibrarySources.h"
#include <map>
#include <set>
#include <fstream>
#include <windows.h>
namespace bms::ui {
class SongFolderNavigation {
    std::filesystem::path root_,current_;
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
    std::vector<std::filesystem::path> folders;
    std::vector<std::filesystem::path> knownFolders;
    std::filesystem::path location()const{return rootSongs_?std::filesystem::path(L"ROOT SONGS"):all_?std::filesystem::path(L"ALL SONGS"):current_;}
    void restore(const std::filesystem::path& p,const std::vector<Chart>& charts){rootSongs_=false;if(p==L"ROOT SONGS"){current_=root_;all_=false;rootSongs_=true;return;}if(p==L"ALL SONGS"){all_=true;return;}if(p==root_){current_=root_;all_=false;return;}
        bool known=isLibraryFolder(p)||std::find(knownFolders.begin(),knownFolders.end(),p)!=knownFolders.end();
        for(const auto& c:charts){auto d=c.path.parent_path();while(!d.empty()&&d!=root_){if(d==p)known=true;auto parent=d.parent_path();if(parent==d)break;d=parent;}}
        if(known&&!owner(p).empty()){current_=p;all_=false;}}
    void root(const std::filesystem::path& p){configuredEmpty_=false;if(root_!=p){root_=current_=p;all_=false;rootSongs_=false;cursors_.clear();knownFolders.clear();}}
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
        root(roots.front());original_=original;roots_.clear();
        for(size_t i=1;i<roots.size();++i) {
            if(libraryContains(root_,roots[i]))continue;
            bool covered=false;
            for(size_t j=1;j<roots.size();++j)if(i!=j&&libraryPathKey(roots[i])!=libraryPathKey(roots[j])&&libraryContains(roots[j],roots[i]))covered=true;
            if(!covered)roots_.push_back(roots[i]);
        }
        if(!all_&&owner(current_).empty())current_=root_;
    }
    bool isLibraryFolder(const std::filesystem::path& p)const{return std::find(roots_.begin(),roots_.end(),p)!=roots_.end();}
    std::filesystem::path displayParent(const std::filesystem::path& chart)const {
        auto it=displayParents_.find(libraryPathKey(chart.parent_path()));
        return it==displayParents_.end()?std::filesystem::path{}:it->second;
    }
    bool accepts(const std::filesystem::path& p)const{return !configuredEmpty_&&(all_||root_.empty()||(!isRoot()&&visibleDirectories_.contains(p.parent_path())));}
    void build(const std::vector<Chart>& charts,const std::vector<SongGroup>* =nullptr){
        if(root_.empty()){folders.clear();return;} // Unconfigured/embedded menu compatibility.
        std::set<std::filesystem::path> children,chartDirectories,hasNestedCharts;
        for(const auto& c:charts)if(!owner(c.path).empty())chartDirectories.insert(c.path.parent_path());
        for(const auto& directory:chartDirectories){const auto source=owner(directory);auto d=directory;
            while(d!=source&&!d.empty()){auto parent=d.parent_path();if(parent==d)break;hasNestedCharts.insert(parent);d=parent;}}
        if(!original_&&!all_&&chartDirectories.contains(current_)&&current_!=owner(current_)&&current_.parent_path()!=root_&&!hasNestedCharts.contains(current_))current_=current_.parent_path();
        // A display location is derived only from directory topology, never the
        // number of title/resource groups produced by a C++ or Rust backend.
        displayParents_.clear();visibleDirectories_.clear();
        for(const auto& directory:chartDirectories){const auto source=owner(directory);
            const auto display=directory==root_?std::filesystem::path(L"ROOT SONGS"):(original_||directory==source||directory.parent_path()==root_||hasNestedCharts.contains(directory)?directory:directory.parent_path());
            displayParents_[libraryPathKey(directory)]=display;
            if((rootSongs_&&directory==root_)||(!rootSongs_&&display==current_))visibleDirectories_.insert(directory);
            if(isRoot()&&directory==root_)children.insert(std::filesystem::path(L"ROOT SONGS"));
            if(!all_&&!rootSongs_&&directory!=root_&&owner(current_)==source){auto d=display;
                while(d!=source&&d!=current_&&!d.empty()){auto parent=d.parent_path();if(parent==current_){children.insert(d);break;}if(parent==d)break;d=parent;}}
        }
        if(!all_)for(const auto& d:knownFolders)if(d.parent_path()==current_&&owner(d)==owner(current_)&&!chartDirectories.contains(d)&&!hasNestedCharts.contains(d))children.insert(d);
        if(isRoot())for(const auto& r:roots_)children.insert(r);
        if(current_!=root_&&!all_&&!isLibraryFolder(current_)){
            bool present=std::find(knownFolders.begin(),knownFolders.end(),current_)!=knownFolders.end();
            for(const auto& directory:chartDirectories)if(libraryContains(current_,directory))present=true;
            if(!present){current_=root_;build(charts);return;}
        }
        if(rootSongs_)children.clear();
        folders.assign(children.begin(),children.end());
    }
    bool isRoot()const{return !all_&&!rootSongs_&&current_==root_;}
    bool back(){if(isRoot())return false;if(rootSongs_){rootSongs_=false;return true;}if(all_)all_=false;else if(isLibraryFolder(current_))current_=root_;else current_=current_.parent_path();return true;}
    // Negative list IDs: -1 parent, -2 ALL SONGS, -3 and below real folders.
    void enter(int id){if(id==-1)back();else if(id==-2){all_=true;rootSongs_=false;}else if(id<=-3&&size_t(-id-3)<folders.size()){const auto destination=folders[size_t(-id-3)];rootSongs_=destination==L"ROOT SONGS";current_=rootSongs_?root_:destination;all_=false;}}
};
}
