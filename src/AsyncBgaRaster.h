#pragma once
#include "BgaChartSnapshot.h"
#include "BgaRaster.h"
#include <thread>
#include <array>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <unordered_set>

// UI owns the display cache. A separate raster owns the worker's decode cache.
class AsyncBgaRaster : public BgaRaster {
    struct Job { std::wstring id,key; bool black{}; bool colored{}; bms::BgaColor color{}; int width{},height{}; };
    struct Ready { Job job; Scaled image; };
    std::shared_ptr<const bms::Chart> chart_;
    std::array<std::vector<bms::BgaEvent>,4> events_;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<Job> jobs_;
    std::deque<Ready> ready_;
    std::unordered_set<std::wstring> pending_,failed_;
    bool stop_{};
    bool synchronousOverflow_{};
    size_t readyBytes_{};
    static std::wstring keyFor(const bms::Chart& chart,const std::wstring& id,bool black,const bms::BgaColor* color) {
        const auto resolved=bms::assetId(chart,id);
        const auto found=chart.bitmapColors.find(resolved);
        if(!color&&found!=chart.bitmapColors.end())color=&found->second;
        if(color)black=true;
        return resolved+(black?L"|black":L"")+(color?L"|"+std::to_wstring(color->red)+L","+std::to_wstring(color->green)+L","+std::to_wstring(color->blue):L"");
    }
    void request(const std::wstring& id,bool black,const bms::BgaColor* color) {
        if(id.empty()||!chart_)return;
        const auto key=keyFor(*chart_,id,black,color);
        if(scaled_.contains(key))return;
        std::lock_guard lock(mutex_);
        if(pending_.contains(key)||failed_.contains(key)||jobs_.size()>=8)return;
        Job job{id,key,black,color!=nullptr,color?*color:bms::BgaColor{},width_,height_};
        jobs_.push_back(std::move(job));pending_.insert(key);changed_.notify_all();
    }
public:
    ~AsyncBgaRaster(){reset();}
    void canvas(int width,int height){
        BgaRaster::canvas(width,height);
        // Very large multi-layer canvases cannot retain four full layers in the bounded cache.
        synchronousOverflow_=width>0&&height>0&&uint64_t(width)*height*16>scaledLimit;
    }
    void reset() {
        {std::lock_guard lock(mutex_);stop_=true;}changed_.notify_all();
        if(worker_.joinable())worker_.join();
        jobs_.clear();ready_.clear();pending_.clear();failed_.clear();readyBytes_=0;chart_.reset();for(auto& events:events_)events.clear();
        BgaRaster::reset();
    }
    void begin(const bms::Chart& chart,int width,int height,double time) {
        reset();canvas(width,height);chart_=bgaChartSnapshot(chart);stop_=false;
        for(const auto& event:chart.bga)events_[static_cast<int>(event.layer)].push_back(event);
        worker_=std::thread([this]{
            const auto com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            {
                BgaRaster raster; raster.backgroundDecode_=true;
                for(;;){
                    Job job;
                    {std::unique_lock lock(mutex_);changed_.wait(lock,[&]{return stop_||!jobs_.empty();});if(stop_)break;job=std::move(jobs_.front());jobs_.pop_front();}
                    try {
                        raster.canvas(job.width,job.height);
                        auto* image=raster.scaled(*chart_,job.id,job.black,job.colored?&job.color:nullptr);
                        if(image){
                            Scaled completed=std::move(*image);raster.scaled_.clear();raster.scaledBytes_=0;
                            const auto bytes=completed.pixels.size()*4;
                            std::unique_lock lock(mutex_);
                            changed_.wait(lock,[&]{return stop_||(ready_.size()<2&&(ready_.empty()||readyBytes_+bytes<=64ull*1024*1024));});
                            if(stop_)break;
                            readyBytes_+=bytes;ready_.push_back({std::move(job),std::move(completed)});
                        }else {std::lock_guard lock(mutex_);pending_.erase(job.key);failed_.insert(job.key);}
                    }catch(...){std::lock_guard lock(mutex_);pending_.erase(job.key);failed_.insert(job.key);}
                    changed_.notify_all();
                }
            }
            if(SUCCEEDED(com))CoUninitialize();
        });
        update(time); // Prime the current and near-future assets before audio starts.
    }
    bool initialPrepared() {pump();std::lock_guard lock(mutex_);return pending_.empty();}
    void start(const bms::Chart& chart,int width,int height,double time) {
        begin(chart,width,height,time);
        for(;;){
            pump();std::unique_lock lock(mutex_);
            if(pending_.empty())break;
            changed_.wait_for(lock,std::chrono::milliseconds(5));
        }
    }
    bool pump() {
        bool changed=false;
        std::deque<Ready> complete;
        {std::lock_guard lock(mutex_);complete.swap(ready_);readyBytes_=0;for(const auto& item:complete)pending_.erase(item.job.key);}
        changed_.notify_all();
        for(auto& item:complete){
            if(item.job.width!=width_||item.job.height!=height_)continue;
            const auto bytes=item.image.pixels.size()*4;
            if(scaled_.contains(item.job.key))continue;
            makeRoom(scaled_,scaledBytes_,bytes,scaledLimit);scaledBytes_+=bytes;item.image.lastUsed=++clock_;
            scaled_.emplace(std::move(item.job.key),std::make_shared<Scaled>(std::move(item.image)));changed=true;
        }return changed;
    }
    bool update(double time) {
        const bool changed=pump();if(!chart_)return changed;
        std::array<const bms::BgaEvent*,4> active{};
        auto next=std::upper_bound(chart_->bga.begin(),chart_->bga.end(),time,[](double t,const bms::BgaEvent& e){return t<e.timeSeconds;});
        // Only four active IDs; future work is bounded by the queue and a two-second horizon.
        for(int layer=0;layer<4;++layer){
            const auto& events=events_[layer];const auto found=std::upper_bound(events.begin(),events.end(),time,[](double t,const bms::BgaEvent& e){return t<e.timeSeconds;});
            if(found!=events.begin())active[layer]=&*std::prev(found);
        }
        const auto schedule=[&](const bms::BgaEvent& event,double at){
            const int layer=static_cast<int>(event.layer);const auto& colors=chart_->bgaColors[layer];
            const auto color=std::upper_bound(colors.begin(),colors.end(),at,[](double t,const bms::ColorEvent& e){return t<e.timeSeconds;});
            request(event.imageId,!chart_->bmson&&(layer==2||layer==3),color==colors.begin()?nullptr:&std::prev(color)->color);
        };
        for(const auto* event:active)if(event)schedule(*event,time);
        if(chart_->bitmaps.contains(L"00"))request(L"00",false,nullptr);
        const auto frameBytes=uint64_t(std::max(1,width_))*std::max(1,height_)*4;
        const int ahead=std::clamp(int(scaledLimit/frameBytes)-6,0,8);
        for(int count=0;next!=chart_->bga.end()&&next->timeSeconds<=time+2&&count<ahead;++next,++count)schedule(*next,next->timeSeconds);
        return changed;
    }
    bool prepared(const bms::Chart& chart,const std::wstring& id,bool black=false,const bms::BgaColor* color=nullptr) {
        if(!chart_||synchronousOverflow_||id.empty())return true;
        const auto key=keyFor(chart,id,black,color);
        if(scaled_.contains(key))return true;
        {std::lock_guard lock(mutex_);if(failed_.contains(key))return true;}
        request(id,black,color);return false;
    }
    bool draw(const bms::Chart& chart,const std::wstring& id,uint32_t* target,unsigned opacity=255,bool black=false,const bms::BgaColor* color=nullptr) {
        if(chart_&&!synchronousOverflow_&&!scaled_.contains(keyFor(chart,id,black,color))){request(id,black,color);return false;}
        return BgaRaster::draw(chart,id,target,opacity,black,color);
    }
    std::shared_ptr<const uint32_t> canvasFrame(const bms::Chart& chart, const std::wstring& id, bool blackKey, bool requireOpaque) {
        if (id.empty()) return {};
        const auto entry = scaled_.find(keyFor(chart,id,blackKey,nullptr));
        if (entry == scaled_.end()) return {};
        const auto& image = entry->second;
        if ((requireOpaque && !image->opaque) || image->x || image->y || image->width != width_ || image->height != height_) return {};
        image->lastUsed = ++clock_;
        return {image, image->pixels.data()};
    }
    bool coversCanvas(const bms::Chart& chart,const std::wstring& id){
        if(chart_&&!synchronousOverflow_&&!scaled_.contains(keyFor(chart,id,false,nullptr))){request(id,false,nullptr);return false;}
        return BgaRaster::coversCanvas(chart,id);
    }
};
