#include "app/voice_recipe.h"

namespace app::voice {

engine::TriggerMode trigger_mode(bool held) {
  return held ? engine::TriggerMode::kHeld : engine::TriggerMode::kOneShot;
}

}  // namespace app::voice
