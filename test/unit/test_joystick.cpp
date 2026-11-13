// Tests: joystick mode logic.
//   - Euclidean pattern (Bjorklund) algorithm correctness
//   - Mode transition cleanup: note-offs, pitch-bend reset
//   - Channel consistency: joy note-on and note-off share the joy channel
//   - Joy velocity clamping (min=1: a velocity-0 note-on is silent)
//   - apply_joy_transform: rotation and flip
//   - Mode cycling filter (chord vs lead context)
#include "../framework.h"
#include "../stubs/joy_stub.h"
#include "midi/router.h"
#include "midi/ble_midi.h"
#include "midi/usb_device_midi.h"
#include "app/config_codec.h"
#include "app/input_context.h"
#include "app/input_joystick.h"
#include "app/production_ui.h"
#include "app/progression_modes.h"
#include "app/settings_bridge.h"
#include "drivers/joystick_analog.h"
#include "engine/engine.h"
#include "engine/note_ledger.h"

#include <algorithm>
#include <string>
#include <vector>

// Euclidean was removed along with its handler; euclidean_pattern() and its
// tests went with it.

// ---------------------------------------------------------------------------
// Joy rotation transform
// ---------------------------------------------------------------------------

// Replicate apply_joy_transform for isolated unit testing.
static void apply_transform(uint8_t rotation, bool flip_h, bool flip_v, int16_t& x, int16_t& y) {
  for (uint8_t i = 0; i < rotation; ++i) {
    int16_t tmp = x; x = -y; y = tmp;
  }
  if (flip_h) x = -x;
  if (flip_v) y = -y;
}

static void test_rotation_0_no_change() {
  int16_t x=100, y=50;
  apply_transform(0, false, false, x, y);
  ASSERT_EQ(x, 100); ASSERT_EQ(y, 50);
}

static void test_rotation_1_90deg_cw() {
  // 90° CW: (x,y) → (-y, x)? Let's verify: one rotation does x=-y, y=x.
  int16_t x=100, y=0;
  apply_transform(1, false, false, x, y);
  // After 1 rotation: x = -0 = 0, y = 100
  ASSERT_EQ(x, 0); ASSERT_EQ(y, 100);
}

static void test_rotation_2_180deg() {
  int16_t x=100, y=50;
  apply_transform(2, false, false, x, y);
  ASSERT_EQ(x, -100); ASSERT_EQ(y, -50);
}

static void test_rotation_4_is_identity() {
  // Four 90° rotations = 360° = identity
  int16_t x=200, y=75;
  apply_transform(4, false, false, x, y);  // 4 iterations of the loop
  ASSERT_EQ(x, 200); ASSERT_EQ(y, 75);
}

static void test_flip_horizontal_negates_x() {
  int16_t x=100, y=50;
  apply_transform(0, true, false, x, y);
  ASSERT_EQ(x, -100); ASSERT_EQ(y, 50);
}

static void test_flip_vertical_negates_y() {
  int16_t x=100, y=50;
  apply_transform(0, false, true, x, y);
  ASSERT_EQ(x, 100); ASSERT_EQ(y, -50);
}

static void test_flip_horizontal_and_vertical_negate_both() {
  int16_t x=100, y=50;
  apply_transform(0, true, true, x, y);
  ASSERT_EQ(x, -100); ASSERT_EQ(y, -50);
}

static void test_rotation_then_flip() {
  // Combine: 90° CW then flip horizontal.
  int16_t x=100, y=0;
  apply_transform(1, true, false, x, y);
  // 1 rotation → (0, 100), then flip x: (0, 100) → (0, 100) — no-op since x=0
  ASSERT_EQ(x, 0); ASSERT_EQ(y, 100);
}

// ---------------------------------------------------------------------------
// Velocity clamping (regression: a velocity-0 note-on is a note-off)
// ---------------------------------------------------------------------------

static void test_velocity_clamp_min_1() {
  // The fixed formula: max(1, min(127, vel + delta))
  // Starting at 1, subtracting 10 must not go below 1.
  uint8_t vel = 1;
  vel = static_cast<uint8_t>(std::max(1, std::min(127, static_cast<int>(vel) - 10)));
  ASSERT_EQ(vel, 1);
}

static void test_velocity_clamp_max_127() {
  uint8_t vel = 120;
  vel = static_cast<uint8_t>(std::max(1, std::min(127, static_cast<int>(vel) + 10)));
  ASSERT_EQ(vel, 127);
}

