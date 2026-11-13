#include "../framework.h"
#include "app/production_ui.h"
#include "app/config_codec.h"
#include "app/progression_modes.h"
#include "app/settings_bridge.h"
#include "app/state.h"
#include "app/joystick_state.h"
#include "engine/engine.h"
#include "midi/router.h"
#include "midi/ble_midi.h"
#include "midi/usb_device_midi.h"
#include "esp_timer.h"
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

struct NavContext {
  app::AppState state; engine::Engine engine; midi::Router router;
  midi::BleMidi ble{router}; midi::UsbDeviceMidi usb{router}; app::JoystickState js;
  app::UiNavState nav; std::vector<engine::ProgressionStep> steps;
  app::InputContext input{state, engine, router, ble, usb, js, nav, steps, 0};
  app::prod::UiState ui;
  NavContext() { state.sanitize(); engine.begin(); engine.set_router(&router);
    app::apply_settings(state, engine, ble, usb); app::prod::begin(ui, input); }
  app::contract::ParamId current() { return app::prod::model_snapshot(input, ui).navigation.parameter; }
  // CON-taps the PLAY cursor round to `id`.
  bool focus(app::contract::ParamId id) {
    for (int i = 0; i < 12 && current() != id; ++i) app::prod::encoder_click(input, ui);
    return current() == id;
  }
  // Edits one parameter through the encoder's own path, from any screen.
  void edit(app::contract::ParamId id, int8_t delta) {
    const auto mode = ui.navigation_mode; const auto selected = ui.selected_parameter;
    ui.navigation_mode = app::contract::NavigationMode::Edit; ui.selected_parameter = id;
    app::prod::encoder_turn(input, ui, delta);
    ui.navigation_mode = mode; ui.selected_parameter = selected;
  }
  void key(uint8_t index, bool down) { app::prod::process_keypad_events(input, ui, {{index, down}}); }
  void flush() { g_test_time_us += 50000; esp_timer_fire_pending(); }
};

// Captures note-ons with their velocity, plus the panic controllers.
struct CaptureOns final : midi::TransportSink {
  struct On { uint8_t channel, note, velocity; };
  std::vector<On> ons;
  int offs = 0, all_notes_off = 0;
  void send_note_on(uint8_t ch, uint8_t note, uint8_t velocity) override { ons.push_back({ch, note, velocity}); }
  void send_note_off(uint8_t, uint8_t, uint8_t) override { ++offs; }
  void send_pitch_bend(uint8_t, int16_t) override {}
  void send_control_change(uint8_t, uint8_t cc, uint8_t) override { if (cc == 123) ++all_notes_off; }
};

// Captures every note-off, channel included - the channel-flush regression below
// only needs to know which channel got released.
struct CaptureOffs final : midi::TransportSink {
  std::vector<std::pair<uint8_t, uint8_t>> offs;  // (channel, note)
  void send_note_on(uint8_t, uint8_t, uint8_t) override {}
  void send_note_off(uint8_t ch, uint8_t note, uint8_t) override { offs.emplace_back(ch, note); }
  void send_pitch_bend(uint8_t, int16_t) override {}
  void send_control_change(uint8_t, uint8_t, uint8_t) override {}
};

static void test_play_quick_adjusts_velocity() {
  NavContext c; const auto before = c.state.settings().velocity;
  app::prod::encoder_turn(c.input, c.ui, 1);
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Play);
  ASSERT_EQ(c.state.settings().velocity, before + 1);
}

static void test_select_edit_live_back() {
  NavContext c; app::prod::button_menu(c.input, c.ui);  // Play -> Select (`#`; encoder push now steps the PLAY cursor instead).
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Select);
  app::prod::encoder_click(c.input, c.ui);
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Edit);
  const auto before = c.state.settings().velocity;
  app::prod::encoder_turn(c.input, c.ui, 1);
  ASSERT_EQ(c.state.settings().velocity, before + 1);
  app::prod::button_back(c.input, c.ui);
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Select);
  app::prod::button_back(c.input, c.ui);
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Play);
}

