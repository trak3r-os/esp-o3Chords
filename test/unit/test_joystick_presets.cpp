#include "../framework.h"

#include "app/config_codec.h"
#include "app/input_joystick.h"
#include "app/parameters.h"
#include "app/production_ui.h"
#include "app/settings_bridge.h"
#include "app/state.h"
#include "app/joystick_state.h"
#include "drivers/joystick_analog.h"
#include "engine/engine.h"
#include "engine/note_ledger.h"
#include "midi/router.h"
#include "midi/ble_midi.h"
#include "midi/usb_device_midi.h"
#include "esp_timer.h"
#include "joy_stub.h"

#include <array>
#include <vector>

struct PresetCapture final : midi::TransportSink {
  int note_on = 0;
  int note_off = 0;
  int bend = 0;
  int cc = 0;
  uint8_t last_channel = 0xff;
  uint8_t last_cc = 0xff;
  int16_t last_bend = 0;

  void send_note_on(uint8_t c, uint8_t, uint8_t) override { ++note_on; last_channel = c; }
  void send_note_off(uint8_t, uint8_t, uint8_t) override { ++note_off; }
  void send_pitch_bend(uint8_t c, int16_t v) override {
    ++bend; last_channel = c; last_bend = v;
  }
  void send_control_change(uint8_t c, uint8_t n, uint8_t) override {
    ++cc; last_channel = c; last_cc = n;
  }
  void clear() { note_on = note_off = bend = cc = 0; last_channel = last_cc = 0xff; }
};

struct PresetLedgerSink final : engine::NoteLedgerSink {
  midi::Router& router;
  explicit PresetLedgerSink(midi::Router& r) : router(r) {}
  void note_on(uint8_t route, uint8_t c, uint8_t n, uint8_t v) override {
    router.route_note_on(1u << route, c, n, v);
  }
  void note_off(uint8_t route, uint8_t c, uint8_t n) override {
    router.route_note_off(1u << route, c, n, 0);
  }
};

struct PresetContext {
  PresetCapture capture;
  midi::Router router;
  PresetLedgerSink sink{router};
  engine::NoteLedger ledger{&sink};
  app::AppState state;
  engine::Engine engine;
  midi::BleMidi ble{router};
  midi::UsbDeviceMidi usb{router};
  app::JoystickState js;
  app::UiNavState nav;
  std::vector<engine::ProgressionStep> steps;
  app::InputContext input{state, engine, router, ble, usb, js, nav, steps, 0, &ledger};
  app::prod::UiState ui;
  drivers::JoystickAnalog joystick;

  PresetContext() {
    router.register_sink(&capture);
    state.sanitize();
    engine.begin();
    engine.set_router(&router);
    engine.set_note_ledger(&ledger);
    engine.set_route_mask(router.active_route_mask());
    engine.performer().set_humanize_ms(0);
    engine.performer().set_strum_ms(0);
    engine::ProgressionStep step{};
    step.scale_degree = 1;
    step.chord_quality = "maj";
    step.velocity = 100;
    steps.push_back(step);
    engine.progression().set_steps(steps);
    nav.active_degree = 1;
    app::apply_settings(state, engine, ble, usb);
    app::prod::begin(ui, input);
  }

  // Walks the Recipe slot to `preset` via the real encoder-driven selection
  // path (production_ui.cpp), exercising the production selector exactly as
  // a musician would.
  void select(app::PresetId preset) {
    const auto current = static_cast<uint8_t>(state.config().active_preset);
    const auto wanted = static_cast<uint8_t>(preset);
    const auto count = static_cast<uint8_t>(app::PresetId::kCount);
    const auto delta = static_cast<int8_t>((wanted + count - current) % count);
    ui.navigation_mode = app::contract::NavigationMode::Edit;
    ui.selected_parameter = app::contract::ParamId::JoystickRecipe;
    for (int8_t i = 0; i < delta; ++i)
      app::prod::encoder_turn(input, ui, 1);
    ASSERT_EQ(state.config().active_preset, preset);
    ASSERT_EQ(app::contract::validate(state.config()).error,
              app::contract::ValidationError::None);
    if (js.keypad_dir >= 0) app::dispatch_keypad_joy_direction(input, -1);
    if (js.physical_dir >= 0) {
      stub::joy_dir = -1;
      app::process_joystick(input, joystick, drivers::JoystickAnalogConfig{});
    }
    capture.clear();
  }

