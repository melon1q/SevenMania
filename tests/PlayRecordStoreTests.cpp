#include "PlayRecordStore.h"
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    try {
        const std::filesystem::path root = argc > 1 ? std::filesystem::path(argv[1]) :
            std::filesystem::temp_directory_path() / L"SevenManiaPlayRecordStoreTests";
        std::filesystem::create_directories(root);
        const auto file = root / L"records.ini";
        std::filesystem::remove(file);

        // Keys are absolute, normalized and case-insensitive; sections are stable FNV-1a hashes.
        const auto chart = root / L"Song" / L"Chart.BMS";
        const auto key = normalizedRecordPath(chart);
        require(normalizedRecordPath(root / L"SONG" / L"." / L"chart.bms") == key, "key must ignore case and dot segments");
        require(normalizedRecordPath({}).empty(), "empty path must have no key");
        require(recordSectionForPath(L"") == L"Play_14695981039346656037", "FNV offset basis must stay stable");
        require(recordSectionForPath(L"a") == L"Play_620337896427418084", "FNV-1a hash of one UTF-16 unit must stay stable");
        require(recordSectionForPath(key) == recordSectionForPath(normalizedRecordPath(chart)), "section must be deterministic");

        // Without a file nothing is written or remembered.
        PlayRecordStore store;
        require(!store.save(chart, true, 10, 500000) && store.records.empty(), "save without file must be a no-op");
        store.file = file;
        require(!store.save({}, true, 10, 500000) && store.records.empty(), "save without key must be a no-op");
        require(store.findPlayed(key) == nullptr, "unknown key has no record");

        // First play takes every provenance; later plays only update the bests they beat.
        require(store.save(chart, false, 10, 800000), "first save");
        const PlayRecord* record = store.findPlayed(key);
        require(record && record->played && !record->cleared && record->maxCombo == 10 && record->bestAccuracyScore == 800000, "first record");
        require(record->scoreJudgement.spec == L"LEGACY" && record->comboJudgement.rank == -1 && record->clearJudgement.spec == L"LEGACY", "default provenance");
        require(store.save(chart, true, 20, 700000, L"SM-JUDGE-1", 3), "second save");
        require(record->bestAccuracyScore == 800000 && record->scoreJudgement.spec == L"LEGACY", "lower score keeps score provenance");
        require(record->maxCombo == 20 && record->comboJudgement.spec == L"SM-JUDGE-1" && record->comboJudgement.rank == 3, "higher combo takes provenance");
        require(record->cleared && record->clearJudgement.spec == L"SM-JUDGE-1", "first clear takes provenance");
        require(store.save(chart, false, 5, 2'000'000, L"SM-JUDGE-1", 1), "third save");
        require(record->bestAccuracyScore == 1'000'000 && record->scoreJudgement.rank == 1, "score is clamped and takes provenance");
        require(record->cleared && record->clearJudgement.rank == 3 && record->comboJudgement.rank == 3, "fail never clears the clear flag");

        // Round trip through the INI file.
        PlayRecordStore reloaded; reloaded.file = file; reloaded.load();
        const PlayRecord* loaded = reloaded.findPlayed(key);
        require(reloaded.records.size() == 1 && loaded, "reload must find the record");
        require(loaded->cleared && loaded->maxCombo == 20 && loaded->bestAccuracyScore == 1'000'000, "reload values");
        require(loaded->scoreJudgement.spec == L"SM-JUDGE-1" && loaded->scoreJudgement.rank == 1, "reload score provenance");
        require(loaded->comboJudgement.rank == 3 && loaded->clearJudgement.rank == 3, "reload combo/clear provenance");
        require(loaded->legacyClearUnknown && !loaded->bestKnownClear, "old boolean clear must remain unclassified");
        require(!loaded->bestExScore,"legacy accuracy is not an EX score");

        // Known lamps and score records advance independently; an older boolean
        // clear stays unknown even after a later, fully classified clear.
        const RecordClearInfo easy{RecordClearLamp::Easy,L"7KEYS",L"EASY"};
        require(reloaded.save(chart,true,8,500000,L"SM-JUDGE-1",2,easy), "classified legacy record save");
        loaded=reloaded.findPlayed(key);
        require(loaded->legacyClearUnknown && loaded->bestKnownClear && loaded->bestKnownClear->lamp==RecordClearLamp::Easy,
                "classified clear must not erase unknown legacy clear");
        const auto typedChart=root/L"Typed.bms";
        const auto typedKey=normalizedRecordPath(typedChart);
        const RecordClearInfo hard{RecordClearLamp::Hard,L"7KEYS",L"HARD"};
        require(reloaded.save(typedChart,true,10,400000,L"SM-JUDGE-1",2,hard,0),"hard clear save");
        require(reloaded.findPlayed(typedKey)->bestExScore==0,"zero is a known EX score");
        require(reloaded.save(typedChart,true,20,900000,L"SM-JUDGE-1",2,easy,120),"easier higher score save");
        const auto* typed=reloaded.findPlayed(typedKey);
        require(typed && typed->bestAccuracyScore==900000 && typed->bestKnownClear &&
                typed->bestKnownClear->lamp==RecordClearLamp::Hard && typed->bestKnownClear->gauge==L"HARD" &&
                !typed->legacyClearUnknown,"score and lamp must update independently");
        const RecordClearInfo perfect{RecordClearLamp::Perfect,L"7KEYS",L"NORMAL"};
        require(reloaded.save(typedChart,true,20,800000,L"SM-JUDGE-1",2,perfect,150),"perfect save");
        require(reloaded.save(typedChart,true,20,800000,L"SM-JUDGE-1",2,perfect,149),"lower EX save");
        reloaded.load();typed=reloaded.findPlayed(typedKey);loaded=reloaded.findPlayed(key);
        require(typed && typed->bestAccuracyScore==900000 && typed->bestExScore==150 && typed->bestKnownClear &&
                typed->bestKnownClear->lamp==RecordClearLamp::Perfect && typed->bestKnownClear->profile==L"7KEYS",
                "lamp and provenance must survive restart");
        require(loaded && loaded->legacyClearUnknown && loaded->bestKnownClear &&
                loaded->bestKnownClear->lamp==RecordClearLamp::Easy,"legacy and known clear must survive restart");

        // Judgements, miss count, play and clear counts. The judgements follow the best EX play only.
        const auto statsChart=root/L"Stats.bms";
        const auto statsKey=normalizedRecordPath(statsChart);
        require(reloaded.save(statsChart,false,30,600000,L"SM-JUDGE-1",2,std::nullopt,100,std::array{40,20,10,5,25},40,100),"stats first play");
        require(reloaded.save(statsChart,true,50,700000,L"SM-JUDGE-1",2,std::nullopt,150,std::array{60,30,5,3,2},5,100),"stats best play");
        require(reloaded.save(statsChart,true,60,650000,L"SM-JUDGE-1",2,std::nullopt,140,std::array{55,30,10,3,1},4,100),"stats lower EX play");
        require(reloaded.save(statsChart,false,10,100000,L"SM-JUDGE-1",2,std::nullopt,150,std::array{70,10,0,0,0},80,100),
                "equal EX does not replace known judgements");
        reloaded.load();
        const auto* stats=reloaded.findPlayed(statsKey);
        require(stats&&stats->bestExScore==150&&stats->bestJudgements==std::array{60,30,5,3,2},"best judgements follow the best EX play");
        require(stats->minMissCount==4&&stats->playCount==4&&stats->clearCount==2&&stats->noteCount==100&&stats->maxCombo==60,
                "miss, play and clear counts survive restart");

        // A record in the format before these fields (written as the old build wrote it) loads them as
        // not recorded, keeps its other values, and starts counting with the next play.
        const auto oldChart=root/L"Old.bms";
        const auto oldKey=normalizedRecordPath(oldChart);
        const auto oldSection=recordSectionForPath(oldKey);
        for(const auto& [name,value]:{std::pair{L"Path",oldKey.c_str()},std::pair{L"Played",L"1"},std::pair{L"Cleared",L"1"},
                                      std::pair{L"MaxCombo",L"77"},std::pair{L"AccuracyScore",L"812345"},std::pair{L"BestExScore",L"321"}})
            WritePrivateProfileStringW(oldSection.c_str(),name,value,file.c_str());
        reloaded.load();
        const auto* old=reloaded.findPlayed(oldKey);
        require(old&&old->cleared&&old->legacyClearUnknown&&old->maxCombo==77&&old->bestAccuracyScore==812345&&old->bestExScore==321,
                "old format values load unchanged");
        require(!old->bestJudgements&&!old->minMissCount&&!old->playCount&&!old->clearCount&&!old->noteCount,
                "fields the old format lacks are not recorded");
        require(reloaded.save(oldChart,false,20,300000,L"SM-JUDGE-1",2,std::nullopt,200,std::array{90,20,30,40,20},70,200),"old record played again");
        reloaded.load();old=reloaded.findPlayed(oldKey);
        require(old&&old->bestExScore==321&&!old->bestJudgements,"a lower play never lends its judgements to the old best");
        require(old->minMissCount==70&&old->playCount==1&&old->clearCount==0&&old->cleared&&old->legacyClearUnknown&&old->maxCombo==77,
                "old record starts counting and keeps its clear");
        require(reloaded.save(oldChart,true,90,900000,L"SM-JUDGE-1",2,std::nullopt,350,std::array{160,30,5,3,2},5,200),"old record beaten");
        old=reloaded.findPlayed(oldKey);
        require(old->bestExScore==350&&old->bestJudgements==std::array{160,30,5,3,2}&&old->minMissCount==5&&
                old->playCount==2&&old->clearCount==1,"beating the old best records its judgements");

        // Malformed new fields are treated as not recorded instead of being misread.
        const auto badChart=root/L"Bad.bms";
        const auto badKey=normalizedRecordPath(badChart);
        const auto badSection=recordSectionForPath(badKey);
        for(const auto& [name,value]:{std::pair{L"Path",badKey.c_str()},std::pair{L"Played",L"1"},std::pair{L"PlayCount",L"-3"},
                                      std::pair{L"ClearCount",L"2"},std::pair{L"MinMissCount",L"9999999999"},
                                      std::pair{L"NoteCount",L"12x"},std::pair{L"BestJudgements",L"1,2,3,4"}})
            WritePrivateProfileStringW(badSection.c_str(),name,value,file.c_str());
        reloaded.load();
        const auto* bad=reloaded.findPlayed(badKey);
        require(bad&&!bad->playCount&&!bad->clearCount&&!bad->minMissCount&&!bad->noteCount&&!bad->bestJudgements,
                "malformed counts are not recorded");
        for(const wchar_t* text:{L"1,2,3,4,5,6",L"1,2,,4,5",L"1,2,3,4,5,",L"1;2;3;4;5"}) {
            WritePrivateProfileStringW(badSection.c_str(),L"BestJudgements",text,file.c_str());
            reloaded.load();
            require(!reloaded.findPlayed(badKey)->bestJudgements,"malformed judgements are not recorded");
        }
        WritePrivateProfileStringW(badSection.c_str(),L"BestJudgements",L"0,0,0,0,4294967296",file.c_str());
        reloaded.load();
        require(!reloaded.findPlayed(badKey)->bestJudgements,"judgements beyond int are not recorded");
        WritePrivateProfileStringW(badSection.c_str(),nullptr,nullptr,file.c_str());
        for(const auto& removed:{statsKey,oldKey})WritePrivateProfileStringW(recordSectionForPath(removed).c_str(),nullptr,nullptr,file.c_str());
        reloaded.load();

        // Sections whose hash does not match their path, or that are not Play_ sections, are ignored.
        WritePrivateProfileStringW(L"Play_1", L"Path", key.c_str(), file.c_str());
        WritePrivateProfileStringW(L"Play_1", L"Played", L"1", file.c_str());
        WritePrivateProfileStringW(L"Practice_x", L"Path", key.c_str(), file.c_str());
        reloaded.load();
        require(reloaded.records.size() == 2, "mismatched or foreign sections must be ignored");

        // A stored but never-played record is not reported, and load() replaces previous contents.
        const auto other = root / L"Other.bms";
        const auto otherKey = normalizedRecordPath(other);
        reloaded.records[otherKey] = PlayRecord{};
        require(reloaded.findPlayed(otherKey) == nullptr, "unplayed record must be hidden");
        reloaded.load();
        require(!reloaded.records.contains(otherKey), "load must clear old records");
        PlayRecordStore empty; empty.records[otherKey] = PlayRecord{}; empty.load();
        require(empty.records.empty(), "load without file must clear records");

        // Totals of every record (LR2 select numbers 30-37): summed once, again only after a save or load.
        {
            const auto totalsFile=root/L"totals.ini";
            std::filesystem::remove(totalsFile);
            PlayRecordStore totals;totals.file=totalsFile;
            const auto first=root/L"TotalsA.bms",second=root/L"TotalsB.bms";
            require(totals.totals().playCount==0&&totals.totalsComputed()==1,"empty store has no plays");
            require(totals.save(first,true,10,500000,L"LEGACY",-1,std::nullopt,20,std::array{8,2,1,1,0},1,12),"totals first play");
            require(totals.save(first,false,4,100000,L"LEGACY",-1,std::nullopt,6,std::array{2,2,1,3,4},7,12),"totals second play");
            require(totals.save(second,true,3,900000,L"LEGACY",-1,std::nullopt,6,std::array{3,0,0,0,0},0,3),"totals other chart");
            const auto& sum=totals.totals();
            require(sum.playCount==3&&sum.clearCount==2&&sum.judgements==std::array<long long,5>{13,4,2,4,4},
                "totals add up every play of every chart");
            const auto computed=totals.totalsComputed();
            (void)totals.totals();(void)totals.totals();
            require(totals.totalsComputed()==computed,"totals are cached between record changes");
            const auto* firstRecord=totals.findPlayed(normalizedRecordPath(first));
            require(firstRecord&&firstRecord->totalJudgements==std::array{10,4,2,4,4}&&firstRecord->bestJudgements==std::array{8,2,1,1,0},
                "a chart keeps its own judgement totals beside the best play's");
            PlayRecordStore reread;reread.file=totalsFile;reread.load();
            require(reread.totals().playCount==3&&reread.totals().judgements[0]==13,"totals survive a reload");
            // An old record without the new counts adds nothing to them.
            const auto legacy=root/L"TotalsOld.bms";
            const auto legacySection=recordSectionForPath(normalizedRecordPath(legacy));
            WritePrivateProfileStringW(legacySection.c_str(),L"Path",normalizedRecordPath(legacy).c_str(),totalsFile.c_str());
            WritePrivateProfileStringW(legacySection.c_str(),L"Played",L"1",totalsFile.c_str());
            reread.load();
            require(reread.records.size()==3&&reread.totals().playCount==3&&reread.totals().judgements[1]==4,
                "legacy records add nothing to the totals");
            std::filesystem::remove(totalsFile);
        }

        std::filesystem::remove(file);
        std::cout << "PlayRecordStore tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