static void test_filter_selection_and_menu_returns_to_play() {
  NavContext c;
  app::prod::button_menu(c.input, c.ui);  // Play -> Select (`#`); joystick_mode starts off -> Chord.
  ASSERT_EQ(c.ui.parameter_filter, app::contract::ParameterFilter::Chord);
  app::prod::button_back(c.input, c.ui);    // Select -> Play.
  // `0` toggles joystick_mode; the next Select entry picks it up as the filter.
  app::prod::process_keypad_events(c.input, c.ui, {{13, true}, {13, false}});
  app::prod::button_menu(c.input, c.ui);
  ASSERT_EQ(c.ui.parameter_filter, app::contract::ParameterFilter::Joystick);
  app::prod::button_system(c.input, c.ui);  // Jumps straight to System, regardless of prior filter.
  ASSERT_EQ(c.ui.parameter_filter, app::contract::ParameterFilter::System);
  // Outside Play, `#` (button_menu) backs all the way out to Play.
  app::prod::button_menu(c.input, c.ui);
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Play);
}

static void test_edit_does_not_rollback() {
  NavContext c;
  app::prod::button_menu(c.input, c.ui);   // Play -> Select (`#`).
  app::prod::encoder_click(c.input, c.ui); // Select -> Edit.
  app::prod::encoder_turn(c.input, c.ui, 2); const auto value = c.state.settings().velocity;
  app::prod::button_back(c.input, c.ui); app::prod::button_back(c.input, c.ui);
  ASSERT_EQ(c.state.settings().velocity, value);
}

// PLAY's quick-adjust does not track whatever SELECT/EDIT last touched; it
// follows an explicit, independent PLAY cursor (UiState::play_cursor). This
// is the regression for that decoupling: editing BPM and exploring Key through
// the `#` menu must not change what PLAY's own encoder turn controls
// afterward - it stays on whatever play_cursor points to (Velocity, its
// default slot), same as if the menu had never been opened.
static void test_play_quick_adjust_is_independent_of_select_exploration() {
  NavContext c;
  // Chord-filter order places BPM after Humanize. Enter it through `#`/SELECT/EDIT.
  app::prod::button_menu(c.input, c.ui);
  app::prod::encoder_turn(c.input, c.ui, 12);
  ASSERT_EQ(c.ui.selected_parameter, app::contract::ParamId::Bpm);
  app::prod::encoder_click(c.input, c.ui);
  app::prod::encoder_turn(c.input, c.ui, 1);
  ASSERT_EQ(c.state.config().bpm, 121.0f);
  app::prod::button_back(c.input, c.ui);
  // Explore Key in SELECT, but do not edit it.
  app::prod::encoder_turn(c.input, c.ui, -10);
  ASSERT_EQ(c.ui.selected_parameter, app::contract::ParamId::Key);
  app::prod::button_back(c.input, c.ui);
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Play);
  const auto velocity_before = c.state.settings().velocity;
  app::prod::encoder_turn(c.input, c.ui, 1);
  // BPM and Key are untouched by the PLAY turn; velocity (play_cursor's
  // default slot) is what actually moved.
  ASSERT_EQ(c.state.config().bpm, 121.0f);
  ASSERT_EQ(c.state.config().key, 0u);
  ASSERT_EQ(c.state.settings().velocity, velocity_before + 1);
}

// -- PLAY/joystick cursor ----------------------------------------------------

// CON tap (encoder_click) steps the PLAY cursor forward through the ordered
// list play_cursor_ids() builds - Voicing, VoiceLeading, Roll, Velocity
// (the default slot), PlayTrigger, Duration, ChordChannel, Articulation,
// Scale, Key - and wraps from the last token back to the first, never
// leaving PLAY.
static void test_play_cursor_con_tap_steps_forward_and_wraps() {
  NavContext c;
  using P = app::contract::ParamId;
  ASSERT_EQ(c.ui.play_cursor, 3);
  ASSERT_EQ(c.current(), P::Velocity);
  const P forward[] = {P::PlayTrigger, P::Duration, P::ChordChannel, P::Articulation, P::Scale, P::Key,
                       P::Voicing, P::VoiceLeading, P::Roll, P::Velocity};
  for (const P expected : forward) {
    app::prod::encoder_click(c.input, c.ui);
    ASSERT_EQ(c.current(), expected);
    ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Play);
  }
  // CON hold and the rotary's long push step the same way.
  app::prod::encoder_long_click(c.input, c.ui);
  ASSERT_EQ(c.current(), P::PlayTrigger);
}