  // Builds an exact config-migration-table row for scenarios where
  // no single factory preset is a faithful stand-in — bypasses the Recipe
  // slot's encoder path and writes Config directly, same as production code
  // would for a live-edited (not recalled) family.
  void select_notes(const app::NotesParams& p) {
    auto config = state.config();
    config.active_preset = app::PresetId::kLead;  // any NOTES preset; only the family matters
    config.joy_families.notes = p;
    state.set_config(config);
    // Mirrors production_ui.cpp's mutate() for ParamId::JoystickRecipe, not
    // apply_joystick_mode()'s full reset_joystick_gesture_state(): it must
    // NOT reset physical_dir/keypad_dir (see the comment at that call site) —
    // otherwise a still-held direction's later center release would fail to
    // register as a change.
    js.recipe_neutral_required = js.physical_dir >= 0 || js.keypad_dir >= 0;
    joy_all_notes_off(input);
    engine.release_held();
    engine.all_notes_off();
    engine.invalidate_source(engine::Engine::kLeadSource);
    capture.clear();
  }

  void flush() {
    g_test_time_us += 50000;
    esp_timer_fire_pending();
  }
};

// The eight selectable factory presets (Harmony folded into Drone; Arp, Feel,
// Dynamics and Flip are gone, and with them every PARAM-family preset).
static constexpr std::array<app::PresetId, 8> kAllPresets = {
    app::PresetId::kLead, app::PresetId::kBass, app::PresetId::kDrone,
    app::PresetId::kColors, app::PresetId::kDiscover, app::PresetId::kVoicings,
    app::PresetId::kBend, app::PresetId::kMacro};

static void every_preset_is_selectable_from_config() {
  PresetContext c;
  for (const auto preset : kAllPresets) {
    c.select(preset);
    ASSERT_EQ(c.state.config().active_preset, preset);
    ASSERT_EQ(app::contract::validate(c.state.config()).error,
               app::contract::ValidationError::None);
    ASSERT_TRUE(app::kPresets[static_cast<size_t>(preset)].family != app::JoyFamily::kParam);
  }
  // The Recipe slot wraps from the last preset straight back to the first:
  // there is no ninth (Flip) stop.
  c.select(app::PresetId::kMacro);
  app::prod::encoder_turn(c.input, c.ui, 1);
  ASSERT_EQ(c.state.config().active_preset, app::PresetId::kLead);
}

static void final_dispatch_covers_notes_modulation_and_release() {
  PresetContext c;

  c.select(app::PresetId::kLead);  // Melody sub A: source=scale, gesture=walk
  app::dispatch_keypad_joy_direction(c.input, 2);
  c.flush();
  ASSERT_GT(c.capture.note_on, 0);
  app::dispatch_keypad_joy_direction(c.input, -1);
  ASSERT_GT(c.capture.note_off, 0);

  c.select_notes([] {  // the old Harmony preset: Drone plus the legacy override
    app::NotesParams p = app::preset_defaults(app::PresetId::kDrone).notes;
    p.source = app::NoteSource::kIntervals;
    app::apply_legacy_harmony_override(p);
    return p;
  }());
  app::dispatch_keypad_joy_direction(c.input, 2);
  ASSERT_NE(c.js.drone_active_mask, 0);
  // Sustain=latch does not release on center; the same direction again does.
  app::dispatch_keypad_joy_direction(c.input, -1);
  app::dispatch_keypad_joy_direction(c.input, 2);
  ASSERT_EQ(c.js.drone_active_mask, 0);
  app::dispatch_keypad_joy_direction(c.input, -1);  // back to neutral before the next preset

  c.select(app::PresetId::kDrone);
  app::dispatch_keypad_joy_direction(c.input, 2);
  ASSERT_GT(c.ledger.active_tokens(), 0);
  app::dispatch_keypad_joy_direction(c.input, -1);
  c.select(app::PresetId::kColors);
  ASSERT_EQ(c.ledger.active_tokens(), 0);

  c.select(app::PresetId::kBend);
  stub::joy_x = stub::joy_y = 0; stub::joy_dir = -1;
  app::process_joystick(c.input, c.joystick, drivers::JoystickAnalogConfig{});
  stub::joy_x = 1500; stub::joy_y = 0; stub::joy_dir = 0;  // ADC units, ~80% of the travel
  c.js.physical_dir = -1;
  app::process_joystick(c.input, c.joystick, drivers::JoystickAnalogConfig{});
  ASSERT_GT(c.capture.bend, 0);
  ASSERT_GT(c.capture.last_bend, 0);
  ASSERT_EQ(c.capture.last_channel, c.state.settings().midi_channel);
  stub::joy_x = stub::joy_y = 0; stub::joy_dir = -1;

  c.select(app::PresetId::kMacro);
  stub::joy_x = 200; stub::joy_y = -200; stub::joy_dir = -1;
  app::process_joystick(c.input, c.joystick, drivers::JoystickAnalogConfig{});
  ASSERT_GE(c.capture.cc, 2);
  ASSERT_EQ(c.capture.last_channel, c.state.settings().midi_channel_joy);
  stub::joy_x = stub::joy_y = 0;
}

