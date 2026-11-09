#pragma once
#include "BmsParser.h"
#include "KeyProfiles.h"

namespace bms {

// Index into KeyProfiles for a chart mode (5K, 7K, PMS 9K, 10K, 14K).
inline int keyProfileIndex(ChartMode mode) {
    switch (mode) {
    case ChartMode::FiveKey: return 0;
    case ChartMode::PopnNine: return 2;
    case ChartMode::TenKey: return 3;
    case ChartMode::FourteenKey: return 4;
    default: return 1;
    }
}

// Profile slot of a chart lane, or -1 if the lane has no key in this mode.
// 10K profile slots are 6 + 6; the parser places the second player at lanes 8-13.
inline int keyProfileSlot(ChartMode mode, int lane) {
    const int profile = keyProfileIndex(mode);
    int slot = lane;
    if (profile == 3) {
        if (lane >= 8 && lane < 14) slot = lane - 2;
        else if (lane >= 6) return -1;
    }
    if (slot < 0 || slot >= KeyProfiles::counts[profile]) return -1;
    return slot;
}

inline UINT laneKeyFor(const KeyProfiles& profiles, ChartMode mode, int lane) {
    const int slot = keyProfileSlot(mode, lane);
    return slot < 0 ? 0 : profiles.keys[keyProfileIndex(mode)][slot];
}

// Key binding capture/listing follows the on-screen left-to-right order (DP: SC 1-7 | 1-7 SC), while
// KeyProfiles keeps slots in lane order. Returns the profile slot captured at position `index`.
inline int keyProfileCaptureSlot(int profile, int index) {
    if (profile == 4) return index < 8 ? index : index < 15 ? index + 1 : 8;    // 14K
    if (profile == 3) return index < 6 ? index : index < 11 ? index + 1 : 6;    // 10K
    return index;
}
// Short Korean label of a profile slot for capture prompts, e.g. "2P 3번 건반", "1P 스크래치", "5번 버튼".
inline std::wstring keyProfileSlotName(int profile, int slot) {
    const bool dual = profile == 3 || profile == 4;
    const int side = dual ? slot / (profile == 4 ? 8 : 6) : 0;
    const int local = dual ? slot % (profile == 4 ? 8 : 6) : slot;
    const std::wstring prefix = dual ? (side == 0 ? L"1P " : L"2P ") : L"";
    if (profile == 2) return std::to_wstring(slot + 1) + L"번 버튼";
    return prefix + (local == 0 ? std::wstring(L"스크래치") : std::to_wstring(local) + L"번 건반");
}

// Gear mode index used by skins (see skin/GearMode.h): 0 = 7K, 1 = 5K, 2 = 10K, 3 = 14K, 4 = PMS.
inline int gearModeIndex(ChartMode mode) {
    switch (mode) {
    case ChartMode::FiveKey: return 1;
    case ChartMode::TenKey: return 2;
    case ChartMode::FourteenKey: return 3;
    case ChartMode::PopnNine: return 4;
    default: return 0;
    }
}

// Controller slots use DP numbering (0 = 1P scratch, 1-7 = 1P keys, 8 = 2P scratch, 9-15 = 2P keys).
// Returns the chart lane that slot drives in this mode, or -1 if the slot is unused.
inline int controllerSlotLane(ChartMode mode, int slot) {
    if (slot < 0 || slot > 15) return -1;
    if (mode == ChartMode::PopnNine) {
        // PMS has no scratch: 1P keys 1-7 -> lanes 0-6, 2P keys 1-2 -> lanes 7-8.
        if (slot >= 1 && slot <= 7) return slot - 1;
        if (slot == 9 || slot == 10) return slot - 2;
        return -1;
    }
    return keyProfileSlot(mode, slot) < 0 ? -1 : slot;
}

}  // namespace bms