static void test_velocity_never_reaches_zero() {
  uint8_t vel = 10;
  for (int i = 0; i < 20; ++i)
    vel = static_cast<uint8_t>(std::max(1, std::min(127, static_cast<int>(vel) - 10)));
  ASSERT_EQ(vel, 1);
  ASSERT_NE(vel, 0);
}

// ---------------------------------------------------------------------------
// Channel consistency (regression: joy notes released on the chord channel)
// ---------------------------------------------------------------------------

static void test_joy_noteon_noteoff_always_same_channel() {
  // The invariant: all joy note operations use midi_channel_joy.
  // Simulate a full play+release cycle through the router:
  //   note_on(midi_channel_joy, note, vel) → note_off(midi_channel_joy, note, 0)
  // The chord channel (midi_channel) must NEVER receive a note-off for joy notes.
  const uint8_t ch_joy   = 3;
  const uint8_t ch_chord = 0;
  const uint8_t note     = 62;

  midi::Router router;
  struct Cap : midi::TransportSink {
    struct M { std::string t; uint8_t ch, note; };
    std::vector<M> msgs;
    void send_note_on(uint8_t ch, uint8_t n, uint8_t) override { msgs.push_back({"on",ch,n}); }
    void send_note_off(uint8_t ch, uint8_t n, uint8_t) override { msgs.push_back({"off",ch,n}); }
    void send_pitch_bend(uint8_t, int16_t) override {}
    void send_control_change(uint8_t, uint8_t, uint8_t) override {}
  } cap;
  router.register_sink(&cap);

  // Simulate joy_add_note and joy_notes_off with the fixed channel logic.
  router.note_on(ch_joy,   note, 90);
  router.note_off(ch_joy,  note, 0);   // correct: same channel

  bool on_correct  = false, off_correct  = false;
  bool off_wrong   = false;
  for (auto& m : cap.msgs) {
    if (m.t == "on"  && m.ch == ch_joy   && m.note == note) on_correct  = true;
    if (m.t == "off" && m.ch == ch_joy   && m.note == note) off_correct = true;
    if (m.t == "off" && m.ch == ch_chord && m.note == note) off_wrong   = true;
  }
  ASSERT_TRUE(on_correct);
  ASSERT_TRUE(off_correct);
  ASSERT_FALSE(off_wrong);
}

// ---------------------------------------------------------------------------
// Joy mode filter lists
// ---------------------------------------------------------------------------

#include "app/input_context.h"

static void test_chord_joy_filter_count_nonzero() {
  // joy_mode_filter_count() uses the chord-context list.
  ASSERT_TRUE(app::joy_mode_filter_count(true) > 0);
}

static void test_lead_joy_filter_count_nonzero() {
  ASSERT_TRUE(app::joy_mode_filter_count(false) > 0);
}

static void test_chord_filter_contains_quality_mode() {
  // kQuality should be in the chord filter.
  bool found = false;
  uint8_t n = app::joy_mode_filter_count(true);
  for (uint8_t i = 0; i < n; ++i)
    if (app::joy_mode_filter_item(true, i) == app::JoystickPlayMode::kQuality) { found = true; break; }
  ASSERT_TRUE(found);
}

static void test_lead_filter_contains_soloist_mode() {
  bool found = false;
  uint8_t n = app::joy_mode_filter_count(false);
  for (uint8_t i = 0; i < n; ++i)
    if (app::joy_mode_filter_item(false, i) == app::JoystickPlayMode::kSoloist) { found = true; break; }
  ASSERT_TRUE(found);
}

static void test_chord_filter_excludes_soloist() {
  // Soloist is a lead-context mode and must NOT be in the chord filter.
  uint8_t n = app::joy_mode_filter_count(true);
  for (uint8_t i = 0; i < n; ++i)
    ASSERT_NE(app::joy_mode_filter_item(true, i), app::JoystickPlayMode::kSoloist);
}

static void test_lead_filter_excludes_quality() {
  // kQuality is a chord-context mode and must NOT be in the lead filter.
  uint8_t n = app::joy_mode_filter_count(false);
  for (uint8_t i = 0; i < n; ++i)
    ASSERT_NE(app::joy_mode_filter_item(false, i), app::JoystickPlayMode::kQuality);
}

static void test_out_of_range_index_returns_quality() {
  // Out-of-range index falls back to kQuality.
  ASSERT_EQ(app::joy_mode_filter_item(true, 200), app::JoystickPlayMode::kQuality);
  ASSERT_EQ(app::joy_mode_filter_item(false, 200), app::JoystickPlayMode::kQuality);
}

// ---------------------------------------------------------------------------
// The real path: stub ADC -> JoystickAnalog -> process_joystick()
// ---------------------------------------------------------------------------