// BAK (the physical button, button_bak()) steps the PLAY cursor backward and
// wraps the other way (first -> last). The keypad `*` tap shares
// button_back()'s Edit/Select unwind but must stay a true no-op in PLAY -
// nothing new is assigned to it.
static void test_bak_steps_backward_and_star_stays_a_noop() {
  NavContext c;
  auto current = [&] { return c.current(); };
  app::prod::button_bak(c.input, c.ui);  // Velocity -> Roll
  ASSERT_EQ(current(), app::contract::ParamId::Roll);
  app::prod::button_bak(c.input, c.ui);  // -> VoiceLeading
  ASSERT_EQ(current(), app::contract::ParamId::VoiceLeading);
  app::prod::button_bak(c.input, c.ui);  // -> Voicing (first token)
  ASSERT_EQ(current(), app::contract::ParamId::Voicing);
  app::prod::button_bak(c.input, c.ui);  // wraps: first -> last
  ASSERT_EQ(current(), app::contract::ParamId::Key);
  app::prod::button_bak(c.input, c.ui);
  ASSERT_EQ(current(), app::contract::ParamId::Scale);
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Play);

  const auto before = current();
  app::prod::process_keypad_events(c.input, c.ui, {{12, true}, {12, false}});  // `*` short tap.
  ASSERT_EQ(current(), before);
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Play);
}

// Rotating edits whatever token the cursor is actually focused on, not just
// the default (Velocity) - the general case behind test_play_quick_adjusts_velocity.
static void test_play_cursor_rotate_edits_the_focused_token() {
  NavContext c;
  ASSERT_TRUE(c.focus(app::contract::ParamId::ChordChannel));
  const auto before = c.state.config().chord_channel;
  const auto velocity = c.state.config().velocity;
  app::prod::encoder_turn(c.input, c.ui, 1);
  ASSERT_EQ(c.state.config().chord_channel, static_cast<uint8_t>(before + 1));
  ASSERT_EQ(c.state.settings().midi_channel, static_cast<uint8_t>(before + 1));
  ASSERT_EQ(c.state.config().velocity, velocity);

  ASSERT_TRUE(c.focus(app::contract::ParamId::Duration));
  app::prod::encoder_turn(c.input, c.ui, -2);  // 240 ms, in 10 ms steps
  ASSERT_EQ(c.state.config().duration_ms, 220);
  ASSERT_EQ(c.state.settings().note_length_ms, 220);

  ASSERT_TRUE(c.focus(app::contract::ParamId::Key));
  app::prod::encoder_turn(c.input, c.ui, 2);
  ASSERT_EQ(c.state.config().key, 2u);
  ASSERT_STREQ(c.state.config().key_name, "D");
  ASSERT_STREQ(c.state.settings().key, "D");
}

// Joystick screens start on Recipe; CON tap reaches the active preset's one
// push-reachable parameter (if it has one) and a second tap returns to
// Recipe - "push again to come back." A preset with no one-param (CONT's
// Macro) leaves the cursor pinned on Recipe.
static void test_joystick_cursor_toggles_recipe_and_one_param() {
  NavContext c;
  c.ui.joystick_mode = true;
  auto current = [&] { return app::prod::model_snapshot(c.input, c.ui).navigation.parameter; };
  auto set_preset = [&](int8_t delta) {
    c.ui.navigation_mode = app::contract::NavigationMode::Edit;
    c.ui.selected_parameter = app::contract::ParamId::JoystickRecipe;
    app::prod::encoder_turn(c.input, c.ui, delta);
    c.ui.navigation_mode = app::contract::NavigationMode::Play;
  };

  set_preset(1);  // Lead(0) -> Bass(1): NOTES, one-param = NotesSource.
  ASSERT_EQ(c.state.config().active_preset, app::PresetId::kBass);
  ASSERT_EQ(current(), app::contract::ParamId::JoystickRecipe);
  app::prod::encoder_click(c.input, c.ui);
  ASSERT_EQ(current(), app::contract::ParamId::NotesSource);
  app::prod::encoder_click(c.input, c.ui);
  ASSERT_EQ(current(), app::contract::ParamId::JoystickRecipe);

  // The one-param slot does not survive a preset change: back on Recipe.
  app::prod::encoder_click(c.input, c.ui);
  ASSERT_EQ(current(), app::contract::ParamId::NotesSource);
  set_preset(2);  // Bass(1) -> Colors(3): CHORD, one-param = ChordPalette.
  ASSERT_EQ(c.state.config().active_preset, app::PresetId::kColors);
  ASSERT_EQ(current(), app::contract::ParamId::JoystickRecipe);
  app::prod::encoder_click(c.input, c.ui);
  ASSERT_EQ(current(), app::contract::ParamId::ChordPalette);
  app::prod::encoder_turn(c.input, c.ui, 1);  // rotating edits it live
  ASSERT_EQ(c.state.config().joy_families.chord.palette, 1);
  app::prod::button_bak(c.input, c.ui);       // BAK steps back to Recipe
  ASSERT_EQ(current(), app::contract::ParamId::JoystickRecipe);

  set_preset(2);  // Colors(3) -> Voicings(5): CHORD, but nothing of its own to push.
  ASSERT_EQ(c.state.config().active_preset, app::PresetId::kVoicings);
  app::prod::encoder_click(c.input, c.ui);
  ASSERT_EQ(current(), app::contract::ParamId::JoystickRecipe);

  set_preset(2);  // Voicings(5) -> Macro(7): CONT, no one-param.
  ASSERT_EQ(c.state.config().active_preset, app::PresetId::kMacro);
  app::prod::encoder_click(c.input, c.ui);
  ASSERT_EQ(current(), app::contract::ParamId::JoystickRecipe);

  // Rotating on Recipe wraps Macro -> Lead: eight presets, no Flip.
  app::prod::encoder_turn(c.input, c.ui, 1);
  ASSERT_EQ(c.state.config().active_preset, app::PresetId::kLead);
  app::prod::encoder_turn(c.input, c.ui, -1);
  ASSERT_EQ(c.state.config().active_preset, app::PresetId::kMacro);
}

