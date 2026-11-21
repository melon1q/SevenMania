// 5K / 10K / 14K / PMS 9K 레인 매핑, 레이아웃, 옵션(RANDOM/MIRROR), 키 프로필 회귀 테스트.
#include "BmsParser.h"
#include "ChartOptions.h"
#include "ChartRules.h"
#include "ControllerModel.h"
#include "KeyLaneMap.h"
#include "KeyProfiles.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& message) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << message << "\n";
    }
}

bms::Chart parseText(const std::filesystem::path& dir, const std::wstring& name, const std::wstring& body) {
    const auto path = dir / name;
    {
        std::ofstream out(path, std::ios::binary);
        std::string utf8;  // ASCII only
        for (const wchar_t c : body) utf8.push_back(static_cast<char>(c));
        out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    }
    return bms::BmsParser().parse(path).chart;
}

std::set<int> noteLanes(const bms::Chart& chart) {
    std::set<int> lanes;
    for (const auto& n : chart.notes) lanes.insert(n.lane);
    return lanes;
}

bool hasLongNote(const bms::Chart& chart, int lane) {
    return std::any_of(chart.notes.begin(), chart.notes.end(),
                       [lane](const bms::Note& n) { return n.lane == lane && n.longNote(); });
}

bms::Chart makeChart(bms::ChartMode mode, std::initializer_list<int> lanes, std::wstring option) {
    bms::Chart chart;
    chart.mode = mode;
    double t = 0;
    for (int lane : lanes) {
        bms::Note n;
        n.lane = lane;
        n.timeSeconds = t += 0.1;
        chart.notes.push_back(n);
    }
    chart.options.push_back(std::move(option));
    return chart;
}

std::vector<int> lanesInOrder(const bms::Chart& chart) {
    std::vector<int> lanes;
    for (const auto& n : chart.notes) lanes.push_back(n.lane);
    return lanes;
}

void testParserLanes(const std::filesystem::path& dir) {
    const std::wstring head = L"#PLAYER 1\n#TITLE T\n#BPM 120\n#WAV01 a.wav\n";

    const auto five = parseText(dir, L"five.bms", head + L"#00011:01\n#00015:01\n#00016:01\n");
    check(five.mode == bms::ChartMode::FiveKey, "5K: mode");
    check((noteLanes(five) == std::set<int>{0, 1, 5}), "5K: channels 11,15,16 -> lanes 1,5,0");

    const auto ten = parseText(dir, L"ten.bms", head + L"#00011:01\n#00021:01\n#00025:01\n#00026:01\n");
    check(ten.mode == bms::ChartMode::TenKey, "10K: mode");
    check((noteLanes(ten) == std::set<int>{1, 9, 13, 8}), "10K: 2P channels 21,25,26 -> lanes 9,13,8");

    const auto dp = parseText(dir, L"dp.bme",
        head + L"#00011:01\n#00018:01\n#00019:01\n#00028:01\n#00029:01\n#00026:01\n"
               L"#00031:01\n#00041:01\n#00051:0101\n#00061:0101\n#000D1:ZZ\n#000E1:ZZ\n");
    check(dp.mode == bms::ChartMode::FourteenKey, "14K: mode");
    check(noteLanes(dp).count(6) && noteLanes(dp).count(7) && noteLanes(dp).count(14) && noteLanes(dp).count(15) &&
              noteLanes(dp).count(8),
          "14K: channels 18,19,28,29,26 -> lanes 6,7,14,15,8");
    check(hasLongNote(dp, 1), "14K: LN channel 51 -> 1P lane 1");
    check(hasLongNote(dp, 9), "14K: LN channel 61 -> 2P lane 9");
    check(dp.invisible.size() == 2 && dp.invisible[0].lane == 1 && dp.invisible[1].lane == 9,
          "14K: invisible 31/41 -> lanes 1,9");
    check(dp.mines.size() == 2, "14K: mines D1/E1 parsed");

    const auto pms = parseText(dir, L"pms.pms",
        head + L"#00011:01\n#00015:01\n#00022:01\n#00025:01\n#00021:01\n#00016:01\n");
    check(pms.mode == bms::ChartMode::PopnNine, "PMS: mode");
    check((noteLanes(pms) == std::set<int>{0, 4, 5, 8}), "PMS: 11-15 -> 0-4, 22-25 -> 5-8, 21/16 dropped");
}

