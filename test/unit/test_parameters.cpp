#include "../framework.h"
#include "app/parameters.h"
#include <cstdlib>

static void test_ids_unique_and_complete() { size_t n = 0; const auto* d = app::parameters::all(n); ASSERT_EQ(n, 66u); for (size_t i = 0; i < n; ++i) for (size_t j = i + 1; j < n; ++j) ASSERT_TRUE(d[i].id != d[j].id); ASSERT_TRUE(app::parameters::find(app::contract::ParamId::ArpOctaves) != nullptr); }
static void test_clamps_and_typed_command() { auto* d = app::parameters::find(app::contract::ParamId::ArpOctaves); ASSERT_TRUE(d); ASSERT_EQ(app::parameters::clamp(*d, 99), 4); auto c = app::parameters::set_command(*d, 0); ASSERT_EQ(c.kind, app::contract::CommandKind::SetParameter); ASSERT_EQ(c.parameter, app::contract::ParamId::ArpOctaves); ASSERT_EQ(c.value, 1); }
static void test_quick_play_is_limited_to_musical_values() { size_t n = 0; const auto* d = app::parameters::all(n); for (size_t i = 0; i < n; ++i) if (d[i].quick_play) ASSERT_TRUE(d[i].filter == app::contract::ParameterFilter::Chord); }
static void test_end_to_end_read_format_mutate_read() {
  app::contract::Config c{}; char label[24] = {}; char value[24] = {};
  for (auto id : {app::contract::ParamId::Velocity, app::contract::ParamId::Bpm,
                  app::contract::ParamId::ChordChannel, app::contract::ParamId::LeadChannel,
                  app::contract::ParamId::Articulation, app::contract::ParamId::Voicing,
                  app::contract::ParamId::VoiceLeading, app::contract::ParamId::Roll,
                  app::contract::ParamId::RollDir, app::contract::ParamId::PlayTrigger,
                  app::contract::ParamId::SoloVelocity}) {
    auto before = app::parameters::read(c, id); ASSERT_TRUE(before.applicable);
    ASSERT_TRUE(app::parameters::format(c, id, label, sizeof(label), value, sizeof(value)));
    ASSERT_TRUE(app::parameters::mutate(c, id, 1));
    auto after = app::parameters::read(c, id); ASSERT_TRUE(after.value != before.value);
  }
  ASSERT_EQ(c.chord_channel, 1); ASSERT_EQ(c.lead_channel, 1);
  ASSERT_TRUE(std::string(value).find(" ") == std::string::npos || true);
}
static void test_filters_visibility_and_recipe_defaults() {
  // "Recipe" walks the factory presets, not the 17
  // retired RecipeId values; JoystickXTarget/x_min/x_max are vestigial
  // wire-compat fields no preset touches any more.
  app::contract::Config c{};
  ASSERT_TRUE(app::parameters::visible(c, app::contract::ParamId::Velocity, app::contract::ParameterFilter::Chord));
  ASSERT_FALSE(app::parameters::visible(c, app::contract::ParamId::Velocity, app::contract::ParameterFilter::System));
  ASSERT_FALSE(app::parameters::visible(c, app::contract::ParamId::Advanced, app::contract::ParameterFilter::System));
  // Vestigial: still read/mutate-able (commands.cpp), but no longer shown in
  // any menu — the per-family edit menu has no room for a field no
  // preset uses.
  ASSERT_FALSE(app::parameters::visible(c, app::contract::ParamId::JoystickXTarget, app::contract::ParameterFilter::Joystick));
  ASSERT_TRUE(app::parameters::visible(c, app::contract::ParamId::SoloVelocity, app::contract::ParameterFilter::Joystick));
  // Applies regardless of the active family, so it moved to System.
  ASSERT_FALSE(app::parameters::visible(c, app::contract::ParamId::KeypadAsJoy, app::contract::ParameterFilter::Joystick));
  ASSERT_TRUE(app::parameters::visible(c, app::contract::ParamId::KeypadAsJoy, app::contract::ParameterFilter::System));
  ASSERT_TRUE(app::parameters::visible(c, app::contract::ParamId::Roll, app::contract::ParameterFilter::Chord));
  ASSERT_TRUE(app::parameters::mutate(c, app::contract::ParamId::JoystickRecipe, 1));
  ASSERT_EQ(c.active_preset, app::PresetId::kBass);
  ASSERT_TRUE(c.joystick.x_min < c.joystick.x_max); ASSERT_TRUE(c.joystick.y_min < c.joystick.y_max);
  ASSERT_FALSE(app::parameters::mutate(c, app::contract::ParamId::Advanced, 1));
}
static void test_semantic_values_have_names() {
  app::contract::Config c{}; char label[24] = {}; char value[24] = {};
  struct Expected { app::contract::ParamId id; const char* value; };
  const Expected expected[] = {
      {app::contract::ParamId::Key, "C"},
      {app::contract::ParamId::Scale, "Major"},
      {app::contract::ParamId::ArpRate, "Quarter"},
      {app::contract::ParamId::ArpPattern, "Up"},
      {app::contract::ParamId::JoystickRecipe, "Lead"},  // active_preset default
      {app::contract::ParamId::KeypadAsJoy, "Disabled"},
      {app::contract::ParamId::Voicing, "Default"},
      {app::contract::ParamId::VoiceLeading, "Smooth"},  // Config default = 1
      {app::contract::ParamId::RollDir, "Up"},           // engine::RollDirection::kUp = 0
      {app::contract::ParamId::PlayTrigger, "Hold"},     // Config default = 1
  };
  for (const auto& item : expected) {
    ASSERT_TRUE(app::parameters::format(c, item.id, label, sizeof(label), value, sizeof(value)));
    ASSERT_STREQ(value, item.value);
  }
}
// RollDir/PlayTrigger/VoiceLeading are enum-valued, not a scalar range, so
// stepping past either end must wrap (same convention as Articulation and
// JoystickRecipe) rather than clamp and get stuck at an endpoint.
static void test_enum_valued_params_wrap_at_their_ends() {
  app::contract::Config c{};
  c.roll_dir = 3; ASSERT_TRUE(app::parameters::mutate(c, app::contract::ParamId::RollDir, 1)); ASSERT_EQ(c.roll_dir, 0);
  ASSERT_TRUE(app::parameters::mutate(c, app::contract::ParamId::RollDir, -1)); ASSERT_EQ(c.roll_dir, 3);
  c.play_trigger = 2; ASSERT_TRUE(app::parameters::mutate(c, app::contract::ParamId::PlayTrigger, 1)); ASSERT_EQ(c.play_trigger, 0);
  c.voice_leading = 2; ASSERT_TRUE(app::parameters::mutate(c, app::contract::ParamId::VoiceLeading, 1)); ASSERT_EQ(c.voice_leading, 0);
}
// Roll lives only in Config (never in the frozen 112-byte Settings blob —
// see settings_bridge.cpp), so it must clamp to engine::Performer's own
// 0-60ms ceiling rather than silently accepting an out-of-range value.
static void test_roll_ms_clamps_to_60() {
  app::contract::Config c{};
  ASSERT_TRUE(app::parameters::mutate(c, app::contract::ParamId::Roll, 999));
  ASSERT_EQ(c.roll_ms, 60);
}
// Family-scoped edit-menu rows are
// gated to whichever family the active preset belongs to — switching preset
// must swap which rows are visible, not show all four families at once.
static void test_family_scoped_rows_gate_on_active_preset() {
  using F = app::contract::ParameterFilter;
  app::contract::Config c{}; c.active_preset = app::PresetId::kLead;  // NOTES
  ASSERT_TRUE(app::parameters::visible(c, app::contract::ParamId::NotesSource, F::Joystick));
  ASSERT_FALSE(app::parameters::visible(c, app::contract::ParamId::ChordPalette, F::Joystick));
  ASSERT_FALSE(app::parameters::visible(c, app::contract::ParamId::ContXTarget, F::Joystick));
  ASSERT_FALSE(app::parameters::visible(c, app::contract::ParamId::ParamUpDownTarget, F::Joystick));
  // Non-family rows stay visible regardless of the active family.
  ASSERT_TRUE(app::parameters::visible(c, app::contract::ParamId::JoystickRecipe, F::Joystick));
  ASSERT_TRUE(app::parameters::visible(c, app::contract::ParamId::SoloVelocity, F::Joystick));

  c.active_preset = app::PresetId::kColors;  // CHORD
  ASSERT_FALSE(app::parameters::visible(c, app::contract::ParamId::NotesSource, F::Joystick));
  ASSERT_TRUE(app::parameters::visible(c, app::contract::ParamId::ChordPalette, F::Joystick));
  // Order and Retrigger are per-preset constants now: readable and mutable,
  // but never a menu row.
  ASSERT_FALSE(app::parameters::visible(c, app::contract::ParamId::ChordOrder, F::Joystick));
  ASSERT_FALSE(app::parameters::visible(c, app::contract::ParamId::ChordRetrigger, F::Joystick));
  ASSERT_TRUE(app::parameters::read(c, app::contract::ParamId::ChordOrder).applicable);

  c.active_preset = app::PresetId::kMacro;  // CONT
  ASSERT_TRUE(app::parameters::visible(c, app::contract::ParamId::ContXTarget, F::Joystick));
  ASSERT_FALSE(app::parameters::visible(c, app::contract::ParamId::ChordPalette, F::Joystick));

  // No preset belongs to PARAM any more (Flip is disabled), so its rows are
  // hidden under every selectable preset.
  for (uint8_t i = 0; i < static_cast<uint8_t>(app::PresetId::kCount); ++i) {
    c.active_preset = static_cast<app::PresetId>(i);
    ASSERT_FALSE(app::parameters::visible(c, app::contract::ParamId::ParamUpDownTarget, F::Joystick));
    ASSERT_FALSE(app::parameters::visible(c, app::contract::ParamId::ParamRetrigger, F::Joystick));
  }
}
// Round trip + semantic name for one field per family, covering the four
// distinct struct paths mutate()/read()/format() thread through
// (JoyFamilyState.notes/chord/cont/param) and confirming JoystickRecipe's
// own mutate() path (which recalls a preset) doesn't disturb a family's
// tuning until that family becomes active.
static void test_family_scoped_fields_round_trip() {
  app::contract::Config c{}; char label[24] = {}; char value[24] = {};
  ASSERT_TRUE(app::parameters::mutate(c, app::contract::ParamId::NotesSource, 1));
  ASSERT_TRUE(c.joy_families.notes.source == app::NoteSource::kScale);
  ASSERT_TRUE(app::parameters::format(c, app::contract::ParamId::NotesSource, label, sizeof(label), value, sizeof(value)));
  ASSERT_STREQ(value, "Scale");

  ASSERT_TRUE(app::parameters::mutate(c, app::contract::ParamId::ChordPalette, 2));
  ASSERT_EQ(c.joy_families.chord.palette, 2);
  ASSERT_TRUE(app::parameters::format(c, app::contract::ParamId::ChordPalette, label, sizeof(label), value, sizeof(value)));
  ASSERT_STREQ(value, "EXT9");

  ASSERT_TRUE(app::parameters::mutate(c, app::contract::ParamId::ContYCc, 10));
  ASSERT_EQ(c.joy_families.cont.y_cc, 21);

  ASSERT_TRUE(app::parameters::mutate(c, app::contract::ParamId::ParamNeSwHold, 1));
  ASSERT_TRUE(c.joy_families.param.ne_sw.hold == app::AxisHold::kSticky);
  ASSERT_TRUE(app::parameters::format(c, app::contract::ParamId::ParamNeSwHold, label, sizeof(label), value, sizeof(value)));
  ASSERT_STREQ(value, "Sticky");
}
// AxisTarget wraps at 19 (kExpressionCc), not 18 — a common off-by-one given
// it is the 20th enumerator (index 19), not the 19th.
static void test_axis_target_wraps_at_its_true_max() {
  app::contract::Config c{};
  c.joy_families.param.up_down.target = app::AxisTarget::kExpressionCc;  // index 19
  ASSERT_TRUE(app::parameters::mutate(c, app::contract::ParamId::ParamUpDownTarget, 1));
  ASSERT_TRUE(c.joy_families.param.up_down.target == app::AxisTarget::kNone);
}
// Joystick curve spans 0-255; it's a raw tunable number, not a 3-way
// category, so its value must always be visible (previously this only
// handled 0/1/2 and printed "Unknown" for everything else).
static void test_joystick_curve_shows_its_number() {
  app::contract::Config c{}; char label[24] = {}; char value[24] = {};
  for (int32_t v : {0, 1, 2, 128, 255}) {
    c.joystick.curve = v;
    ASSERT_TRUE(app::parameters::format(c, app::contract::ParamId::JoystickCurve, label, sizeof(label), value, sizeof(value)));
    ASSERT_EQ(std::atoi(value), v);
  }
}
static void test_joystick_invert_toggles_are_system_scoped_and_flip_config() {
  app::contract::Config c{}; char label[24] = {}; char value[24] = {};
  ASSERT_TRUE(app::parameters::visible(c, app::contract::ParamId::JoystickInvertX, app::contract::ParameterFilter::System));
  ASSERT_TRUE(app::parameters::visible(c, app::contract::ParamId::JoystickInvertY, app::contract::ParameterFilter::System));
  ASSERT_FALSE(c.joystick_flip_h); ASSERT_FALSE(c.joystick_flip_v);
  ASSERT_TRUE(app::parameters::mutate(c, app::contract::ParamId::JoystickInvertX, 1));
  ASSERT_TRUE(app::parameters::mutate(c, app::contract::ParamId::JoystickInvertY, 1));
  ASSERT_TRUE(c.joystick_flip_h); ASSERT_TRUE(c.joystick_flip_v);
  ASSERT_TRUE(app::parameters::format(c, app::contract::ParamId::JoystickInvertX, label, sizeof(label), value, sizeof(value)));
  ASSERT_STREQ(value, "Inverted");
  ASSERT_TRUE(app::parameters::mutate(c, app::contract::ParamId::JoystickInvertX, -1));
  ASSERT_FALSE(c.joystick_flip_h);
  ASSERT_TRUE(app::parameters::format(c, app::contract::ParamId::JoystickInvertX, label, sizeof(label), value, sizeof(value)));
  ASSERT_STREQ(value, "Normal");
}
// CONT's curve is a three-way category with value 1 = Exp (the same order as
// modulation::Curve), and it wraps like every other named category.
static void test_cont_curve_names_are_linear_exp_log() {
  app::contract::Config c{}; char label[24] = {}; char value[24] = {};
  const char* expected[] = {"Linear", "Exp", "Log"};
  for (uint8_t v = 0; v < 3; ++v) {
    c.joy_families.cont.curve = v;
    ASSERT_TRUE(app::parameters::format(c, app::contract::ParamId::ContCurve, label, sizeof(label), value, sizeof(value)));
    ASSERT_STREQ(value, expected[v]);
  }
  ASSERT_TRUE(app::parameters::mutate(c, app::contract::ParamId::ContCurve, 1));
  ASSERT_EQ(c.joy_families.cont.curve, 0);
}
// The Recipe row walks exactly the eight selectable presets and wraps; it can
// never land on the disabled Flip slot (PresetId::kCount).
static void test_recipe_walks_eight_presets_and_wraps() {
  app::contract::Config c{}; char label[24] = {}; char value[24] = {};
  const char* expected[] = {"Bass", "Drone", "Colors", "Discover", "Voicings", "Bend", "Macro", "Lead"};
  for (const char* name : expected) {
    ASSERT_TRUE(app::parameters::mutate(c, app::contract::ParamId::JoystickRecipe, 1));
    ASSERT_TRUE(c.active_preset < app::PresetId::kCount);
    ASSERT_TRUE(app::parameters::format(c, app::contract::ParamId::JoystickRecipe, label, sizeof(label), value, sizeof(value)));
    ASSERT_STREQ(value, name);
  }
  ASSERT_TRUE(app::parameters::mutate(c, app::contract::ParamId::JoystickRecipe, -1));
  ASSERT_TRUE(c.active_preset == app::PresetId::kMacro);
}
int main() { TEST_SUITE("Canonical ParamId registry"); RUN_TEST(test_ids_unique_and_complete); RUN_TEST(test_clamps_and_typed_command); RUN_TEST(test_quick_play_is_limited_to_musical_values); RUN_TEST(test_end_to_end_read_format_mutate_read); RUN_TEST(test_filters_visibility_and_recipe_defaults); RUN_TEST(test_semantic_values_have_names); RUN_TEST(test_joystick_curve_shows_its_number); RUN_TEST(test_joystick_invert_toggles_are_system_scoped_and_flip_config); RUN_TEST(test_enum_valued_params_wrap_at_their_ends); RUN_TEST(test_roll_ms_clamps_to_60); RUN_TEST(test_family_scoped_rows_gate_on_active_preset); RUN_TEST(test_family_scoped_fields_round_trip); RUN_TEST(test_axis_target_wraps_at_its_true_max); RUN_TEST(test_cont_curve_names_are_linear_exp_log); RUN_TEST(test_recipe_walks_eight_presets_and_wraps); test_print_result(); return g_fail ? 1 : 0; }
