#include "BmsParser.h"
#include "BgaTimeline.h"
#include "ChartOrdering.h"
#include "PreviewTiming.h"
#include "ScrollTimeline.h"
#include "HiSpeedFix.h"
#include "SelectFrameClock.h"
#include "ChartHash.h"
#include "SongMetadataCache.h"
#include <iterator>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void writeWave(const std::filesystem::path& path) {
    constexpr uint32_t rate = 44100;
    constexpr uint16_t channels = 2, bits = 16;
    constexpr uint32_t frames = 2205;
    constexpr uint32_t dataSize = frames * channels * sizeof(int16_t);
    std::ofstream out(path, std::ios::binary);
    auto write = [&out](const auto& value) { out.write(reinterpret_cast<const char*>(&value), sizeof(value)); };
    out.write("RIFF", 4); uint32_t riff = 36 + dataSize; write(riff); out.write("WAVEfmt ", 8);
    uint32_t fmtSize = 16; write(fmtSize); uint16_t format = 1; write(format); write(channels); write(rate);
    uint32_t byteRate = rate * channels * bits / 8; write(byteRate); uint16_t align = channels * bits / 8; write(align); write(bits);
    out.write("data", 4); write(dataSize);
    for (uint32_t i = 0; i < frames; ++i) {
        const double phase = 6.283185307179586 * 523.25 * i / rate;
        const int16_t sample = static_cast<int16_t>(std::sin(phase) * 4500.0);
        write(sample); write(sample);
    }
}