void testLayoutConsistency() {
    struct Case { bms::ChartMode mode; int lanes; };
    const Case cases[] = {{bms::ChartMode::FiveKey, 6}, {bms::ChartMode::SevenKey, 8}, {bms::ChartMode::TenKey, 16},
                          {bms::ChartMode::FourteenKey, 16}, {bms::ChartMode::PopnNine, 9}};
    for (const auto& c : cases) {
        bms::Chart chart;
        chart.mode = c.mode;
        const auto layout = bms::chartLayout(chart, 0, 68, 104);
        check(bms::chartLanes(chart) == c.lanes, "chartLanes count for mode " + std::to_string(int(c.mode)));
        check(layout.laneCount() <= bms::chartLanes(chart), "layout.laneCount <= chartLanes, mode " + std::to_string(int(c.mode)));
        int visible = 0;
        for (int lane = 0; lane < layout.laneCount(); ++lane) visible += layout.width(lane) > 0;
        const int expected = c.mode == bms::ChartMode::FiveKey ? 6 : c.mode == bms::ChartMode::SevenKey ? 8
                           : c.mode == bms::ChartMode::TenKey ? 12 : c.mode == bms::ChartMode::FourteenKey ? 16 : 9;
        check(visible == expected, "visible lane count, mode " + std::to_string(int(c.mode)));
    }
    bms::Chart pms, ten, five;
    pms.mode = bms::ChartMode::PopnNine; ten.mode = bms::ChartMode::TenKey; five.mode = bms::ChartMode::FiveKey;
    check(!bms::isScratchLane(pms, 0) && !bms::isScratchLane(pms, 8), "PMS has no scratch lanes");
    check(bms::isScratchLane(ten, 0) && bms::isScratchLane(ten, 8), "10K scratch lanes are 0 and 8");
    check(bms::isScratchLane(five, 0) && !bms::isScratchLane(five, 1), "5K scratch is lane 0");
}

void testOptions() {
    auto apply = [](bms::Chart chart, uint64_t seed = 7) {
        std::vector<std::wstring> errors;
        bms::applyStaticOptions(chart, seed, errors);
        return chart;
    };
    using bms::ChartMode;
    check((lanesInOrder(apply(makeChart(ChartMode::FiveKey, {0, 1, 5}, L"random_mirror"))) == std::vector<int>{0, 5, 1}),
          "5K mirror swaps keys, keeps scratch");
    check((lanesInOrder(apply(makeChart(ChartMode::FourteenKey, {0, 1, 7, 8, 9, 15}, L"random_mirror"))) ==
           std::vector<int>{0, 7, 1, 8, 15, 9}),
          "14K mirror stays inside each side, keeps both scratches");
    check((lanesInOrder(apply(makeChart(ChartMode::TenKey, {8, 9, 13}, L"random_mirror"))) == std::vector<int>{8, 13, 9}),
          "10K mirror covers 2P lanes 9..13 only");
    check((lanesInOrder(apply(makeChart(ChartMode::PopnNine, {0, 4, 8}, L"random_mirror"))) == std::vector<int>{8, 4, 0}),
          "PMS mirror covers all nine lanes");

    // RANDOM은 순열이어야 하고 스크래치/반대편 사이드를 건드리면 안 된다.
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        const auto dp = apply(makeChart(ChartMode::FourteenKey, {1, 2, 3, 4, 5, 6, 7}, L"random_random"), seed);
        const auto dpLanes = lanesInOrder(dp);
        std::set<int> out(dpLanes.begin(), dpLanes.end());
        check(out == std::set<int>{1, 2, 3, 4, 5, 6, 7}, "14K random keeps notes on the same side as a permutation");
        const auto sc = apply(makeChart(ChartMode::FourteenKey, {0, 8}, L"random_random"), seed);
        check((lanesInOrder(sc) == std::vector<int>{0, 8}), "14K random keeps scratch fixed");
        const auto pms = apply(makeChart(ChartMode::PopnNine, {0, 1, 2, 3, 4, 5, 6, 7, 8}, L"random_random"), seed);
        const auto pmsLanes = lanesInOrder(pms);
        std::set<int> pl(pmsLanes.begin(), pmsLanes.end());
        check(pl.size() == 9 && *pl.begin() == 0 && *pl.rbegin() == 8, "PMS random is a permutation of 0..8");
    }

    // notes_copy: 5K -> 10K, 7K -> 14K.
    auto copy5 = apply(makeChart(ChartMode::FiveKey, {0, 1}, L"notes_copy"));
    check(copy5.mode == ChartMode::TenKey, "5K notes_copy becomes 10K");
    auto copy7 = apply(makeChart(ChartMode::SevenKey, {0, 1}, L"notes_copy"));
    check(copy7.mode == ChartMode::FourteenKey, "7K notes_copy becomes 14K");
}

