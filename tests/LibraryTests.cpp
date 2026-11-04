#include "SongLibrary.h"
#include "SongMetadataCache.h"
#include "SongIndexWorker.h"
#include "SelectedBgaMetadata.h"
#include <condition_variable>
#include <psapi.h>
#include "SongFolderNavigation.h"
#include "SongEntryPresentation.h"
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
void require(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
void write(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream output(file, std::ios::binary); output << text;
}
std::string read(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    return {(std::istreambuf_iterator<char>(input)), {}};
}
int wmain(int argc, wchar_t** argv) {
    if(argc==3&&std::wstring(argv[1])==L"--collect-variants"){
        auto charts=bms::SongMetadataCache(fs::absolute(argv[2]),0).snapshot();bms::ui::SongSelectionModel model;model.setCollectVariants(true);model.rebuild(charts);
        std::set<size_t> seen;std::map<fs::path,size_t> expected,actual;size_t oekakiGroups=0;
        for(const auto& c:charts)if(bms::toLower(c.title).starts_with(L"oekaki"))++expected[c.path.parent_path()];
        for(const auto& group:model.groups()){for(auto index:group.chartIndices)require(seen.insert(index).second,"collection duplicated cached chart");if(bms::toLower(group.title)==L"oekaki"){++oekakiGroups;const auto parent=charts[group.chartIndices.front()].path.parent_path();require(!actual.contains(parent),"Oekaki is still split within a directory");actual[parent]=group.chartIndices.size();std::cout<<"Oekaki variants="<<group.chartIndices.size()<<'\n';}}
        require(!expected.empty()&&expected==actual&&seen.size()==charts.size(),"cached Oekaki grouping lost charts");std::cout<<"cached charts="<<charts.size()<<" Oekaki groups="<<oekakiGroups<<"; same-directory variants consolidated, all chart indices retained\n";return 0;
    }
    if(argc==3&&std::wstring(argv[1])==L"--root-policy"){
        const auto root=fs::absolute(argv[2]);auto charts=bms::SongMetadataCache(root,0).snapshot();require(!charts.empty(),"real cache fixture empty");
        for(bool original:{false,true}){bms::ui::SongFolderNavigation home;home.configure({root},original);home.build(charts);
            for(const auto& c:charts)require(!home.accepts(c.path),"ROOT exposed a song");
            std::vector<bms::ui::SongFolderNavigation> pending{home};std::map<fs::path,int> paths;size_t visits=0;
            while(!pending.empty()){auto n=std::move(pending.back());pending.pop_back();n.build(charts);require(++visits<10000,"navigation cycle");for(const auto& c:charts)if(n.accepts(c.path))++paths[c.path];for(size_t i=0;i<n.folders.size();++i){auto child=n;child.enter(-3-int(i));pending.push_back(std::move(child));}}
            require(paths.size()==charts.size(),"ROOT policy lost real chart paths");for(const auto& [path,count]:paths)require(count==1,"ROOT policy duplicated charts");
            std::cout<<"original="<<original<<" root_songs=0 reachable="<<paths.size()<<" visits="<<visits<<'\n';
            if(!original){std::set<fs::path> printed;for(const auto& c:charts){auto title=bms::toLower(c.title);if((title.find(L"freedom dive")!=std::wstring::npos||title.find(L"cat rotation")!=std::wstring::npos||title.find(L"cutter")!=std::wstring::npos)&&printed.insert(c.path.parent_path()).second)std::wcout<<L"path="<<c.path.wstring()<<L" display="<<home.displayParent(c.path).wstring()<<L"\n";}}
        }return 0;
    }
    if(argc==4&&std::wstring(argv[1])==L"--library2-profile"){
        const auto directory=fs::absolute(argv[2]),output=fs::absolute(argv[3]);fs::create_directories(output);
        const auto storage=output/(L"cache-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        const auto measure=[](auto action){const auto start=std::chrono::steady_clock::now();action();return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();};
        std::vector<bms::Chart> cold,warm,fast;
        const double coldMs=measure([&]{cold=bms::scanSongFolder(directory,nullptr,{},0,true,storage);});
        bms::SongMetadataCache cache(directory,0,storage);
        const double snapshotMs=measure([&]{fast=cache.snapshot();});
        const double warmMs=measure([&]{warm=bms::scanSongFolder(directory,nullptr,{},0,true,storage);});
        bms::ui::SongFolderNavigation navigation;navigation.configure({directory},false);
        const double navigationMs=measure([&]{for(int i=0;i<100;++i){navigation.build(warm);if(!navigation.folders.empty()){navigation.enter(-3);navigation.build(warm);navigation.back();}}});
        const double modeMs=measure([&]{for(int i=0;i<100;++i){navigation.configure({directory},i%2!=0);navigation.build(warm);}});
        bms::ui::SongSelectionModel model;
        const double groupMs=measure([&]{model.setIndividualCharts(false);model.rebuild(warm);});const auto groupCount=model.groups().size();
        const double individualMs=measure([&]{model.setIndividualCharts(true);model.rebuild(warm);});
        PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m));
        std::cout<<"charts="<<warm.size()<<" groups="<<groupCount<<" cold_metadata_index_ms="<<coldMs<<" cache_snapshot_ms="<<snapshotMs<<" incremental_verify_ms="<<warmMs<<" folder_roundtrip_mean_ms="<<navigationMs/100<<" navigation_mode_switch_mean_ms="<<modeMs/100<<" grouped_rebuild_ms="<<groupMs<<" individual_rebuild_ms="<<individualMs<<" process_private_MiB="<<m.PrivateUsage/1048576.0<<" process_peak_working_set_MiB="<<m.PeakWorkingSetSize/1048576.0<<'\n';
        return cold.size()==warm.size()&&fast.size()==warm.size()&&model.groups().size()==warm.size()?0:1;
    }
    if(argc==3&&std::wstring(argv[1])==L"--index-profile"){
        const auto directory=fs::absolute(argv[2]);bms::SongMetadataCache cache(directory,0);cache.invalidate();
        const auto measure=[](auto action){const auto start=std::chrono::steady_clock::now();action();return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();};
        std::vector<bms::Chart> cold,warm,fast;
        const double coldMs=measure([&]{cold=bms::scanSongFolder(directory,nullptr,{},0,true);});
        const double snapshotMs=measure([&]{fast=cache.snapshot();});
        const double warmMs=measure([&]{warm=bms::scanSongFolder(directory,nullptr,{},0,true);});
        bms::ui::SongFolderNavigation navigation;navigation.root(directory);
        const double navigationMs=measure([&]{for(int i=0;i<100;++i){navigation.build(warm);if(!navigation.folders.empty()){navigation.enter(-3);navigation.build(warm);navigation.back();}}});
        PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m));
        std::cout<<"charts="<<warm.size()<<" cold_index_ms="<<coldMs<<" cache_snapshot_ms="<<snapshotMs<<" incremental_verify_ms="<<warmMs<<" folder_roundtrip_mean_ms="<<navigationMs/100<<" private_MiB="<<m.PrivateUsage/1048576.0<<" peak_working_set_MiB="<<m.PeakWorkingSetSize/1048576.0<<'\n';return cold.size()==warm.size()&&fast.size()==warm.size()?0:1;
    }
    if (argc == 2 || (argc == 4 && std::wstring(argv[1]) == L"--verify-import")) {
        try {
            const auto start = std::chrono::steady_clock::now();
            const auto folder = argc == 2 ? fs::path(argv[1]) : bms::importSongFolder(argv[2], argv[3]).folder;
            const auto charts = bms::scanSongFolder(folder);
            size_t notes = 0, playable = 0;
            for (const auto& chart : charts) {
                notes += chart.notes.size(); playable += chart.playable ? 1 : 0;
                std::wcout << chart.path.filename().wstring() << L": playable=" << chart.playable
                    << L", notes=" << chart.notes.size() << L", samples=" << chart.samples.size()
                    << L", blocked=" << chart.blockReason << L"\n";
            }
            std::cout << "charts=" << charts.size() << " notes=" << notes << " playable=" << playable << " milliseconds="
                      << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() << '\n';
            return !charts.empty() && playable == charts.size() ? 0 : 1;
            } catch (const bms::LibraryError& error) {
            std::wcerr << error.message << L"\n"; return 1;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n'; return 1;
        }
    }
    const auto root = (argc == 3 ? fs::absolute(argv[2]) : fs::current_path()) /
        (L"BmsLibraryTests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    bool created = false;
    int result = 0;
    try {
        created = fs::create_directory(root); require(created, "temporary fixture must be unique");
        const auto source = root / L"원본" / L"Cat's_곡";
        const auto library = root / L"게임" / L"Songs";
        const std::string header = "#PLAYER 1\n#TITLE fixture\n#BPM 120\n#PLAYLEVEL 3\n";
        write(source / L"normal.bme", header + "#WAV01 audio.ogg\n#00011:01\n");
        write(source / L"shared.bme", header + "#00011:01\n");
        write(source / L"five.bms", header + "#WAV01 audio.ogg\n#00011:01\n");
        write(source / L"broken.bme", "invalid file");
        write(source / L"audio.ogg", "sample bytes");
        write(source / L"_bga.mp4", "video bytes");
        write(source / L"하위" / L"data.bin", "original");
        fs::create_directory(source / L"empty");
        size_t lastCopyCount = 0;
        const auto imported = bms::importSongFolder(source, library, nullptr, [&](size_t done, size_t total) {
            require(done <= total && done >= lastCopyCount, "copy progress must be monotonic"); lastCopyCount = done;
        });
        require(imported.copiedFiles == 7 && lastCopyCount == 7 && imported.folder.parent_path() == library, "full copy or managed destination failed");
        require(read(imported.folder / L"하위" / L"data.bin") == "original" && fs::is_directory(imported.folder / L"empty"), "nested files or empty directories lost");
        require(read(source / L"audio.ogg") == read(imported.folder / L"audio.ogg") && read(source / L"_bga.mp4") == "video bytes", "media changed during copy");
        const auto repeated = bms::importSongFolder(source, library);
        require(repeated.folder == imported.folder && repeated.copiedFiles == 0 && repeated.unchangedFiles == 7, "unchanged import must reuse destination and skip file copies");
        write(source / L"하위" / L"data.bin", "updated!");
        fs::last_write_time(source / L"하위" / L"data.bin", fs::last_write_time(source / L"하위" / L"data.bin") + std::chrono::seconds(2));
        const auto refreshed = bms::importSongFolder(source, library);
        require(refreshed.copiedFiles == 1 && read(imported.folder / L"하위" / L"data.bin") == "updated!", "changed file was not refreshed");
        require(bms::importSongFolder(imported.folder, library).folder == imported.folder, "managed folder must not be copied into itself");
        const auto otherSource = root / L"other" / L"Cat's_곡";
        write(otherSource / L"other.bme", header + "#00011:01\n");
        const auto other = bms::importSongFolder(otherSource, library);
        require(other.folder != imported.folder, "same-named source folders collided");
        const auto batchLibrary=root/L"batch-Songs";
        size_t batchProgress=0;
        const auto batch=bms::importSongSources({source/L"normal.bme",source/L"shared.bme",otherSource/L"other.bme"},batchLibrary,nullptr,[&](size_t done,size_t total){
            require(done>=batchProgress&&done<=total,"batch progress must be monotonic");batchProgress=done;
        });
        require(batch.importedFolders==2&&batch.copiedFiles==8&&batch.errors.empty()&&batchProgress==2000,"multi-chart import or shared-folder dedup failed");
        require(bms::scanSongFolder(batchLibrary).size()==5,"multi-chart import lost charts or companion media");
        const auto nestedBatch=bms::importSongSources({source/L"하위",source,source/L"normal.bme"},batchLibrary);
        require(nestedBatch.importedFolders==1&&nestedBatch.copiedFiles==0&&nestedBatch.unchangedFiles==7,"overlapping folders copied twice");
        const auto partial=bms::importSongSources({root/L"missing.bms",source/L"audio.ogg",otherSource},batchLibrary);
        require(partial.importedFolders==1&&partial.errors.size()==2&&partial.unchangedFiles==1,"one invalid selection blocked valid songs");
        const auto parentRejected=bms::importSongSources({root,otherSource},batchLibrary);
        require(parentRejected.errors.size()==1&&parentRejected.importedFolders==1,"unsafe parent blocked independent valid selection");
        require(bms::importSongSources({},batchLibrary).importedFolders==0,"empty selection changed library");
        std::atomic_bool batchCancelled{true};bool batchCancelledThrown=false;
        try{bms::importSongSources({source,otherSource},batchLibrary,&batchCancelled);}catch(const bms::LibraryError&){batchCancelledThrown=true;}
        require(batchCancelledThrown,"cancelled batch was imported");
        const auto cancelBatchLibrary=root/L"cancel-batch-Songs";
        const auto completedBatchFolder=cancelBatchLibrary/imported.folder.filename();
        std::atomic_bool stopAfterFirst{false};bool stoppedAfterFirst=false;
        try {
            bms::importSongSources({source,otherSource},cancelBatchLibrary,&stopAfterFirst,[&](size_t done,size_t){
                if(done==1000&&fs::is_directory(completedBatchFolder))stopAfterFirst=true;
            });
        } catch(const bms::LibraryError&){stoppedAfterFirst=true;}
        require(stoppedAfterFirst&&fs::is_regular_file(completedBatchFolder/L"normal.bme")&&
            !fs::exists(cancelBatchLibrary/other.folder.filename()),"batch cancellation lost completed songs or imported remaining songs");
        fs::remove_all(source);
        size_t completed = 0;
        const auto charts = bms::scanSongFolder(library, nullptr, [&](size_t done, size_t total) {
            require(done <= total && done >= completed, "scan progress must be monotonic"); completed = done;
        });
        require(charts.size() == 5 && completed == 5, "recursive batch scan lost charts");
        size_t playable = 0;
        for (const auto& chart : charts) {
            if (chart.playable) ++playable;
            const auto direct = bms::BmsParser().parse(chart.path).chart;
            require(chart.playable == direct.playable && chart.mode == direct.mode && chart.notes.size() == direct.notes.size() &&
                chart.samples == direct.samples && chart.fallbackVideo == direct.fallbackVideo && chart.sharedSampleDefinitions == direct.sharedSampleDefinitions,
                "batch cache changed parser compatibility");
        }
        require(playable == 4, "5-key charts must be playable while malformed files stay blocked");
        fs::remove(imported.folder / L"audio.ogg");
        const auto missing = bms::scanSongFolder(imported.folder);
        const auto normal = std::find_if(missing.begin(), missing.end(), [](const auto& chart) { return chart.path.filename() == L"normal.bme"; });
        require(normal != missing.end() && normal->playable && normal->partialLoad, "new scan must keep runnable charts with missing samples and flag them partial");
        write(imported.folder / L"audio.ogg", "restored sample");
        require(bms::BmsParser().parse(imported.folder / L"normal.bme").chart.playable, "new scan retained stale missing-sample state");
        const auto unreadableSource = root / L"unreadable-source";
        const auto unreadableLibrary = root / L"unreadable-library";
        write(unreadableSource / L"recoverable.bme", header + "#WAV01 locked.ogg\n#00011:01\n");
        write(unreadableSource / L"locked.ogg", "locked audio");
        const auto initialImport = bms::importSongFolder(unreadableSource, unreadableLibrary);
        fs::remove(initialImport.folder / L"locked.ogg");
        HANDLE lock = CreateFileW((unreadableSource / L"locked.ogg").c_str(), GENERIC_READ, 0, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(lock != INVALID_HANDLE_VALUE, "fixture source file must be lockable");
        const auto partialImport = bms::importSongFolder(unreadableSource, unreadableLibrary);
        CloseHandle(lock);
        require(partialImport.skippedFiles.size() == 1 && partialImport.skippedFiles.front().filename() == L"locked.ogg",
            "an unreadable individual asset must be skipped without aborting the folder import");
        const auto partialCharts = bms::scanSongFolder(partialImport.folder);
        require(partialCharts.size() == 1 && partialCharts.front().playable && partialCharts.front().partialLoad,
            "the rest of the folder must scan and a chart missing a locked asset must remain runnable and partial");
        const auto cancelSource = root / L"cancel";
        write(cancelSource / L"a.bin", "a"); write(cancelSource / L"b.bin", "b");
        std::atomic_bool cancelled = false;
        bool rejected = false;
        try { bms::importSongFolder(cancelSource, library, &cancelled, [&](size_t done, size_t) { if (done == 1) cancelled = true; }); }
        catch (const bms::LibraryError&) { rejected = true; }
        require(rejected, "cancelled copy was published");
        for (const auto& entry : fs::directory_iterator(library))
            require(entry.path() == imported.folder || entry.path() == other.folder, "cancelled import left a partial directory");
        rejected = false;
        try { bms::importSongFolder(root, library); } catch (const bms::LibraryError&) { rejected = true; }
        require(rejected, "copy into a descendant must be rejected");
        const auto conditional = root / L"condition" / L"own.bme";
        write(conditional, header + "#RANDOM 1\n#IF 2\n#WAV01 never.wav\n#ENDIF\n#00011:01\n");
        bms::BmsParser cached(true);
        require(!cached.parse(conditional).chart.sharedSampleDefinitions, "inactive own definitions must not be inherited");
        // Reproduce nested archive names plus the importer's temporary suffix.
        const auto longSource = root / L"long-source" / std::wstring(64, L'곡');
        const auto nested = fs::path(std::wstring(64, L'曲')) / std::wstring(64, L'音');
        write(longSource / nested / L"long.bms", header + "#WAV01 tone.ogg\n#00011:01\n");
        write(longSource / nested / L"tone.ogg", "fixture audio");
        const auto longImported = bms::importSongFolder(longSource, root / L"long-library");
        const auto longChartPath = longImported.folder / nested / L"long.bms";
        require(longChartPath.wstring().size() > MAX_PATH, "long-path regression fixture is too short");
        const auto longCharts = bms::scanSongFolder(longImported.folder);
        require(longImported.copiedFiles == 2 && longImported.skippedFiles.empty() &&
            longCharts.size() == 1 && longCharts.front().playable && !longCharts.front().partialLoad,
            "nested archive import or chart/sample read failed beyond MAX_PATH");
        const auto longRefresh = bms::importSongFolder(longSource, root / L"long-library");
        require(longRefresh.unchangedFiles == 2 && longRefresh.copiedFiles == 0,
            "long-path incremental import did not reuse existing files");
        const auto cachedRoot=root/L"cache-fixture";
        write(cachedRoot/L"root.bms",header+"#00011:01\n");
        write(cachedRoot/L"A"/L"B"/L"nested.bms",header+"#00011:01\n");
        require(bms::libraryCacheStartAllowed({cachedRoot},cachedRoot)&&!bms::libraryCacheStartAllowed({source},cachedRoot),"startup root refresh did not allow cache-first loading");
        auto initial=bms::scanSongFolder(cachedRoot,nullptr,{},19,true);
        require(initial.size()==2&&initial[0].catalogOnly&&initial[0].notes.empty(),"metadata scan retained gameplay events");
        bms::SongMetadataCache cache(cachedRoot,19);bms::Chart metaChart;
        require(cache.load(cachedRoot/L"A"/L"B"/L"nested.bms",metaChart),"persistent metadata cache missed unchanged nested file");
        write(cachedRoot/L"A"/L"B"/L"nested.bms",header+"#SUBTITLE changed-size\n#BMP01 still.png\n#00004:01\n#00011:01\n");
        require(!cache.load(cachedRoot/L"A"/L"B"/L"nested.bms",metaChart),"edited chart reused stale metadata");
        require(cache.snapshot().size()==2,"cache-first snapshot failed");
        write(cachedRoot/L"A"/L"B"/L"still.png","image fixture");
        auto changed=bms::scanSongFolder(cachedRoot,nullptr,{},19,true);
        require(std::any_of(changed.begin(),changed.end(),[](auto& c){return c.subtitle==L"changed-size";}),"incremental reparse missed changed metadata");
        fs::remove(cachedRoot/L"root.bms");
        require(cache.snapshot().size()==2,"snapshot incorrectly required source files before validation");
        require(bms::scanSongFolder(cachedRoot,nullptr,{},19,true).size()==1,"deleted chart remained in index");
        for(const auto& e:fs::directory_iterator(cachedRoot/L".sevenmania-index"))write(e.path(),"broken cache");
        require(bms::scanSongFolder(cachedRoot,nullptr,{},19,true).size()==1,"corrupt cache failed recovery");
        SelectedBgaMetadata selectedMetadata;
        selectedMetadata.get(cachedRoot/L"A"/L"B"/L"nested.bms");std::shared_ptr<const bms::Chart> enriched;
        for(int attempt=0;attempt<1000&&!enriched;++attempt){std::this_thread::sleep_for(std::chrono::milliseconds(1));enriched=selectedMetadata.get(cachedRoot/L"A"/L"B"/L"nested.bms");}
        const auto originalBga=bms::BmsParser().parse(cachedRoot/L"A"/L"B"/L"nested.bms").chart;
        require(enriched&&enriched->notes.empty()&&enriched->samples.empty()&&!enriched->bga.empty()&&enriched->bga.size()==originalBga.bga.size()&&enriched->bitmaps==originalBga.bitmaps,"selected BGA metadata worker failed or retained gameplay resources");
        for(size_t i=0;i<originalBga.bga.size();++i)require(enriched->bga[i].imageId==originalBga.bga[i].imageId&&enriched->bga[i].layer==originalBga.bga[i].layer&&enriched->bga[i].timeSeconds==originalBga.bga[i].timeSeconds,"BGA metadata changed authored event timing");
        selectedMetadata.reset();
        size_t metaCount=0;for(const auto& e:fs::directory_iterator(cachedRoot/L".sevenmania-index"))if(e.path().extension()==L".meta")++metaCount;
        require(metaCount==1,"deleted metadata was not pruned");
        fs::create_directories(cachedRoot/L"empty-빈폴더");
        bms::SongIndexWorker indexWorker;std::mutex doneMutex;std::condition_variable doneCv;bool done=false;
        indexWorker.start(cachedRoot,[&]{std::lock_guard lock(doneMutex);done=true;doneCv.notify_one();});
        {std::unique_lock lock(doneMutex);require(doneCv.wait_for(lock,std::chrono::seconds(10),[&]{return done;}),"background index did not finish");}
        auto update=indexWorker.take();require(update&&update->charts.size()==1&&std::find(update->folders.begin(),update->folders.end(),cachedRoot/L"empty-빈폴더")!=update->folders.end(),"background update or empty folder missing");
        indexWorker.cancel();indexWorker.start(cachedRoot,[]{});indexWorker.cancel();require(!indexWorker.take(),"cancelled worker published a stale result");
        bms::ui::SongFolderNavigation navigation;navigation.root(cachedRoot);navigation.build(initial);
        require(navigation.folders.size()==2&&!navigation.accepts(cachedRoot/L"root.bms"),"ROOT must contain folders only");
        const auto rootSongs=std::find(navigation.folders.begin(),navigation.folders.end(),fs::path(L"ROOT SONGS"));
        require(rootSongs!=navigation.folders.end(),"direct root charts have no virtual folder");
        navigation.enter(-3-int(rootSongs-navigation.folders.begin()));navigation.build(initial);
        require(navigation.accepts(cachedRoot/L"root.bms")&&!navigation.accepts(cachedRoot/L"A"/L"B"/L"nested.bms"),"ROOT SONGS is not isolated");
        navigation.save(cachedRoot/L"root.bms");bms::ui::SongFolderNavigation restoredRoot;restoredRoot.root(cachedRoot);restoredRoot.restoreSaved(initial);restoredRoot.build(initial);
        require(restoredRoot.location()==L"ROOT SONGS"&&restoredRoot.accepts(cachedRoot/L"root.bms"),"ROOT SONGS restoration failed");
        require(navigation.back()&&navigation.isRoot(),"ROOT SONGS back failed");navigation.build(initial);
        navigation.remember(2);navigation.enter(-3-int(std::find(navigation.folders.begin(),navigation.folders.end(),cachedRoot/L"A")-navigation.folders.begin()));navigation.build(initial);
        require(navigation.folders.empty()&&!navigation.accepts(cachedRoot/L"root.bms")&&navigation.accepts(cachedRoot/L"A"/L"B"/L"nested.bms"),"leaf song folder was not flattened");
        navigation.enter(-3);require(navigation.accepts(cachedRoot/L"A"/L"B"/L"nested.bms"),"deep chart inaccessible");
        require(navigation.back()&&navigation.cursor()==2,"parent/cursor restoration failed");
        navigation.enter(-2);require(navigation.accepts(cachedRoot/L"root.bms")&&navigation.accepts(cachedRoot/L"A"/L"B"/L"nested.bms"),"ALL SONGS omitted a chart");
        require(navigation.back()&&navigation.isRoot()&&!navigation.back(),"virtual folder back handling incorrect");
        navigation.root(cachedRoot);navigation.knownFolders=update->folders;navigation.build(initial);
        require(std::find(navigation.folders.begin(),navigation.folders.end(),cachedRoot/L"empty-빈폴더")!=navigation.folders.end(),"empty directory was not navigable");
        navigation.restore(cachedRoot/L"empty-빈폴더",initial);navigation.save(cachedRoot/L"A"/L"B"/L"nested.bms");
        bms::ui::SongFolderNavigation reopened;reopened.root(cachedRoot);reopened.knownFolders=update->folders;
        require(reopened.restoreSaved(initial)==cachedRoot/L"A"/L"B"/L"nested.bms"&&reopened.location()==cachedRoot/L"empty-빈폴더","navigation state did not survive reload");
        reopened.restore(root/L"missing",initial);require(reopened.location()==cachedRoot/L"empty-빈폴더","unknown navigation path was accepted");
        std::vector<bms::Chart> layout;
        for(const auto& path:{cachedRoot/L"Event"/L"Song A"/L"normal.bms",cachedRoot/L"Event"/L"Song A"/L"another.bms",cachedRoot/L"Event"/L"Song B"/L"hyper.bms",cachedRoot/L"Mixed"/L"own.bms",cachedRoot/L"Mixed"/L"Child"/L"song.bms"}){bms::Chart c;c.path=path;layout.push_back(c);}
        bms::ui::SongFolderNavigation organized;organized.root(cachedRoot);organized.knownFolders={cachedRoot/L"Event"/L"Song A"/L"Audio"};organized.build(layout);
        require(organized.folders.size()==2,"classification folders disappeared");organized.restore(cachedRoot/L"Event",layout);organized.build(layout);
        require(organized.folders.empty()&&organized.accepts(layout[0].path)&&organized.accepts(layout[1].path)&&organized.accepts(layout[2].path),"song folders or their variants were not exposed directly");
        organized.restore(cachedRoot/L"Mixed",layout);organized.build(layout);require(organized.accepts(layout[3].path)&&organized.accepts(layout[4].path),"mixed direct and nested songs were lost");
        layout.push_back(bms::Chart{});layout.back().path=cachedRoot/L"Event"/L"Song A"/L"Subgroup"/L"other.bms";organized.restore(cachedRoot/L"Event",layout);organized.build(layout);
        require(organized.folders.size()==1&&!organized.accepts(layout[0].path),"mixed folder did not fall back to navigable classification");
        layout.pop_back();organized.build(layout);require(organized.folders.empty()&&organized.accepts(layout[0].path),"folder classification did not refresh after deletion");
        const auto graphDirectory=root/L"navigation-fixture";
        std::vector<bms::Chart> graph;
        for(const auto& relative:{L"Event/Song A/normal.bms",L"Event/Song A/another.bme",L"Event/Song B/song.bml",L"Mixed/own.bms",L"Mixed/Child/song.bms",L"Misc/A.bms",L"Misc/B.bms",L"Misc/C.bms",L"root.bms",L"大会/Team/曲/song.bms"}){bms::Chart c;c.path=graphDirectory/relative;graph.push_back(c);write(c.path,header+"#00011:01\n");}
        fs::create_directories(graphDirectory/L"empty");fs::create_directories(graphDirectory/L"Event"/L"Song A"/L"audio");
        std::vector<bms::ui::SongGroup> graphGroups;for(const auto& indices:std::vector<std::vector<size_t>>{{0,1},{2},{3},{4},{5},{6},{7},{8},{9}}){bms::ui::SongGroup group;group.chartIndices=indices;graphGroups.push_back(group);}
        bms::ui::SongFolderNavigation graphRoot;graphRoot.root(graphDirectory);graphRoot.knownFolders={graphDirectory/L"empty",graphDirectory/L"Event"/L"Song A"/L"audio"};graphRoot.build(graph,&graphGroups);
        require(std::find(graphRoot.folders.begin(),graphRoot.folders.end(),graphDirectory/L"Misc")!=graphRoot.folders.end()&&!graphRoot.accepts(graph[5].path),"ROOT leaf directory must remain navigable");
        std::map<fs::path,int> reachable;std::vector<bms::ui::SongFolderNavigation> pending{graphRoot};size_t visits=0;
        while(!pending.empty()){auto node=std::move(pending.back());pending.pop_back();node.build(graph,&graphGroups);if(node.isRoot())for(const auto& c:graph)require(!node.accepts(c.path),"ROOT contains music");require(++visits<100,"folder cycle detected");
            for(const auto& c:graph)if(node.accepts(c.path))++reachable[c.path];
            for(size_t i=0;i<node.folders.size();++i){auto child=node;child.enter(-3-int(i));pending.push_back(std::move(child));}}
        require(reachable.size()==graph.size(),"automatic folder traversal lost charts");for(const auto& [path,count]:reachable){(void)path;require(count==1,"automatic folder traversal duplicated charts");}
        auto metadataGraph=graph;for(auto& c:metadataGraph)c=bms::SongMetadataCache::metadata(c);
        auto cachedView=graphRoot;cachedView.build(metadataGraph,&graphGroups);require(cachedView.folders==graphRoot.folders,"cache metadata changed folder classification");
        graphRoot.enter(-2);for(const auto& c:graph)require(graphRoot.accepts(c.path),"ALL SONGS lost an automatically exposed chart");
        require(std::wstring(bms::ui::entryTypeLabel(bms::ui::SongEntryKind::Folder))==L"DIR"&&std::wstring(bms::ui::entryTypeLabel(bms::ui::SongEntryKind::AllSongs)).empty(),"entry label/type separation regressed");
        // Real paths, source registration and navigation are independent views.
        const auto primary=root/L"Library2"/L"Songs",external=root/L"外部 音楽";
        write(primary/L"Event"/L"Song A"/L"normal.bms",header+"#00011:01\n");
        write(primary/L"Event"/L"Song A"/L"another.bms",header+"#SUBTITLE ANOTHER\n#00011:01\n");
        write(primary/L"Mixed"/L"direct.bms",header+"#00011:01\n");
        write(primary/L"Mixed"/L"Child"/L"nested.bms",header+"#00011:01\n");
        write(external/L"大会"/L"曲 !"/L"song.bms",header+"#00011:01\n");
        const auto config=root/L"library2.ini";
        bms::SongLibrarySources sources;sources.initialize(primary,config);
        require(sources.roots.empty()&&bms::snapshotSongLibraries(sources).empty()&&bms::scanSongLibraries(sources).charts.empty(),"Songs was implicitly registered");
        bms::ui::SongFolderNavigation emptyNavigation;emptyNavigation.configure(sources.navigationRoots(),false);emptyNavigation.build(graph);
        for(const auto& c:graph)require(!emptyNavigation.accepts(c.path),"empty sources exposed unrelated charts");
        require(sources.add(primary)&&sources.add(external),"explicit library registration failed");
        require(!sources.add(external/L".")&&!sources.add(fs::path(bms::toLower(external.wstring()))),"case/normalized source duplicate registered");
        require(sources.add(external/L"大会")&&sources.scanRoots().size()==2,"overlapping roots were not collapsed for scanning");
        require(sources.save(),"source settings could not be saved");
        bms::SongLibrarySources restored;restored.initialize(primary,config);
        require(restored.roots==sources.roots,"source registration did not survive restart");
        auto multi=bms::scanSongLibraries(sources);
        require(multi.charts.size()==5&&multi.warnings.empty(),"multi-root index lost/duplicated charts");
        require(!fs::exists(external/L".sevenmania-index")&&!fs::exists(primary/L"外部 音楽"),"external source was modified/copied");
        auto fast=bms::snapshotSongLibraries(sources);require(fast.size()==5,"multi-root cache snapshot lost charts");
        const auto traverse=[&](bool original){
            bms::ui::SongFolderNavigation nav;nav.configure(sources.roots,original);nav.knownFolders=multi.folders;
            std::vector<bms::ui::SongFolderNavigation> queue{nav};std::map<std::wstring,int> paths;size_t nodes=0;
            while(!queue.empty()){auto n=std::move(queue.back());queue.pop_back();n.build(multi.charts);require(++nodes<100,"multi-root navigation cycle");
                for(const auto& c:multi.charts)if(n.accepts(c.path))++paths[bms::libraryPathKey(c.path)];
                for(size_t i=0;i<n.folders.size();++i){auto child=n;child.enter(-3-int(i));queue.push_back(std::move(child));}}
            require(paths.size()==multi.charts.size(),"navigation view lost a chart");for(auto [path,count]:paths)require(count==1,"navigation view duplicated a chart");return paths;
        };
        require(traverse(false)==traverse(true),"smart and original folder views disagree on chart paths");
        bms::ui::SongFolderNavigation smart;smart.configure(sources.roots,false);smart.build(multi.charts);
        require(smart.displayParent(primary/L"Event"/L"Song A"/L"normal.bms")==primary/L"Event","actual/display parent separation failed");
        require(smart.displayParent(primary/L"Mixed"/L"direct.bms")==primary/L"Mixed","mixed directory lost safe parent");
        smart.restore(external,multi.charts);require(smart.back()&&smart.isRoot(),"external root back escaped library home");
        // An external ancestor of the built-in root is indexed once, with the
        // built-in root retaining its normal home presentation.
        require(sources.add(primary.parent_path()),"ancestor root registration failed");
        multi=bms::scanSongLibraries(sources);require(multi.charts.size()==5,"ancestor root caused duplicate charts");
        require(traverse(false)==traverse(true),"overlapping source topology lost charts");
        require(sources.remove(sources.roots.size()-1)&&sources.remove(2)&&sources.remove(1),"source removal failed");
        auto emptySources=sources;require(emptySources.remove(0)&&emptySources.roots.empty(),"first registered root could not be removed");
        require(fs::is_regular_file(external/L"大会"/L"曲 !"/L"song.bms"),"unregister deleted original files");
        sources.add(external);sources.add(root/L"disconnected");
        multi=bms::scanSongLibraries(sources);require(multi.charts.size()==5&&multi.warnings.size()==1,"missing external root was not safely reported");
        write(external/L"大会"/L"曲 !"/L"new.bms",header+"#TITLE new song\n#00011:01\n");
        const auto selected=bms::scanSongLibraries(sources,nullptr,{},external,multi.charts,multi.folders);
        require(std::find(selected.folders.begin(),selected.folders.end(),primary/L"Event")!=selected.folders.end(),"targeted refresh discarded another source folder index");
        require(selected.charts.size()==6,"targeted source refresh discarded other source charts");
        fs::remove(external/L"大会"/L"曲 !"/L"new.bms");
        auto deleted=bms::scanSongLibraries(sources,nullptr,{},external,selected.charts);
        require(deleted.charts.size()==5,"targeted refresh retained deleted chart");
        bms::SongMetadataCache extCache(external,0,sources.cacheDirectory(external));
        for(const auto& e:fs::directory_iterator(sources.cacheDirectory(external)))if(e.path().extension()==L".meta")write(e.path(),"invalid metadata");
        require(bms::scanSongLibraries(sources).charts.size()==5,"external corrupt cache recovery failed");
        const auto blockedCache=root/L"cache-location-is-a-file";write(blockedCache,"not a directory");
        require(bms::scanSongFolder(external,nullptr,{},0,true,blockedCache).size()==1,"cache write failure prevented chart indexing");
        bms::ui::SongFolderNavigation switched;switched.configure(sources.roots,true);switched.restore(primary/L"Event"/L"Song A",multi.charts);switched.build(multi.charts);
        switched.configure(sources.roots,false);switched.build(multi.charts);require(switched.location()==primary/L"Event"&&switched.accepts(primary/L"Event"/L"Song A"/L"normal.bms"),"mode switch left cursor inside a hidden song directory");
        bms::SongIndexWorker multiWorker;multiWorker.start(sources,[]{});multiWorker.cancel();require(!multiWorker.take()&&!multiWorker.running(),"cancelled multi-root scan leaked publication/thread");
        sources.originalFolders=true;sources.individualCharts=true;sources.collectVariants=true;require(sources.save(),"view preferences save failed");
        restored.initialize(primary,config);require(restored.originalFolders&&restored.individualCharts&&restored.collectVariants,"view preferences lost on restart");
        std::cout << "Library: complete and partial media copy, incremental refresh, unique paths, cancellation cleanup, original independence and batch compatibility passed.\n";
    } catch (const bms::LibraryError& error) {
        const int length = WideCharToMultiByte(CP_UTF8, 0, error.message.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string text(static_cast<size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, error.message.c_str(), -1, text.data(), length, nullptr, nullptr);
        std::cerr << text << '\n'; result = 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    if (created) { std::error_code ec; fs::remove_all(root, ec); }
    return result;
}
