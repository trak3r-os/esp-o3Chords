#pragma once

#include "app/contracts/contracts.h"
#include <cstddef>
#include <cstdint>
#include <cstddef>

namespace app::parameters {

struct Descriptor {
  contract::ParamId id;
  const char* label;
  const char* unit;
  int32_t min;
  int32_t max;
  int32_t step;
  bool quick_play;
  contract::ParameterFilter filter;
  bool editable = true;
  bool action = false;
  // Named-category values (e.g. Articulation, JoystickRecipe) wrap at their
  // ends on mutate() rather than clamping stuck at an endpoint, since turning
  // past the last item should reach the first. Numeric ranges (Velocity,
  // Swing, ...) leave this false.
  bool wraps = false;
};

struct ReadResult { bool applicable = false; int32_t value = 0; };

const Descriptor* all(size_t& count);
const Descriptor* find(contract::ParamId id);
int32_t clamp(const Descriptor& descriptor, int32_t value);
contract::Command set_command(const Descriptor& descriptor, int32_t value);
ReadResult read(const contract::Config& config, contract::ParamId id);
bool visible(const contract::Config& config, contract::ParamId id,
             contract::ParameterFilter filter, bool advanced = false);
bool mutate(contract::Config& config, contract::ParamId id, int32_t delta);
bool format(const contract::Config& config, contract::ParamId id,
            char* label, size_t label_len, char* value, size_t value_len);

// AxisTarget -> ParamId for tier-1 targets (already in the registry); false
// for tier-2 targets, which exist in Config but have no ParamId/menu row.
// Shared by the PARAM compass preview (input_joystick.cpp) and its renderer.
bool axis_target_param_id(app::AxisTarget target, contract::ParamId& out);
// Formats a tier-2 target's raw value directly, since it has no ParamId to
// format through.
void format_axis_tier2_value(app::AxisTarget target, int32_t value, char* buf, size_t len);

}  // namespace app::parameters
