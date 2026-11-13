#include "../framework.h"
#include "app/production_ui.h"
#include "app/settings_bridge.h"
#include "app/state.h"
#include "app/joystick_state.h"
#include "engine/engine.h"
#include "midi/router.h"
#include "midi/ble_midi.h"
#include "midi/usb_device_midi.h"
#include <vector>

struct SnapshotContext {
  app::AppState state; engine::Engine engine; midi::Router router;
  midi::BleMidi ble{router}; midi::UsbDeviceMidi usb{router}; app::JoystickState js;
  app::UiNavState nav; std::vector<engine::ProgressionStep> steps;
  app::InputContext input{state, engine, router, ble, usb, js, nav, steps, 0}; app::prod::UiState ui;
  SnapshotContext() { state.sanitize(); engine.begin(); engine.set_router(&router); app::apply_settings(state, engine, ble, usb); app::prod::begin(ui, input); }
};

static bool has_row(const app::contract::UiModel& m, app::contract::ParamId id, const char* value) {
  for (uint8_t i = 0; i < m.row_count; ++i) if (m.rows[i].parameter == id && std::string(m.rows[i].value) == value) return true;
  return false;
}

static void test_home_snapshot_contains_musical_and_transport_state() {
  SnapshotContext c; auto m = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_EQ(m.navigation.mode, app::contract::NavigationMode::Play);
  ASSERT_TRUE(std::string(m.config.key_name) == "C"); ASSERT_TRUE(std::string(m.config.scale_name) == "Major");
  ASSERT_TRUE(std::string(m.articulation_name) == "Chord"); ASSERT_TRUE(std::string(m.joystick_legend).find("JOY") == 0);
  ASSERT_EQ(m.config.bpm, 120.0f); ASSERT_TRUE(m.restart_required);
}

static void test_select_snapshot_has_real_values_and_actions() {
  SnapshotContext c; app::prod::button_menu(c.input, c.ui);  // Play -> Select (`#`; encoder push now steps the PLAY cursor instead).
  auto chord = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_TRUE(has_row(chord, app::contract::ParamId::Velocity, "110"));
  ASSERT_TRUE(has_row(chord, app::contract::ParamId::Bpm, "120 BPM"));
  app::prod::button_back(c.input, c.ui);    // Select -> Play.
  // `0` toggles joystick_mode; the next Select entry picks it up as the filter.
  app::prod::process_keypad_events(c.input, c.ui, {{13, true}, {13, false}});
  app::prod::button_menu(c.input, c.ui);
  auto joystick = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_TRUE(has_row(joystick, app::contract::ParamId::LeadChannel, "1"));
  app::prod::button_system(c.input, c.ui);  // Jumps straight to System, regardless of prior filter.
  auto system = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_TRUE(has_row(system, app::contract::ParamId::ChordChannel, "1"));
  bool panic = false; for (uint8_t i = 0; i < system.row_count; ++i) panic |= system.rows[i].action == app::contract::ConfirmationAction::Panic;
  ASSERT_TRUE(panic);
}

static void test_edit_feedback_tracks_selected_param() {
  SnapshotContext c; app::prod::button_menu(c.input, c.ui);  // Play -> Select.
  app::prod::encoder_click(c.input, c.ui);  // Select -> Edit.
  auto m = app::prod::model_snapshot(c.input, c.ui);
  ASSERT_TRUE(m.feedback.active); ASSERT_EQ(m.feedback.parameter, app::contract::ParamId::Velocity); ASSERT_EQ(m.feedback.value, 110);
}

int main() { TEST_SUITE("UI model snapshot"); RUN_TEST(test_home_snapshot_contains_musical_and_transport_state); RUN_TEST(test_select_snapshot_has_real_values_and_actions); RUN_TEST(test_edit_feedback_tracks_selected_param); test_print_result(); return g_fail ? 1 : 0; }
