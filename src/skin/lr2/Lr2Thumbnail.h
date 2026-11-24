#pragma once
// Preview pictures for the skin select screen. In order: the picture the skin names in #INFORMATION,
// a picture kept beside the skin, or the skin itself drawn offscreen in a representative game state.
#include "Lr2Parser.h"
#include <filesystem>
#include <string>

namespace sevenmania::skin::lr2 {
// The load options one representative state per screen uses: a selected song, a stage image, a
// cleared AAA result, a play in progress. The settings pre-check and the preview renderer share them.
Options representativeOptions(ScreenType screen, const SkinChoices& choices);

// The picture #INFORMATION names (when the file exists), else one kept beside the skin whose name
// matches it (see the .cpp for the order). Empty when nothing fits without guessing.
std::filesystem::path findThumbnail(const std::filesystem::path& skin);

// Draws the skin at its own resolution scaled to fit maxWidth x maxHeight and writes a PNG.
// Runs without a window on a WARP device; call from any thread that has initialized COM.
bool renderThumbnail(const std::filesystem::path& skin, ScreenType screen, const SkinChoices& choices,
                     const std::filesystem::path& output, std::string& error,
                     unsigned maxWidth = 1280, unsigned maxHeight = 720);

// File name for a rendered preview: changes with the skin file, its modification time and the choices.
std::wstring thumbnailCacheName(const std::filesystem::path& skin, const SkinChoices& choices);
}
