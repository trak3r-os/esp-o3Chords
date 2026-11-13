#include "../framework.h"
#include "app/production_ui.h"
#include "app/config_codec.h"
#include "app/parameters.h"
#include "app/input_joystick.h"
#include "app/progression_modes.h"
#include "app/settings_bridge.h"
#include "app/state.h"
#include "app/joystick_state.h"
#include "engine/engine.h"
#include "midi/router.h"
#include "midi/ble_midi.h"
#include "midi/usb_device_midi.h"
#include "esp_timer.h"
#include <algorithm>
#include <string>
#include <vector>

// Records note traffic in order, so a re-voiced chord can be read back.
struct KeypadJoyCapture final : midi::TransportSink {
  std::vector<uint8_t> ons, offs;
  void send_note_on(uint8_t, uint8_t note, uint8_t) override { ons.push_back(note); }
  void send_note_off(uint8_t, uint8_t note, uint8_t) override { offs.push_back(note); }
  void send_pitch_bend(uint8_t, int16_t) override {}
  void send_control_change(uint8_t, uint8_t, uint8_t) override {}
  void clear() { ons.clear(); offs.clear(); }
  std::vector<uint8_t> sorted_ons() const { auto v = ons; std::sort(v.begin(), v.end()); return v; }
};

struct KeypadJoyContext {
  app::AppState state; engine::Engine engine; midi::Router router;
  midi::BleMidi ble{router}; midi::UsbDeviceMidi usb{router}; app::JoystickState js;
  app::UiNavState nav; std::vector<engine::ProgressionStep> steps;
  app::InputContext input{state, engine, router, ble, usb, js, nav, steps, 0}; app::prod::UiState ui;
  KeypadJoyContext() { state.sanitize(); engine.begin(); engine.set_router(&router); app::apply_settings(state, engine, ble, usb); app::prod::begin(ui, input); js.last_dir = -1; }
  void event(uint8_t index, bool pressed) { app::prod::process_keypad_events(input, ui, {{index, pressed}}); }
  // A C-major rig with a capture sink and no timing feel, for the tests that
  // listen to what 8/9 and C/D do to a held chord.
  KeypadJoyCapture capture;
  bool edit(app::contract::ParamId id, int32_t value) {
    auto config = state.config();
    const auto now = app::parameters::read(config, id).value;
    if (value != now && !app::parameters::mutate(config, id, value - now)) return false;
    return app::commit_config(input, config);
  }
  void listen() {
    router.register_sink(&capture);
    app::apply_progression_mode(engine, app::ProgressionMode::kMajor, state.settings());
    edit(app::contract::ParamId::Humanize, 0);
    capture.clear();
  }
  void flush() { g_test_time_us += 50000; esp_timer_fire_pending(); }
  void tap(uint8_t index) { event(index, true); event(index, false); flush(); }
  // By value: the model is a temporary.
  std::string inversion() { return app::prod::model_snapshot(input, ui).inversion_name; }
  std::string code() { return app::prod::model_snapshot(input, ui).inversion_code; }
  void configure_keypad_joy(bool enabled) {
    // Only keypad_as_joy matters here — this suite checks the numpad-to-
    // direction mapping upstream of family dispatch, not family behavior.
    auto config = state.config();
    config.joystick.keypad_as_joy = enabled;
    state.set_config(config);
    app::config::apply_to_settings(config, state.settings());
  }
};

static void test_numpad_1_to_9_maps_and_releases() {
  KeypadJoyContext c; c.configure_keypad_joy(true);
  const uint8_t keys[] = {0, 1, 2, 4, 5, 6, 8};
  const int8_t dirs[] = {5, 6, 7, 4, -1, 0, 3};
  for (size_t i = 0; i < 7; ++i) {
    c.event(keys[i], true); ASSERT_EQ(c.js.last_dir, dirs[i]);
    c.event(keys[i], false); ASSERT_EQ(c.js.last_dir, -1);
  }
}

static void test_8_and_9_are_reserved_for_inversion() {
  KeypadJoyContext c; c.configure_keypad_joy(true);
  // Fresh state: voice leading is Smooth and the engine picks the inversion.
  ASSERT_TRUE(c.engine.modifier().voice_leads());
  ASSERT_FALSE(c.engine.modifier().manual_inversion());
  c.event(10, true);  // 9 leaves AUTO for manual root position.
  ASSERT_TRUE(c.engine.modifier().manual_inversion());
  ASSERT_FALSE(c.engine.modifier().voice_leads());
  ASSERT_EQ(c.engine.modifier().inversion_override(), 0u);
  ASSERT_EQ(c.js.last_dir, -1);
  c.event(10, true);  // 9 advances.
  ASSERT_EQ(c.engine.modifier().inversion_override(), 1u);
  c.event(9, true);   // 8 goes back.
  ASSERT_EQ(c.engine.modifier().inversion_override(), 0u);
  ASSERT_TRUE(c.engine.modifier().manual_inversion());
  ASSERT_EQ(c.js.last_dir, -1);  // never a joystick direction, even with keypad-as-joy on
}