// `0` (the CHORD<->JOY lane swap) never disturbs either cursor - each
// screen's cursor is independent and sticky.
static void test_zero_toggle_does_not_disturb_either_cursor() {
  NavContext c;
  auto current = [&] { return app::prod::model_snapshot(c.input, c.ui).navigation.parameter; };
  ASSERT_TRUE(c.focus(app::contract::ParamId::ChordChannel));  // PLAY cursor: off its default slot
  ASSERT_EQ(current(), app::contract::ParamId::ChordChannel);

  app::prod::process_keypad_events(c.input, c.ui, {{13, true}, {13, false}});  // `0`: CHORD -> JOY
  ASSERT_TRUE(c.ui.joystick_mode);
  ASSERT_EQ(current(), app::contract::ParamId::JoystickRecipe);  // joystick_cursor untouched (still 0)

  app::prod::process_keypad_events(c.input, c.ui, {{13, true}, {13, false}});  // JOY -> CHORD
  ASSERT_FALSE(c.ui.joystick_mode);
  ASSERT_EQ(current(), app::contract::ParamId::ChordChannel);  // play_cursor survived the round trip
}

// Ch: stays live-editable on the play screen, and changing it must release
// whatever is sounding on the old channel before the new channel takes over
// (otherwise those notes hang). Drives the mutate() -> apply_settings() ->
// engine.set_channel() path (MidiScheduler::set_channel() flushes on a real
// channel change) through the PLAY cursor, proving that entry point
// reaches it too.
static void test_play_cursor_channel_edit_flushes_old_channel() {
  NavContext c;
  CaptureOffs capture;
  c.router.register_sink(&capture);
  // trigger_step() plays the chord channel/source; play_notes() is the
  // NOTES-family lead path (kLeadSource, lead_channel_) and would not
  // exercise this hazard at all.
  engine::ProgressionStep step{};
  step.scale_degree = 1;
  step.chord_quality = "maj";
  step.velocity = 100;
  c.steps.push_back(step);
  c.engine.progression().set_steps(c.steps);

  const uint8_t old_channel = c.state.config().chord_channel;
  c.engine.trigger_step(0, engine::TriggerMode::kHeld);
  g_test_time_us += 50000;
  esp_timer_fire_pending();
  ASSERT_TRUE(capture.offs.empty());  // still sounding - nothing released yet

  ASSERT_TRUE(c.focus(app::contract::ParamId::ChordChannel));
  app::prod::encoder_turn(c.input, c.ui, 1);
  ASSERT_NE(c.state.config().chord_channel, old_channel);
  bool saw_old_channel_off = false;
  for (const auto& off : capture.offs) if (off.first == old_channel) saw_old_channel_off = true;
  ASSERT_TRUE(saw_old_channel_off);
}

