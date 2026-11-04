#include "BmsParser.h"
#include "BmsControlFlow.h"
#include "BmsText.h"
#include "ChartRules.h"
#include "ChartOptions.h"
#include "ScrollTimeline.h"
#include "LongNoteHold.h"
#include "GrooveGauge.h"
#include "BgaRaster.h"
#include "VideoFixtures.h"
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

static void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
static void checkNear(double a,double b,const char* message){require(std::abs(a-b)<1e-7,message);}
static void write(const std::filesystem::path& path,const std::string& text){std::ofstream out(path,std::ios::binary);out<<text;}
static void wave(const std::filesystem::path& path){
    std::ofstream out(path,std::ios::binary);const auto word=[&](uint32_t n,int bytes){for(int i=0;i<bytes;++i)out.put(static_cast<char>(n>>(i*8)));};
    out<<"RIFF";word(36+4410*2,4);out<<"WAVEfmt ";word(16,4);word(1,2);word(1,2);word(44100,4);word(88200,4);word(2,2);word(16,2);out<<"data";word(4410*2,4);
    for(int i=0;i<4410;++i)word(static_cast<uint16_t>(std::sin(i*440*6.283185307/44100)*4000),2);
}
static void bmp(const std::filesystem::path& path,uint32_t color,bool hole=false){
    Gdiplus::Bitmap image(256,256,PixelFormat32bppARGB);
    Gdiplus::Graphics g(&image);Gdiplus::SolidBrush brush{Gdiplus::Color(color)};g.FillRectangle(&brush,0,0,256,256);
    if(hole){Gdiplus::SolidBrush black{Gdiplus::Color(255,0,0,0)};g.FillRectangle(&black,0,0,128,256);}
    const CLSID encoder{0x557cf400,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    require(image.Save(path.c_str(),&encoder,nullptr)==Gdiplus::Ok,"sample BMP creation failed");
}
int wmain(int argc,wchar_t** argv){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);AVIFileInit();
    ULONG_PTR gdiplus{};Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&gdiplus,&input,nullptr);
    try{
        const auto root=argc>1?std::filesystem::path(argv[1]):std::filesystem::temp_directory_path()/L"BmsCompatibilitySamples";
        std::filesystem::create_directories(root);wave(root/L"tone.wav");
        bmp(root/L"base.bmp",0xff0000ff);bmp(root/L"layer.bmp",0xffff0000,true);bmp(root/L"poor.bmp",0xff00ff00);
        fixtures::writeVideo(root/L"movie.avi");fixtures::writeVideo(root/L"offset.avi",10);
        const std::string prefix="#PLAYER 1\n#BPM 120\n#WAV01 tone.wav\n#WAV02 tone.wav\n#WAV00 tone.wav\n";
        const auto parse=[&](const std::string& name,const std::string& body){auto file=root/(name+".bme");write(file,prefix+body);return bms::BmsParser(false,123,false).parse(file);};
        auto integer=parse("01-integer-measure","#TITLE Integer measure\n#00102:2\n#00219:01\n");
        require(integer.chart.playable,"integer sample unplayable");checkNear(integer.chart.notes[0].timeSeconds,6,"single-digit measure ignored");
        auto decimal=parse("01-decimal-measure","#00102:2.0\n#00219:01\n");checkNear(decimal.chart.notes[0].timeSeconds,6,"integer/decimal measure differ");
        auto flow=parse("02-control-flow","#TITLE Nested branch\n#SETRANDOM 2\n#IF 1\n#00011:01\n#ELSEIF 2\n#SETRANDOM 3\n#IF 3\n#00012:01\n#ENDIF\n#ENDRANDOM\n#IF 2\n#00013:01\n#ENDIF\n#ELSE\n#00014:01\n#ENDIF\n#ENDRANDOM\n#SETSWITCH 2\n#CASE 1\n#00015:01\n#SKIP\n#CASE 2\n#00018:01\n#CASE 3\n#00019:01\n#SKIP\n#DEF\n#00111:01\n#ENDSW\n");
        require(flow.chart.notes.size()==4 && flow.errors.empty(),"nested random/switch leaked branches");
        std::set<int> choices;
        for(uint64_t seed=1;seed<=64;++seed){bms::BmsControlFlow c(seed);std::vector<std::wstring> e;c.consume(L"random",L"4",e);choices.insert(c.selections[0]);}
        require(choices.size()==4,"random selection still fixed");
        auto stp=parse("03-stp-stop","#TITLE STP and last STOP\n#STOP01 48\n#STOP02 96\n#00009:01\n#00009:02\n#STP 000.500 250\n#00119:01\n");checkNear(stp.chart.notes[0].timeSeconds,3.25,"STOP duplicate or STP units incorrect");
        auto visual=parse("04-scroll-speed","#TITLE Signed SCROLL and interpolated SPEED\n#SCROLL01 -1\n#SCROLL02 0\n#SCROLL03 1\n#SPEED01 2\n#001SC:01\n#002SC:02\n#003SC:03\n#001SP:01\n#00019:01\n#00419:01\n");
        require(visual.chart.nonMonotonicScroll,"reverse scrolling not marked");checkNear(bms::scrollPositionSeconds(visual.chart,3),1,"SCROLL not integrated");checkNear(bms::scrollPositionSeconds(visual.chart,5),0,"zero SCROLL moved");checkNear(bms::speedAt(visual.chart,1),1.5,"SPEED interpolation missing");
        auto events=parse("05-rules-invisible-mine-text","#TITLE Extended play rules\n#RANK 0\n#DEFEXRANK 100\n#TOTAL 420\n#EXRANK01 50\n#TEXT01 Compatibility sample\n#00031:02\n#001D1:0A\n#001A0:01\n#00199:01\n#00219:01\n");
        require(events.chart.invisible.size()==1 && events.chart.mines.size()==1,"invisible/mine missing");checkNear(events.chart.mines[0].damage,5,"mine damage must be base36/2");checkNear(bms::rankPercent(events.chart,2.1),37.5,"dynamic EXRANK unused");
        checkNear(bms::chartTotal(events.chart,1),420,"TOTAL ignored");require(bms::hiddenAt(events.chart,1,0)!=nullptr,"hidden keysound lookup missing");require(bms::textAt(events.chart,2.1)==L"Compatibility sample","TEXT timeline unused");
        GrooveGauge gauge;gauge.configure(100,GrooveGaugeMode::Normal,420);gauge.apply(TimingJudgement::PGreat);checkNear(gauge.value(),24.2,"TOTAL not used by gauge");gauge.damage(5);checkNear(gauge.value(),19.2,"mine damage not used by gauge");
        auto mgq=parse("06-mgq-long-note","#TITLE MGQ continuous slots\n#LNTYPE 2\n#00051:01010000\n#00019:01\n");require(mgq.chart.playable,"MGQ blocked");const auto ln=std::find_if(mgq.chart.notes.begin(),mgq.chart.notes.end(),[](const auto& n){return n.longNote();});require(ln!=mgq.chart.notes.end(),"MGQ not converted");checkNear(ln->lengthSeconds,1,"MGQ closes at wrong slot");
        for(int mode=1;mode<=3;++mode){
            auto result=parse("07-lnmode-"+std::to_string(mode),"#TITLE LN MODE "+std::to_string(mode)+"\n#LNMODE "+std::to_string(mode)+"\n#00051:0102\n#00019:01\n");
            const auto n=std::find_if(result.chart.notes.begin(),result.chart.notes.end(),[](const auto& n){return n.longNote();});require(n!=result.chart.notes.end() && n->endSampleId==L"02" && int(n->mode)==mode,"LN type/tail sample lost");
            LongNoteHold hold;hold.start(0,1,0,n->mode);
            if(mode==1)require(hold.update(1,true)==HoldResult::PGreat,"LN requires release");
            if(mode==2){require(hold.update(1,true)==HoldResult::None,"CN ended before release");require(hold.release(1)==HoldResult::PGreat,"CN release not judged");}
            if(mode==3){require(hold.release(.4)==HoldResult::None && hold.active(),"HCN cannot recover");require(hold.takeGaugeTicks(.6)==3,"HCN gauge tick not elapsed-time based");require(hold.update(1,true)==HoldResult::PGreat,"HCN completion failed");}
        }
        auto base62=parse("08-base62","#TITLE Case-sensitive IDs\n#BASE 62\n#WAVaA tone.wav\n#WAVAa tone.wav\n#00019:aAAa\n");require(base62.chart.samples.contains(L"aA") && base62.chart.samples.contains(L"Aa") && base62.chart.samples.size()==2,"BASE62 IDs collided");
        auto path=parse("09-path-wavcmd-metadata","#TITLE Asset metadata\n#SUBARTIST Contributor\n#COMMENT Fixture comment\n#DIFFICULTY 3\n#VOLWAV 50\n#WAVCMD 00 01 72\n#WAVCMD 01 01 25\n#WAVCMD 02 01 100\n#WAV01 \"tone.wav\"\n#00019:01\n");require(path.chart.samples.size()==1 && path.chart.sampleEffects.at(L"01").pitch==72 && path.chart.difficulty==3 && path.chart.subartist==L"Contributor","asset metadata not preserved");
        std::wstring korean=L"#CHARSET EUC-KR\n#TITLE 한국어 테스트\n";int size=WideCharToMultiByte(949,0,korean.data(),int(korean.size()),nullptr,0,nullptr,nullptr);std::vector<char> bytes(size);WideCharToMultiByte(949,0,korean.data(),int(korean.size()),bytes.data(),size,nullptr,nullptr);require(bms::decodeBmsText(bytes)==korean,"declared Korean charset ignored");
        std::vector<char> utf16{static_cast<char>(0xfeu),static_cast<char>(0xffu)};for(wchar_t c:korean){utf16.push_back(char(c>>8));utf16.push_back(char(c));}require(bms::decodeBmsText(utf16)==korean,"UTF16BE BOM ignored");
        auto reference=parse("17-reference-bpm","#TITLE BASEBPM and EXBPM alias\n#BASEBPM 240\n#EXBPM01 60\n#00108:01\n#00219:01\n");
        checkNear(reference.chart.notes[0].timeSeconds,6,"EXBPM alias not used");checkNear(bms::scrollPositionSeconds(reference.chart,2),1,"BASEBPM not used");
        auto missingStop=parse("18-missing-stop-warning","#TITLE Recoverable missing STOP\n#00009:FF\n#00119:01\n");
        require(missingStop.chart.playable && missingStop.chart.partialLoad && !missingStop.errors.empty(),"undefined STOP lacks recovery diagnostic");
        auto invalid=parse("19-invalid-object-diagnostic","#TITLE Recoverable invalid object\n#00003:1G\n#00119:01\n");require(invalid.chart.playable && !invalid.errors.empty(),"invalid hex BPM accepted silently");
        std::filesystem::create_directories(root/L"audio");std::filesystem::copy_file(root/L"tone.wav",root/L"audio"/L"tone.wav",std::filesystem::copy_options::overwrite_existing);
        auto folder=parse("20-path-wav","#TITLE WAV base directory\n#PATH_WAV audio\n#00019:01\n");require(folder.chart.samples.at(L"01").parent_path().filename()==L"audio","PATH_WAV ignored");
        auto options=parse("21-forced-options","#TITLE Authored options\n#OPTION 774:HI-SPEED_x2.00\n#OPTION 774:HIDDEN_HIDDEN\n#OPTION 774:CRAZY_STAR\n#OPTION 774:RANDOM_MIRROR\n#OPTION 774:NOTES_COPY\n#OPTION 774:GAMELEVEL_DANGER\n#00011:01\n#00119:01\n");
        auto forced=bms::forcedOptions(options.chart);require(forced.hiSpeed==2 && forced.hidden==L"hidden" && forced.crazy.contains(L"star") && options.chart.mode==bms::ChartMode::FourteenKey,"OPTION not applied");
        GrooveGauge danger;danger.configure(100,GrooveGaugeMode::Normal,400);danger.configureAuthored(forced.gameLevel,forced.extra);for(int i=0;i<4;++i)danger.apply(TimingJudgement::Poor);require(!danger.depleted(),"DANGER failed before five misses");danger.apply(TimingJudgement::Poor);require(danger.depleted(),"DANGER fifth miss did not fail");
        auto endpoints=parse("22-multiple-lnobj","#TITLE Multiple LNOBJ end sounds\n#WAVBB tone.wav\n#WAVDD tone.wav\n#LNOBJ BB\n#LNOBJ DD\n#00011:01BB\n#00113:02DD\n#00219:01\n");
        require(endpoints.chart.notes.size()==3 && endpoints.chart.notes[0].endSampleId==L"BB" && endpoints.chart.notes[1].endSampleId==L"DD","multiple LNOBJ end sounds lost");
        bms::Chart typed;typed.notes={{1,0,1,L"01",L"02",bms::LongNoteMode::CN},{2,0,1,L"01",L"02",bms::LongNoteMode::LN}};
        require(bms::judgementNoteCount(typed)==3,"CN endpoints not counted for TOTAL/scoring");
        std::vector<char> utf16le{static_cast<char>(0xffu),static_cast<char>(0xfeu)};for(wchar_t c:korean){utf16le.push_back(static_cast<char>(c));utf16le.push_back(static_cast<char>(c>>8));}require(bms::decodeBmsText(utf16le)==korean,"UTF16LE BOM ignored");
        std::vector<char> utf32{0,0,static_cast<char>(0xfeu),static_cast<char>(0xffu)};for(wchar_t c:korean){utf32.push_back(0);utf32.push_back(0);utf32.push_back(static_cast<char>(c>>8));utf32.push_back(static_cast<char>(c));}require(bms::decodeBmsText(utf32)==korean,"UTF32BE BOM ignored");
        wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);
        const auto material=std::filesystem::path(module).parent_path()/L"Materials"/L"bms-compatibility-fixture";
        std::filesystem::create_directories(material);std::filesystem::copy_file(root/L"tone.wav",material/L"tone.wav",std::filesystem::copy_options::overwrite_existing);
        auto sharedMaterial=parse("26-materials","#TITLE Shared material header\n#MATERIALSWAV bms-compatibility-fixture\n#00019:01\n");
        require(sharedMaterial.chart.samples.at(L"01").parent_path()==material,"MATERIALSWAV root incorrect");
        auto inlineMaterial=parse("27-materials-inline","#TITLE Inline shared material\n#WAV01 <bms-compatibility-fixture>tone.wav\n#00019:01\n");
        require(inlineMaterial.chart.samples.at(L"01").parent_path()==material,"inline MATERIALS reference ignored");
        const auto unicodeBody=L"#CHARSET EUC-KR\n#TITLE 한국어 채보\n#BPM 120\n#WAV01 tone.wav\n#00019:01\n";
        size=WideCharToMultiByte(949,0,unicodeBody,-1,nullptr,0,nullptr,nullptr);std::string encoded(size-1,' ');
        WideCharToMultiByte(949,0,unicodeBody,static_cast<int>(wcslen(unicodeBody)),encoded.data(),size-1,nullptr,nullptr);
        write(root/L"28-korean.bme",encoded);auto encodedChart=bms::BmsParser().parse(root/L"28-korean.bme");
        require(encodedChart.chart.playable && encodedChart.chart.title==L"한국어 채보","CHARSET did not reach parsed title");
        auto hexBase=parse("30-base16","#TITLE Hexadecimal asset IDs\n#BASE 16\n#WAV0F tone.wav\n#00019:0F\n");
        require(hexBase.chart.playable && hexBase.chart.objectBase==16 && hexBase.chart.samples.contains(L"0F"),"BASE16 ID lookup failed");
        auto opacity=parse("31-opacity-zero","#TITLE Opacity FF to zero\n#0000B:FF00\n#00119:01\n");
        require(opacity.chart.bgaOpacity[0].size()==2 && opacity.chart.bgaOpacity[0].back().value==0,"zero opacity object was discarded as an empty note");
        auto gaugeAliases=parse("32-legacy-gauge","#TITLE Legacy gauge aliases\n#OPTION 774:GTP-FULL\n#OPTION 774:GLV-TOTAL100\n#OPTION 774:HSP-MANUAL\n#00119:01\n");
        const auto aliasOptions=bms::forcedOptions(gaugeAliases.chart);
        require(gaugeAliases.errors.empty() && gaugeAliases.chart.total==100 && aliasOptions.manualHiSpeed,"legacy gauge/hi-speed aliases lost");
        GrooveGauge aliasGauge;aliasGauge.configure(400,GrooveGaugeMode::Normal,400);aliasGauge.configureAuthored(L"gtp-cool",L"off");
        const auto before=aliasGauge.value();aliasGauge.apply(TimingJudgement::Great);checkNear(aliasGauge.value(),before,"GTP-COOL gained gauge on GREAT");aliasGauge.apply(TimingJudgement::PGreat);require(aliasGauge.value()>before,"GTP-COOL failed to gain on PGREAT");
        auto oldOptions=parse("24-legacy-options","#TITLE Legacy authored visual options\n#OPTION 774:CRA-XWAVE1\n#OPTION 774:HID-BLINK2\n#OPTION 774:CRAZY_REVERSE\n#OPTION 774:CRAZY_SMALL\n#OPTION 774:CRAZY_LINEMOVE\n#00019:01\n");
        require(oldOptions.errors.empty(),"known legacy aliases were rejected");
        auto oldForced=bms::forcedOptions(oldOptions.chart);const auto effect=bms::optionNoteVisual(oldForced,oldOptions.chart.notes.front(),.3,200);
        require(effect.shift!=0 && effect.scale==.5 && bms::optionJudgeLine(oldForced,0,480,720)==240,"legacy visual options were not consumed");
        require(bms::optionVisibility(oldForced,100,.1)!=bms::optionVisibility(oldForced,100,.5),"blink does not animate");
        auto unknownOption=parse("25-unknown-option","#TITLE Recoverable unknown option\n#OPTION proprietary:unknown\n#00019:01\n");
        require(unknownOption.chart.playable && unknownOption.chart.partialLoad && !unknownOption.errors.empty(),"unknown OPTION silently accepted");
        auto showcase=parse("23-showcase","#TITLE Compatibility Showcase\n#SUBTITLE [NORMAL]\n#ARTIST Generated test assets\n#SUBARTIST BmsPlayerCpp\n#PLAYLEVEL 3\n#DIFFICULTY 2\n#RANK 2\n#TOTAL 400\n#LNMODE 2\n#LNOBJ ZZ\n#CHARSET UTF-8\n#VOLWAV 70\n#BASEBPM 120\n#BPM01 150\n#STOP01 48\n#SCROLL01 0\n#SCROLL02 1\n#SPEED01 1.5\n#SPEED02 1\n#EXRANK01 125\n#TEXT01 BPM / STOP / SCROLL / LN / Mine / BGA\n#TEXT02 SPEED interpolation and dynamic judgement rank\n#BMP01 base.bmp\n#BMP02 layer.bmp\n#BMP00 poor.bmp\n#BMP0A movie.avi\n#PREVIEW tone.wav\n#00004:01\n#00099:01\n#00101:01010101\n#00111:01000000\n#00112:00010000\n#00113:00000100\n#00114:00000001\n#00215:01000000\n#00218:00010000\n#00219:00000100\n#00216:00000001\n#00207:02\n#00351:0102\n#00331:02\n#00309:00010000\n#00408:01\n#004D2:000A\n#00411:01000100\n#00499:02\n#004A0:01\n#00513:01ZZ\n#00504:0A\n#005SP:01\n#006SC:0102\n#006SP:02\n#00615:01000100\n#00619:00010001\n#STP 007.500 250\n#00711:01000100\n#00714:00010001\n#00819:01\n");
        for(const auto& e:showcase.errors)std::wcerr<<e<<L"\n";
        require(showcase.chart.playable && showcase.errors.empty() && showcase.chart.notes.size()>15,"showcase fixture failed");
        std::cout<<"Compatibility: conditional flow, timing, SCROLL/SPEED, rules, hidden/mine, LN/CN/HCN, BASE62, assets and encodings passed.\n";
        std::wcout<<L"Playable sample charts: "<<root<<L'\n';
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';Gdiplus::GdiplusShutdown(gdiplus);AVIFileExit();CoUninitialize();return 1;}
    Gdiplus::GdiplusShutdown(gdiplus);AVIFileExit();CoUninitialize();return 0;
}
