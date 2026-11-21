#pragma once
#include "ChartRules.h"
#include "GrooveGauge.h"

namespace bms {
// The same preparation applies on first start and restart. Practice keeps its
// previous TOTAL behavior until its original-chart scaling is verified.
inline void configurePlaybackGauge(GrooveGauge& gauge,const Chart& chart,GrooveGaugeMode mode,bool practice) {
    const auto count=judgementNoteCount(chart);
    const auto total=gauge.usesLr2orajaTrial()&&!practice?lr2orajaChartTotal(chart,count):chartTotal(chart,count);
    gauge.configure(count,mode,total);
}

// Charge-note tails are separate gauge units. When their head expires without
// input, the compatibility profile also receives the tail's POOR; LN remains
// a single unit. The caller keeps score and judgement handling unchanged.
inline bool applyMissedChargeTail(GrooveGauge& gauge,const Note& note) {
    if(!gauge.compatibilityActive() || !note.longNote() || note.mode==LongNoteMode::LN)return false;
    gauge.apply(TimingJudgement::Poor);
    return true;
}
}