// `#` (index 14): a tap opens the menu for the current lane, a hold opens
// System - while it is still held, not on release.
static void test_hash_tap_opens_menu_and_long_hash_opens_system() {
  NavContext c;
  c.key(14, true);
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Play);  // nothing on the press edge
  c.input.t += 100; c.key(14, false);
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Select);
  ASSERT_EQ(c.ui.parameter_filter, app::contract::ParameterFilter::Chord);
  c.key(14, true); c.input.t += 100; c.key(14, false);  // tap again: back to PLAY
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Play);

  c.key(14, true);
  c.input.t += 649; app::prod::process_keypad_events(c.input, c.ui, {});
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Play);
  c.input.t += 1; app::prod::process_keypad_events(c.input, c.ui, {});  // 650 ms: fires while held
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Select);
  ASSERT_EQ(c.ui.parameter_filter, app::contract::ParameterFilter::System);
  c.input.t += 200; c.key(14, false);  // the release does not then toggle the menu shut
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Select);
  ASSERT_EQ(c.ui.parameter_filter, app::contract::ParameterFilter::System);
}

// `*` (index 12): a hold is panic - everything off, back to PLAY.
static void test_star_long_press_is_panic() {
  NavContext c;
  CaptureOns capture;
  c.router.register_sink(&capture);
  app::apply_progression_mode(c.engine, app::ProgressionMode::kMajor, c.state.settings());
  c.key(0, true); c.flush();
  ASSERT_TRUE(c.engine.held_active());
  ASSERT_EQ(c.nav.active_degree, 1);
  app::prod::button_system(c.input, c.ui);  // panic also leaves any menu

  c.key(12, true);
  c.input.t += 649; app::prod::process_keypad_events(c.input, c.ui, {});
  ASSERT_TRUE(c.engine.held_active());
  ASSERT_EQ(capture.all_notes_off, 0);
  c.input.t += 1; app::prod::process_keypad_events(c.input, c.ui, {});
  ASSERT_FALSE(c.engine.held_active());
  ASSERT_FALSE(c.nav.holding);
  ASSERT_EQ(c.nav.active_degree, -1);
  ASSERT_GE(capture.all_notes_off, 1);
  ASSERT_GE(capture.offs, 3);
  ASSERT_EQ(c.ui.navigation_mode, app::contract::NavigationMode::Play);
  const int sent = capture.all_notes_off;
  c.input.t += 100; c.key(12, false);  // the release is not a second panic
  ASSERT_EQ(capture.all_notes_off, sent);
}

// Panic also forgets the previous chord, so voice leading starts afresh: the
// next chord is in root position instead of being pulled toward a chord that
// was silenced.
static void test_panic_restarts_voice_leading() {
  NavContext c;
  app::apply_progression_mode(c.engine, app::ProgressionMode::kMajor, c.state.settings());
  ASSERT_TRUE(c.engine.modifier().voice_leads());
  c.key(0, true); c.flush(); c.key(0, false); c.flush();  // I: root position
  ASSERT_EQ(c.engine.sounding_inversion(), 0u);
  c.key(4, true); c.flush(); c.key(4, false); c.flush();  // IV, led from I: C F A
  ASSERT_EQ(c.engine.sounding_inversion(), 2u);

  app::prod::panic(c.input);
  c.key(4, true); c.flush();                              // IV again, nothing to lead from
  ASSERT_EQ(c.engine.sounding_inversion(), 0u);
  ASSERT_STREQ(app::prod::model_snapshot(c.input, c.ui).inversion_code, "AR");
}

