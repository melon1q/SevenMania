#pragma once
#include "BmsParser.h"
#include <memory>
// Workers need only immutable BGA metadata, never another copy of notes/audio events.
inline std::shared_ptr<const bms::Chart> bgaChartSnapshot(const bms::Chart& source) {
    auto chart=std::make_shared<bms::Chart>();
    chart->objectBase=source.objectBase;chart->bmson=source.bmson;
    chart->bga=source.bga;chart->bgaOpacity=source.bgaOpacity;chart->bgaColors=source.bgaColors;
    chart->bitmapColors=source.bitmapColors;chart->options=source.options;
    chart->videoFps=source.videoFps;chart->poorBgaMode=source.poorBgaMode;
    chart->bitmaps=source.bitmaps;chart->bgaClips=source.bgaClips;
    return chart;
}
