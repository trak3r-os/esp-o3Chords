#pragma once

#include "app/input_context.h"
#include "app/types.h"
#include "drivers/joystick_analog.h"

#include <cstdint>

namespace app {

struct JoystickPreview {
  bool available = false;
  bool velocity_x = false;
  char current[16] = {};
  char x_name[8] = "Note", y_name[8] = "Note";
  // Sized to match current[16]: the CHORD case copies out.current into
  // these verbatim (joystick_preview()'s x_value/y_value assignment), and a
  // 12-byte destination can't provably hold a 16-byte source, which trips
  // -Werror=format-truncation on the esp32s3 build (native tests don't
  // enable that flag, so this only surfaced on a real firmware build).
  char x_value[16] = {}, y_value[16] = {};
  char up[12] = {}, down[12] = {}, left[12] = {}, right[12] = {};
  uint8_t velocity = 0;
  // All 8 directions, not just the 4 cardinals above (E=0,NE=1,N=2,NW=3,
  // W=4,SW=5,S=6,SE=7 - kSubLabels' convention). Every gesture clock
  // handler computes a candidate for every direction, and the compass
  // screens need all of them, diagonals included. Populated for
  // NOTES/gesture=clock, CHORD and PARAM (the value each direction would
  // produce); empty for NOTES/walk and CONT, which have no 8-way compass.
  char compass[8][12] = {};
  // NOTES/Gesture=walk's full 8-note scale strip (the note source's whole
  // notes_for_source() array, direct-indexed - unlike compass[8], walk has
  // no clock-map permutation). Populated only for NOTES/walk; empty
  // otherwise. The current position within it is ctx.js.walk_index % 8,
  // read directly by the renderer rather than duplicated here.
  char walk[8][12] = {};
};

// Read-only projection of the same note maps used by the gesture handlers.
JoystickPreview joystick_preview(const InputContext& input);

// Reads the live Settings value a PARAM axis currently targets - exported
// for the PARAM joystick screen's 4 axis rows (production_render.cpp),
// which needs each axis's own current value, not just the combined
// up_down/left_right pair joystick_preview() summarizes into `current`.
int32_t axis_get(const Settings& s, AxisTarget t);

void process_joystick(InputContext& ctx, drivers::JoystickAnalog& joystick,
                      const drivers::JoystickAnalogConfig& joy_cfg);

void service_joystick_ticks(InputContext& ctx, uint16_t clock_ticks);

// Dispatch a keypad-originated direction as if it came from the joystick.
// Used when keypad_as_joy is enabled on the Soloist screen.
void dispatch_keypad_joy_direction(InputContext& ctx, int8_t dir);

// Rebuild discover mode tension slots (call when degree/key/scale changes).
void discover_rebuild_slots(InputContext& ctx);

// Apply a new joystick play mode (stops notes, resets state, sets ctx.js.mode).
void apply_joystick_mode(InputContext& ctx, JoystickPlayMode next_mode);

// Cycle joystick mode within the context-appropriate filter set.
// Uses ctx.nav.screen to pick chord vs. lead modes.
void cycle_joystick_mode_in_filter(InputContext& ctx);

}  // namespace app
