#pragma once
#include "FrameRateSettings.h"
#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <utility>

namespace bms {
struct SettingResult { bool applied{}; int value{}; std::wstring error; };
// Numeric command IDs preserve the existing native/RML mapping. Values are
// domain values (FPS, quarter-speed steps, percentages), never HWNDs.
class SettingsController {
public:
    using Read = std::function<int(int)>;
    using Apply = std::function<bool(int, int, std::wstring&)>;
    SettingsController(Read read, Apply apply, std::function<void()> save)
        : read_(std::move(read)), apply_(std::move(apply)), save_(std::move(save)) {}
    static int canonicalId(int id) {
        constexpr int originals[]{1012, 1021, 1013, 1026, 1023, 1029, 1049};
        return id >= 1260 && id <= 1266 ? originals[id - 1260] : id;
    }
    static bool normalize(int id, int& value) {
        if(id==1096){value=std::clamp(value,0,100);return true;}
        if(id==1097)return value==0||value==1;
        if(id==1098)return value>=800&&value<=1500;
        if(id==1099)return value>=0&&value<=2;
        if(id==1052)return value>=0&&value<=3;
        if(id==1050||id==1051)return value>=-1&&value<=1;
        if(id==1053)return value==0||value==1;
        if (id == 1049) return value >= 0 && value <= 4;
        if (id == 1012) return value==vsyncFps||(value>=0&&value<=1500);
        if (id == 1013) { value = std::clamp(value, 2, 40); return true; }
        if (id == 1018) { value = std::clamp(value, -4096, 4096); return true; }
        if (id == 1045 || id == 1046) { value = std::clamp(value, -256, 256); return true; }
        if (id >= 1041 && id <= 1043) { value = std::clamp(value, 10, 100); return true; }
        if (id == 1020 || id == 1021 || id == 1022 || (id >= 1030 && id <= 1033) || id == 1037 || id == 1038 || id == 1040) {
            value = std::clamp(value, 0, 100); if (id == 1021) value = (value + 5) / 10 * 10; return true;
        }
        if (id == 1025 || id == 1273 || id == 1270 || id == 1271 || id == 1272 || id == 1039 || id == 1044 || id == 1047 || (id >= 1200 && id <= 1206))
            return value == 0 || value == 1;
        if (id == 1023 || id == 1024 || id == 1029) return value >= 0 && value <= 2;
        if (id == 1026 || id == 1035) return value >= 0 && value <= 3;
        if (id == 1027) return value >= 4 && value <= 32 && value % 2 == 0;
        if (id == 1034) return value == 32 || value == 64 || value == 128 || value == 256 || value == 512 || value == 1024;
        if (id >= 1100 && id <= 1107) return value >= 0 && value < 10;
        if (id == 1048) return value >= 0; // Backend checks the current resolution enumeration.
        return false;
    }
    SettingResult set(int id, int value) {
        id = canonicalId(id);
        if (!normalize(id, value)) return {false, read_(id), L"지원하지 않는 설정 값입니다."};
        std::wstring error;
        if (!apply_(id, value, error)) return {false, read_(id), std::move(error)};
        save_(); return {true, read_(id), {}};
    }
    bool enqueue(int id, int value) {
        id = canonicalId(id); if (!normalize(id, value)) return false;
        pending_.push_back({id, value}); return true;
    }
    // Drain outside RmlUi event dispatch before the next UI update. Failed
    // hardware commands return the unchanged actual value and do not persist.
    std::vector<SettingResult> drain() {
        std::vector<SettingResult> results;
        while (!pending_.empty()) { const auto item = pending_.front(); pending_.pop_front(); results.push_back(set(item.first, item.second)); }
        return results;
    }
    void clearPending() { pending_.clear(); }
    std::map<int, int> snapshot() const {
        std::map<int, int> values;
        for (int id : {1012,1013,1018,1020,1021,1022,1023,1024,1025,1026,1027,1029,1034,1035,1037,1038,1039,1040,1041,1042,1043,1044,1045,1046,1047,1048,1049,1050,1051,1052,1053,1096,1097,1098,1099,1270,1271,1272,1273}) values[id] = read_(id);
        for (const auto& range : {std::pair{1030,1033}, std::pair{1100,1107}, std::pair{1200,1206}})
            for (int id = range.first; id <= range.second; ++id) values[id] = read_(id);
        return values;
    }
private:
    Read read_; Apply apply_; std::function<void()> save_;
    std::deque<std::pair<int,int>> pending_;
};
}
