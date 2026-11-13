#pragma once

#include "engine/engine.h"

#include <cstdint>

namespace app::voice {

// Shared articulation policy for all joystick note producers.
engine::TriggerMode trigger_mode(bool held);

}  // namespace app::voice