// With voice leading on, 8/9 walk AUTO -> ROOT -> 1ST -> 2ND -> AUTO (9
// forward, 8 backward); AUTO hands the choice back to the engine. The count
// is the sounding chord's note count, 3 when no chord is active.
static void test_9_cycles_auto_root_first_second_with_voice_leading_on() {
  KeypadJoyContext c;
  ASSERT_EQ(c.engine.modifier().voice_leading(), engine::VoiceLeading::kSmooth);
  ASSERT_STREQ(c.inversion().c_str(), "AUTO");
  ASSERT_STREQ(c.code().c_str(), "A");
  const char* names[] = {"ROOT", "1ST", "2ND", "AUTO", "ROOT"};
  const char* codes[] = {"R", "1", "2", "A", "R"};
  for (size_t i = 0; i < 5; ++i) {
    c.event(10, true); c.event(10, false);
    ASSERT_STREQ(c.inversion().c_str(), names[i]);
    ASSERT_STREQ(c.code().c_str(), codes[i]);
    ASSERT_EQ(c.engine.modifier().voice_leads(), i == 3);
    ASSERT_EQ(c.engine.modifier().manual_inversion(), i != 3);
  }
  ASSERT_TRUE(c.nav.dirty);
}

static void test_8_cycles_backward_past_auto() {
  KeypadJoyContext c;
  c.event(9, true); c.event(9, false);  // from AUTO, backward wraps to the last inversion
  ASSERT_TRUE(c.engine.modifier().manual_inversion());
  ASSERT_EQ(c.engine.modifier().inversion_override(), 2u);
  ASSERT_STREQ(c.inversion().c_str(), "2ND");
  const char* names[] = {"1ST", "ROOT", "AUTO", "2ND"};
  for (const char* name : names) {
    c.event(9, true); c.event(9, false);
    ASSERT_STREQ(c.inversion().c_str(), name);
  }
}

// With voice leading Off there is no AUTO stop: ROOT -> 1ST -> 2ND -> ROOT.
static void test_8_and_9_have_no_auto_stop_with_voice_leading_off() {
  KeypadJoyContext c;
  ASSERT_TRUE(c.edit(app::contract::ParamId::VoiceLeading, 0));
  ASSERT_EQ(c.engine.modifier().voice_leading(), engine::VoiceLeading::kOff);
  ASSERT_FALSE(c.engine.modifier().voice_leads());
  ASSERT_STREQ(c.inversion().c_str(), "ROOT");
  const char* forward[] = {"1ST", "2ND", "ROOT", "1ST"};
  for (const char* name : forward) {
    c.event(10, true); c.event(10, false);
    ASSERT_STREQ(c.inversion().c_str(), name);
    ASSERT_TRUE(c.engine.modifier().manual_inversion());
  }
  const char* backward[] = {"ROOT", "2ND", "1ST"};
  for (const char* name : backward) {
    c.event(9, true); c.event(9, false);
    ASSERT_STREQ(c.inversion().c_str(), name);
  }
}

// A chord has as many inversions as it has notes: a seventh chord adds a
// 3RD stop before the cycle wraps.
static void test_inversion_cycle_wraps_at_the_sounding_chords_note_count() {
  KeypadJoyContext c; c.listen();
  ASSERT_TRUE(c.edit(app::contract::ParamId::VoiceLeading, 0));
  ASSERT_TRUE(c.edit(app::contract::ParamId::Scale, static_cast<int32_t>(app::ProgressionMode::kSevenths)));
  c.event(0, true); c.flush();  // hold I: a four-note chord
  ASSERT_EQ(c.capture.ons.size(), 4u);
  const char* names[] = {"1ST", "2ND", "3RD", "ROOT"};
  for (const char* name : names) {
    c.event(10, true); c.event(10, false);
    ASSERT_STREQ(c.inversion().c_str(), name);
  }
}

