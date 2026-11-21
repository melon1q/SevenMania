#pragma once
#include <algorithm>

namespace sevenmania::skin::lr2 {
// The LR2 play screen before the song: the scene (timer 0) starts when the skin first appears,
// loading is shown from LOADSTART for at least LOADEND milliseconds, READY (timer 40) starts when
// both the load and that minimum are over, and the song (timer 41) starts PLAYSTART later.
struct PlayLeadInTimes { int loadStart{}, loadEnd{}, playStart{}; };
struct PlayLeadIn {
    double sceneAge{};       // Timer 0, milliseconds since the scene appeared.
    double readyAge=-1;      // Timer 40, or -1 before READY.
    bool loaded{};           // Option 81 (80 is its negation).
    float progress{};        // Bar graph 2.
    bool start{};            // The song may start now.
};
// The lane cover as drawn, 0 (open) to 1 (closed). It is the player's setting during the song and
// slides down to closed once it starts closing (see laneCoverClosingAge): half the ending, 200-800 ms,
// eased at both ends. Only the picture moves; the setting is an input and is never written.
// Milliseconds the cover has been closing: from 500 ms after the last judgement (judgedAge, -1 before
// it) or from the start of the ending (endingAge, -1 before it), whichever comes first; -1 if neither.
inline constexpr double laneCoverCloseDelay=500;
inline double laneCoverClosingAge(double judgedAge,double endingAge) noexcept {
    const double afterNotes=judgedAge<0?-1.:judgedAge-laneCoverCloseDelay;
    const double age=std::max(afterNotes,endingAge);
    return age<0?-1.:age;
}
inline float laneCoverDisplay(int settingPercent,double endingAge,int endingMilliseconds) noexcept {
    const double base=std::clamp(settingPercent,0,100)/100.;
    if(endingAge<0)return static_cast<float>(base);
    const double duration=std::clamp(endingMilliseconds*.5,200.,800.);
    const double t=std::clamp(endingAge/duration,0.,1.);
    return static_cast<float>(base+(1.-base)*t*t*(3.-2.*t));
}
// sceneAge: milliseconds since the scene appeared. loadedAt: scene milliseconds at which the host
// finished loading, or a negative value while it is still loading.
constexpr PlayLeadIn playLeadIn(const PlayLeadInTimes& times,double sceneAge,double loadedAt) noexcept {
    const double loadStart=std::max(0,times.loadStart),loadEnd=std::max(0,times.loadEnd);
    PlayLeadIn out;
    out.sceneAge=std::max(0.,sceneAge);
    const double minimum=loadStart+loadEnd;
    const bool hostLoaded=loadedAt>=0;
    const double readyAt=hostLoaded?std::max(loadedAt,minimum):-1;
    out.loaded=hostLoaded&&out.sceneAge>=readyAt;
    if(out.loaded) {
        out.progress=1.f;
        out.readyAge=out.sceneAge-readyAt;
        out.start=out.readyAge>=std::max(0,times.playStart);
    } else {
        // The bar follows the skin's minimum; it stops short of the end until the load is over.
        const double share=loadEnd>0?(out.sceneAge-loadStart)/loadEnd:(out.sceneAge>=loadStart?1.:0.);
        out.progress=static_cast<float>(std::clamp(share,0.,hostLoaded?1.:.99));
    }
    return out;
}
}
