#include "SystemSoundSet.h"

#include <fstream>
#include <iostream>
#include <set>

namespace fs = std::filesystem;

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
void touch(const fs::path& file) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << "x";
}
} // namespace

int main() {
    using bms::SystemSound;
    const auto root = fs::temp_directory_path() / (L"SevenManiaSystemSoundTests-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);

    // Table: indexed by enum, unique effect ids, only select / decide live in the bgm folder (beatoraja).
    {
        std::set<std::wstring> ids;
        for (size_t i = 0; i < bms::systemSoundTable.size(); ++i) {
            const auto& info = bms::systemSoundTable[i];
            check(static_cast<size_t>(info.sound) == i, "table order matches the enum");
            check(ids.insert(info.effectId).second, "effect ids are unique");
            check(info.bgm == (info.sound == SystemSound::Select || info.sound == SystemSound::Decide), "only select/decide are bgm");
        }
        check(std::wstring(bms::systemSoundInfo(SystemSound::Scratch).stem) == L"scratch", "scratch file name");
        check(std::wstring(bms::systemSoundInfo(SystemSound::Scratch).effectId) == L"songlistscroll", "scratch replaces the list scroll effect");
        check(std::wstring(bms::systemSoundInfo(SystemSound::FolderOpen).stem) == L"f-open", "folder open file name");
        check(std::wstring(bms::systemSoundInfo(SystemSound::ResultClose).stem) == L"resultclose", "result close file name");
        check(std::wstring(bms::systemSoundInfo(SystemSound::CourseClear).stem) == L"course_clear", "course clear file name");
    }

    // Extension order follows beatoraja: wav, flac, ogg, mp3.
    {
        const auto folder = root / L"ext";
        touch(folder / L"clear.mp3");
        check(bms::findSystemSoundFile(folder, L"clear").extension() == L".mp3", "mp3 is found alone");
        touch(folder / L"clear.ogg");
        check(bms::findSystemSoundFile(folder, L"clear").extension() == L".ogg", "ogg beats mp3");
        touch(folder / L"clear.flac");
        check(bms::findSystemSoundFile(folder, L"clear").extension() == L".flac", "flac beats ogg");
        touch(folder / L"clear.wav");
        check(bms::findSystemSoundFile(folder, L"clear").extension() == L".wav", "wav comes first");
        touch(folder / L"fail.m4a");
        check(bms::findSystemSoundFile(folder, L"fail").empty(), "unsupported extensions are ignored");
        check(bms::findSystemSoundFile({}, L"fail").empty(), "empty folder gives no file");
    }

    // Sets: the chosen folder when it holds the marker, otherwise the first set below it by name.
    {
        const auto sounds = root / L"sounds";
        touch(sounds / L"b-set" / L"clear.wav");
        touch(sounds / L"b-set" / L"f-open.wav");
        touch(sounds / L"a-set" / L"nested" / L"clear.ogg");
        touch(sounds / L"a-set" / L"nested" / L"scratch.ogg");
        check(bms::resolveSystemSoundSet(sounds, false) == sounds / L"a-set" / L"nested", "first set by name, searched recursively");
        check(bms::resolveSystemSoundSet(sounds / L"b-set", false) == sounds / L"b-set", "folder with marker is its own set");
        check(bms::resolveSystemSoundSet(sounds, true).empty(), "no select marker means no bgm set");
        check(bms::resolveSystemSoundSet(root / L"missing", false).empty(), "missing folder has no set");

        const auto skin = root / L"skin";
        const auto assets = root / L"assets";
        touch(skin / L"songlistscroll.mp3");
        touch(assets / L"songlistscroll.mp3");
        touch(assets / L"click.mp3");
        touch(assets / L"select.mp3");
        bms::SystemSoundFolders folders{sounds, {}};
        check(bms::resolveSystemSound(SystemSound::Scratch, folders, skin, assets) == sounds / L"a-set" / L"nested" / L"scratch.ogg", "user set wins");
        check(bms::resolveSystemSound(SystemSound::FolderOpen, folders, skin, assets).empty(), "only the chosen set is used for user files");
        check(bms::resolveSystemSound(SystemSound::Click, folders, skin, assets) == assets / L"click.mp3", "missing user file falls back to Assets");
        check(bms::resolveSystemSound(SystemSound::Select, folders, skin, assets) == assets / L"select.mp3", "sound folder does not provide bgm");
        folders.sound = sounds / L"b-set";
        check(bms::resolveSystemSound(SystemSound::FolderOpen, folders, skin, assets) == sounds / L"b-set" / L"f-open.wav", "explicit set folder");
        check(bms::resolveSystemSound(SystemSound::Scratch, folders, skin, assets) == skin / L"songlistscroll.mp3", "skin sound before Assets");
        check(bms::resolveSystemSound(SystemSound::ResultFail, {}, skin, assets).empty(), "no builtin file means silence");

        // A folder without the marker still supplies the files it has.
        const auto loose = root / L"loose";
        touch(loose / L"scratch.flac");
        folders.sound = loose;
        check(bms::resolveSystemSound(SystemSound::Scratch, folders, skin, assets) == loose / L"scratch.flac", "loose folder files are used");

        const auto bgm = root / L"bgm";
        touch(bgm / L"select.ogg");
        touch(bgm / L"decide.wav");
        folders.bgm = bgm;
        check(bms::resolveSystemSound(SystemSound::Select, folders, skin, assets) == bgm / L"select.ogg", "bgm folder select");
        check(bms::resolveSystemSound(SystemSound::Decide, folders, skin, assets) == bgm / L"decide.wav", "bgm folder decide");
    }

    // Settings round trip in the keybindings.ini [Sound] section, including non-ASCII paths and clearing.
    {
        fs::create_directories(root);
        const auto ini = root / L"settings.ini";
        check(bms::loadSystemSoundFolders(ini) == bms::SystemSoundFolders{}, "missing ini means default folders");
        const bms::SystemSoundFolders saved{root / L"효과음 세트", root / L"BGM フォルダ"};
        check(bms::saveSystemSoundFolders(ini, saved), "save succeeds");
        check(bms::loadSystemSoundFolders(ini) == saved, "folders survive a reload");
        std::wstring raw(512, L'\0');
        raw.resize(GetPrivateProfileStringW(L"Sound", L"SystemBgmFolder", L"", raw.data(), 512, ini.c_str()));
        check(raw == saved.bgm.wstring(), "bgm folder key name");
        check(bms::saveSystemSoundFolders(ini, {}), "clearing succeeds");
        check(bms::loadSystemSoundFolders(ini) == bms::SystemSoundFolders{}, "cleared folders load as default");
        check(!bms::saveSystemSoundFolders({}, saved), "no settings file means no save");
    }

    fs::remove_all(root, ec);
    if (failures) return 1;
    std::cout << "System sound tests passed.\n";
    return 0;
}