// ChartMode 숫자값은 메타데이터 캐시와 Rust FFI에 저장되므로 바뀌면 안 된다.
static_assert(static_cast<int>(bms::ChartMode::Unknown) == 0 && static_cast<int>(bms::ChartMode::FiveKey) == 1 &&
              static_cast<int>(bms::ChartMode::SevenKey) == 2 && static_cast<int>(bms::ChartMode::TenKey) == 3 &&
              static_cast<int>(bms::ChartMode::FourteenKey) == 4 && static_cast<int>(bms::ChartMode::PopnNine) == 5 &&
              static_cast<int>(bms::ChartMode::Other) == 6, "ChartMode values are persisted; do not renumber");

void testLaneKeyMapping() {
    using bms::ChartMode;
    check(bms::keyProfileSlot(ChartMode::TenKey, 0) == 0 && bms::keyProfileSlot(ChartMode::TenKey, 5) == 5, "10K 1P slots 0..5");
    check(bms::keyProfileSlot(ChartMode::TenKey, 8) == 6 && bms::keyProfileSlot(ChartMode::TenKey, 13) == 11, "10K 2P lanes 8..13 -> slots 6..11");
    for (int lane : {6, 7, 14, 15}) check(bms::keyProfileSlot(ChartMode::TenKey, lane) == -1, "10K empty lane " + std::to_string(lane) + " has no key");
    check(bms::keyProfileSlot(ChartMode::FiveKey, 5) == 5 && bms::keyProfileSlot(ChartMode::FiveKey, 6) == -1, "5K has six keys");
    check(bms::keyProfileSlot(ChartMode::PopnNine, 8) == 8 && bms::keyProfileSlot(ChartMode::PopnNine, 9) == -1, "PMS has nine keys");
    check(bms::keyProfileSlot(ChartMode::FourteenKey, 15) == 15, "14K lane 15 -> slot 15");

    // 보이는 모든 레인은 서로 다른 키를 가져야 하고, 보이지 않는 레인은 키가 없어야 한다.
    bms::KeyProfiles profiles;
    for (auto mode : {ChartMode::FiveKey, ChartMode::SevenKey, ChartMode::TenKey, ChartMode::FourteenKey, ChartMode::PopnNine}) {
        bms::Chart chart;
        chart.mode = mode;
        const auto layout = bms::chartLayout(chart, 0, 68, 104);
        std::set<UINT> keys;
        for (int lane = 0; lane < bms::chartLanes(chart); ++lane) {
            const UINT key = bms::laneKeyFor(profiles, mode, lane);
            const bool visible = lane < layout.laneCount() && layout.width(lane) > 0;
            check(visible == (key != 0), "mode " + std::to_string(int(mode)) + " lane " + std::to_string(lane) + " visibility matches key assignment");
            if (key) check(keys.insert(key).second, "mode " + std::to_string(int(mode)) + " lane " + std::to_string(lane) + " key is unique");
        }
    }
}

