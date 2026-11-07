#pragma once
#include "SkinData.h"

// Compiled in the main player and its dedicated integration test only.
// Call on the game's UI owner thread, before rendering. No engine state is modified.
// Returns false without touching the provider if called off the game's startup/UI owner thread.
bool captureSevenManiaSkinData(sevenmania::skin::SkinDataProvider& provider);