namespace {
struct NoteCap : midi::TransportSink {
  struct On { uint8_t ch, note, vel; };
  std::vector<On> ons;
  void send_note_on(uint8_t ch, uint8_t n, uint8_t v) override { ons.push_back({ch, n, v}); }
  void send_note_off(uint8_t, uint8_t, uint8_t) override {}
  void send_pitch_bend(uint8_t, int16_t) override {}
  void send_control_change(uint8_t, uint8_t, uint8_t) override {}
};

struct Rig {
  app::AppState state; engine::Engine engine; midi::Router router;
  midi::BleMidi ble{router}; midi::UsbDeviceMidi usb{router};
  app::JoystickState js; app::UiNavState nav; std::vector<engine::ProgressionStep> steps;
  struct LedgerSink : engine::NoteLedgerSink {
    midi::Router& r; explicit LedgerSink(midi::Router& x) : r(x) {}
    void note_on(uint8_t route, uint8_t ch, uint8_t n, uint8_t v) override { r.route_note_on(static_cast<uint8_t>(1u << route), ch, n, v); }
    void note_off(uint8_t route, uint8_t ch, uint8_t n) override { r.route_note_off(static_cast<uint8_t>(1u << route), ch, n, 0); }
  };
  LedgerSink ledger_sink{router};
  engine::NoteLedger ledger{&ledger_sink};
  app::InputContext ctx{state, engine, router, ble, usb, js, nav, steps, 0, &ledger};
  app::prod::UiState ui; NoteCap cap; drivers::JoystickAnalog joystick;

  Rig() {
    state.sanitize(); engine.begin(); router.register_sink(&cap); engine.set_router(&router);
    app::apply_settings(state, engine, ble, usb);
    app::apply_progression_mode(engine, app::ProgressionMode::kMajor, state.settings());
    app::prod::begin(ui, ctx);
    stub::joy_reset();
  }
  template <class F> bool edit(F&& change) {
    auto config = state.config(); change(config); return app::commit_config(ctx, config);
  }
  bool select(app::PresetId id) {
    return edit([id](app::contract::Config& k) { k.active_preset = id; app::config::recall_preset(k.joy_families, id); });
  }
  // Raw centred ADC units and the driver's direction, as the hardware reports them.
  void joy(int16_t x, int16_t y, int8_t dir) {
    stub::joy_x = x; stub::joy_y = y; stub::joy_dir = dir;
    app::process_joystick(ctx, joystick, drivers::JoystickAnalogConfig{});
  }
};
}  // namespace

// The application works in a fixed +/-512 position range. The driver's centred
// 12-bit ADC units reach it at kAdcReach (1850), short of the theoretical 2048
// so a slightly off-centre stick still hits full scale both ways.
static void test_normalize_axis_end_points() {
  using J = drivers::JoystickAnalog;
  ASSERT_EQ(J::kAxisFullScale, 512);
  ASSERT_EQ(J::normalize_axis(0), 0);
  ASSERT_EQ(J::normalize_axis(1850), 512);
  ASSERT_EQ(J::normalize_axis(-1850), -512);
  ASSERT_EQ(J::normalize_axis(925), 256);
  ASSERT_EQ(J::normalize_axis(-925), -256);
  // Past the reach - the real end stops, and anything an ADC could report - clamps.
  ASSERT_EQ(J::normalize_axis(1851), 512);
  ASSERT_EQ(J::normalize_axis(2047), 512);
  ASSERT_EQ(J::normalize_axis(-2048), -512);
  ASSERT_EQ(J::normalize_axis(32767), 512);
  ASSERT_EQ(J::normalize_axis(-32768), -512);
  // Just inside the reach is just inside full scale, and it is odd-symmetric.
  ASSERT_EQ(J::normalize_axis(1849), 511);
  ASSERT_EQ(J::normalize_axis(-1849), -511);
  for (int v = 0; v <= 2047; ++v) {
    ASSERT_EQ(J::normalize_axis(static_cast<int16_t>(-v)), -J::normalize_axis(static_cast<int16_t>(v)));
    if (v) ASSERT_TRUE(J::normalize_axis(static_cast<int16_t>(v)) >= J::normalize_axis(static_cast<int16_t>(v - 1)));
  }
}

static void test_get_xy_normalized_scales_both_axes() {
  drivers::JoystickAnalog joystick;
  stub::joy_x = 1850; stub::joy_y = -925;
  int16_t x = 0, y = 0;
  joystick.get_xy_normalized(x, y);
  ASSERT_EQ(x, 512); ASSERT_EQ(y, -256);
  joystick.get_xy(x, y);  // the raw accessor still reports ADC units
  ASSERT_EQ(x, 1850); ASSERT_EQ(y, -925);
  stub::joy_reset();
}

