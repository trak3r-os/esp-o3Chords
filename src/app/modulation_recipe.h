#pragma once

#include <cstdint>

namespace app::modulation {

enum class Curve : uint8_t { Linear, Exponential, Logarithmic };

struct CcPair {
  uint8_t x = 1;
  uint8_t y = 11;
};

// Presets are recipe data, not runtime mode identity.  preset 0 is Custom.
CcPair cc_pair_for_preset(uint8_t preset, uint8_t custom_x, uint8_t custom_y);

// Axis positions are +/-kAxisFullScale (drivers::JoystickAnalog's normalised
// range); deadzones are in the same units. Past the deadzone the remaining
// travel is rescaled to the full output range, so the output leaves the
// neutral value smoothly instead of jumping by the deadzone's width.
inline constexpr int16_t kAxisFullScale = 512;
int16_t apply_deadzone(int16_t value, int16_t deadzone);
uint8_t axis_to_cc(int16_t value, int16_t deadzone, Curve curve = Curve::Linear);
int16_t axis_to_bend(int16_t value, int16_t deadzone);
// A "Deadzone %" setting (0-100) as axis units, capped so some travel remains.
int16_t deadzone_units(uint8_t percent);

// Restore transient modulation state when a recipe changes.  The caller owns
// transport emission; this function only defines the neutral values.
struct RestoreState {
  int16_t cc_x = -1;
  int16_t cc_y = -1;
  int16_t bend = 0;
  bool neutral_required = true;
};
RestoreState restore_state();

}  // namespace app::modulation
