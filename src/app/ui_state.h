#pragma once

#include "app/types.h"

#include <cstdint>

namespace app {

struct ModifierKeys {
  bool star_held = false;     // * key (idx 12) currently held
  bool hash_held = false;     // # key (idx 14) currently held
  bool star_used = false;     // * was consumed by a combo (suppress tap)
  bool hash_used = false;     // # was consumed by a combo (suppress tap)
  bool zero_held = false;     // 0 key (idx 13) currently held
  bool zero_used = false;     // 0 was consumed by a combo (suppress tap)
};

struct UiNavState {
  bool dirty = true;
  bool settings_dirty = false;
  uint32_t last_settings_change = 0;
  uint32_t settings_revision = 0;
  uint32_t last_ui = 0;
  int active_degree = -1;
  bool holding = false;
  ProgressionMode prog_mode = ProgressionMode::kMajor;
  KeyNavMode key_nav_mode = KeyNavMode::kSemitone;
  uint32_t toast_until = 0;
  char toast_text[16] = {};          // custom toast message (empty = use default)
  bool chord_context = true;
  uint32_t chord_flash_until = 0;
  uint32_t repeat_next_ms = 0;      // next auto-retrigger time (REPE mode)
  // Accelerating param repeat (0+8, 0+9, joystick L/R in menu mode)
  int8_t param_repeat_dir = 0;      // 0=off, 1=up, -1=down
  uint32_t param_repeat_start = 0;  // when repeat began (for acceleration)
  uint32_t param_repeat_next = 0;   // next fire time
  ModifierKeys mod;
  // Double-click detection for * key
  uint32_t star_last_tap_ms = 0;       // timestamp of last * tap
  bool star_pending_single = false;     // deferred single-click waiting for timeout
  // Long-press detection for * key
  uint32_t star_press_ms = 0;          // timestamp when * was pressed down
};

}  // namespace app
