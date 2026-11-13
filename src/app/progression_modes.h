#pragma once

#include "app/types.h"
#include "app/state.h"
#include "engine/engine.h"

namespace app {

engine::ProgressionStep gstep(uint8_t deg, const char* qual, uint8_t vel, int8_t tp = 0);
void apply_progression_mode(engine::Engine& engine, ProgressionMode mode, Settings& s);

}  // namespace app
