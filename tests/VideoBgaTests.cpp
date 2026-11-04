#include "AsyncVideoBga.h"
#include "BgaVideoLayers.h"
#include "BgaTimeline.h"
#include <fstream>
#include <vfw.h>
#include <iostream>
#include <stdexcept>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

#include "VideoFixtures.h"
using fixtures::writeVideo;

int wmain(int argc, wchar_t** argv) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
    if (FAILED(MFStartup(MF_VERSION))) { CoUninitialize(); return 1; }
    AVIFileInit();
    int result = 0;
    try {
        bool longMpeg=false;
        for(int i=2;i<argc;++i)if(std::wstring_view(argv[i])==L"--long-mpeg")longMpeg=true;
        const auto root = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path(L"Validation/VideoTests");
        std::filesystem::create_directories(root);
        const auto clip = root / L"timing.avi";
        writeVideo(clip);
        const auto offsetClip = root / L"offset.avi";
        writeVideo(offsetClip, 10);
        {
            const auto mpeg=std::filesystem::path(BMS_VIDEO_FIXTURE_DIR)/L"colors.m1v";
            auto red=[](uint32_t p){return ((p>>16)&255)>200&&((p>>8)&255)<40&&(p&255)<40&&(p>>24)==255;};
            auto green=[](uint32_t p){return ((p>>8)&255)>200&&((p>>16)&255)<40&&(p&255)<40;};
            auto blue=[](uint32_t p){return (p&255)>200&&((p>>8)&255)<40&&((p>>16)&255)<40;};
            VideoBga video;
            require(video.open(mpeg)&&video.update(0)&&video.width()==16&&video.height()==16&&red(video.pixels().front()),"MPEG-1 first frame or BGRA conversion failed");
            require(video.update(1.1,false)&&green(video.pixels().front()),"MPEG-1 chart clock failed");
            require(video.update(2.2,false)&&blue(video.pixels().front()),"MPEG-1 final color missing");
            require(video.update(3.1,true)&&red(video.pixels().front()),"MPEG-1 EOF looping failed");
            require(video.update(1.2,true)&&green(video.pixels().front()),"MPEG-1 loop clock failed");
            require(video.update(0)&&red(video.pixels().front()),"MPEG-1 rewind failed");
            require(video.open(mpeg)&&video.update(1.2,false)&&green(video.pixels().front()),"MPEG-1 initial practice seek failed");
            video.reset();require(!video.ready(),"MPEG-1 reset retained old frame");
            const auto unicode=root/L"한글 영상.mpg";
            std::filesystem::copy_file(mpeg,unicode,std::filesystem::copy_options::overwrite_existing);
            require(video.open(unicode)&&video.update(0)&&red(video.pixels().front()),"MPEG-1 Unicode path failed");
            for(int i=0;i<10;++i){require(video.open(unicode)&&video.update(0),"MPEG-1 repeated open failed");video.reset();}
            const auto broken=root/L"invalid.m1v";{std::ofstream file(broken);file<<"not MPEG";}
            require(!video.open(broken),"invalid elementary stream accepted");
            std::ifstream encoded(mpeg,std::ios::binary);
            std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(encoded),std::istreambuf_iterator<char>()};
            require(bytes.size()>16&&bytes[3]==0xb3,"MPEG fixture sequence header missing");
            const auto damaged=root/L"damaged.m1v";
            auto writeDamaged=[&](const std::vector<unsigned char>& data){std::ofstream file(damaged,std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<const char*>(data.data()),data.size());};
            Mpeg1Video fallback;
            auto oversized=bytes;oversized[4]=0xff;oversized[5]|=0xf0;writeDamaged(oversized);
            require(!fallback.open(damaged),"oversized MPEG-1 header accepted");
            auto mpeg2=bytes;mpeg2.insert(mpeg2.begin()+12,{0,0,1,0xb5,0x10});writeDamaged(mpeg2);
            require(!fallback.open(damaged),"MPEG-2 sent to MPEG-1 decoder");
            bytes.resize(16);writeDamaged(bytes);
            require(!fallback.open(damaged),"truncated MPEG sequence accepted");
        }
        {
            VideoBga video;
            require(video.open(offsetClip) && video.update(0) && video.ready(), "video timestamp offset delayed the first frame");
            require(video.pixels().front() == 0xffff0000u, "wrong first frame for offset video");
            require(video.update(1.1) && video.pixels().front() == 0xff00ff00u, "offset video drifted after its first frame");
            require(video.update(0) && video.pixels().front() == 0xffff0000u, "offset video rewind used the wrong stream timestamp");
            require(video.open(offsetClip) && video.update(2.1) && video.pixels().front() == 0xff0000ffu,
                "initial preview seek treated the seek target as the stream origin");
        }
        {
            VideoBga video;
            require(video.open(clip) && video.update(0) && video.ready(), "first video frame missing at time zero");
            require(video.pixels().front() == 0xffff0000u, "wrong first video frame");
            require(video.update(1.1) && video.pixels().front() == 0xff00ff00u, "video clock advanced incorrectly");
            require(video.update(0) && video.pixels().front() == 0xffff0000u, "video rewind did not restore first frame");
        }
        {
            AsyncVideoBga video;
            video.open(root / L"missing.mp4", 32, 32);
            require(!video.waitUntilPrepared(), "missing video was marked prepared");
            for (int repeat = 0; repeat < 3; ++repeat) {
                video.open(clip, 32, 32);
                require(video.waitUntilPrepared(), "song loading finished before first video frame was prepared");
                const auto first = video.frame();
                require(first && first->width == 32 && first->pixels.front() == 0xffff0000u, "initial scaled video frame unavailable");
                video.request(1.1, 32, 32);
                require(video.waitUntilPrepared() && video.frame()->pixels.front() == 0xff00ff00u, "prepared frame does not follow requested song time");
                video.request(0, 32, 32);
                require(video.waitUntilPrepared() && video.frame()->pixels.front() == 0xffff0000u, "restart completed before rewind frame was prepared");
            }
            const std::atomic_bool cancelled{true};
            require(!video.waitUntilPrepared(&cancelled), "cancelled preparation succeeded");
            video.reset();
            require(!video.ready(), "old video survived reset");
        }
        {
            // Hold more generations than the bounded pool, forcing its overflow
            // path. Published pixels and metadata must remain immutable even
            // across resize, seek, decoder shutdown and reopening.
            AsyncVideoBga video;video.open(clip,64,64);
            require(video.waitUntilPrepared(),"pool first frame unavailable");
            std::vector<std::shared_ptr<const AsyncVideoBga::Frame>> held;
            std::vector<std::vector<uint32_t>> expected;
            for(int i=0;i<8;++i){
                video.request((i%3)*1.1,64+i,64+i);
                require(video.waitUntilPrepared(),"pool overflow stalled publication");
                auto f=video.frame();held.push_back(f);expected.push_back(f->pixels);
            }
            for(size_t i=0;i<held.size();++i)require(held[i]->pixels==expected[i]&&held[i]->width==64+int(i),"pool overwrote a published frame");
            held.clear();expected.clear();
            std::vector<const void*> addresses;
            for(int i=0;i<90;++i){
                video.request((i%3)*1.1,64,64);
                require(video.waitUntilPrepared(),"pool reuse failed to publish");
                const auto f=video.frame();
                const uint32_t color=i%3==0?0xffff0000u:i%3==1?0xff00ff00u:0xff0000ffu;
                require(f->pixels.front()==color,"pool exposed old or partially written pixels");
                if(i>6&&std::find(addresses.begin(),addresses.end(),f.get())==addresses.end())addresses.push_back(f.get());
            }
            require(addresses.size()<=3,"pool did not reuse its bounded slots");
            auto retained=video.frame();const auto pixels=retained->pixels;
            video.reset();require(retained->pixels==pixels&&!video.ready(),"reset invalidated an external frame");
            video.open(clip,32,32);require(video.waitUntilPrepared()&&retained->pixels==pixels,"reopen reused a live old frame");
            // Above the per-slot retention budget the original allocation path
            // must still publish correct images and retain old external owners.
            video.open(clip,2048,1080);require(video.waitUntilPrepared(),"large frame fallback failed");
            const auto large=video.frame();require(large->pixels.front()==0xffff0000u,"large first frame incorrect");
            video.request(1.1,2048,1080);require(video.waitUntilPrepared()&&video.frame()->pixels.front()==0xff00ff00u&&large->pixels.front()==0xffff0000u,"large frame fallback overwrote old pixels");
        }
        {
            // Present but broken original + a playable, same-stem conversion.
            const auto broken=root/L"converted.mpeg";
            {std::ofstream file(broken,std::ios::binary);file<<"not a playable MPEG";}
            const auto replacement=root/L"converted.avi";writeVideo(replacement);
            bms::Chart chart;chart.bitmaps[L"01"]=broken;chart.fallbackVideo=replacement;
            chart.bga={{2,bms::BgaLayer::Base,L"01"},{4,bms::BgaLayer::Base,L"01"}};
            BgaVideoLayers layers;layers.prepare(chart,32,32);
            require(layers.declared() && layers.failures()==0,"same-stem video replacement did not prepare");
            require(!layers.frame(chart,0,1.9,32,32,true),"replacement played before its authored BGA event");
            auto frame=layers.frame(chart,0,2,32,32,true);
            require(frame && frame->pixels.front()==0xffff0000u,"replacement lost its authored start");
            frame=layers.frame(chart,0,3.1,32,32,true);
            require(frame && frame->pixels.front()==0xff00ff00u,"replacement did not follow the event-relative clock");
            frame=layers.frame(chart,0,4,32,32,true);
            require(frame && frame->pixels.front()==0xffff0000u,"repeated BGA event did not restart replacement");
            layers.reset();
            chart.bitmaps[L"01"]=root/L"unrelated.mpeg";
            layers.prepare(chart,32,32);
            require(layers.failures()==1 && !layers.frame(chart,0,2,32,32,true),"unrelated movie replaced an authored BGA");
            const std::atomic_bool cancel{true};layers.prepare(chart,32,32,&cancel);
            require(!layers.declared() && layers.failures()==0,"cancelled video preparation continued");
        }
        {
            bms::Chart many;
            for(int i=0;i<40;++i){const auto id=std::to_wstring(i+100);many.bitmaps[id]=clip;many.bga.push_back({double(i*4),bms::BgaLayer::Base,id});}
            BgaVideoLayers layers;layers.prepare(many,32,32);
            require(layers.residentPlayers()<=2,"prepared every movie instead of a bounded window");
            for(double time:{0.,5.1,81.1,0.}){
                const auto frame=layers.frame(many,0,time,32,32,true);
                require(frame&&frame->pixels.front()==(std::fmod(time,4.)>=1.?0xff00ff00u:0xffff0000u),"bounded movie pool changed clock or seek");
                require(layers.residentPlayers()<=2,"movie pool grew while switching assets");
            }
            const auto held=layers.frame(many,0,0,32,32,true);layers.reset();
            require(held&&held->pixels.front()==0xffff0000u,"pool reset modified a published frame");
        }
        {
            VideoBga normal;require(normal.open(clip)&&normal.update(0),"normal video open failed");
            require(normal.update(1.9,false)&&normal.pixels().front()==0xff00ff00u,"normal clock selection failed");
            require(normal.update(3.1,true)&&normal.pixels().front()==0xffff0000u,"normal video loop failed");
        }
        {
            bms::Chart layered;for(const auto* id:{L"01",L"02",L"03",L"00"})layered.bitmaps[id]=clip;
            layered.bga={{0,bms::BgaLayer::Base,L"01"},{1,bms::BgaLayer::Layer,L"02"},{2,bms::BgaLayer::Layer2,L"03"}};
            BgaVideoLayers layers;layers.prepare(layered,32,32);
            const auto base=layers.frame(layered,0,2.1,32,32,true);
            const auto middle=layers.frame(layered,2,2.1,32,32,true);
            const auto top=layers.frame(layered,3,2.1,32,32,true);
            const auto poor=layers.frame(layered,1,2.1,32,32,true,2.0);
            require(base&&middle&&top&&poor&&base->pixels.front()==0xff0000ffu&&middle->pixels.front()==0xff00ff00u&&top->pixels.front()==0xffff0000u&&poor->pixels.front()==0xffff0000u,"movie pool mixed independent layer clocks or POOR restart");
            require(layers.residentPlayers()<=8,"layered movie pool exceeded its limit");
        }
        // Optional, read-only checks against real song video formats.
        for (int argument = 2; argument < argc; ++argument) {
            if(std::wstring_view(argv[argument])==L"--long-mpeg")continue;
            if(bms::toLower(std::filesystem::path(argv[argument]).extension().wstring())==L".bms")continue;
            {
                VideoBga probe;
                require(probe.open(argv[argument]), "real video reader could not open media");
                require(probe.update(0) && probe.ready(), "real video timestamp offset delayed the first frame");
            }
            AsyncVideoBga video;
            const auto started = std::chrono::steady_clock::now();
            video.open(argv[argument], 320, 180);
            require(video.waitUntilPrepared(), "real video has no first frame at song time zero");
            const auto firstFrame = video.frame();
            std::wcout << std::filesystem::path(argv[argument]).filename().wstring() << L": first frame prepared in "
                << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() << L" s\n";
            video.request(1.0, 320, 180);
            require(video.waitUntilPrepared(), "real video failed to follow song clock");
            video.request(0.0, 320, 180);
            require(video.waitUntilPrepared() && video.frame()->pixels == firstFrame->pixels,
                "real video failed to prepare the original restart frame");
            if(longMpeg&&std::filesystem::path(argv[argument]).extension()==L".mpg"){
                for(double time:{10.,65.,120.,134.,0.}){
                    video.request(time,320,180);require(video.waitUntilPrepared(),"real MPEG-1 later frame or rewind failed");
                    require(std::abs(video.frame()->timeSeconds-time)<.05,"real MPEG-1 stopped advancing or drifted from chart time");
                }
                require(video.frame()->pixels==firstFrame->pixels,"real MPEG-1 late rewind changed first frame");
            }
        }
        // Optional complete Random chart probe, retaining actual parsed BGA events.
        if(argc>2 && bms::toLower(std::filesystem::path(argv[2]).extension().wstring())==L".bms") {
            const auto parsed=bms::BmsParser(false,0x20261025).parse(argv[2]);
            BgaVideoLayers layers;layers.prepare(parsed.chart,320,180);
            require(parsed.chart.playable && layers.declared() && layers.failures()==0,"real chart BGA failed to prepare");
            const auto first=std::find_if(parsed.chart.bga.begin(),parsed.chart.bga.end(),[](const auto& e){return e.layer==bms::BgaLayer::Base;});
            require(first!=parsed.chart.bga.end(),"real chart has no base BGA event");
            require(!layers.frame(parsed.chart,0,first->timeSeconds-.001,320,180,true),"real BGA started early");
            for(double offset:{0.,1.,10.,0.})require(layers.frame(parsed.chart,0,first->timeSeconds+offset,320,180,true)!=nullptr,"real chart movie disappeared");
            std::cout<<"Real chart BGA start="<<first->timeSeconds<<" seconds; replacement, seek and restart passed\n";
        }
        std::cout << "Video first-frame preparation, chart-clock updates, restart, missing media and cancellation passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    AVIFileExit(); MFShutdown(); CoUninitialize();
    return result;
}