void testControllerSlots() {
    using bms::ChartMode;
    // 7K/5K: 슬롯 = 레인, 2P 슬롯은 사용되지 않는다.
    check(bms::controllerSlotLane(ChartMode::SevenKey, 0) == 0 && bms::controllerSlotLane(ChartMode::SevenKey, 7) == 7, "7K slots 0..7");
    check(bms::controllerSlotLane(ChartMode::SevenKey, 8) == -1 && bms::controllerSlotLane(ChartMode::SevenKey, 15) == -1, "7K has no 2P slots");
    check(bms::controllerSlotLane(ChartMode::FiveKey, 5) == 5 && bms::controllerSlotLane(ChartMode::FiveKey, 6) == -1, "5K slots 0..5");
    // DP: 슬롯 = 레인. 10K의 빈 레인(6,7,14,15)은 사용되지 않는다.
    check(bms::controllerSlotLane(ChartMode::FourteenKey, 8) == 8 && bms::controllerSlotLane(ChartMode::FourteenKey, 15) == 15, "14K 2P slots map to lanes 8..15");
    check(bms::controllerSlotLane(ChartMode::TenKey, 13) == 13 && bms::controllerSlotLane(ChartMode::TenKey, 14) == -1 && bms::controllerSlotLane(ChartMode::TenKey, 7) == -1, "10K unused slots");
    // PMS: 스크래치 없음. 1P 건반 1-7 -> 레인 0-6, 2P 건반 1-2 -> 레인 7-8.
    check(bms::controllerSlotLane(ChartMode::PopnNine, 0) == -1 && bms::controllerSlotLane(ChartMode::PopnNine, 8) == -1, "PMS ignores scratch slots");
    check(bms::controllerSlotLane(ChartMode::PopnNine, 1) == 0 && bms::controllerSlotLane(ChartMode::PopnNine, 7) == 6, "PMS 1P keys -> lanes 0..6");
    check(bms::controllerSlotLane(ChartMode::PopnNine, 9) == 7 && bms::controllerSlotLane(ChartMode::PopnNine, 10) == 8 && bms::controllerSlotLane(ChartMode::PopnNine, 11) == -1, "PMS 2P keys -> lanes 7,8");
    std::set<int> lanes;
    for (int slot = 0; slot < 16; ++slot) if (const int lane = bms::controllerSlotLane(ChartMode::PopnNine, slot); lane >= 0) check(lanes.insert(lane).second, "PMS slot->lane is injective");
    check(lanes.size() == 9, "PMS covers all nine lanes");

    bms::controller::Config config;
    config.enabled = true;
    config.keys2p[0] = 20;
    config.scratch2pCw = 30;
    bms::controller::Mapper mapper;
    bms::controller::Sample sample;
    sample.connected = true;
    sample.axes.fill(32767);
    mapper.update(config, sample, 0, true);
    sample.buttons[20] = true;
    sample.buttons[30] = true;
    const auto state = mapper.update(config, sample, 5, true);
    check(state.lanes[9] && state.lanes[8], "controller 2P key and scratch buttons map to slots 9 and 8");
    check(!state.lanes[1] && !state.lanes[0], "2P buttons do not leak into 1P slots");
}

