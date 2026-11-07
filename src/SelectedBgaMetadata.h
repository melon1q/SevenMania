#pragma once
#include "BgaChartSnapshot.h"
#include <condition_variable>
#include <mutex>
#include <thread>
// One replaceable request and one immutable result. No audio/image/video decode.
class SelectedBgaMetadata {
    std::mutex mutex_;std::condition_variable changed_;std::thread worker_;
    bool stop_{};uint64_t revision_{},consumed_{};std::filesystem::path requested_;
    std::shared_ptr<const bms::Chart> ready_;
    void run(){for(;;){std::filesystem::path path;uint64_t serial;
        {std::unique_lock lock(mutex_);changed_.wait(lock,[&]{return stop_||revision_!=consumed_;});if(stop_)return;serial=consumed_=revision_;path=requested_;}
        if(path.empty())continue;
        std::shared_ptr<bms::Chart> result;
        try{auto parsed=bms::BmsParser().parse(path).chart;auto source=bgaChartSnapshot(parsed);
            result=std::make_shared<bms::Chart>(*source);result->path=parsed.path;result->titleImage=parsed.titleImage;result->fallbackVideo=parsed.fallbackVideo;result->previewStartSeconds=parsed.previewStartSeconds;result->durationSeconds=parsed.durationSeconds;
        }catch(...){}
        {std::lock_guard lock(mutex_);if(serial==revision_)ready_=std::move(result);}
    }}
public:
    ~SelectedBgaMetadata(){{std::lock_guard lock(mutex_);stop_=true;}changed_.notify_all();if(worker_.joinable())worker_.join();}
    std::shared_ptr<const bms::Chart> get(const std::filesystem::path& path){
        std::lock_guard lock(mutex_);if(requested_!=path){requested_=path;ready_.reset();++revision_;if(!worker_.joinable())worker_=std::thread(&SelectedBgaMetadata::run,this);changed_.notify_one();}
        return ready_&&ready_->path==path?ready_:nullptr;
    }
    void reset(){std::lock_guard lock(mutex_);requested_.clear();ready_.reset();++revision_;changed_.notify_one();}
};