void writeEncoded(const std::filesystem::path& path, const std::wstring& content, UINT codePage) {
    std::ofstream out(path, std::ios::binary);
    const int bytes = WideCharToMultiByte(codePage, 0, content.data(), static_cast<int>(content.size()), nullptr, 0, nullptr, nullptr);
    std::string encoded(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(codePage, 0, content.data(), static_cast<int>(content.size()), encoded.data(), bytes, nullptr, nullptr);
    out.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
}

void writeChart(const std::filesystem::path& path, const std::wstring& content) { writeEncoded(path, content, CP_UTF8); }

const bms::Note* findNote(const bms::Chart& chart, int lane, double time, bool longNote = false) {
    for (const auto& note : chart.notes) if (note.lane == lane && std::abs(note.timeSeconds - time) < 0.001 && note.longNote() == longNote) return &note;
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    try {
        bms::Chart level1, level3, level3Decimal, level10, levelMissing, levelInvalid;
        level1.playLevel = L"1"; level3.playLevel = L"3"; level3Decimal.playLevel = L"3.5";
        level10.playLevel = L"10"; levelInvalid.playLevel = L"3custom";
        require(bms::comparePlayLevelAscending(level1, level3) < 0 &&
            bms::comparePlayLevelAscending(level3, level3Decimal) < 0 &&
            bms::comparePlayLevelAscending(level3Decimal, level10) < 0 &&
            bms::comparePlayLevelAscending(level10, levelMissing) < 0 &&
            bms::comparePlayLevelAscending(levelMissing, levelInvalid) == 0,
            "numeric #PLAYLEVEL ascending order / unranked fallback failed");
        const auto root = std::filesystem::temp_directory_path() / (L"BmsPlayerCppParserTests_" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(root);
        writeWave(root / L"click.wav");
        const auto timingPath = root / L"시간표.bme";
        writeChart(timingPath,
            L"#PLAYER 1\n#TITLE 타임라인 확인\n#ARTIST 테스트\n#GENRE 테스트 장르\n#DIFFICULTY 1\n#PLAYLEVEL 3\n#BPM 120\n#BPM01 60\n#STOP01 48\n#WAV01 click.wav\n#LNOBJ ZZ\n#LNTYPE 1\n"
            L"#00001:0100\n#00011:01ZZ\n#00016:0001\n#00018:0001\n#00019:0001\n#00102:0.5\n#00108:01\n#00109:01\n#00111:0101\n#00211:0100\n#00251:01010000\n");
        auto result = bms::BmsParser().parse(timingPath);
        require(result.chart.mode == bms::ChartMode::SevenKey, "BME 7+1 모드 판별 실패");
        require(result.chart.playable, "정상 7+1 채보가 실행 가능 상태가 아님");
        require(result.chart.title == L"타임라인 확인", "UTF-8 메타데이터 읽기 실패");
        require(result.chart.genre == L"테스트 장르", "#GENRE must be preserved exactly");
        require(result.chart.measureTimes.size()==3 &&
            std::abs(result.chart.measureTimes[0])<1e-9 &&
            std::abs(result.chart.measureTimes[1]-2)<1e-9 &&
            std::abs(result.chart.measureTimes[2]-5)<1e-9,
            "Measure boundaries must follow the meter, BPM and STOP timeline");
        require(result.chart.playLevel == L"3", "#DIFFICULTY chart category must not override #PLAYLEVEL");
        require(result.chart.bgm.size() == 1 && std::abs(result.chart.bgm.front().timeSeconds) < 0.001, "BGM 이벤트 시간 실패");
        bms::Chart delayedMusic;
        delayedMusic.bgm = {{2.5, L"01"}, {4.0, L"02"}};
        require(std::abs(bms::bgaTimelineSeconds(delayedMusic, 0.0) + 2.5) < 0.001 &&
            std::abs(bms::bgaTimelineSeconds(delayedMusic, 2.5)) < 0.001 &&
            std::abs(bms::bgaTimelineSeconds(delayedMusic, 5.0) - 2.5) < 0.001,
            "BGA timeline did not wait for the first background-music event");
        delayedMusic.samples[L"01"] = root / L"click.wav";
        delayedMusic.notes = {{1, 0.5, 0, L"01"}};
        delayedMusic.bgm.insert(delayedMusic.bgm.begin(), {0, L"undefined"});
        require(std::abs(bms::bgaVideoStartSeconds(delayedMusic) - 0.5) < 0.001,
            "fallback video ignored the first keysound or started at an undefined sample");
        delayedMusic.fallbackVideo = root / L"bga.mp4";
        delayedMusic.bitmaps[L"movie"] = delayedMusic.fallbackVideo;
        delayedMusic.bga = {{1.25, bms::BgaLayer::Base, L"movie"}};
        require(std::abs(bms::bgaTimelineSeconds(delayedMusic, 1.25)) < 0.001,
            "explicit movie event acquired an extra first-BGM delay");
        delayedMusic.bga.front().timeSeconds = 0;
        require(bms::bgaVideoStartSeconds(delayedMusic) == 0,
            "authored video start at chart time zero was delayed until a later BGM event");
        delayedMusic.bga.front().timeSeconds = 1.25;
        delayedMusic.bitmaps[L"movie"] = root / L"BGA.mpeg";
        require(std::abs(bms::bgaVideoStartSeconds(delayedMusic) - 1.25) < 0.001,
            "MP4 replacement lost the declared MPEG start event");
        delayedMusic.bitmaps[L"movie"] = root / L"bga.png";
        require(std::abs(bms::bgaVideoStartSeconds(delayedMusic) - 0.5) < 0.001,
            "static artwork incorrectly supplied the movie's start time");
        delayedMusic.bitmaps[L"movie"] = root / L"other-folder" / L"bga.mpeg";
        require(std::abs(bms::bgaVideoStartSeconds(delayedMusic) - 0.5) < 0.001,
            "unrelated movie in a different folder supplied the fallback's start time");
        require(findNote(result.chart, 1, 0, true) && std::abs(findNote(result.chart, 1, 0, true)->lengthSeconds - 1.0) < 0.001, "LNOBJ 롱노트 변환 실패");
        require(findNote(result.chart, 0, 1), "스크래치 채널 매핑 실패");
        require(findNote(result.chart, 6, 1) && findNote(result.chart, 7, 1), "7키 확장 채널 매핑 실패");
        require(findNote(result.chart, 1, 2) && findNote(result.chart, 1, 4), "BPM 변경·마디 길이·STOP 시작점 노트 타임라인 실패");
        const auto* ln = findNote(result.chart, 1, 5, true);
        require(ln && std::abs(ln->lengthSeconds - 1.0) < 0.001, "LNTYPE 1 롱노트 변환 실패");
        require(!result.chart.tempos.empty() && std::abs(result.chart.tempos.back().bpm - 60) < 0.001, "BPM 이벤트 저장 실패");

        const auto stopsPath = root / L"stops.bms";
        writeChart(stopsPath,
            L"#PLAYER 1\n#BPM 120\n#BPM01 240\n#STOP01 48\n#STOP02 96\n#WAV01 click.wav\n#BMP01 movie.mpeg\n"
            L"#00019:00000101\n#00001:0001\n#00004:0001\n#00008:0001\n#00009:0001\n#00009:0002\n"
            L"#00051:01\n#00103:3C\n#00119:0001\n#00151:0001\n");
        const auto stops = bms::BmsParser().parse(stopsPath).chart;
        require(findNote(stops, 7, 1.0) && findNote(stops, 7, 1.75) && findNote(stops, 7, 4.0),
            "STOP beat notes must precede last-authored duplicate delay and use the new BPM");
        require(stops.bgm.size() == 1 && std::abs(stops.bgm[0].timeSeconds - 1) < 1e-9 &&
            stops.bga.size() == 1 && std::abs(stops.bga[0].timeSeconds - 1) < 1e-9,
            "BGM/BGA on a STOP beat were delayed until the stop ended");
        const auto* stoppedLongNote = findNote(stops, 1, 0, true);
        require(stoppedLongNote && std::abs(stoppedLongNote->lengthSeconds - 4.0) < 1e-9,
            "long note spanning a STOP has the wrong real duration");
        for (double time : {1.0, 1.1, 1.3, 1.499, 1.5})
            require(std::abs(bms::scrollPositionSeconds(stops, time) - 1) < 1e-9,
                "visual scroll moved during consecutive STOP events");
        require(std::abs(bms::scrollPositionSeconds(stops, 1.75) - 1.5) < 1e-9 &&
            std::abs(bms::scrollPositionSeconds(stops, 2.0) - 2) < 1e-9 &&
            std::abs(bms::scrollPositionSeconds(stops, 4.0) - 3) < 1e-9,
            "BPM-relative scroll did not resume at the correct speed");

        const auto rapidStopsPath = root / L"rapid-stops.bms";
        writeChart(rapidStopsPath,
            L"#PLAYER 1\n#BPM 132\n#BPM0A 99132\n#STOP0A 750\n#WAV01 click.wav\n"
            L"#00008:000a\n#00009:000A\n#00019:0001\n#00119:01\n");
        const auto rapid = bms::BmsParser().parse(rapidStopsPath).chart;
        const double stopStart = 2 * 60.0 / 132;
        const double stopDuration = 750 * 60.0 / 99132 / 48;
        require(findNote(rapid, 7, stopStart) && findNote(rapid, 7, stopStart + stopDuration + 2 * 60.0 / 99132),
            "high-BPM STOP trick lost its authored note times");
        require(std::abs(bms::scrollPositionSeconds(rapid, stopStart + stopDuration / 2) - stopStart) < 1e-9 &&
            std::abs(bms::scrollPositionSeconds(rapid, rapid.notes.back().timeSeconds) - 4 * 60.0 / 132) < 1e-8,
            "high-BPM STOP trick did not freeze and then advance by the authored beat distance");

        const auto cardRoot = root / L"cards";
        std::filesystem::create_directories(cardRoot / L"art");
        std::filesystem::create_directories(cardRoot / L"result-assets");
        { std::ofstream image(cardRoot / L"art" / L"start.png", std::ios::binary); image << "image"; }
        { std::ofstream image(cardRoot / L"result-assets" / L"mes.bmp", std::ios::binary); image << "image"; }
        const auto cardPath = cardRoot / L"cards.bme";
        writeChart(cardPath, L"#PLAYER 1\n#TITLE Cards\n#BPM 120\n#WAV01 click.wav\n#STAGEFILE art/start.png\n#BACKBMP result-assets/mes.bmp\n#00019:01\n");
        const auto cards = bms::BmsParser().parse(cardPath);
        require(cards.chart.titleImage.filename() == L"start.png" && cards.chart.bitmaps.contains(L"__titlecard"), "declared title stage image resolution failed");
        require(cards.chart.finishImage.filename() == L"mes.bmp" && cards.chart.bitmaps.contains(L"__finishcard"), "declared #BACKBMP result background resolution failed");
        const auto fallbackCardPath = cardRoot / L"title-only.bme";
        writeChart(fallbackCardPath, L"#PLAYER 1\n#TITLE Title fallback\n#BPM 120\n#WAV01 click.wav\n#STAGEFILE art/start.png\n#00019:01\n");
        const auto fallbackCard = bms::BmsParser().parse(fallbackCardPath);
        require(fallbackCard.chart.finishImage == fallbackCard.chart.titleImage && fallbackCard.chart.bitmaps.contains(L"__finishcard"),
            "title card must be used as the result background when no #BACKBMP/finish image exists");

        const auto decimalLevelPath = root / L"decimal-level.bme";
        writeChart(decimalLevelPath, L"#PLAYER 1\n#DIFFICULTY 4\n#PLAYLEVEL 12.5\n#BPM 120\n#00011:01\n");
        const auto decimalLevel = bms::BmsParser().parse(decimalLevelPath);
        require(decimalLevel.chart.playLevel == L"12.5", "decimal #PLAYLEVEL must be preserved exactly");
        {
            // Record and difficulty-table identity: digests of the file's raw bytes.
            const auto abc = bms::chartHashes("abc", 3);
            require(abc.md5 == L"900150983cd24fb0d6963f7d28e17f72" &&
                abc.sha256 == L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "MD5/SHA-256 test vectors");
            std::ifstream in(decimalLevelPath, std::ios::binary);
            const std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const auto expected = bms::chartHashes(bytes);
            require(decimalLevel.chart.sha256 == expected.sha256 && decimalLevel.chart.md5 == expected.md5 &&
                decimalLevel.chart.sha256.size() == 64 && decimalLevel.chart.md5.size() == 32, "parsed chart must carry its file digests");
            const auto missingFile = bms::BmsParser().parse(root / L"does-not-exist.bms");
            require(missingFile.chart.sha256.empty() && missingFile.chart.md5.empty(), "an unreadable chart has no digest");
            const auto cacheDir = root / L"hash-cache";
            bms::SongMetadataCache cache(root, 1, cacheDir);
            cache.save(decimalLevel.chart);
            bms::Chart cached;
            require(cache.load(decimalLevelPath, cached) && cached.sha256 == expected.sha256 && cached.md5 == expected.md5,
                "metadata cache must keep the chart digests");
        }
        const auto missingLevelPath = root / L"category-only.bme";
        writeChart(missingLevelPath, L"#PLAYER 1\n#DIFFICULTY 5\n#BPM 120\n#00011:01\n");
        const auto missingLevel = bms::BmsParser().parse(missingLevelPath);
        require(missingLevel.chart.playLevel.empty(), "#DIFFICULTY must not be invented as a missing #PLAYLEVEL");

        const auto fivePath = root / L"classic.bms";
        writeChart(fivePath, L"#PLAYER 1\n#TITLE Five Key\n#BPM 120\n#WAV01 click.wav\n#00011:01\n#00015:01\n");
        auto five = bms::BmsParser().parse(fivePath);
        require(five.chart.mode == bms::ChartMode::FiveKey && five.chart.playable, "5키 모드 판별 실패");

        const auto tenPath = root / L"ten.bms";
        writeChart(tenPath, L"#PLAYER 1\n#TITLE Ten\n#BPM 120\n#WAV01 click.wav\n#00011:01\n#00021:01\n");
        auto ten = bms::BmsParser().parse(tenPath);
        require(ten.chart.mode == bms::ChartMode::TenKey && ten.chart.playable, "10키 모드 판별 실패");

        const auto modernSevenPath = root / L"modern-seven.bms";
        writeChart(modernSevenPath, L"#PLAYER 1\n#TITLE Modern Seven\n#BPM 120\n#WAV01 click.wav\n#00018:01\n");
        auto modernSeven = bms::BmsParser().parse(modernSevenPath);
        require(modernSeven.chart.mode == bms::ChartMode::SevenKey && modernSeven.chart.playable, "7키 확장 채널 모드 판별 실패");

        const auto doublePath = root / L"double.bme";
        writeChart(doublePath, L"#PLAYER 1\n#TITLE Double\n#BPM 120\n#WAV01 click.wav\n#00011:01\n#00021:01\n#00028:01\n");
        auto dual = bms::BmsParser().parse(doublePath);
        require(dual.chart.mode == bms::ChartMode::FourteenKey && dual.chart.playable, "DP 모드 판별 실패");

        const auto popnPath = root / L"popn.pms";
        writeChart(popnPath, L"#PLAYER 1\n#TITLE Popn\n#BPM 120\n#WAV01 click.wav\n#00011:01\n");
        auto popn = bms::BmsParser().parse(popnPath);
        require(popn.chart.mode == bms::ChartMode::PopnNine && popn.chart.playable, "PMS 모드 판별 실패");

        const auto randomPath = root / L"branch.bme";
        writeChart(randomPath, L"#PLAYER 1\n#TITLE Branch\n#BPM 120\n#WAV01 click.wav\n#SETRANDOM 1\n#IF 2\n#00011:01\n#ELSE\n#00018:01\n#ENDIF\n");
        auto branch = bms::BmsParser().parse(randomPath);
        require(branch.chart.playable && branch.chart.notes.size() == 1 && branch.chart.notes[0].lane == 6, "조건 분기 평가 실패");

        const auto sjisPath = root / L"shift-jis.bme";
        writeEncoded(sjisPath, L"#PLAYER 1\n#TITLE 日本語タイトル\n#BPM 120\n#WAV01 click.wav\n#00019:01\n", 932);
        auto sjis = bms::BmsParser().parse(sjisPath);
        require(sjis.chart.playable && sjis.chart.title == L"日本語タイトル", "Shift-JIS 채보 읽기 실패");

        // 가나 없이 한자/전각 기호뿐인 Shift-JIS 메타데이터는 CP949로도 유효하게 풀리지만
        // 전부 확장 영역의 희귀 한글이 되므로 한국어로 판정하면 안 된다.
        const auto kanjiPath = root / L"shift-jis-kanji.bme";
        writeEncoded(kanjiPath, L"#PLAYER 1\n#GENRE ART CORE\n#TITLE -零-［HYPER］\n#ARTIST SOMON\n#SUBARTIST obj:大我大和\n#BPM 142\n#WAV01 click.wav\n#00019:01\n", 932);
        auto kanji = bms::BmsParser().parse(kanjiPath);
        require(kanji.chart.title == L"-零-［HYPER］" && kanji.chart.subartist == L"obj:大我大和", "한자뿐인 Shift-JIS 제목이 CP949로 오판됨");
        const auto mixedPath = root / L"shift-jis-mixed.bme";
        writeEncoded(mixedPath, L"#PLAYER 1\n#TITLE 果て前の花\n#ARTIST noyrfa\n#SUBARTIST obj 四馬鉐屋\n#BPM 120\n#WAV01 click.wav\n#00019:01\n", 932);
        require(bms::BmsParser().parse(mixedPath).chart.title == L"果て前の花", "가나가 적은 Shift-JIS 제목이 CP949로 오판됨");
        const auto koreanPath = root / L"cp949-title.bme";
        writeEncoded(koreanPath, L"#PLAYER 1\n#TITLE 한국어 제목 테스트\n#ARTIST 작곡가\n#BPM 120\n#WAV01 click.wav\n#00019:01\n", 949);
        require(bms::BmsParser().parse(koreanPath).chart.title == L"한국어 제목 테스트", "선언 없는 CP949 채보가 한국어로 판정되지 않음");

        const auto unknownPath = root / L"unknown.bms";
        writeChart(unknownPath, L"#PLAYER 1\n#TITLE Unknown\n#BPM 120\n");
        auto unknown = bms::BmsParser().parse(unknownPath);
        require(unknown.chart.mode == bms::ChartMode::Unknown && !unknown.chart.playable, "모호한 모드 보수적 차단 실패");

        const auto lnTypePath = root / L"other-ln.bme";
        writeChart(lnTypePath, L"#PLAYER 1\n#TITLE Other LN\n#BPM 120\n#WAV01 click.wav\n#LNTYPE 2\n#00051:0101\n");
        auto otherLn = bms::BmsParser().parse(lnTypePath);
        require(otherLn.chart.playable, "미지원 롱노트 형식 차단 실패");

        const auto missingPath = root / L"missing.bme";
        writeChart(missingPath, L"#PLAYER 1\n#TITLE Missing\n#BPM 120\n#WAV01 absent.wav\n#00011:01\n");
        auto missing = bms::BmsParser().parse(missingPath);
        require(missing.chart.playable && missing.chart.partialLoad, "음원이 없는 실행 가능 채보의 부분 로드 표시 실패");
        require(missing.chart.partialLoadReason.find(L"absent.wav") != std::wstring::npos,
            "부분 로드 이유에 누락된 음원 파일이 표시되지 않음");
        require(missing.chart.samples.empty() && missing.chart.missingSampleFiles.size() == 1,
            "a wholly missing sample set must remain runnable and report its missing file");
        const auto partialPath = root / L"partial-samples.bms";
        writeChart(partialPath, L"#PLAYER 1\n#BPM 120\n#WAV01 click.wav\n#WAV02 absent.wav\n#WAV03 absent.wav\n#00019:010203\n#00001:02\n");
        const auto partial = bms::BmsParser().parse(partialPath);
        require(partial.chart.playable && partial.errors.empty() && partial.chart.notes.size() == 3 && partial.chart.bgm.size() == 1,
            "partially missing sample sets must keep all note and BGM timing playable");
        require(partial.chart.samples.size() == 1 && partial.chart.samples.contains(L"01") &&
            partial.chart.missingSampleFiles.size() == 1 && partial.chart.missingSampleFiles.front().filename() == L"absent.wav",
            "missing samples must be deduplicated and excluded from audio decoding");

        const auto extensionFallbackRoot = root / L"extension-fallback";
        std::filesystem::create_directories(extensionFallbackRoot);
        { std::ofstream ogg(extensionFallbackRoot / L"click.ogg", std::ios::binary); ogg.write("OggS", 4); }
        const auto extensionFallbackPath = extensionFallbackRoot / L"ogg-backed.bms";
        writeChart(extensionFallbackPath, L"#PLAYER 1\n#TITLE OGG fallback\n#BPM 120\n#WAV01 click.wav\n#00019:01\n");
        auto extensionFallback = bms::BmsParser().parse(extensionFallbackPath);
        require(extensionFallback.chart.playable && extensionFallback.chart.samples.contains(L"01"), "같은 이름의 OGG 음원 대체 탐색 실패");
        require(extensionFallback.chart.samples.at(L"01").filename() == L"click.ogg", "BMS의 WAV 확장자 참조를 실제 OGG 파일로 연결하지 못함");
        writeWave(extensionFallbackRoot / L"click.wav");
        auto exactExtension = bms::BmsParser().parse(extensionFallbackPath);
        require(exactExtension.chart.samples.at(L"01").filename() == L"click.wav", "정확히 선언된 음원 파일보다 확장자 대체를 우선함");

        const auto archiveEncodingRoot = root / L"korean-archive-encoding";
        std::filesystem::create_directories(archiveEncodingRoot);
        writeWave(archiveEncodingRoot / L"먛귟궶궢_000.ogg");
        const auto archiveEncodingPath = archiveEncodingRoot / L"normal.bms";
        writeEncoded(archiveEncodingPath,
            L"#PLAYER 1\n#TITLE energy trixxx\n#BPM 160\n#WAVB8 切りなし_000.wav\n#00019:B8\n", 932);
        auto archiveEncoding = bms::BmsParser().parse(archiveEncodingPath);
        require(archiveEncoding.chart.playable && archiveEncoding.errors.empty(),
            "Shift-JIS filename extracted as CP949 must remain playable");
        require(archiveEncoding.chart.samples.at(L"B8").filename() == L"먛귟궶궢_000.ogg",
            "archive filename encoding fallback must resolve the actual OGG file");
        writeWave(archiveEncodingRoot / L"切りなし_000.ogg");
        archiveEncoding = bms::BmsParser().parse(archiveEncodingPath);
        require(archiveEncoding.chart.samples.at(L"B8").filename() == L"切りなし_000.ogg",
            "normal Unicode filename with alternate extension must precede an archive alias");
        writeWave(archiveEncodingRoot / L"切りなし_000.wav");
        archiveEncoding = bms::BmsParser().parse(archiveEncodingPath);
        require(archiveEncoding.chart.samples.at(L"B8").filename() == L"切りなし_000.wav",
            "declared filename must take precedence over all compatibility fallbacks");

        const auto gmFallbackRoot = root / L"gm-zero-based";
        std::filesystem::create_directories(gmFallbackRoot);
        writeWave(gmFallbackRoot / L"gm_000.ogg");
        writeWave(gmFallbackRoot / L"gm_002.ogg");
        const auto gmFallbackPath = gmFallbackRoot / L"gm-fallback.bms";
        writeChart(gmFallbackPath, L"#PLAYER 1\n#TITLE GM fallback\n#BPM 120\n#WAV01 gm_001.ogg\n#00018:01\n");
        auto gmFallback = bms::BmsParser().parse(gmFallbackPath);
        require(gmFallback.chart.samples.contains(L"01") && gmFallback.chart.samples.at(L"01").filename() == L"gm_000.ogg",
            "누락된 gm_001이 0번부터 시작하는 GM 샘플 세트의 gm_000으로 대체되지 않음");
        require(gmFallback.chart.playable, "GM 번호 예외가 채보 실행 가능 상태를 유지하지 못함");
        writeWave(gmFallbackRoot / L"gm_001.ogg");
        gmFallback = bms::BmsParser().parse(gmFallbackPath);
        require(gmFallback.chart.samples.at(L"01").filename() == L"gm_001.ogg",
            "정확히 선언된 gm_001보다 호환 대체 음원을 우선함");

        const auto malformedPath = root / L"malformed.bme";
        writeChart(malformedPath, L"#PLAYER 1\n#TITLE Malformed\n#BPM 120\n#WAV01 click.wav\n#00011:01Z\n");
        auto malformed = bms::BmsParser().parse(malformedPath);
        require(!malformed.chart.playable, "잘못된 오브젝트 데이터 차단 실패");

        const auto recoverableMalformedPath = root / L"recoverable-malformed.bme";
        writeChart(recoverableMalformedPath, L"#PLAYER 1\n#TITLE Recoverable malformed\n#BPM 120\n#WAV01 click.wav\n#00011:01\n#00111:01Z\n");
        auto recoverableMalformed = bms::BmsParser().parse(recoverableMalformedPath);
        require(recoverableMalformed.chart.playable && recoverableMalformed.chart.partialLoad &&
            recoverableMalformed.chart.notes.size() == 1,
            "잘못된 일부 행이 다른 정상 노트의 실행을 막거나 부분 로드로 표시되지 않음");
        require(!recoverableMalformed.chart.partialLoadReason.empty(),
            "부분적으로 잘못된 채보 데이터의 이유가 저장되지 않음");

        // Omitted definitions represent silent objects; sibling definitions are
        // only an experimental fallback, and a local definition must win.
        const auto sharingRoot = root / L"sharing";
        std::filesystem::create_directories(sharingRoot);
        writeWave(sharingRoot / L"click.wav");
        writeChart(sharingRoot / L"silent.bme", L"#PLAYER 1\n#BPM 120\n#LNOBJ ZZ\n#00011:01ZZ\n#00001:01\n");
        auto silent = bms::BmsParser().parse(sharingRoot / L"silent.bme");
        require(silent.chart.playable && silent.chart.notes.size() == 1 && silent.chart.notes[0].longNote(), "무음 롱노트가 삭제되거나 차단됨");
        require(silent.chart.samples.empty() && silent.chart.silentSampleIds == std::vector<std::wstring>{L"01"} && !silent.chart.sharedSampleDefinitions, "무음 샘플 상태 실패");
        writeChart(sharingRoot / L"donor.bme", L"#PLAYER 1\n#BPM 120\n#WAV01 click.wav\n#00011:01\n");
        auto shared = bms::BmsParser().parse(sharingRoot / L"silent.bme");
        require(shared.chart.playable && shared.chart.sharedSampleDefinitions && shared.chart.samples.contains(L"01") && shared.chart.silentSampleIds.empty(), "실험적 샘플 정의 공유 실패");
        writeChart(sharingRoot / L"local.bme", L"#PLAYER 1\n#BPM 120\n#WAV01 absent.wav\n#00011:01\n");
        auto local = bms::BmsParser().parse(sharingRoot / L"local.bme");
        require(local.chart.playable && local.chart.partialLoad && !local.chart.sharedSampleDefinitions,
            "로컬 누락 음원 정의보다 공유 정의를 우선하거나 실행 가능한 부분 로드를 차단함");

        if (argc > 2) {
            const auto externalFolder = std::filesystem::path(argv[2]);
            for (const auto* filename : { L"__Cat's_rule_Easy3.bms", L"__Cat's_rule_Hyper9.bms", L"__Cat's_rule_Insane12.bms", L"__Cat's_rule_Normal7.bms" }) {
                const auto chart = bms::BmsParser().parse(externalFolder / filename);
                require(chart.chart.playable && chart.chart.mode == bms::ChartMode::SevenKey,
                    std::string("Cat's_rule 7+1 채보를 실행할 수 없음: ") + std::filesystem::path(filename).string());
                require(!chart.chart.samples.empty(), "Cat's_rule 샘플 경로가 비어 있음");
                for (const auto& [id, audioPath] : chart.chart.samples)
                    require(std::filesystem::exists(audioPath), "Cat's_rule 샘플 파일을 찾지 못함");
            }
        }

        const auto previewRoot = root / L"previews";
        std::filesystem::create_directories(previewRoot);
        writeWave(previewRoot / L"preview.wav");
        writeWave(previewRoot / L"custom.wav");
        writeChart(previewRoot / L"preview.bms", L"#PLAYER 1\n#BPM 120\n#PREVIEW custom.wav\n#PREVIEWPOINT 12.5\n#PREVIEWDURATION 8\n#00019:01\n");
        auto preview = bms::BmsParser().parse(previewRoot / L"preview.bms").chart;
        require(preview.previewAudio.filename() == L"custom.wav" && preview.previewStartSeconds == 12.5 && preview.previewDurationSeconds == 8,
            "explicit preview file or timing was lost");
        writeChart(previewRoot / L"preview.bms", L"#PLAYER 1\n#BPM 120\n#PREVIEW missing.wav\n#PREVIEWPOINT -1\n#PREVIEWDURATION -1\n#00019:01\n");
        preview = bms::BmsParser().parse(previewRoot / L"preview.bms").chart;
        require(preview.previewAudio.filename() == L"preview.wav" && preview.previewStartSeconds == 0 && preview.previewDurationSeconds == 20,
            "preview fallback or invalid timing defaults failed");
        std::filesystem::remove(previewRoot / L"preview.wav");
        preview = bms::BmsParser().parse(previewRoot / L"preview.bms").chart;
        require(preview.previewAudio.empty(), "unrelated audio incorrectly selected as a preview");
        bms::Chart denseChart;
        denseChart.notes.push_back({1, 0, 0, L"01"});
        denseChart.notes.push_back({1, 100, 0, L"01"});
        for (int i = 0; i < 100; ++i) denseChart.notes.push_back({1, 50 + i * 0.005, 0, L"01"});
        require(bms::automaticPreviewStart(denseChart) >= 45 && bms::automaticPreviewStart(denseChart) <= 55,
            "automatic highlight missed the dense central section");
        writeChart(previewRoot / L"automatic.bms", L"#PLAYER 1\n#BPM 120\n#00019:01\n#02519:01010101010101010101010101010101\n#05019:01\n");
        auto automatic = bms::BmsParser().parse(previewRoot / L"automatic.bms").chart;
        require(!automatic.previewStartDefined && automatic.previewStartSeconds > 40 && automatic.previewStartSeconds < 60,
            "chart without preview headers starts at the intro instead of an automatic highlight");
        writeChart(previewRoot / L"automatic.bms", L"#PLAYER 1\n#BPM 120\n#PREVIEWSTART 0\n#00019:01\n#02519:01010101010101010101010101010101\n#05019:01\n");
        automatic = bms::BmsParser().parse(previewRoot / L"automatic.bms").chart;
        require(automatic.previewStartDefined && automatic.previewStartSeconds == 0, "explicit intro preview was overwritten");
        std::vector<bms::Chart> group{automatic, denseChart};
        group[0].previewStartSeconds = 0; group[0].previewStartDefined = true;
        group[1].previewStartSeconds = 50;
        require(bms::sharePreviewTiming(group, {1, 0}) == 0 && group[0].previewStartSeconds == 0 && group[1].previewStartSeconds == 0,
            "group did not share its explicitly defined preview section");
        group[0].previewStartDefined = false; group[0].previewStartSeconds = 45;
        group[1].previewStartSeconds = 95;
        require(bms::sharePreviewTiming(group, {0, 1}) == 0 && group[1].previewStartSeconds == 45,
            "group variants used different automatic highlight sections");
        std::filesystem::create_directory(previewRoot / L"audio");
        writeWave(previewRoot / L"audio" / L"Case Preview.FLAC");
        writeWave(previewRoot / L"_preview.wav");
        writeChart(previewRoot / L"formats.bms", L"#PLAYER 1\n#BPM 120\n#PREVIEW \"audio/Case Preview.wav\"\n#00019:01\n");
        auto formats = bms::BmsParser().parse(previewRoot / L"formats.bms").chart;
        require(formats.previewAudio.filename() == L"Case Preview.FLAC" && formats.previewAudioCandidates.size() == 2,
            "quoted subfolder reference, FLAC substitution or alternate preview candidate lost");
        const auto japaneseSample=root/L"【C】tone.wav";writeWave(japaneseSample);
        std::wstring crowded=L"#PLAYER 1\n#TITLE ha＠ppy s＠mmer v＠c＠tion!!\n#GENRE Tropic＠l H＠rdcore\n#ARTIST ああああ + 翡乃イスカ\n#BPM 120\n";
        for(int i=0;i<200;++i)crowded+=L"#WAV01 【C】tone.wav\n";
        crowded+=L"#00019:01\n";writeEncoded(root/L"crowded-sjis.bme",crowded,932);
        const auto crowdedChart=bms::BmsParser(false,123,false).parse(root/L"crowded-sjis.bme");
        require(crowdedChart.chart.title==L"ha＠ppy s＠mmer v＠c＠tion!!" && crowdedChart.chart.artist==L"ああああ + 翡乃イスカ" && !crowdedChart.chart.partialLoad,"WAV repetition biased charset or lost Japanese keysounds");
        writeWave(root/L"한국어음원.wav");writeEncoded(root/L"auto-korean.bme",L"#PLAYER 1\n#TITLE ASCII title\n#BPM 120\n#WAV01 한국어음원.wav\n#00019:01\n",949);
        const auto koreanAsset=bms::BmsParser(false,123,false).parse(root/L"auto-korean.bme");
        require(!koreanAsset.chart.partialLoad && koreanAsset.chart.samples.size()==1,"ASCII metadata lost automatic Korean asset encoding");
        std::filesystem::remove_all(root);
        {   // HI-SPEED FIX: 150 BPM for 10 s, 300 BPM for 10 s, then 75 BPM to the last note at 35 s.
            bms::Chart fixChart;fixChart.initialBpm=150;
            fixChart.tempos={{0,150},{10,300},{20,75}};
            fixChart.scroll={{0,0,1},{10,10,2},{20,30,.5}};
            bms::Note last;last.timeSeconds=35;fixChart.notes.push_back(last);
            const auto closeTo=[](double a,double b){return std::abs(a-b)<1e-9;};
            require(closeTo(bms::mainBpm(fixChart),75)&&closeTo(bms::hiSpeedFixBpm(fixChart,bms::HiSpeedFix::Max),300)&&
                closeTo(bms::hiSpeedFixBpm(fixChart,bms::HiSpeedFix::Min),75)&&closeTo(bms::hiSpeedFixBpm(fixChart,bms::HiSpeedFix::Start),150),
                "HI-SPEED FIX reference BPMs");
            require(closeTo(bms::hiSpeedFixScale(fixChart,bms::HiSpeedFix::Off),1)&&closeTo(bms::hiSpeedFixScale(fixChart,bms::HiSpeedFix::Start),1)&&
                closeTo(bms::hiSpeedFixScale(fixChart,bms::HiSpeedFix::Max),.5)&&closeTo(bms::hiSpeedFixScale(fixChart,bms::HiSpeedFix::Main),2)&&
                closeTo(bms::hiSpeedFixScale(fixChart,bms::HiSpeedFix::Min),2)&&closeTo(bms::hiSpeedFixScale(fixChart,bms::HiSpeedFix::Constant),1),
                "HI-SPEED FIX scales");
            // Off keeps the BPM-following scroll exactly; Constant moves with chart time through the BPM changes.
            require(closeTo(bms::fixedScrollPosition(fixChart,15,bms::HiSpeedFix::Off),bms::scrollPositionSeconds(fixChart,15))&&
                closeTo(bms::fixedScrollPosition(fixChart,15,bms::HiSpeedFix::Off),20)&&closeTo(bms::fixedScrollPosition(fixChart,15,bms::HiSpeedFix::Constant),15)&&
                closeTo(bms::fixedScrollPosition(fixChart,25,bms::HiSpeedFix::Main),bms::scrollPositionSeconds(fixChart,25)),
                "HI-SPEED FIX scroll positions");
            fixChart.referenceBpm=300;
            require(closeTo(bms::hiSpeedFixScale(fixChart,bms::HiSpeedFix::Start),2)&&closeTo(bms::hiSpeedFixScale(fixChart,bms::HiSpeedFix::Max),1),
                "HI-SPEED FIX against #BASEBPM");
            bms::Chart flat;flat.initialBpm=120;
            require(closeTo(bms::mainBpm(flat),120)&&closeTo(bms::hiSpeedFixScale(flat,bms::HiSpeedFix::Main),1),"HI-SPEED FIX without tempo events");
        }
        {   // Select screen frame clock: 60 or 120 FPS, anything else falls back to 60.
            SelectFrameClock clock;
            clock.setFps(144);
            require(clock.fps()==60,"select FPS outside 60/120 falls back to 60");
            clock.setFps(120);
            const auto start=SelectFrameClock::Clock::now();
            require(clock.fps()==120&&clock.due(start)&&clock.waitMilliseconds(start)==0,"first select frame is due at once");
            clock.advance(start);
            require(!clock.due(start)&&clock.waitMilliseconds(start)==8&&clock.due(start+clock.period()),"120 FPS select period");
            clock.advance(start+clock.period());
            require(!clock.due(start+clock.period())&&clock.due(start+2*clock.period()),"select frames keep a fixed grid");
            clock.advance(start+std::chrono::seconds(1));
            require(!clock.due(start+std::chrono::seconds(1))&&clock.due(start+std::chrono::seconds(1)+clock.period()),
                "a stalled select frame restarts from now");
            clock.setFps(60);
            require(clock.fps()==60&&clock.waitMilliseconds(start)==0&&std::chrono::duration_cast<std::chrono::microseconds>(clock.period()).count()==16666,
                "60 FPS select period");
            for (const int hz : {144, 240}) {
                clock.setRate(hz);
                require(clock.due(start), "first transition frame is immediately due");
                clock.advance(start);
                const auto period = std::chrono::nanoseconds(1'000'000'000 / hz);
                require(!clock.due(start + period - std::chrono::nanoseconds(1)) && clock.due(start + period),
                    "transition clock uses the monitor period");
            }
        }
        std::wcout << L"BmsCoreTests: all checks passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "BmsCoreTests failed: " << e.what() << "\n";
        return 1;
    }
}
