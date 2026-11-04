#pragma once
#include "BgaChartSnapshot.h"
#include "AsyncVideoBga.h"
#include "ChartRules.h"
#include "ChartOptions.h"
#include <array>
#include <memory>
#include <unordered_map>
#include <unordered_set>

// One scheduler owns all readers. UI calls only publish requests and read snapshots.
class BgaVideoLayers {
    std::array<std::vector<bms::BgaEvent>,4> events_;
    std::shared_ptr<const bms::Chart> chart_;
    struct Output {std::wstring id;double start{};std::shared_ptr<const AsyncVideoBga::Frame> frame;};
    std::array<Output,4> output_;
    std::thread worker_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    bool stop_{true},declared_{};
    std::atomic_bool stopping_{true};
    double time_{},poorTime_=-1;
    int width_{},height_{};
    uint64_t request_{},completed_{};
    size_t failures_{},resident_{};
    const bms::BgaEvent* active(int layer,double time) const {
        const auto& events=events_[layer];
        const auto next=std::upper_bound(events.begin(),events.end(),time,[](double t,const bms::BgaEvent& e){return t<e.timeSeconds;});
        return next==events.begin()?nullptr:&*std::prev(next);
    }
    std::wstring idAt(int layer,double time) const {
        const auto* event=active(layer,time);return event?bms::assetId(*chart_,event->imageId):(layer==1?L"00":L"");
    }
    void run(){
        using Player=std::unique_ptr<AsyncVideoBga>;
        std::array<std::unordered_map<std::wstring,Player>,4> players;
        std::unordered_set<std::wstring> failed;
        const int mode=bms::forcedOptions(*chart_).bgaMode;
        for(;;){
            double time,poor;int width,height;uint64_t serial;
            {std::unique_lock lock(mutex_);changed_.wait(lock,[&]{return stop_||request_!=completed_;});if(stop_)break;time=time_;poor=poorTime_;width=width_;height=height_;serial=request_;}
            std::array<std::vector<std::wstring>,4> wanted;
            for(int layer=0;layer<4;++layer){
                const auto add=[&](const std::wstring& id){const auto file=chart_->bitmaps.find(id);if(file!=chart_->bitmaps.end()&&isVideo(file->second)&&std::find(wanted[layer].begin(),wanted[layer].end(),id)==wanted[layer].end())wanted[layer].push_back(id);};
                add(idAt(layer,time));
                const auto& events=events_[layer];const auto next=std::upper_bound(events.begin(),events.end(),time,[](double t,const bms::BgaEvent&e){return t<e.timeSeconds;});
                // One upcoming event per layer, even if distant, avoids first-use reader creation on the UI.
                if(next!=events.end())add(bms::assetId(*chart_,next->imageId));
                for(auto it=players[layer].begin();it!=players[layer].end();)if(std::find(wanted[layer].begin(),wanted[layer].end(),it->first)==wanted[layer].end())it=players[layer].erase(it);else ++it;
            }
            std::array<Output,4> output;
            for(int layer=0;layer<4;++layer)for(const auto& id:wanted[layer]){
                {std::lock_guard lock(mutex_);if(stop_)return;}
                const auto failureKey=std::to_wstring(layer)+L":"+id;
                if(failed.contains(failureKey))continue;
                auto& player=players[layer][id];
                if(!player){
                    auto candidate=std::make_unique<AsyncVideoBga>();
                    const auto prepare=[&](const std::filesystem::path& path){candidate->open(path,1,1,false,chart_->videoFps);return candidate->waitUntilPrepared(&stopping_);};
                    const auto original=chart_->bitmaps.at(id);bool ok=prepare(original);
                    if(stopping_.load())return;
                    if(!ok)for(const auto* ext:{L".mp4",L".m4v",L".mov",L".wmv",L".avi",L".mpeg",L".mpg",L".m1v",L".m2v"}){
                        auto alternative=original;alternative.replace_extension(ext);
                        if(bms::toLower(alternative.wstring())!=bms::toLower(original.wstring())&&bms::readableAssetExists(alternative)&&prepare(alternative)){ok=true;break;}
                    }
                    if(!ok){failed.insert(failureKey);players[layer].erase(id);std::lock_guard lock(mutex_);++failures_;continue;}
                    player=std::move(candidate);
                }
                if(id!=idAt(layer,time))continue;
                const auto* event=active(layer,time);const double start=layer==1&&poor>=0?poor:(event?event->timeSeconds:0);
                int w=width,h=height;
                if(mode>0)if(const auto frame=player->frame()){const double scale=mode==2?.8:1;w=std::max(1,int(frame->sourceWidth*scale));h=std::max(1,int(frame->sourceHeight*scale));}
                player->request(std::max(0.0,time-start),w,h);player->waitUntilPrepared(&stopping_);
                output[layer]={id,start,player->frame()};
            }
            {std::lock_guard lock(mutex_);output_=std::move(output);resident_=0;for(const auto& layer:players)resident_+=layer.size();completed_=serial;}
            changed_.notify_all();
        }
    }
public:
    ~BgaVideoLayers(){reset();}
    static bool isVideo(const std::filesystem::path& path){const auto ext=bms::toLower(path.extension().wstring());return ext==L".mp4"||ext==L".m4v"||ext==L".mov"||ext==L".wmv"||ext==L".avi"||ext==L".mpeg"||ext==L".mpg"||ext==L".m1v"||ext==L".m2v";}
    size_t failures()const{std::lock_guard lock(mutex_);return failures_;}
    size_t residentPlayers()const{std::lock_guard lock(mutex_);return resident_;}
    bool declared()const{return declared_;}
    void reset(){
        stopping_=true;{std::lock_guard lock(mutex_);stop_=true;}changed_.notify_all();if(worker_.joinable())worker_.join();
        chart_.reset();for(auto& e:events_)e.clear();output_={};failures_=resident_=0;declared_=false;request_=completed_=0;poorTime_=-1;
    }
    void prepare(const bms::Chart& chart,int width,int height,const std::atomic_bool* cancel=nullptr){
        reset();if(cancel&&cancel->load())return;
        chart_=bgaChartSnapshot(chart);
        auto events=chart.bga;if(chart.bitmaps.contains(L"00"))events.insert(events.begin(),{0,bms::BgaLayer::Poor,L"00"});
        for(const auto& e:events){events_[static_cast<int>(e.layer)].push_back(e);const auto f=chart.bitmaps.find(bms::assetId(chart,e.imageId));if(f!=chart.bitmaps.end()&&isVideo(f->second))declared_=true;}
        for(auto& layer:events_)std::stable_sort(layer.begin(),layer.end(),[](const auto&a,const auto&b){return a.timeSeconds<b.timeSeconds;});
        if(!declared_)return;
        width_=width;height_=height;time_=0;stop_=false;stopping_=false;request_=1;worker_=std::thread([this]{try{run();}catch(...){std::lock_guard lock(mutex_);stop_=true;changed_.notify_all();}});
        std::unique_lock lock(mutex_);while(!stop_&&completed_<1){if(cancel&&cancel->load()){lock.unlock();reset();return;}changed_.wait_for(lock,std::chrono::milliseconds(10));}
    }
    std::shared_ptr<const AsyncVideoBga::Frame> frame(const bms::Chart&,int layer,double time,int width,int height,bool wait=false,double poorTime=-1){
        if(!declared_||layer<0||layer>=4)return {};
        const auto id=idAt(layer,time);const auto* event=active(layer,time);const double start=layer==1&&poorTime>=0?poorTime:(event?event->timeSeconds:0);
        std::unique_lock lock(mutex_);time_=time;width_=width;height_=height;poorTime_=poorTime;const auto serial=++request_;changed_.notify_all();
        if(wait)changed_.wait(lock,[&]{return stop_||completed_>=serial;});
        const auto& output=output_[layer];return output.id==id&&output.start==start?output.frame:nullptr;
    }
};