void testLaneGeometry() {
    using bms::ChartMode;
    // 보이는 레인은 겹치지 않고 [0,totalWidth)를 빈틈없이 채워야 한다.
    for (auto mode : {ChartMode::FiveKey, ChartMode::SevenKey, ChartMode::TenKey, ChartMode::FourteenKey, ChartMode::PopnNine}) {
        bms::Chart chart;
        chart.mode = mode;
        const auto layout = bms::chartLayout(chart, 100, 68, 104);
        std::vector<std::pair<int, int>> spans;
        for (int lane = 0; lane < layout.laneCount(); ++lane)
            if (layout.width(lane) > 0) spans.push_back({layout.left(lane), layout.right(lane)});
        std::sort(spans.begin(), spans.end());
        int x = 100;
        bool tiled = !spans.empty();
        for (const auto& span : spans) { tiled = tiled && span.first == x; x = span.second; }
        check(tiled && x == 100 + layout.totalWidth(), "lanes tile the field without gaps, mode " + std::to_string(int(mode)));
    }

    bms::Chart dp;
    dp.mode = ChartMode::FourteenKey;
    const auto layout = bms::chartLayout(dp, 0, 68, 104);
    check(layout.left(0) == 0, "14K 1P scratch is leftmost");
    check(layout.right(8) == layout.totalWidth(), "14K 2P scratch is on the right edge");
    check(layout.left(9) == layout.right(7), "14K 2P key 1 directly follows 1P key 7");
    for (int lane = 9; lane < 15; ++lane) check(layout.right(lane) == layout.left(lane + 1), "14K 2P keys are contiguous left to right");
    check(layout.right(15) == layout.left(8), "14K 2P scratch follows 2P key 7");
    for (int position = 0; position < 16; ++position) check(layout.position(layout.laneAt(position)) == position, "laneAt/position are inverse");

    bms::Chart ten;
    ten.mode = ChartMode::TenKey;
    const auto tenLayout = bms::chartLayout(ten, 0, 68, 104);
    check(tenLayout.right(8) == tenLayout.totalWidth() && tenLayout.left(9) == tenLayout.right(5), "10K: 2P scratch on the right, 2P keys follow 1P key 5");

    // 구분선: 5K는 가장자리를 그리지 않고, 7K는 기존과 같고, DP는 플레이어 경계와 2P 스크래치를 포함한다.
    auto separators = [](const bms::Chart& chart) {
        const auto l = bms::chartLayout(chart, 0, 68, 104);
        std::vector<int> out;
        for (int lane = 0; lane < l.laneCount(); ++lane) out.push_back(l.separatorIndex(lane));
        return out;
    };
    bms::Chart five, seven, pms;
    five.mode = ChartMode::FiveKey; seven.mode = ChartMode::SevenKey; pms.mode = ChartMode::PopnNine;
    check((separators(seven) == std::vector<int>{-1, 0, 1, 2, 3, 4, 5, 6}), "7K separator indices unchanged");
    check((separators(five) == std::vector<int>{-1, 0, 1, 2, 3, 4}), "5K has no separators past its last key");
    check((separators(pms) == std::vector<int>{-1, 0, 1, 2, 3, 4, 5, 6, 6}), "PMS separators between all nine buttons");
    const auto dpSeparators = separators(dp);
    check(dpSeparators[8] == 0 && dpSeparators[9] == 0 && dpSeparators[15] == 6 && dpSeparators[0] == -1, "14K separators cover both sides");
    const auto tenSeparators = separators(ten);
    check(tenSeparators[6] == -1 && tenSeparators[7] == -1 && tenSeparators[14] == -1 && tenSeparators[15] == -1, "10K empty lanes have no separator");

    // 색: PMS는 스크래치 색이 중앙 버튼에만 쓰이고 양끝이 같은 색이다.
    check(layout.colorSlot(0) == 0 && layout.colorSlot(8) == 0, "DP scratch lanes share the scratch color slot");
    const auto pmsLayout = bms::chartLayout(pms, 0, 68, 104);
    check(pmsLayout.colorSlot(4) == 0 && pmsLayout.colorSlot(0) == pmsLayout.colorSlot(8) && pmsLayout.colorSlot(0) != 0, "PMS colors mirror around the center button");
    for (int lane = 0; lane < 9; ++lane) check(pmsLayout.colorSlot(lane) == pmsLayout.colorSlot(8 - lane), "PMS colors are symmetric");
}