// Chord velocity lives on each progression step; an edited Velocity must be
// what the very next chord key sends, without waiting for the progression
// to be rebuilt.
static void test_velocity_edit_reaches_the_next_chords_note_on() {
  NavContext c;
  CaptureOns capture;
  c.router.register_sink(&capture);
  app::apply_progression_mode(c.engine, app::ProgressionMode::kMajor, c.state.settings());
  c.edit(app::contract::ParamId::Humanize, -40);
  ASSERT_EQ(c.state.config().velocity, 110);
  c.key(0, true); c.flush(); c.key(0, false); c.flush();
  ASSERT_EQ(capture.ons.size(), 3u);
  for (const auto& on : capture.ons) ASSERT_EQ(on.velocity, 110);

  capture.ons.clear();
  app::prod::encoder_turn(c.input, c.ui, -37);  // PLAY cursor rests on Velocity
  ASSERT_EQ(c.state.config().velocity, 73);
  ASSERT_EQ(c.state.settings().velocity, 73);
  for (const auto& step : c.engine.progression().steps()) ASSERT_EQ(step.velocity, 73);
  c.key(1, true); c.flush(); c.key(1, false); c.flush();
  ASSERT_EQ(capture.ons.size(), 3u);
  for (const auto& on : capture.ons) ASSERT_EQ(on.velocity, 73);

  // And it survives a progression rebuild in either order.
  capture.ons.clear();
  c.edit(app::contract::ParamId::Scale, 1);
  c.edit(app::contract::ParamId::Velocity, 7);
  c.key(2, true); c.flush(); c.key(2, false); c.flush();
  ASSERT_FALSE(capture.ons.empty());
  for (const auto& on : capture.ons) ASSERT_EQ(on.velocity, 80);
}

static void confirm_reset(NavContext& c) {
  app::prod::button_system(c.input, c.ui);
  app::prod::encoder_turn(c.input, c.ui, 100);
  while (c.ui.selected_action != app::contract::ConfirmationAction::Reset && c.ui.select_cursor > 0)
    app::prod::encoder_turn(c.input, c.ui, -1);
  app::prod::encoder_click(c.input, c.ui);    // arm the confirmation
  app::prod::encoder_turn(c.input, c.ui, 1);  // select Confirm
  app::prod::encoder_click(c.input, c.ui);    // execute
}

// System > Reset restores factory_defaults() (Settings' defaults projected
// into Config, Lead recalled) through commit_config(), so everything
// downstream follows: Settings, the engine, the progression and the chord
// modifier. A bare Config{} would have left USB off and velocity at 100.
static void test_reset_restores_factory_defaults() {
  NavContext c;
  app::apply_progression_mode(c.engine, app::ProgressionMode::kMajor, c.state.settings());
  c.edit(app::contract::ParamId::Scale, 11);        // Jazz: a different progression
  c.edit(app::contract::ParamId::Key, 5);
  c.edit(app::contract::ParamId::Velocity, -60);
  c.edit(app::contract::ParamId::Duration, 30);
  c.edit(app::contract::ParamId::UsbRole, -1);      // Device -> Off
  c.edit(app::contract::ParamId::ClockSource, 1);   // Auto -> External
  c.edit(app::contract::ParamId::Voicing, 3);
  c.edit(app::contract::ParamId::JoystickRecipe, 3);  // Colors
  c.edit(app::contract::ParamId::ChordPalette, 4);
  c.engine.modifier().adjust_octave(2);
  c.engine.modifier().set_manual_inversion(2);
  c.engine.modifier().set_quality("dom7");
  ASSERT_STREQ(c.state.config().scale_name, "Jazz");
  ASSERT_STREQ(c.engine.progression().steps()[0].chord_quality.c_str(), "min7");
  ASSERT_EQ(c.state.config().usb_role, app::contract::UsbRole::Off);
  app::prod::encoder_click(c.input, c.ui);  // PLAY cursor off its default slot
  c.ui.joystick_cursor = 1;

  confirm_reset(c);

  const auto want = app::config::factory_defaults();
  const auto& got = c.state.config();
  ASSERT_EQ(got.velocity, 110); ASSERT_EQ(got.duration_ms, 240);
  ASSERT_EQ(got.usb_role, app::contract::UsbRole::Device);
  ASSERT_EQ(got.clock_source, app::contract::ClockSource::Auto);
  ASSERT_EQ(got.key, 0u); ASSERT_EQ(got.scale, 0u);
  ASSERT_STREQ(got.key_name, "C"); ASSERT_STREQ(got.scale_name, "Major");
  ASSERT_EQ(got.voicing_spread, want.voicing_spread);
  ASSERT_EQ(got.play_trigger, want.play_trigger);
  ASSERT_EQ(got.humanize, want.humanize);
  ASSERT_EQ(got.strum_feel, want.strum_feel);
  ASSERT_TRUE(got.active_preset == app::PresetId::kLead);
  ASSERT_TRUE(got.joy_families.notes.source == app::NoteSource::kScale);
  ASSERT_TRUE(got.joy_families.notes.gesture == app::NoteGesture::kWalk);
  ASSERT_EQ(got.joy_families.chord.palette, want.joy_families.chord.palette);

  // Settings and the engine follow the restored Config.
  const auto& s = c.state.settings();
  ASSERT_EQ(s.velocity, 110); ASSERT_EQ(s.note_length_ms, 240);
  ASSERT_STREQ(s.key, "C"); ASSERT_STREQ(s.scale, "Major");
  ASSERT_TRUE(s.usb_device_midi_enabled); ASSERT_FALSE(s.usb_host_midi_enabled);
  ASSERT_EQ(s.clock_mode, 1);
  ASSERT_TRUE(c.nav.prog_mode == app::ProgressionMode::kMajor);
  const auto& steps = c.engine.progression().steps();
  ASSERT_EQ(steps.size(), 7u);
  const char* major[] = {"maj", "min", "min", "maj", "maj", "min", "dim"};
  for (size_t i = 0; i < 7; ++i) {
    ASSERT_STREQ(steps[i].chord_quality.c_str(), major[i]);
    ASSERT_EQ(steps[i].velocity, 110);
  }
  // Performance state the Config does not carry is cleared too.
  ASSERT_EQ(c.engine.modifier().octave_shift(), 0);
  ASSERT_FALSE(c.engine.modifier().manual_inversion());
  ASSERT_STREQ(c.engine.modifier().quality_name(), "");
  ASSERT_TRUE(c.engine.modifier().voicing_spread() == engine::VoicingSpread::kDefault);
  ASSERT_EQ(c.ui.play_cursor, 3);
  ASSERT_EQ(c.ui.joystick_cursor, 0);
  ASSERT_TRUE(c.nav.settings_dirty);
}

