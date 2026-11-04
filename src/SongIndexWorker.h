#pragma once
#include "SongScanner.h"
#include <mutex>
#include <optional>
#include <thread>
namespace bms {
class SongIndexWorker{
    std::thread worker_;std::atomic_bool cancelled_{},running_{};std::mutex mutex_;std::optional<SongIndexUpdate> pending_;
public:
    ~SongIndexWorker(){cancel();}
    void cancel(){cancelled_=true;if(worker_.joinable())worker_.join();std::lock_guard lock(mutex_);pending_.reset();}
    bool running()const{return running_;}
    template<class Notify>void start(std::filesystem::path root,Notify notify){SongLibrarySources sources;sources.roots={root};start(std::move(sources),notify);}
    template<class Notify>void start(SongLibrarySources sources,Notify notify,LibraryProgress progress={},std::filesystem::path target={},std::vector<Chart> previous={},std::vector<std::filesystem::path> previousFolders={}){
        cancel();cancelled_=false;running_=true;
        worker_=std::thread([this,sources=std::move(sources),notify,progress,target,previous=std::move(previous),previousFolders=std::move(previousFolders)]()mutable{
            try {
                auto update=scanSongLibraries(sources,&cancelled_,progress,target,std::move(previous),std::move(previousFolders));
                if(!cancelled_){std::lock_guard lock(mutex_);pending_=std::move(update);}
            }catch(...){
                if(!cancelled_){SongIndexUpdate error;error.complete=false;error.warnings.push_back(L"라이브러리 확인에 실패했습니다. 이전 곡 목록을 유지합니다.");std::lock_guard lock(mutex_);pending_=std::move(error);}
            }
            running_=false;if(!cancelled_)notify();
        });
    }
    bool hasPending(){std::lock_guard lock(mutex_);return pending_.has_value();}
    std::optional<SongIndexUpdate> take(){std::lock_guard lock(mutex_);auto out=std::move(pending_);pending_.reset();return out;}
};
}