void testKeyProfileLoad() {
    const auto ini = std::filesystem::temp_directory_path() / L"BmsKeyModeLaneTests-keys.ini";
    std::filesystem::remove(ini);
    bms::KeyProfiles defaults;
    check(defaults.keys[4][8] == VK_RSHIFT && defaults.keys[3][6] == VK_RSHIFT, "DP 2P scratch defaults to RShift");
    check(defaults.keys[4][0] == VK_LSHIFT && defaults.keys[0][0] == VK_LSHIFT, "1P scratch defaults to LShift");

    // 잘못된 레인 하나는 그 레인만 기본값으로 되돌리고 나머지 사용자 설정은 유지한다.
    WritePrivateProfileStringW(L"KeyBindings14", L"Lane1", L"27", ini.c_str());   // VK_ESCAPE: 예약키
    WritePrivateProfileStringW(L"KeyBindings14", L"Lane4", L"81", ini.c_str());   // 'Q'
    bms::KeyProfiles partial;
    partial.load(ini);
    check(partial.keys[4][1] == defaults.keys[4][1], "invalid lane falls back to its default only");
    check(partial.keys[4][4] == 'Q', "other custom lanes are kept");
    check(!partial.warnings.empty(), "fallback leaves a warning");

    // 예전에 저장된 좌우 구분 없는 Shift(16)는 그 레인의 기본 Shift로 바꿔 읽는다(눌러도 반응하지 않던 문제).
    WritePrivateProfileStringW(L"KeyBindings14", L"Lane1", nullptr, ini.c_str());
    WritePrivateProfileStringW(L"KeyBindings14", L"Lane4", nullptr, ini.c_str());
    WritePrivateProfileStringW(L"KeyBindings14", L"Lane8", L"16", ini.c_str());
    WritePrivateProfileStringW(L"KeyBindings7", L"Lane0", L"16", ini.c_str());
    bms::KeyProfiles generic;
    generic.load(ini);
    check(generic.keys[4][8] == VK_RSHIFT, "side-less Shift on the 2P scratch becomes Right Shift");
    check(generic.keys[1][0] == VK_LSHIFT, "side-less Shift on the 1P scratch becomes Left Shift");
    WritePrivateProfileStringW(L"KeyBindings14", L"Lane8", nullptr, ini.c_str());
    WritePrivateProfileStringW(L"KeyBindings7", L"Lane0", nullptr, ini.c_str());

    // 기본값과 충돌하면 해당 모드만 전체 기본값으로 되돌린다.
    WritePrivateProfileStringW(L"KeyBindings14", L"Lane1", nullptr, ini.c_str());
    WritePrivateProfileStringW(L"KeyBindings14", L"Lane4", nullptr, ini.c_str());
    WritePrivateProfileStringW(L"KeyBindings14", L"Lane2", L"88", ini.c_str());   // 'X'는 Lane3 기본값
    WritePrivateProfileStringW(L"KeyBindings5", L"Lane5", L"75", ini.c_str());    // 'K'
    bms::KeyProfiles collide;
    collide.load(ini);
    check(collide.keys[4] == defaults.keys[4], "colliding profile resets that mode to defaults");
    check(collide.keys[0][5] == 'K', "other modes are unaffected by the collision");
    std::filesystem::remove(ini);
}

void testDeckWidths() {
    using bms::ChartMode;
    // The loading preview, gameplay and the skin gear share these widths; the default skin's gear
    // panels (tools/generate-gear-modes.py) are 384 / 492 / 504 / 648 / 540 px wide.
    const struct { ChartMode mode; int total; } cases[] = {{ChartMode::FiveKey, 384}, {ChartMode::SevenKey, 492}, {ChartMode::TenKey, 504},
                                                           {ChartMode::FourteenKey, 648}, {ChartMode::PopnNine, 540}};
    for (const auto& c : cases) {
        bms::Chart chart;
        chart.mode = c.mode;
        const auto widths = bms::chartDeckWidths(chart);
        check(bms::chartLayout(chart, 0, widths.key, widths.scratch).totalWidth() == c.total, "deck width of mode " + std::to_string(int(c.mode)));
    }
}

