#pragma once
#include "BmsParser.h"
#include <algorithm>
#include <cmath>

struct PracticeMode {
    bool enabled{};
    int ratePercent = 100;
    double startSeconds{}, endSeconds = 1, maximumSeconds = 1;
    double rate() const noexcept { return ratePercent / 100.0; }
    void configure(double chartDuration) {
        enabled = false; ratePercent = 100; startSeconds = 0;
        maximumSeconds = endSeconds = std::isfinite(chartDuration) ? std::max(1.0, chartDuration + 1.0) : 1.0;
    }
    void adjustRate(int direction) { ratePercent = std::clamp(ratePercent + direction * 5, 50, 150); }
    void restoreRange(double start, double end) {
        if (!std::isfinite(start) || !std::isfinite(end) || end - start < 1) return;
        endSeconds = std::clamp(end, 1.0, maximumSeconds);
        startSeconds = std::clamp(start, 0.0, endSeconds - 1);
    }
    double previewStart(bool editingEnd) const {
        return editingEnd ? std::max(startSeconds, endSeconds - 3.0) : startSeconds;
    }
    void adjustBoundary(bool end, double seconds) {
        if (end) endSeconds = std::clamp(endSeconds + seconds, startSeconds + 1.0, maximumSeconds);
        else startSeconds = std::clamp(startSeconds + seconds, 0.0, endSeconds - 1.0);
    }
    bool valid() const noexcept {
        return ratePercent >= 50 && ratePercent <= 150 && std::isfinite(startSeconds) && std::isfinite(endSeconds) &&
            startSeconds >= 0 && endSeconds - startSeconds >= 1.0 && endSeconds <= maximumSeconds;
    }
    std::vector<bms::Note> notesFor(const std::vector<bms::Note>& notes) const {
        std::vector<bms::Note> result;
        for (auto note : notes) {
            if (note.timeSeconds >= endSeconds) break;
            const double tail = note.timeSeconds + note.lengthSeconds;
            if (note.timeSeconds < startSeconds) {
                if (!note.longNote() || tail <= startSeconds) continue;
                note.timeSeconds = startSeconds;
                note.sampleId.clear(); // Do not replay a keysound that began before the selected section.
            }
            if (note.longNote()) note.lengthSeconds = std::max(0.0, std::min(tail, endSeconds) - note.timeSeconds);
            result.push_back(std::move(note));
        }
        return result;
    }
};
