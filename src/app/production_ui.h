#pragma once

#include "app/input_context.h"
#include "app/ui_model.h"
#include "drivers/keypad_matrix.h"
#include "drivers/joystick_analog.h"

#include <cstdint>
#include <vector>

namespace app::prod {

struct UiState {
  contract::NavigationMode navigation_mode = contract::NavigationMode::Play;
  contract::ParameterFilter parameter_filter = contract::ParameterFilter::Chord;
  contract::ParamId selected_parameter = contract::ParamId::Velocity;
  uint8_t select_cursor = 0;
  // PLAY-screen cursor: index into the ordered list of Config-backed values
  // draw_play() prints on rows 5-7 — see play_cursor_ids() in
  // production_ui.cpp. Defaults to Velocity's fixed slot in that ordering
  // (index 3), so a fresh turn adjusts velocity rather than landing on
  // Voicing just because it prints first. Sticky and independent of
  // joystick_cursor, so toggling `0` between the CHORD and JOY lanes never
  // disturbs either.
  uint8_t play_cursor = 3;
  // Joystick-screen cursor: 0 = Recipe (the encoder always starts there), 1 =
  // the active preset's one push-reachable parameter, if it has one
  // (joystick_push_param() in joy_family.cpp).
  uint8_t joystick_cursor = 0;
  bool star_down = false;
  bool hash_down = false;
  bool joystick_mode = false;
  bool star_long_sent = false;
  bool hash_long_sent = false;
  uint32_t star_pressed_ms = 0;
  uint32_t hash_pressed_ms = 0;
  bool zero_down = false;
  bool zero_used = false;
  // Storage state, mirrored by main.cpp: `storage_blocked` is shown in the
  // System header; Reset raises `storage_overwrite_requested` so main lifts
  // the block and the factory defaults can be saved.
  bool storage_blocked = false;
  bool storage_overwrite_requested = false;
  bool dfu_reboot_pending = false;
  bool reboot_pending = false;
  bool calibration_open = false;
  bool calibration_requested = false;
  drivers::CalibrationResult calibration_result = drivers::CalibrationResult::Ready;
  uint8_t calibration_progress = 0;
  uint16_t calibration_x = 2048, calibration_y = 2048, calibration_zone = 48;
  bool info_open = false;
  uint8_t info_page = 0;
  contract::ConfirmationAction selected_action = contract::ConfirmationAction::None;
  contract::ConfirmationAction confirmation_action = contract::ConfirmationAction::None;
  bool confirmation_selected = false;
};

void begin(UiState& ui, InputContext& ctx);
void encoder_turn(InputContext& ctx, UiState& ui, int8_t delta);
void encoder_click(InputContext& ctx, UiState& ui);
void encoder_long_click(InputContext& ctx, UiState& ui);
void button_back(InputContext& ctx, UiState& ui);
// The physical BAK button (main.cpp's GpioButtonId::kButton2): same as
// button_back() everywhere except PLAY, where it steps the PLAY cursor
// backward instead of the keypad `*` tap's no-op (see back_impl() in
// production_ui.cpp).
void button_bak(InputContext& ctx, UiState& ui);
void button_menu(InputContext& ctx, UiState& ui);
void button_system(InputContext& ctx, UiState& ui);
void process_keypad_events(InputContext& ctx, UiState& ui,
                           const std::vector<drivers::KeypadEvent>& events);
contract::UiModel model_snapshot(const InputContext& ctx, const UiState& ui);
void panic(InputContext& ctx);

}  // namespace app::prod