// Under voice leading the register readout shows what the engine chose for
// the sounding chord: "A" plus R/1/2.
static void test_inversion_code_reports_the_voice_led_inversion() {
  KeypadJoyContext c; c.listen();
  c.event(0, true); c.flush();  // the first chord is always root position
  ASSERT_STREQ(c.inversion().c_str(), "AUTO");
  ASSERT_STREQ(c.code().c_str(), "AR");
  ASSERT_EQ(c.engine.sounding_inversion(), 0u);
  c.event(0, false); c.flush();
  c.event(4, true); c.flush();  // IV after I: the nearest placement is C F A
  ASSERT_STREQ(c.inversion().c_str(), "AUTO");
  const auto notes = c.engine.sounding_inversion();
  char expected[4];
  std::snprintf(expected, sizeof(expected), "A%c", "R12"[notes]);
  ASSERT_STREQ(c.code().c_str(), expected);
  ASSERT_STREQ(c.code().c_str(), "A2");
}

// 8/9 act on the chord being held, not only on the next one: in Hold trigger
// mode a sustained chord is re-voiced on the spot.
static void test_9_revoices_a_held_chord_immediately() {
  KeypadJoyContext c; c.listen();
  c.event(0, true); c.flush();  // hold I
  const auto root_position = c.capture.sorted_ons();
  ASSERT_EQ(root_position.size(), 3u);
  ASSERT_EQ(root_position[0] % 12, 0);  // C in the bass

  c.capture.clear();
  c.event(10, true); c.event(10, false); c.flush();  // AUTO -> ROOT: re-triggered as written
  ASSERT_EQ(c.capture.offs.size(), 3u);
  ASSERT_TRUE(c.capture.sorted_ons() == root_position);

  c.capture.clear();
  c.event(10, true); c.event(10, false); c.flush();  // ROOT -> 1ST
  auto first = c.capture.sorted_ons();
  ASSERT_EQ(first.size(), 3u);
  ASSERT_EQ(c.capture.offs.size(), 3u);
  ASSERT_EQ(first[0] % 12, 4);  // E in the bass
  ASSERT_TRUE(c.engine.held_active());
  ASSERT_STREQ(c.code().c_str(), "1");

  c.capture.clear();
  c.event(9, true); c.event(9, false); c.flush();  // 8: back to ROOT
  ASSERT_TRUE(c.capture.sorted_ons() == root_position);

  // Releasing the key stops exactly what is sounding now.
  c.capture.clear();
  c.event(0, false); c.flush();
  auto released = c.capture.offs; std::sort(released.begin(), released.end());
  ASSERT_TRUE(released == root_position);
  ASSERT_TRUE(c.capture.ons.empty());
  ASSERT_FALSE(c.engine.held_active());
}

// C (index 11) / D (index 15) change octave, and a held chord follows.
static void test_octave_keys_revoice_a_held_chord_immediately() {
  KeypadJoyContext c; c.listen();
  c.event(0, true); c.flush();
  const auto base = c.capture.sorted_ons();
  ASSERT_EQ(base.size(), 3u);

  c.capture.clear();
  c.event(11, true); c.event(11, false); c.flush();  // C: octave up
  ASSERT_EQ(c.engine.modifier().octave_shift(), 1);
  auto up = c.capture.sorted_ons();
  ASSERT_EQ(up.size(), 3u);
  ASSERT_EQ(c.capture.offs.size(), 3u);
  for (size_t i = 0; i < 3; ++i) ASSERT_EQ(up[i], base[i] + 12);

  c.capture.clear();
  c.event(15, true); c.event(15, false); c.flush();  // D: back down
  c.event(15, true); c.event(15, false); c.flush();  // D: one below
  ASSERT_EQ(c.engine.modifier().octave_shift(), -1);
  ASSERT_EQ(c.capture.ons.size(), 6u);
  std::vector<uint8_t> last(c.capture.ons.end() - 3, c.capture.ons.end());
  std::sort(last.begin(), last.end());
  for (size_t i = 0; i < 3; ++i) ASSERT_EQ(last[i], base[i] - 12);
  ASSERT_STREQ(app::prod::model_snapshot(c.input, c.ui).octave_name, "DOWN 1");
}

// Nothing is re-triggered when no chord is held, or when the trigger mode
// does not sustain: the change simply applies to the next chord.
static void test_inversion_and_octave_do_not_sound_without_a_held_chord() {
  KeypadJoyContext c; c.listen();
  c.tap(10); c.tap(11); c.tap(9); c.tap(15);
  ASSERT_TRUE(c.capture.ons.empty());

  c.event(0, true); c.flush(); c.event(0, false); c.flush();  // played and released
  c.capture.clear();
  c.tap(10); c.tap(11);
  ASSERT_TRUE(c.capture.ons.empty());

  KeypadJoyContext shot; shot.listen();
  ASSERT_TRUE(shot.edit(app::contract::ParamId::PlayTrigger, 0));  // Shot
  shot.event(0, true); shot.flush();
  shot.capture.clear();
  shot.event(10, true); shot.event(10, false); shot.flush();
  shot.event(11, true); shot.event(11, false); shot.flush();
  ASSERT_TRUE(shot.capture.ons.empty());
  ASSERT_EQ(shot.engine.modifier().octave_shift(), 1);  // still armed for the next chord
  ASSERT_TRUE(shot.engine.modifier().manual_inversion());
}

