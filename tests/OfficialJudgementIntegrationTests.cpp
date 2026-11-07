#include "../src/Main.cpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try {
    const auto root=std::filesystem::absolute(argc>1?argv[1]:"Validation/OfficialJudge");std::filesystem::create_directories(root);
    auto parse=[&](const std::string& headers){auto path=root/L"rank.bms";{std::ofstream f(path);f<<"#TITLE test\n#BPM 120\n"<<headers<<"\n#00111:01\n";}return bms::BmsParser().parse(path);};
    for(int rank=0;rank<4;++rank){auto r=parse("#RANK "+std::to_string(rank));check(r.chart.rank==rank&&!r.chart.rankDefaulted,"rank parse");}
    auto absent=parse("");check(absent.chart.rank==2&&absent.chart.rankDefaulted,"missing rank");
    for(auto text:{"4","-1","2.5","2garbage","","9999999999999999999"}){auto r=parse(std::string("#RANK ")+text);check(r.chart.rank==2&&r.chart.rankDefaulted&&!r.warnings.empty(),"invalid rank warning/default");}
    auto valid=parse("#RANK 0\n#RANK 4\n#RANK 3\n#RANK bad");check(valid.chart.rank==3&&!valid.chart.rankDefaulted,"last valid active rank");
    auto conditional=parse("#RANK 1\n#SETRANDOM 1\n#IF 2\n#RANK 0\n#ELSE\n#RANK 3\n#ENDIF\n#ENDRANDOM");check(conditional.chart.rank==3,"conditional rank");
    auto extended=parse("#RANK 1\n#DEFEXRANK 100");check(extended.chart.judgementSpec==L"LEGACY-EXTENDED"&&!bms::usesOfficialJudgement(extended.chart),"extended compatibility");
    bms::SongMetadataCache cache(root,123,root/L"cache");cache.save(conditional.chart);bms::Chart restored;
    check(cache.load(conditional.chart.path,restored)&&restored.rank==3&&!restored.rankDefaulted&&restored.judgementSpec==L"SM-JUDGE-1","rank cache roundtrip");
    bms::Chart chart;chart.mode=bms::ChartMode::SevenKey;chart.rank=2;
    check(!bms::usesOfficialLane(chart,0)&&!bms::usesOfficialLane(chart,8)&&bms::usesOfficialLane(chart,1),"scratch policy");
    check(bms::judgeLaneOffset(chart,0,0,.04,JudgementProfile::Normal)==bms::judgeChartOffset(chart,0,.04,JudgementProfile::Normal),"scratch legacy grade");
    for(auto profile:{JudgementProfile::Basic,JudgementProfile::Easy,JudgementProfile::Normal,JudgementProfile::Hard})check(bms::judgeLaneOffset(chart,0,1,.04,profile)==TimingJudgement::Great,"official independent of user profile");
    chart.notes.resize(3);chart.notes[0].timeSeconds=1;chart.notes[0].lane=1;chart.notes[1].timeSeconds=1.1;chart.notes[1].lane=1;chart.notes[2].timeSeconds=1.2;chart.notes[2].lane=0;
    std::vector<uint8_t> hit(3);
    check(bms::nearestLaneNote(chart,hit,1,.816,1,JudgementProfile::Normal)==-1,"too early consumed note");
    check(bms::nearestLaneNote(chart,hit,1,1.07,1,JudgementProfile::Normal)==1,"nearest overlapping note");hit[1]=true;
    check(bms::nearestLaneNote(chart,hit,1,1.07,1,JudgementProfile::Normal)==0,"processed note repeated");hit[0]=true;
    check(bms::nearestLaneNote(chart,hit,1,1.07,1,JudgementProfile::Normal)==-1,"duplicate consumed note");
    check(!bms::laneNoteExpired(chart,chart.notes[0],1.2049,1,JudgementProfile::Normal)&&bms::laneNoteExpired(chart,chart.notes[0],1.2051,1,JudgementProfile::Normal),"slow deadline");
    for(int fps:{30,60,120,240,1500}){(void)fps;check(bms::judgeLaneOffset(chart,1.1,1,.0171,JudgementProfile::Normal)==TimingJudgement::Great,"frame independent timestamp");}
    chart.mode=bms::ChartMode::PopnNine;check(bms::usesOfficialLane(chart,0)&&bms::usesOfficialLane(chart,8),"popn mistaken scratch");
    // Exercise the real input path at the inactive audio transport's exact t=0.
    // No audio device, hardware input, render loop or user records are touched.
    gCharts.assign(1,bms::Chart{});gCharts[0].mode=bms::ChartMode::SevenKey;gCharts[0].notes.resize(1);
    gCharts[0].notes[0].lane=1;gCharts[0].notes[0].timeSeconds=.04;
    gActiveChartIndex=0;gPlaying=true;gPractice.enabled=true;gPractice.startSeconds=0;
    gPressed.fill(false);gHit.assign(1,0);gResultScore.reset(1);gFeedback.reset();gTimingHintCondition=0;
    pressLane(1);check(gHit[0]&&gSkinJudgementGrade==int(TimingJudgement::Great)&&gTimingHint==-1&&gResultScore.value()==800000,"actual key/FAST/score path");
    pressLane(1);check(gResultScore.value()==800000,"held key repeated judgement");
    gPressed.fill(false);gHit.assign(1,0);gResultScore.reset(1);gFeedback.reset();
    gCharts[0].notes[0].timeSeconds=0;gCharts[0].notes[0].lengthSeconds=1;gCharts[0].notes[0].mode=bms::LongNoteMode::LN;
    pressLane(1);check(gHolds[1].active()&&gHolds[1].headJudgement()==TimingJudgement::PGreat,"official LN head");
    gPlayhead=1;resolveHold(1,gHolds[1].update(1,true,currentJudgementWindows().goodMs/1000));
    check(!gHolds[1].active()&&gResultScore.value()==1000000,"legacy LN tail and million score");
    check(belowAccuracyLimit(AccuracyLimit::BelowPGreat,*bms::judgeLaneOffset(gCharts[0],0,1,.04,gJudgementProfile)),"accuracy limit grade interoperability");
    gPlaying=false;gPractice.enabled=false;gCharts.clear();gActiveChartIndex=-1;
    gPlayRecordsFile=root/L"records.ini";std::filesystem::remove(gPlayRecordsFile);gPlayRecords.clear();
    const auto path=root/L"record.bms";savePlayRecord(path,false,10,800000);loadPlayRecords();auto key=normalizedRecordPath(path);
    check(gPlayRecords.at(key).scoreJudgement.spec==L"LEGACY","legacy relabeled");
    savePlayRecord(path,true,20,700000,L"SM-JUDGE-1",3);loadPlayRecords();
    check(gPlayRecords.at(key).bestAccuracyScore==800000&&gPlayRecords.at(key).scoreJudgement.spec==L"LEGACY"&&gPlayRecords.at(key).clearJudgement.spec==L"SM-JUDGE-1","independent best provenance");
    savePlayRecord(path,true,15,900000,L"SM-JUDGE-1",1);loadPlayRecords();
    check(gPlayRecords.at(key).bestAccuracyScore==900000&&gPlayRecords.at(key).scoreJudgement.rank==1&&gPlayRecords.at(key).comboJudgement.rank==3,"new score provenance");
    std::cout<<"rank parsing, cache, lane/candidate/miss policies, timestamp independence and record provenance passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
