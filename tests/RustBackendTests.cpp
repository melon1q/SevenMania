#include "RustBackend.h"
#include "SongGrouping.h"
#include "SongLibrary.h"
#include "JsonValue.h"
#include <algorithm>
#include <chrono>
#include <bit>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>
using namespace bms;
void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
std::vector<std::wstring> cppLines(const std::wstring& text){std::wistringstream stream(text);std::wstring line;std::vector<std::wstring> out;while(std::getline(stream,line)){if(!line.empty()&&line.back()==L'\r')line.pop_back();out.push_back(std::move(line));}return out;}
bool equal(const JsonValue& a,const JsonValue& b){
    if(a.type!=b.type||a.number!=b.number||a.boolean!=b.boolean||a.string!=b.string||a.array.size()!=b.array.size()||a.object.size()!=b.object.size())return false;
    for(size_t i=0;i<a.array.size();++i)if(!equal(a.array[i],b.array[i]))return false;
    auto x=a.object.begin(),y=b.object.begin();for(;x!=a.object.end();++x,++y)if(x->first!=y->first||!equal(x->second,y->second))return false;return true;
}
void jsonParity(const std::wstring& text){JsonValue a,b;bool ca=true,cb=true;try{a=JsonReaderCpp(text).read();}catch(...){ca=false;}try{b=JsonReader(text).read();}catch(...){cb=false;}require(ca==cb&&(!ca||equal(a,b)),"JSON parity");}
uint64_t chartHash(const std::vector<Chart>& charts){
    uint64_t hash=14695981039346656037ull;
    const auto n=[&](uint64_t value){hash^=value;hash*=1099511628211ull;};
    const auto d=[&](double value){n(std::bit_cast<uint64_t>(value));};
    const auto text=[&](const std::wstring& value){n(value.size());for(auto c:value)n(c);};
    for(const auto& c:charts){
        text(c.path.wstring());text(c.title);text(c.subtitle);text(c.artist);text(c.genre);text(c.playLevel);text(c.blockReason);text(c.partialLoadReason);
        n(c.playable);n(c.partialLoad);n(c.bmson);n(c.rank);n(static_cast<unsigned>(c.mode));d(c.initialBpm);d(c.durationSeconds);d(c.referenceBpm);d(c.extendedRank);d(c.total);n(c.notes.size());
        for(const auto& v:c.notes){n(v.lane);d(v.timeSeconds);d(v.lengthSeconds);text(v.sampleId);text(v.endSampleId);n(static_cast<unsigned>(v.mode));}
        for(const auto& v:c.bgm){d(v.timeSeconds);text(v.sampleId);}
        for(const auto& v:c.tempos){d(v.timeSeconds);d(v.bpm);}
        for(const auto& v:c.scroll){d(v.timeSeconds);d(v.positionSeconds);d(v.speed);d(v.acceleration);}
        for(const auto& v:c.invisible){n(v.lane);d(v.timeSeconds);text(v.sampleId);}
        for(const auto& v:c.mines){n(v.lane);d(v.timeSeconds);d(v.damage);}
        for(const auto& v:c.bga){d(v.timeSeconds);n(static_cast<unsigned>(v.layer));text(v.imageId);}
        std::map<std::wstring,std::filesystem::path> samples(c.samples.begin(),c.samples.end());for(const auto& [key,path]:samples){text(key);text(path.wstring());}
    }return hash;
}
int wmain(int argc,wchar_t** argv){try{
#ifndef BMS_USE_RUST_BACKEND
    std::cout<<"Rust backend disabled in this configuration\n";return 0;
#else
    require(rust_backend::enabled(),"backend disabled");
    std::mt19937 random(20261024);
    for(const auto* s:{L"",L"\n",L"a\r\nb\n",L"a\n\n",L"a\rb\r",L"\r\n"}){std::vector<std::wstring> lines;require(rust_backend::splitLines(s,lines)&&lines==cppLines(s),"lines parity");}
    for(unsigned i=0;i<10000;++i){
        std::wstring s;for(unsigned j=0,n=random()%100;j<n;++j)s+=static_cast<wchar_t>(random()%7==0?L'\n':random()%7==0?L'\r':random());
        std::vector<std::wstring> lines;const bool split=rust_backend::splitLines(s,lines);if(!split||lines!=cppLines(s)){std::cerr<<"case="<<i<<" split="<<split<<" rust="<<lines.size()<<" cpp="<<cppLines(s).size()<<" input=";for(auto c:s)std::cerr<<std::hex<<unsigned(c)<<',';std::cerr<<std::dec<<'\n';throw std::runtime_error("random lines parity");}
        std::wstring title;require(rust_backend::normalizeTitle(s,title)&&title==songTitleWithoutChartLabelCpp(s),"random title parity");
    }
    for(const auto* s:{L"Signager (Another)",L"-零-【NORMAL】",L"Amplifying Effect -SP HYPER-",L"Anti Function -SP STARTER-",L"Song (Remix)",L"Song [A] (Normal)",L"Song ［ＨＹＰＥＲ］",L"Song -Normal LV12keys-",L"\tTitle\u3000"}){
        std::wstring title;require(rust_backend::normalizeTitle(s,title)&&title==songTitleWithoutChartLabelCpp(s),"title parity");
    }
    for(const auto* s:{L"null",L"[true,false,0,-0,1e3]",LR"({"a":1,"a":2,"u":"\ud800\u0000"})",L"",L"[1,]",L"01",L"1.",L"1e999",L"1e-999",L"1e-308",L"true false"})jsonParity(s);
    for(unsigned i=0;i<2000;++i){jsonParity(L"{\"a\":["+std::to_wstring(random())+L",\"unicode零\",false],\"x\":-0.125e2}");jsonParity(std::wstring(i%140,L'[')+L"0"+std::wstring(i%140,L']'));}
    // Simultaneous calls exercise ownership and thread-safe counters without changing modes.
    std::atomic_bool okay{true};std::vector<std::jthread> workers;for(int i=0;i<8;++i)workers.emplace_back([&]{try{for(int j=0;j<1000;++j)jsonParity(L"{\"a\":[1,2,3]}");}catch(...){okay=false;}});workers.clear();require(okay,"concurrent JSON parity");
    const std::filesystem::path root=argc>1?argv[1]:L"RustFixtures";std::filesystem::create_directories(root/L"곡 零");
    for(const auto* name:{L"a.bms",L"b.BME",L"c.bml",L"d.pms",L"e.bmson",L"f.mbm",L"ignore.wav"}){std::ofstream f(root/L"곡 零"/name);f<<"#TITLE Rust test\n#BPM 120\n#00111:01\n";}
    std::vector<std::filesystem::path> actual,expected;require(rust_backend::discoverCharts(root,nullptr,actual),"discovery");
    for(const auto& e:std::filesystem::recursive_directory_iterator(root)){const auto ext=toLower(e.path().extension().wstring());if(e.is_regular_file()&&(ext==L".bms"||ext==L".bme"||ext==L".bml"||ext==L".pms"||ext==L".bmson"||ext==L".mbm"))expected.push_back(e.path());}
    std::sort(actual.begin(),actual.end());std::sort(expected.begin(),expected.end());require(actual==expected,"discovery parity");
    std::atomic_bool cancel{true};bool cancelled=false;try{rust_backend::discoverCharts(root,&cancel,actual);}catch(const LibraryError&){cancelled=true;}require(cancelled,"cancel");
    for(unsigned i=1;i<=4;++i)require(rust_backend::successes(i)>0,"unused Rust operation");
    std::cout<<"PASS: UTF16 line/title fuzz=10000 JSON cases=12000 concurrency=8 discovery/cancellation and all 4 operations used\n";
    // Same executable, alternating order, includes bridge and C++ AST allocation cost.
    std::wstring json=L"{\"notes\":[";for(int i=0;i<4000;++i){if(i)json+=L',';json+=L"{\"x\":1,\"y\":"+std::to_wstring(i)+L",\"l\":0}";}json+=L"]}";
    for(int round=-2;round<12;++round)for(int slot=0;slot<2;++slot){const bool rust=((round+slot)&1)!=0;rust_backend::setEnabledForValidation(rust);const auto begin=std::chrono::steady_clock::now();size_t checksum=0;for(int i=0;i<8;++i)checksum+=JsonReader(json).read()[L"notes"].array.size();const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();if(round>=0)std::cout<<"BENCH,json,"<<round<<','<<(rust?"rust":"cpp")<<','<<ms<<','<<checksum<<'\n';}
    rust_backend::setEnabledForValidation(true);
    if(argc>2){uint64_t baselineHash=0;for(int round=-1;round<8;++round)for(int slot=0;slot<2;++slot){const bool rust=((round+slot)&1)!=0;rust_backend::setEnabledForValidation(rust);const auto begin=std::chrono::steady_clock::now();auto charts=scanSongFolder(argv[2]);std::sort(charts.begin(),charts.end(),[](const Chart&a,const Chart&b){return a.path<b.path;});const auto hash=chartHash(charts);if(baselineHash)require(hash==baselineHash,"scan semantic mismatch");else baselineHash=hash;const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();if(round>=0)std::cout<<"BENCH,scan,"<<round<<','<<(rust?"rust":"cpp")<<','<<ms<<','<<hash<<','<<charts.size()<<'\n';}}
    rust_backend::setEnabledForValidation(true);return 0;
#endif
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
