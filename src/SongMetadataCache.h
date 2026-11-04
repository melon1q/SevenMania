#pragma once
#include "BmsParser.h"
#include "SongResourceIdentity.h"
#include <windows.h>
#include <fstream>
#include <limits>
#include <cmath>
#include <set>
#include <atomic>

namespace bms {
// Disposable selection metadata only. Never deserialize notes or executable data.
class SongMetadataCache {
    std::filesystem::path directory_;
    std::filesystem::path sourceRoot_;
    uint64_t seed_;
    template<class T> static void io(std::iostream& f,T& v,bool write) {
        if(write)f.write(reinterpret_cast<const char*>(&v),sizeof(v));
        else f.read(reinterpret_cast<char*>(&v),sizeof(v));
    }
    static void text(std::iostream& f,std::wstring& v,bool write){
        uint32_t n=write?uint32_t(v.size()):0;io(f,n,write);
        if(n>32768)throw std::runtime_error("cache string limit");
        if(!write)v.resize(n);
        if(write)f.write(reinterpret_cast<const char*>(v.data()),n*sizeof(wchar_t));
        else f.read(reinterpret_cast<char*>(v.data()),n*sizeof(wchar_t));
    }
    static void path(std::iostream& f,std::filesystem::path& p,bool w){auto s=p.wstring();text(f,s,w);if(!w)p=s;}
    std::filesystem::path entry(const std::filesystem::path& p)const{
        uint64_t h=14695981039346656037ull;
        for(auto c:toLower(std::filesystem::absolute(p).lexically_normal().wstring()))h=(h^uint16_t(c))*1099511628211ull;
        return directory_/(std::to_wstring(h)+L".meta");
    }
    void transfer(std::iostream& f,Chart& c,bool w,bool validate=true){
        uint64_t magic=0x534D434143480003ull,seed=seed_,size=(w||validate)?std::filesystem::file_size(c.path):0;
        int64_t time=(w||validate)?std::filesystem::last_write_time(c.path).time_since_epoch().count():0;
        int64_t parent=(w||validate)?std::filesystem::last_write_time(c.path.parent_path()).time_since_epoch().count():0;
        const auto expectedMagic=magic,expectedSeed=seed_,expectedSize=size;
        const auto expectedTime=time,expectedParent=parent;auto expectedPath=c.path;
        io(f,magic,w);io(f,seed,w);io(f,size,w);io(f,time,w);io(f,parent,w);path(f,c.path,w);
        if(!w&&(magic!=expectedMagic||seed!=expectedSeed||(validate&&(size!=expectedSize||time!=expectedTime||parent!=expectedParent||c.path!=expectedPath))))throw std::runtime_error("stale cache");
        for(auto* s:{&c.title,&c.subtitle,&c.artist,&c.genre,&c.playLevel,&c.subartist,&c.comment,&c.blockReason,&c.partialLoadReason,&c.catalogGroupKey})text(f,*s,w);
        for(auto* p:{&c.banner,&c.titleImage,&c.previewAudio,&c.fallbackVideo,&c.finishImage})path(f,*p,w);
        int mode=int(c.mode),play=c.playable,partial=c.partialLoad,defined=c.previewStartDefined;
        io(f,mode,w);io(f,play,w);io(f,partial,w);io(f,defined,w);
        io(f,c.rank,w);int rankDefault=c.rankDefaulted;io(f,rankDefault,w);text(f,c.judgementSpec,w);
        if(c.rank<0||c.rank>3||rankDefault<0||rankDefault>1)throw std::runtime_error("invalid rank cache");
        c.rankDefaulted=rankDefault!=0;
        io(f,c.difficulty,w);io(f,c.initialBpm,w);io(f,c.durationSeconds,w);io(f,c.previewStartSeconds,w);io(f,c.previewDurationSeconds,w);
        if(!f||mode<0||mode>int(ChartMode::Other)||!std::isfinite(c.initialBpm)||!std::isfinite(c.durationSeconds)||!std::isfinite(c.previewStartSeconds)||!std::isfinite(c.previewDurationSeconds)||c.durationSeconds<0||c.previewStartSeconds<0||c.previewDurationSeconds<0||c.durationSeconds>1e9||c.previewStartSeconds>1e9||c.previewDurationSeconds>1e9||play<0||play>1||partial<0||partial>1||defined<0||defined>1)throw std::runtime_error("invalid cache");
        c.mode=static_cast<ChartMode>(mode);c.playable=play!=0;c.partialLoad=partial!=0;c.previewStartDefined=defined!=0;
    }
public:
    SongMetadataCache(const std::filesystem::path& root,uint64_t seed,const std::filesystem::path& storage={}):directory_(storage.empty()?root/L".sevenmania-index":storage),sourceRoot_(root),seed_(seed){std::error_code ec;std::filesystem::create_directories(directory_,ec);}
    bool load(const std::filesystem::path& p,Chart& out){try{Chart c;c.path=p;std::fstream f(entry(p),std::ios::binary|std::ios::in);if(!f)return false;transfer(f,c,false);c.catalogOnly=true;out=std::move(c);return true;}catch(...){return false;}}
    std::vector<Chart> snapshot(const std::atomic_bool* cancel=nullptr){
        std::vector<Chart> out;std::error_code ec;
        for(std::filesystem::directory_iterator it(directory_,ec),end;!ec&&it!=end;it.increment(ec)){
            if(cancel&&cancel->load())break;
            if(it->path().extension()!=L".meta")continue;
            try{Chart c;std::fstream f(it->path(),std::ios::binary|std::ios::in);transfer(f,c,false,false);
                const auto relative=c.path.lexically_relative(sourceRoot_);
                if(relative.empty()||*relative.begin()==L".."||!c.path.is_absolute())continue;
                c.catalogOnly=true;out.push_back(std::move(c));
            }catch(...){}
        }return out;
    }
    void prune(const std::vector<Chart>& charts){std::set<std::filesystem::path> keep;for(const auto& c:charts)keep.insert(entry(c.path));std::error_code ec;
        for(std::filesystem::directory_iterator it(directory_,ec),end;!ec&&it!=end;it.increment(ec))if(it->path().extension()==L".meta"&&!keep.contains(it->path())){std::error_code ignored;std::filesystem::remove(it->path(),ignored);}}
    void invalidate(){prune({});}
    static Chart metadata(const Chart& x){Chart c;c.catalogOnly=true;
#define META(field) c.field=x.field;
        c.catalogGroupKey=songResourceIdentity(x);
        META(path) META(title) META(subtitle) META(artist) META(genre) META(playLevel) META(subartist) META(comment) META(blockReason) META(partialLoadReason)
        META(banner) META(titleImage) META(previewAudio) META(fallbackVideo) META(finishImage) META(mode) META(playable) META(partialLoad) META(previewStartDefined)
        META(rank) META(rankDefaulted) META(judgementSpec) META(difficulty) META(initialBpm) META(durationSeconds) META(previewStartSeconds) META(previewDurationSeconds)
#undef META
        return c;}
    void save(const Chart& chart){try{auto c=metadata(chart);auto target=entry(c.path),temp=target;temp+=L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
        {std::fstream f(temp,std::ios::binary|std::ios::out|std::ios::trunc);if(!f)return;transfer(f,c,true);f.flush();if(!f)throw std::runtime_error("cache write failed");}
        if(!MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){std::error_code ec;std::filesystem::remove(temp,ec);}
    }catch(...){/* Cache failure never prevents normal parsing. */}}
};
}