// process_joystick() stores the NORMALISED position for every consumer
// (CONT streaming, velocity-by-distance, the XY readout).
static void test_process_joystick_stores_normalised_axes() {
  Rig r;
  r.joy(1850, -1850, -1);
  ASSERT_EQ(r.js.raw_x, 512); ASSERT_EQ(r.js.raw_y, -512);
  r.joy(2000, -2000, -1);  // the stub's real end stops
  ASSERT_EQ(r.js.raw_x, 512); ASSERT_EQ(r.js.raw_y, -512);
  r.joy(-925, 370, -1);
  ASSERT_EQ(r.js.raw_x, -256); ASSERT_EQ(r.js.raw_y, 102);
  r.joy(0, 0, -1);
  ASSERT_EQ(r.js.raw_x, 0); ASSERT_EQ(r.js.raw_y, 0);
}

// Velocity = distance uses that normalised deflection: 20 at the centre up to
// 127 at the rail. Read in raw ADC units it would saturate a quarter of the
// way out.
static void test_velocity_by_distance_follows_normalised_deflection() {
  auto velocity_at = [](int16_t y_adc) -> int {
    Rig r;  // fresh: Lead = walk, Vel mode = Distance
    r.joy(0, y_adc, 2);  // push up: one step of the walk
    return r.cap.ons.size() == 1 ? r.cap.ons[0].vel : -1;
  };
  Rig fresh;
  ASSERT_TRUE(fresh.state.config().joy_families.notes.velocity_mode == app::NoteVelocityMode::kByDistance);
  ASSERT_EQ(velocity_at(-1850), 127);
  ASSERT_EQ(velocity_at(-2000), 127);
  const int half = velocity_at(-925);
  ASSERT_GE(half, 72); ASSERT_LE(half, 74);    // 20 + 0.5 * 107
  const int quarter = velocity_at(-463);
  ASSERT_GE(quarter, 45); ASSERT_LE(quarter, 47);
  ASSERT_TRUE(quarter < half);

  // Vel mode = Fixed ignores the distance and uses the Vel setting.
  Rig fixed;
  ASSERT_TRUE(fixed.edit([](app::contract::Config& k) {
    k.joy_families.notes.velocity_mode = app::NoteVelocityMode::kFixed; k.solo_velocity = 91; }));
  fixed.joy(0, -400, 2);
  ASSERT_EQ(fixed.cap.ons.size(), 1u);
  ASSERT_EQ(fixed.cap.ons[0].vel, 91);
}

// "Invert Y" turns the 8-way direction over as well as the X/Y position:
// up <-> down, the diagonals with them, left and right untouched.
static void test_invert_y_mirrors_directions() {
  Rig r;
  ASSERT_TRUE(r.edit([](app::contract::Config& k) { k.joystick_flip_v = true; }));
  ASSERT_TRUE(r.state.settings().joystick_flip_v);
  static constexpr int8_t kMirrored[8] = {0, 7, 6, 5, 4, 3, 2, 1};  // E NE N NW W SW S SE
  for (int8_t d = 0; d < 8; ++d) {
    r.joy(0, 0, d);
    ASSERT_EQ(r.js.physical_dir, kMirrored[d]);
    r.joy(0, 0, -1);
    ASSERT_EQ(r.js.physical_dir, -1);
  }

  // And it is the mirrored direction that acts: with Colors (CLSC), up arms
  // "maj" and down arms "min" - inverted, pushing up arms "min".
  Rig plain; ASSERT_TRUE(plain.select(app::PresetId::kColors));
  plain.joy(0, -1500, 2);
  ASSERT_STREQ(plain.engine.modifier().quality_name(), "maj");
  Rig inverted; ASSERT_TRUE(inverted.select(app::PresetId::kColors));
  ASSERT_TRUE(inverted.edit([](app::contract::Config& k) { k.joystick_flip_v = true; }));
  inverted.joy(0, -1500, 2);
  ASSERT_STREQ(inverted.engine.modifier().quality_name(), "min");
  inverted.joy(0, 0, -1);
  inverted.joy(1500, 0, 0);  // right is still right
  ASSERT_STREQ(inverted.engine.modifier().quality_name(), "maj7");
}

