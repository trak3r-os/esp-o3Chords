#include "app/modulation_recipe.h"

#include <algorithm>
#include <cmath>

namespace app::modulation {

CcPair cc_pair_for_preset(uint8_t preset, uint8_t custom_x, uint8_t custom_y) {
  constexpr CcPair kPresets[] = {
      {0, 0}, {1, 11}, {74, 71}, {10, 7}, {91, 93}};
  return (preset > 0 && preset < 5) ? kPresets[preset] : CcPair{custom_x, custom_y};
}

int16_t deadzone_units(uint8_t percent) {
  return static_cast<int16_t>(std::min<int32_t>(percent, 90) * kAxisFullScale / 100);
}

int16_t apply_deadzone(int16_t value, int16_t deadzone) {
  const int32_t dz = std::clamp<int32_t>(deadzone, 0, kAxisFullScale - 1);
  const int32_t magnitude = std::min<int32_t>(std::abs(static_cast<int32_t>(value)), kAxisFullScale);
  if (magnitude <= dz) return 0;
  const int32_t scaled = (magnitude - dz) * kAxisFullScale / (kAxisFullScale - dz);
  return static_cast<int16_t>(value < 0 ? -scaled : scaled);
}

uint8_t axis_to_cc(int16_t value, int16_t deadzone, Curve curve) {
  const int16_t centered = apply_deadzone(value, deadzone);
  float normalized = (static_cast<float>(centered) + kAxisFullScale) / (2.0f * kAxisFullScale);
  normalized = std::max(0.0f, std::min(1.0f, normalized));
  if (curve == Curve::Exponential) normalized *= normalized;
  if (curve == Curve::Logarithmic) normalized = std::sqrt(normalized);
  return static_cast<uint8_t>(normalized * 127.0f);
}

int16_t axis_to_bend(int16_t value, int16_t deadzone) {
  const int32_t centered = apply_deadzone(value, deadzone);
  const int32_t bend = centered * 8192 / kAxisFullScale;
  return static_cast<int16_t>(std::clamp<int32_t>(bend, -8192, 8191));
}

RestoreState restore_state() { return {}; }

}  // namespace app::modulation
