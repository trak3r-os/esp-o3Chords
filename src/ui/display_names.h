#pragma once

#include "app/joystick_state.h"
#include "app/types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ui {

int key_nav_step(app::KeyNavMode mode);
const char* key_nav_mode_name(app::KeyNavMode mode);

void joystick_mode_label(app::JoystickPlayMode m, uint8_t map_idx, char* buf, size_t len);
const char* arp_rate_name(uint8_t v);
const char* arp_pattern_name(uint8_t p);

void joystick_note_output_label(const std::array<uint8_t, 8>& active_notes,
                                uint8_t active_count, uint8_t last_note,
                                char* out, size_t out_len);

const char* joy_mode_name(app::JoystickPlayMode m);
const char* joy_mode_config_label(app::JoystickPlayMode m,
                                   const app::JoystickState& js);

const char* interval_sub_name(uint8_t sub);
const char* penta_sub_name(uint8_t sub);
const char* solo_scale_lock_name(uint8_t v);
const char* vel_curve_name(uint8_t v);
const char* voicing_name(uint8_t v);

}  // namespace ui