static void test_panic_and_navigation_have_priority() {
  KeypadJoyContext c; c.configure_keypad_joy(true); c.js.last_dir = -1;
  c.event(0, true); ASSERT_EQ(c.js.last_dir, 5);
  c.event(12, true); ASSERT_EQ(c.js.last_dir, 5); // * is panic, never joystick SW
  c.event(12, false);
  c.ui.navigation_mode = app::contract::NavigationMode::Select;
  c.event(0, true); ASSERT_EQ(c.js.last_dir, 5); // navigation consumes digit, no new direction
  c.event(0, false); ASSERT_EQ(c.js.last_dir, -1); // release still neutralizes
}

static void test_zero_toggles_joystick_mode_hash_opens_and_closes_menu() {
  // `0` (keypad_zero) only ever toggles joystick_mode, and only from Play; it
  // never opens a menu itself. `#` tap (button_menu) opens Select using
  // whichever filter joystick_mode currently selects, and — outside Play —
  // backs all the way out to Play. `#` long-hold (button_system, exercised
  // directly here) is the only way into System; `0` can never reach it.
  KeypadJoyContext c;
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Play);
  ASSERT_FALSE(c.ui.joystick_mode);

  c.event(13, true); c.event(13, false);  // 0: toggle joystick_mode; stays in Play.
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Play);
  ASSERT_TRUE(c.ui.joystick_mode);

  c.event(14, true); c.event(14, false);  // # tap: opens Select with the current mode's filter.
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Select);
  ASSERT_EQ(c.ui.parameter_filter, app::contract::ParameterFilter::Joystick);

  c.event(13, true); c.event(13, false);  // 0 outside Play is a no-op.
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Select);
  ASSERT_EQ(c.ui.parameter_filter, app::contract::ParameterFilter::Joystick);

  c.event(14, true); c.event(14, false);  // # tap again, outside Play: back to Play.
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Play);

  c.event(13, true); c.event(13, false);  // 0 again: toggle joystick_mode back off.
  ASSERT_FALSE(c.ui.joystick_mode);
  c.event(14, true); c.event(14, false);  // # tap: opens Select + Chord.
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Select);
  ASSERT_EQ(c.ui.parameter_filter, app::contract::ParameterFilter::Chord);

  app::prod::button_system(c.input, c.ui);  // # long-hold: straight to System.
  ASSERT_EQ(c.ui.parameter_filter, app::contract::ParameterFilter::System);
  c.event(13, true); c.event(13, false);  // 0 is still a no-op outside Play.
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Select);
  ASSERT_EQ(c.ui.parameter_filter, app::contract::ParameterFilter::System);
}

static void test_keypad_mode_returns_when_disabled() {
  KeypadJoyContext c; c.configure_keypad_joy(false); c.js.last_dir = -1;
  c.event(0, true);
  ASSERT_EQ(c.js.last_dir, -1);
  ASSERT_EQ(c.input.nav.active_degree, 1);
}

int main() { TEST_SUITE("Keypad as joystick"); RUN_TEST(test_numpad_1_to_9_maps_and_releases); RUN_TEST(test_8_and_9_are_reserved_for_inversion); RUN_TEST(test_9_cycles_auto_root_first_second_with_voice_leading_on); RUN_TEST(test_8_cycles_backward_past_auto); RUN_TEST(test_8_and_9_have_no_auto_stop_with_voice_leading_off); RUN_TEST(test_inversion_cycle_wraps_at_the_sounding_chords_note_count); RUN_TEST(test_inversion_code_reports_the_voice_led_inversion); RUN_TEST(test_9_revoices_a_held_chord_immediately); RUN_TEST(test_octave_keys_revoice_a_held_chord_immediately); RUN_TEST(test_inversion_and_octave_do_not_sound_without_a_held_chord); RUN_TEST(test_panic_and_navigation_have_priority); RUN_TEST(test_zero_toggles_joystick_mode_hash_opens_and_closes_menu); RUN_TEST(test_keypad_mode_returns_when_disabled); test_print_result(); return g_fail ? 1 : 0; }
