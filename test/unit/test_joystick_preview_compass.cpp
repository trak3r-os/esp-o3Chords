// Regression for a compass bug: joystick_preview() once surfaced only 4 of
// the 8 directions (up, down, left, right), even though every gesture-clock handler it wraps
// already computes a candidate for all 8 — the four diagonals were
// silently invisible to any screen. JoystickPreview::compass[8] fixes that;
// this suite is the regression that it stays fixed and stays distinct.
#include "../framework.h"
#include "../stubs/joy_stub.h"

#include "app/production_ui.h"
#include "app/config_codec.h"
#include "app/input_context.h"
#include "app/input_joystick.h"
#include "app/settings_bridge.h"
#include "app/progression_modes.h"
#include "app/state.h"
#include "app/joystick_state.h"
#include "app/ui_state.h"
#include "drivers/joystick_analog.h"
#include "engine/engine.h"
#include "midi/router.h"
#include "midi/ble_midi.h"
#include "midi/usb_device_midi.h"

#include <cstring>

using namespace app;
using namespace app::prod;

struct Ctx {
  AppState app_state;
  engine::Engine engine;
  midi::Router router;
  midi::BleMidi ble{router};
  midi::UsbDeviceMidi usb{router};
  JoystickState js;
  UiNavState nav;
  std::vector<engine::ProgressionStep> play_prog;
  InputContext ctx{app_state, engine, router, ble, usb, js, nav, play_prog, 0};
  UiState ui;

  Ctx() {
    app_state.sanitize();
    engine.begin();
    engine.set_router(&router);
    apply_settings(app_state, engine, ble, usb);
    apply_progression_mode(engine, ProgressionMode::kMajor, app_state.settings());
    prod::begin(ui, ctx);
  }

  void select(PresetId preset) {
    auto config = app_state.config();
    config.active_preset = preset;
    config::recall_preset(config.joy_families, preset);
    app_state.set_config(config);
  }
};

// NOTES/gesture=clock (kBass's default - kLead defaults to walk instead):
// all 8 compass slots must be non-empty note labels, and not all identical —
// a real 8-way destination set, the same distinctness bar
// test_chord_building.cpp's voicing-styles test and
// test_joystick_analog_modes.cpp's quality-directions test use.
static void test_notes_clock_compass_has_8_distinct_notes() {
  Ctx c;
  c.select(PresetId::kBass);
  const auto preview = joystick_preview(c.ctx);
  ASSERT_TRUE(preview.available);
  std::string seen[8];
  for (int i = 0; i < 8; ++i) {
    ASSERT_TRUE(preview.compass[i][0] != '\0');
    seen[i] = preview.compass[i];
  }
  bool all_same = true;
  for (int i = 1; i < 8; ++i) if (seen[i] != seen[0]) all_same = false;
  ASSERT_FALSE(all_same);
}

// The compass must still agree with the pre-existing cardinal fields
// (up/down/left/right) at the matching directions (2=N/up, 6=S/down,
// 4=W/left, 0=E/right) — the new data is additive, not a second source of
// truth that could drift from the one screens already read.
static void test_notes_clock_compass_matches_cardinal_fields() {
  Ctx c;
  c.select(PresetId::kBass);
  const auto preview = joystick_preview(c.ctx);
  ASSERT_STREQ(preview.compass[2], preview.up);
  ASSERT_STREQ(preview.compass[6], preview.down);
  ASSERT_STREQ(preview.compass[4], preview.left);
  ASSERT_STREQ(preview.compass[0], preview.right);
}

// CHORD/Colors: same 8-distinct bar, this time for quality names rather
// than notes (chord_candidate_for_direction() already computes all 8 for
// dispatch — joystick_preview() just wasn't surfacing them all).
static void test_chord_colors_compass_has_8_distinct_qualities() {
  Ctx c;
  c.select(PresetId::kColors);
  const auto preview = joystick_preview(c.ctx);
  ASSERT_TRUE(preview.available);
  std::string seen[8];
  for (int i = 0; i < 8; ++i) {
    ASSERT_TRUE(preview.compass[i][0] != '\0');
    seen[i] = preview.compass[i];
  }
  for (int i = 0; i < 8; ++i)
    for (int j = i + 1; j < 8; ++j)
      ASSERT_TRUE(seen[i] != seen[j]);
}

// CHORD/Voicings: the cells are the eight voicing names a direction writes
// to the Voicing parameter (harmony::voicing_for_direction()), not quality
// names - every direction its own label, none falling back to "--".
static void test_chord_voicings_compass_has_8_distinct_voicing_names() {
  Ctx c;
  c.select(PresetId::kVoicings);
  const auto preview = joystick_preview(c.ctx);
  ASSERT_TRUE(preview.available);
  ASSERT_STREQ(preview.x_name, "Voice");
  std::string seen[8];
  for (int i = 0; i < 8; ++i) {
    ASSERT_TRUE(preview.compass[i][0] != '\0');
    ASSERT_TRUE(std::strcmp(preview.compass[i], "--") != 0);
    seen[i] = preview.compass[i];
  }
  for (int i = 0; i < 8; ++i)
    for (int j = i + 1; j < 8; ++j)
      ASSERT_TRUE(seen[i] != seen[j]);
  ASSERT_STREQ(preview.compass[2], "Default");  // N
  ASSERT_STREQ(preview.compass[1], "Close");    // NE
}