// Cancel (the default choice) leaves everything as it was.
static void test_reset_cancel_changes_nothing() {
  NavContext c;
  c.edit(app::contract::ParamId::Velocity, -60);
  app::prod::button_system(c.input, c.ui);
  app::prod::encoder_turn(c.input, c.ui, 100);
  while (c.ui.selected_action != app::contract::ConfirmationAction::Reset && c.ui.select_cursor > 0)
    app::prod::encoder_turn(c.input, c.ui, -1);
  ASSERT_EQ(c.ui.selected_action, app::contract::ConfirmationAction::Reset);
  app::prod::encoder_click(c.input, c.ui);
  app::prod::encoder_click(c.input, c.ui);  // Cancel is preselected
  ASSERT_EQ(c.state.config().velocity, 50);
  ASSERT_EQ(c.ui.confirmation_action, app::contract::ConfirmationAction::None);
}

// A genre mode keeps its own name for display (Config.scale_name) while the
// engine runs on Major (Settings::scale) - and stays that way when something
// unrelated is edited afterwards.
static void test_genre_scale_keeps_its_display_name_across_unrelated_edits() {
  NavContext c;
  c.edit(app::contract::ParamId::Scale, static_cast<int8_t>(app::ProgressionMode::kBossaNova));
  ASSERT_EQ(c.state.config().scale, static_cast<uint8_t>(app::ProgressionMode::kBossaNova));
  ASSERT_STREQ(c.state.config().scale_name, "Bossa Nova");
  ASSERT_STREQ(c.state.settings().scale, "Major");
  ASSERT_TRUE(c.nav.prog_mode == app::ProgressionMode::kBossaNova);
  const std::string first_quality = c.engine.progression().steps()[0].chord_quality;

  c.edit(app::contract::ParamId::Velocity, -5);
  c.edit(app::contract::ParamId::Bpm, 12);
  c.key(3, true); c.key(3, false);  // A: key up a semitone
  app::prod::encoder_turn(c.input, c.ui, 1);
  ASSERT_EQ(c.state.config().scale, static_cast<uint8_t>(app::ProgressionMode::kBossaNova));
  ASSERT_STREQ(c.state.config().scale_name, "Bossa Nova");
  ASSERT_STREQ(c.state.settings().scale, "Major");
  ASSERT_STREQ(c.state.config().key_name, "C#");
  ASSERT_STREQ(c.state.settings().key, "C#");
  ASSERT_STREQ(c.engine.progression().steps()[0].chord_quality.c_str(), first_quality.c_str());
  ASSERT_STREQ(app::prod::model_snapshot(c.input, c.ui).config.scale_name, "Bossa Nova");

  // A real minor mode does reach the engine under its own name.
  c.edit(app::contract::ParamId::Scale, -100);
  c.edit(app::contract::ParamId::Scale, static_cast<int8_t>(app::ProgressionMode::kDorian));
  ASSERT_STREQ(c.state.config().scale_name, "Dorian");
  ASSERT_STREQ(c.state.settings().scale, "Dorian");
}

