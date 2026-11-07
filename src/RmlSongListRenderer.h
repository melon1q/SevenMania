#pragma once

#include <windows.h>
#include "SongEntryPresentation.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bms::ui {

// Value-only view data. The native ListView remains the selection model; the
// RmlUi painter uses these fractional visual bounds inside its clipped viewport.
struct SongListVisualRow {
    SongEntryKind kind{SongEntryKind::Song};
    int index{};
    float top{};
    int height{};
    std::wstring title, artist, level, variant, banner;
    COLORREF difficultyColor{};
    int lamp{}; // 0: unplayed, 1: cleared, 2: played
    bool selected{}, blocked{}, alternate{};
    bool operator==(const SongListVisualRow&) const = default;
};

class RmlSongListRenderer {
public:
    RmlSongListRenderer();
    ~RmlSongListRenderer();
    RmlSongListRenderer(const RmlSongListRenderer&) = delete;
    RmlSongListRenderer& operator=(const RmlSongListRenderer&) = delete;

    // Renders only visible rows. Fractional track translations update the existing
    // DOM; row markup is rebuilt only when row data or local layout changes.
    // textSeconds is time since the selected chart changed. Returns false to allow
    // the native painter to take over.
    bool paint(HDC dc, int width, int height, const std::vector<SongListVisualRow>& rows,
        double textSeconds = 0, float trackTop = 0, float guideTop = 0, int rowHeight = 58);
    bool needsAnimation() const noexcept;
    bool enabled() const noexcept;
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool failed_{};
};

} // namespace bms::ui