void testCaptureOrder() {
    using bms::ChartMode;
    // Binding capture follows the screen: slots form a permutation and their lanes run left to right.
    const struct { int profile; ChartMode mode; } cases[] = {{0, ChartMode::FiveKey}, {1, ChartMode::SevenKey}, {2, ChartMode::PopnNine},
                                                             {3, ChartMode::TenKey}, {4, ChartMode::FourteenKey}};
    for (const auto& c : cases) {
        bms::Chart chart;
        chart.mode = c.mode;
        const auto layout = bms::chartLayout(chart, 0, 68, 104);
        std::set<int> slots;
        int previousLeft = -1;
        for (int index = 0; index < bms::KeyProfiles::counts[c.profile]; ++index) {
            const int slot = bms::keyProfileCaptureSlot(c.profile, index);
            check(slot >= 0 && slot < bms::KeyProfiles::counts[c.profile] && slots.insert(slot).second, "capture order is a permutation, profile " + std::to_string(c.profile));
            int lane = -1;
            for (int candidate = 0; candidate < bms::chartLanes(chart); ++candidate)
                if (bms::keyProfileSlot(c.mode, candidate) == slot) lane = candidate;
            check(lane >= 0, "capture slot maps to a lane, profile " + std::to_string(c.profile));
            if (lane < 0) continue;
            check(layout.left(lane) > previousLeft, "capture order runs left to right, profile " + std::to_string(c.profile) + " index " + std::to_string(index));
            previousLeft = layout.left(lane);
        }
    }
    check(bms::keyProfileSlotName(4, 8) == L"2P 스크래치" && bms::keyProfileSlotName(4, 9) == L"2P 1번 건반" && bms::keyProfileSlotName(1, 0) == L"스크래치" &&
          bms::keyProfileSlotName(3, 7) == L"2P 1번 건반" && bms::keyProfileSlotName(2, 8) == L"9번 버튼", "capture prompt names");
}

void testKeyProfiles() {
    bms::KeyProfiles profiles;
    const std::set<UINT> reserved{VK_ESCAPE, VK_RETURN, VK_PRIOR, VK_NEXT, VK_F1, VK_F2, VK_F5, VK_TAB, VK_LWIN, VK_RWIN, VK_LMENU, VK_RMENU};
    for (int m = 0; m < 5; ++m) {
        std::set<UINT> seen;
        for (int i = 0; i < bms::KeyProfiles::counts[m]; ++i) {
            const UINT key = profiles.keys[m][i];
            check(key != 0 && !reserved.count(key), "key profile " + std::to_string(bms::KeyProfiles::modes[m]) + "K lane " + std::to_string(i) + " is not reserved");
            check(seen.insert(key).second, "key profile " + std::to_string(bms::KeyProfiles::modes[m]) + "K lane " + std::to_string(i) + " is unique");
        }
    }
}

}  // namespace

int main() {
    const auto dir = std::filesystem::temp_directory_path() / L"BmsKeyModeLaneTests";
    std::filesystem::create_directories(dir);
    try {
        testParserLanes(dir);
        testLayoutConsistency();
        testOptions();
        testKeyProfiles();
        testLaneKeyMapping();
        testControllerSlots();
        testKeyProfileLoad();
        testLaneGeometry();
        testCaptureOrder();
        testDeckWidths();
    } catch (const std::exception& e) {
        std::cerr << "exception: " << e.what() << "\n";
        ++failures;
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    if (failures) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "BmsKeyModeLaneTests passed\n";
    return 0;
}
