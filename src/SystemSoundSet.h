#pragma once
// User-selectable system sound folders. File names follow beatoraja's SystemSoundManager
// (bgm folder: select / decide, sound folder: scratch, f-open, ... clear, fail) and the extensions
// its AudioDriver tries (.wav, .flac, .ogg, .mp3). SevenMania-only events (click, levelchange)
// use their own stems. A missing file falls back to the skin / built-in asset, then to silence.
#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>
#include <windows.h>

namespace bms {

enum class SystemSound {
    Select, Decide,
    Scratch, FolderOpen, FolderClose, OptionChange, OptionOpen, OptionClose,
    PlayReady, PlayStop, ResultClear, ResultFail, ResultClose,
    CourseClear, CourseFail, CourseClose,
    Click, DifficultyChange,
    Count
};

struct SystemSoundInfo {
    SystemSound sound;
    const wchar_t* effectId;    // id passed to AudioEngine::loadEffect / triggerEffect
    const wchar_t* stem;        // file name without extension inside the chosen folder
    bool bgm;                   // true: bgm folder, false: sound folder
    const wchar_t* builtinFile; // skin "sounds" / Assets fallback, nullptr when none ships
};

inline constexpr std::array<SystemSoundInfo, static_cast<size_t>(SystemSound::Count)> systemSoundTable{{
    {SystemSound::Select, L"select", L"select", true, L"select.mp3"},
    {SystemSound::Decide, L"decide", L"decide", true, L"decide.mp3"},
    {SystemSound::Scratch, L"songlistscroll", L"scratch", false, L"songlistscroll.mp3"},
    {SystemSound::FolderOpen, L"f-open", L"f-open", false, nullptr},
    {SystemSound::FolderClose, L"f-close", L"f-close", false, nullptr},
    {SystemSound::OptionChange, L"o-change", L"o-change", false, nullptr},
    {SystemSound::OptionOpen, L"o-open", L"o-open", false, nullptr},
    {SystemSound::OptionClose, L"o-close", L"o-close", false, nullptr},
    {SystemSound::PlayReady, L"playready", L"playready", false, nullptr},
    {SystemSound::PlayStop, L"playstop", L"playstop", false, nullptr},
    {SystemSound::ResultClear, L"clear", L"clear", false, nullptr},
    {SystemSound::ResultFail, L"fail", L"fail", false, nullptr},
    {SystemSound::ResultClose, L"resultclose", L"resultclose", false, nullptr},
    {SystemSound::CourseClear, L"course_clear", L"course_clear", false, nullptr},
    {SystemSound::CourseFail, L"course_fail", L"course_fail", false, nullptr},
    {SystemSound::CourseClose, L"course_close", L"course_close", false, nullptr},
    {SystemSound::Click, L"click", L"click", false, L"click.mp3"},
    {SystemSound::DifficultyChange, L"levelchange", L"levelchange", false, L"levelchange.mp3"},
}};

inline const SystemSoundInfo& systemSoundInfo(SystemSound sound) noexcept { return systemSoundTable[static_cast<size_t>(sound)]; }

// Same order as beatoraja's AudioDriver.getPaths. All four decode through AudioEngine
// (Media Foundation for wav/flac/mp3, stb_vorbis for ogg).
inline constexpr std::array<const wchar_t*, 4> systemSoundExtensions{L".wav", L".flac", L".ogg", L".mp3"};

inline std::filesystem::path findSystemSoundFile(const std::filesystem::path& folder, const wchar_t* stem) {
    if (folder.empty() || !stem) return {};
    std::error_code ec;
    for (const auto* extension : systemSoundExtensions) {
        auto file = folder / (std::wstring(stem) + extension);
        if (std::filesystem::is_regular_file(file, ec)) return file;
        ec.clear();
    }
    return {};
}

// beatoraja treats any directory holding the marker (select.* for bgm, clear.* for sound) as a set and
// picks one at random. SevenMania keeps it deterministic: the chosen folder itself when it has the
// marker, otherwise the first set found in name order below it (a few levels deep).
inline std::filesystem::path resolveSystemSoundSet(const std::filesystem::path& folder, bool bgm, int depth = 3) {
    std::error_code ec;
    if (folder.empty() || !std::filesystem::is_directory(folder, ec)) return {};
    if (!findSystemSoundFile(folder, bgm ? L"select" : L"clear").empty()) return folder;
    if (depth <= 0) return {};
    std::vector<std::filesystem::path> children;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entryError;
        if (it->is_directory(entryError) && !it->is_symlink(entryError)) children.push_back(it->path());
    }
    std::sort(children.begin(), children.end());
    for (const auto& child : children)
        if (auto set = resolveSystemSoundSet(child, bgm, depth - 1); !set.empty()) return set;
    return {};
}

struct SystemSoundFolders {
    std::filesystem::path sound, bgm;
    bool operator==(const SystemSoundFolders&) const = default;
};

// Resolution order: user folder (its set) -> skin sounds folder -> Assets. Empty means "no sound".
// When the user folder has no set marker, the folder itself is still searched for individual files.
inline std::filesystem::path resolveSystemSound(SystemSound sound, const SystemSoundFolders& folders,
    const std::filesystem::path& skinSounds, const std::filesystem::path& assets) {
    const auto& info = systemSoundInfo(sound);
    const auto& userFolder = info.bgm ? folders.bgm : folders.sound;
    if (!userFolder.empty()) {
        auto set = resolveSystemSoundSet(userFolder, info.bgm);
        if (set.empty()) set = userFolder;
        if (auto file = findSystemSoundFile(set, info.stem); !file.empty()) return file;
    }
    if (!info.builtinFile) return {};
    std::error_code ec;
    for (const auto* folder : {&skinSounds, &assets}) {
        if (folder->empty()) continue;
        auto file = *folder / info.builtinFile;
        if (std::filesystem::is_regular_file(file, ec)) return file;
        ec.clear();
    }
    return {};
}

inline constexpr const wchar_t* systemSoundSection = L"Sound";
inline constexpr const wchar_t* systemSoundFolderKey = L"SystemSoundFolder";
inline constexpr const wchar_t* systemBgmFolderKey = L"SystemBgmFolder";

inline std::wstring readIniPath(const std::filesystem::path& ini, const wchar_t* key) {
    std::wstring buffer(MAX_PATH * 4, L'\0');
    const DWORD length = GetPrivateProfileStringW(systemSoundSection, key, L"", buffer.data(), static_cast<DWORD>(buffer.size()), ini.c_str());
    buffer.resize(length);
    return buffer;
}

inline SystemSoundFolders loadSystemSoundFolders(const std::filesystem::path& ini) {
    if (ini.empty()) return {};
    return {readIniPath(ini, systemSoundFolderKey), readIniPath(ini, systemBgmFolderKey)};
}

inline bool saveSystemSoundFolders(const std::filesystem::path& ini, const SystemSoundFolders& folders) {
    if (ini.empty()) return false;
    return WritePrivateProfileStringW(systemSoundSection, systemSoundFolderKey, folders.sound.c_str(), ini.c_str()) &&
        WritePrivateProfileStringW(systemSoundSection, systemBgmFolderKey, folders.bgm.c_str(), ini.c_str());
}

} // namespace bms