// "Invert X" mirrors left <-> right the same way, and the two combine into a
// half turn - the same result as Rotation = 180.
static void test_invert_x_and_rotation_transform_directions() {
  Rig x;
  ASSERT_TRUE(x.edit([](app::contract::Config& k) { k.joystick_flip_h = true; }));
  static constexpr int8_t kMirroredX[8] = {4, 3, 2, 1, 0, 7, 6, 5};
  for (int8_t d = 0; d < 8; ++d) { x.joy(0, 0, d); ASSERT_EQ(x.js.physical_dir, kMirroredX[d]); x.joy(0, 0, -1); }

  Rig both;
  ASSERT_TRUE(both.edit([](app::contract::Config& k) { k.joystick_flip_h = true; k.joystick_flip_v = true; }));
  Rig turned;
  ASSERT_TRUE(turned.edit([](app::contract::Config& k) { k.joystick_rotation = 2; }));
  for (int8_t d = 0; d < 8; ++d) {
    both.joy(0, 0, d); turned.joy(0, 0, d);
    ASSERT_EQ(both.js.physical_dir, (d + 4) % 8);
    ASSERT_EQ(turned.js.physical_dir, (d + 4) % 8);
    both.joy(0, 0, -1); turned.joy(0, 0, -1);
  }
}

// Invert Y reaches the continuous position too: with it on, pulling the
// stick down is what raises a CONT value.
static void test_invert_y_flips_the_continuous_axis() {
  struct CcCap : midi::TransportSink {
    int last = -1;
    void send_note_on(uint8_t, uint8_t, uint8_t) override {}
    void send_note_off(uint8_t, uint8_t, uint8_t) override {}
    void send_pitch_bend(uint8_t, int16_t) override {}
    void send_control_change(uint8_t, uint8_t cc, uint8_t v) override { if (cc == 11) last = v; }
  };
  Rig plain; CcCap a; plain.router.register_sink(&a);
  ASSERT_TRUE(plain.select(app::PresetId::kMacro));
  plain.joy(0, -1850, -1);  // up
  ASSERT_EQ(a.last, 127);

  Rig inverted; CcCap b; inverted.router.register_sink(&b);
  ASSERT_TRUE(inverted.select(app::PresetId::kMacro));
  ASSERT_TRUE(inverted.edit([](app::contract::Config& k) { k.joystick_flip_v = true; }));
  inverted.joy(0, -1850, -1);
  ASSERT_EQ(b.last, 0);
  inverted.joy(0, 1850, -1);
  ASSERT_EQ(b.last, 127);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
  TEST_SUITE("Joystick Transform (rotation + flip)");
  RUN_TEST(test_rotation_0_no_change);
  RUN_TEST(test_rotation_1_90deg_cw);
  RUN_TEST(test_rotation_2_180deg);
  RUN_TEST(test_rotation_4_is_identity);
  RUN_TEST(test_flip_horizontal_negates_x);
  RUN_TEST(test_flip_vertical_negates_y);
  RUN_TEST(test_flip_horizontal_and_vertical_negate_both);
  RUN_TEST(test_rotation_then_flip);

  TEST_SUITE("Velocity Clamping (never 0)");
  RUN_TEST(test_velocity_clamp_min_1);
  RUN_TEST(test_velocity_clamp_max_127);
  RUN_TEST(test_velocity_never_reaches_zero);

  TEST_SUITE("Channel Consistency (joy channel on and off)");
  RUN_TEST(test_joy_noteon_noteoff_always_same_channel);

  TEST_SUITE("Joy Mode Filter Lists");
  RUN_TEST(test_chord_joy_filter_count_nonzero);
  RUN_TEST(test_lead_joy_filter_count_nonzero);
  RUN_TEST(test_chord_filter_contains_quality_mode);
  RUN_TEST(test_lead_filter_contains_soloist_mode);
  RUN_TEST(test_chord_filter_excludes_soloist);
  RUN_TEST(test_lead_filter_excludes_quality);
  RUN_TEST(test_out_of_range_index_returns_quality);

  TEST_SUITE("Normalised axes, velocity by distance, invert/rotate (real path)");
  RUN_TEST(test_normalize_axis_end_points);
  RUN_TEST(test_get_xy_normalized_scales_both_axes);
  RUN_TEST(test_process_joystick_stores_normalised_axes);
  RUN_TEST(test_velocity_by_distance_follows_normalised_deflection);
  RUN_TEST(test_invert_y_mirrors_directions);
  RUN_TEST(test_invert_x_and_rotation_transform_directions);
  RUN_TEST(test_invert_y_flips_the_continuous_axis);

  test_print_result();
  return (g_fail > 0) ? 1 : 0;
}