// CHORD/Discover ranks all eight directions (no direction is a "suggested
// next chord" sentinel any more): the compass shows the ranked slot each
// direction arms, eight distinct qualities.
static void test_chord_discover_compass_shows_the_ranked_slots() {
  Ctx c;
  c.select(PresetId::kDiscover);
  c.ctx.nav.active_degree = 1;
  discover_rebuild_slots(c.ctx);
  const auto preview = joystick_preview(c.ctx);
  ASSERT_TRUE(preview.available);
  ASSERT_STREQ(preview.x_name, "Qual");
  std::string seen[8];
  for (int i = 0; i < 8; ++i) {
    ASSERT_STREQ(preview.compass[i], c.js.discover_slots[kDiscoverMap[i]]);
    seen[i] = preview.compass[i];
  }
  for (int i = 0; i < 8; ++i)
    for (int j = i + 1; j < 8; ++j)
      ASSERT_TRUE(seen[i] != seen[j]);
}

// CHORD's `current` is the cell under the held direction, "--" at centre.
static void test_chord_current_follows_the_held_direction() {
  Ctx c;
  c.select(PresetId::kColors);
  auto preview = joystick_preview(c.ctx);
  ASSERT_STREQ(preview.current, "--");
  c.js.physical_dir = 0;  // E
  preview = joystick_preview(c.ctx);
  ASSERT_STREQ(preview.current, preview.compass[0]);
  ASSERT_STREQ(preview.current, "maj7");
  ASSERT_STREQ(preview.right, "maj7");
}

// CONT: each axis reads back what its own target last sent, under that
// target's name.
static void test_cont_readout_names_each_axis_by_its_target() {
  Ctx c;
  c.select(PresetId::kBend);
  auto preview = joystick_preview(c.ctx);
  ASSERT_TRUE(preview.available);
  ASSERT_STREQ(preview.x_name, "Bend"); ASSERT_STREQ(preview.y_name, "Vib");
  c.js.last_bend = -1234; c.js.bend_vibrato_depth = 64;
  preview = joystick_preview(c.ctx);
  ASSERT_STREQ(preview.x_value, "-1234"); ASSERT_STREQ(preview.y_value, "64");

  c.select(PresetId::kMacro);
  preview = joystick_preview(c.ctx);
  ASSERT_STREQ(preview.x_name, "CC"); ASSERT_STREQ(preview.y_name, "CC");
  ASSERT_STREQ(preview.x_value, "--");  // nothing sent yet
  c.js.cc_last_x = 90; c.js.cc_last_y = 0;
  auto config = c.app_state.config();
  config.joy_families.cont.y_target = ContTarget::kAftertouch;
  c.app_state.set_config(config);
  preview = joystick_preview(c.ctx);
  ASSERT_STREQ(preview.x_value, "90");
  ASSERT_STREQ(preview.y_name, "AT"); ASSERT_STREQ(preview.y_value, "0");
}

// NOTES/walk: preview.walk[8] must expose the full notes_for_source() array
// (the mockup's 8-note scale strip), not just the current/up/down triple -
// same 8-distinct bar as the compass fields, direct-indexed rather than
// permuted through kClockMap.
static void test_notes_walk_strip_has_8_distinct_notes() {
  Ctx c;
  c.select(PresetId::kLead);  // NOTES, gesture=walk by default.
  ASSERT_TRUE(c.app_state.config().joy_families.notes.gesture == NoteGesture::kWalk);
  const auto preview = joystick_preview(c.ctx);
  ASSERT_TRUE(preview.available);
  std::string seen[8];
  for (int i = 0; i < 8; ++i) {
    ASSERT_TRUE(preview.walk[i][0] != '\0');
    seen[i] = preview.walk[i];
  }
  bool all_same = true;
  for (int i = 1; i < 8; ++i) if (seen[i] != seen[0]) all_same = false;
  ASSERT_FALSE(all_same);
}

// NOTES/walk and CONT have no 8-way compass (walk is linear; CONT reads
// position, not direction) — compass stays all-empty rather than filled
// with stale or misleading data from a previous family.
static void test_families_without_a_compass_leave_it_empty() {
  Ctx c;
  c.select(PresetId::kBass);  // NOTES, clock by default — force walk instead.
  auto config = c.app_state.config();
  config.joy_families.notes.gesture = NoteGesture::kWalk;
  c.app_state.set_config(config);
  auto preview = joystick_preview(c.ctx);
  for (int i = 0; i < 8; ++i) ASSERT_EQ(preview.compass[i][0], '\0');

  c.select(PresetId::kMacro);  // CONT
  preview = joystick_preview(c.ctx);
  for (int i = 0; i < 8; ++i) ASSERT_EQ(preview.compass[i][0], '\0');
}

int main() {
  TEST_SUITE("joystick_preview() compass — all 8 directions");
  RUN_TEST(test_notes_clock_compass_has_8_distinct_notes);
  RUN_TEST(test_notes_clock_compass_matches_cardinal_fields);
  RUN_TEST(test_chord_colors_compass_has_8_distinct_qualities);
  RUN_TEST(test_chord_voicings_compass_has_8_distinct_voicing_names);
  RUN_TEST(test_chord_discover_compass_shows_the_ranked_slots);
  RUN_TEST(test_chord_current_follows_the_held_direction);
  RUN_TEST(test_cont_readout_names_each_axis_by_its_target);
  RUN_TEST(test_notes_walk_strip_has_8_distinct_notes);
  RUN_TEST(test_families_without_a_compass_leave_it_empty);
  test_print_result();
  return g_fail ? 1 : 0;
}