// commit_config() is the one path that makes an edited Config live: it
// derives the names from the indexes, bumps the revision, marks the change
// for autosave, and refuses - changing nothing - what does not validate.
static void test_commit_config_is_the_single_live_path() {
  NavContext c;
  const auto before = c.state.config();
  const uint32_t revision = c.nav.settings_revision;
  c.nav.settings_dirty = false; c.nav.dirty = false;

  auto bad = before; bad.velocity = 0;
  ASSERT_FALSE(app::commit_config(c.input, bad));
  bad = before; bad.active_preset = app::PresetId::kFlip;  // disabled preset
  ASSERT_FALSE(app::commit_config(c.input, bad));
  ASSERT_EQ(c.state.config().velocity, before.velocity);
  ASSERT_TRUE(c.state.config().active_preset == before.active_preset);
  ASSERT_EQ(c.nav.settings_revision, revision);
  ASSERT_FALSE(c.nav.settings_dirty);

  auto next = before;
  next.key = 7; next.scale = static_cast<uint8_t>(app::ProgressionMode::kMixolydian);
  std::snprintf(next.key_name, sizeof(next.key_name), "C");       // stale on purpose
  std::snprintf(next.scale_name, sizeof(next.scale_name), "Major");
  next.lead_channel = 4; next.roll_ms = 20; next.joystick.keypad_as_joy = true;
  c.input.t = 1234;
  ASSERT_TRUE(app::commit_config(c.input, next));
  ASSERT_STREQ(c.state.config().key_name, "G");
  ASSERT_STREQ(c.state.config().scale_name, "Mixolydian");
  ASSERT_STREQ(c.state.settings().key, "G");
  ASSERT_STREQ(c.state.settings().scale, "Mixolydian");
  ASSERT_EQ(c.state.settings().midi_channel_joy, 4);
  ASSERT_EQ(c.engine.lead_channel(), 4u);
  ASSERT_EQ(c.engine.performer().roll_ms(), 20);
  ASSERT_TRUE(c.js.keypad_as_joy);
  ASSERT_TRUE(c.nav.prog_mode == app::ProgressionMode::kMixolydian);
  ASSERT_EQ(c.state.config().revision, revision + 1);
  ASSERT_EQ(c.nav.settings_revision, revision + 1);
  ASSERT_TRUE(c.nav.settings_dirty);
  ASSERT_TRUE(c.nav.dirty);
  ASSERT_EQ(c.nav.last_settings_change, 1234u);
}

int main() {
  TEST_SUITE("Physical navigation");
  RUN_TEST(test_play_quick_adjusts_velocity); RUN_TEST(test_select_edit_live_back);
  RUN_TEST(test_filter_selection_and_menu_returns_to_play); RUN_TEST(test_edit_does_not_rollback);
  RUN_TEST(test_play_quick_adjust_is_independent_of_select_exploration);
  RUN_TEST(test_play_cursor_con_tap_steps_forward_and_wraps);
  RUN_TEST(test_bak_steps_backward_and_star_stays_a_noop);
  RUN_TEST(test_play_cursor_rotate_edits_the_focused_token);
  RUN_TEST(test_joystick_cursor_toggles_recipe_and_one_param);
  RUN_TEST(test_zero_toggle_does_not_disturb_either_cursor);
  RUN_TEST(test_play_cursor_channel_edit_flushes_old_channel);
  RUN_TEST(test_hash_tap_opens_menu_and_long_hash_opens_system);
  RUN_TEST(test_star_long_press_is_panic);
  RUN_TEST(test_panic_restarts_voice_leading);
  RUN_TEST(test_velocity_edit_reaches_the_next_chords_note_on);
  RUN_TEST(test_reset_restores_factory_defaults);
  RUN_TEST(test_reset_cancel_changes_nothing);
  RUN_TEST(test_genre_scale_keeps_its_display_name_across_unrelated_edits);
  RUN_TEST(test_commit_config_is_the_single_live_path);
  test_print_result(); return g_fail ? 1 : 0;
}