static void final_dispatch_covers_clock_and_preset_switch_barrier() {
  PresetContext c;

  // Euclidean was removed; BassClock ->
  // Pentatonic covers the same "clock producer -> switch -> clean state ->
  // new producer" shape this suite originally used Euclidean for.
  c.select(app::PresetId::kBass);
  app::dispatch_keypad_joy_direction(c.input, 2);
  ASSERT_GT(c.ledger.active_tokens(), 0);

  c.select_notes([] {  // Pentatonic: source=pentatonic, gesture=walk
    app::NotesParams p; p.source = app::NoteSource::kPentatonic;
    p.gesture = app::NoteGesture::kWalk; return p;
  }());
  ASSERT_EQ(c.ledger.active_tokens(), 0);
  app::dispatch_keypad_joy_direction(c.input, -1);
  app::dispatch_keypad_joy_direction(c.input, 2);
  c.flush();
  ASSERT_GT(c.capture.note_on, 0);

  // A CHORD preset is not a producer at all: the same direction arms a
  // colour and leaves the ledger and the wire untouched.
  app::dispatch_keypad_joy_direction(c.input, -1);
  c.select(app::PresetId::kColors);
  ASSERT_EQ(c.ledger.active_tokens(), 0);
  app::dispatch_keypad_joy_direction(c.input, 2);
  c.flush();
  ASSERT_EQ(c.capture.note_on, 0);
  ASSERT_EQ(c.ledger.active_tokens(), 0);
  ASSERT_STREQ(c.engine.modifier().quality_name(), "maj");
}

static void final_dispatch_covers_remaining_strategies_and_overrides() {
  PresetContext c;
  c.select_notes([] {  // ChordVel: source=degrees, gesture=clock, voices=chord, vel=distance
    app::NotesParams p; p.source = app::NoteSource::kDegrees;
    p.gesture = app::NoteGesture::kClock; p.voices = app::NoteVoices::kChord;
    p.velocity_mode = app::NoteVelocityMode::kByDistance; return p;
  }());
  app::dispatch_keypad_joy_direction(c.input, 2);
  ASSERT_GT(c.ledger.active_tokens(), 0);
  app::dispatch_keypad_joy_direction(c.input, -1);

  c.select_notes([] {  // Interval: source=intervals, gesture=clock
    app::NotesParams p; p.source = app::NoteSource::kIntervals;
    p.gesture = app::NoteGesture::kClock; return p;
  }());
  app::dispatch_keypad_joy_direction(c.input, -1);
  app::dispatch_keypad_joy_direction(c.input, 2);
  c.flush();
  ASSERT_GT(c.capture.note_on, 0);

  c.select(app::PresetId::kVoicings);
  app::dispatch_keypad_joy_direction(c.input, 0);  // E -> Drop2
  c.flush();
  ASSERT_EQ(c.js.voicing_selection, 2);
  ASSERT_EQ(c.state.config().voicing_spread, 2);
  ASSERT_EQ(c.state.settings().voicing_spread, 2);
  ASSERT_TRUE(c.engine.modifier().voicing_spread() == engine::VoicingSpread::kDrop2);
  ASSERT_EQ(c.capture.note_on, 0);  // it sets the voicing; it does not strum
  app::dispatch_keypad_joy_direction(c.input, -1);

  // Discover arms a ranked quality without sounding it; the next chord
  // trigger is what plays, with that quality.
  c.select(app::PresetId::kDiscover);
  stub::joy_dir = -1;
  c.nav.active_degree = 1;
  app::dispatch_keypad_joy_direction(c.input, 2);
  c.flush();
  ASSERT_EQ(c.capture.note_on, 0);
  ASSERT_TRUE(c.engine.modifier().quality_name()[0] != '\0');
  ASSERT_STREQ(c.engine.modifier().quality_name(), c.js.discover_slots[app::kDiscoverMap[2]]);
  c.engine.trigger_step(0, engine::TriggerMode::kOneShot);
  c.flush();
  ASSERT_GT(c.capture.note_on, 0);
}

int main() {
  TEST_SUITE("Joystick factory presets end to end");
  RUN_TEST(every_preset_is_selectable_from_config);
  RUN_TEST(final_dispatch_covers_notes_modulation_and_release);
  RUN_TEST(final_dispatch_covers_clock_and_preset_switch_barrier);
  RUN_TEST(final_dispatch_covers_remaining_strategies_and_overrides);
  test_print_result();
  return g_fail ? 1 : 0;
}
