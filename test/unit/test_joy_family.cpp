// Tests: the JoyFamily/preset data model: eight selectable presets in three
// families (NOTES, CHORD, CONT). Harmony folded into Drone; Arp/Feel/Dynamics
// and the PARAM-family Flip preset are no longer offered.
#include "../framework.h"
#include "app/contracts/contracts.h"
#include "app/joy_family.h"

using namespace app;

static void test_eight_presets_in_family_order() {
  size_t n = static_cast<size_t>(PresetId::kCount);
  ASSERT_EQ(n, (size_t)8);

  static constexpr JoyFamily kExpectedFamily[8] = {
      JoyFamily::kNotes, JoyFamily::kNotes, JoyFamily::kNotes,
      JoyFamily::kChord, JoyFamily::kChord, JoyFamily::kChord,
      JoyFamily::kCont, JoyFamily::kCont,
  };
  for (size_t i = 0; i < n; ++i) {
    ASSERT_TRUE(kPresets[i].family == kExpectedFamily[i]);
    ASSERT_TRUE(kPresets[i].name != nullptr);
    ASSERT_TRUE(static_cast<size_t>(kPresets[i].id) == i);
  }
}

static void test_preset_names_match_the_factory_list() {
  static constexpr const char* kExpectedName[8] = {
      "Lead", "Bass", "Drone",
      "Colors", "Discover", "Voicings",
      "Bend", "Macro",
  };
  for (size_t i = 0; i < 8; ++i) {
    ASSERT_STREQ(kPresets[i].name, kExpectedName[i]);
  }
}

// No selectable preset belongs to PARAM any more: Flip was its only member.
static void test_no_preset_belongs_to_the_param_family() {
  for (size_t i = 0; i < static_cast<size_t>(PresetId::kCount); ++i)
    ASSERT_TRUE(kPresets[i].family != JoyFamily::kParam);
}

static void test_lead_preset_matches_melody_sub_a() {
  auto s = preset_defaults(PresetId::kLead);
  ASSERT_TRUE(s.notes.source == NoteSource::kScale);
  ASSERT_TRUE(s.notes.gesture == NoteGesture::kWalk);
  ASSERT_TRUE(s.notes.voices == NoteVoices::kNote);
  ASSERT_TRUE(s.notes.sustain == NoteSustain::kMomentary);
  ASSERT_TRUE(s.notes.velocity_mode == NoteVelocityMode::kByDistance);
  ASSERT_TRUE(s.notes.channel == NoteChannel::kLead);
}

static void test_bass_preset_matches_bassclock() {
  auto s = preset_defaults(PresetId::kBass);
  ASSERT_TRUE(s.notes.source == NoteSource::kScale);
  ASSERT_TRUE(s.notes.gesture == NoteGesture::kClock);
  ASSERT_EQ(s.notes.octave, -2);
  ASSERT_TRUE(s.notes.mono);
}

static void test_drone_preset_matches_drone() {
  auto s = preset_defaults(PresetId::kDrone);
  ASSERT_TRUE(s.notes.source == NoteSource::kDegrees);
  ASSERT_TRUE(s.notes.gesture == NoteGesture::kClock);
  ASSERT_TRUE(s.notes.voices == NoteVoices::kChord);
  ASSERT_TRUE(s.notes.sustain == NoteSustain::kLatch);
}

static void test_apply_legacy_harmony_override_matches_old_harmony_preset() {
  // Harmony folded into Drone (config migration table): forcing
  // voices=note/mono=true/channel=lead on top of Drone's own defaults must
  // reproduce exactly what the old, now-deleted Harmony preset was.
  auto s = preset_defaults(PresetId::kDrone);
  apply_legacy_harmony_override(s.notes);
  ASSERT_TRUE(s.notes.source == NoteSource::kDegrees);  // unrelated to Harmony; Drone's own default survives
  ASSERT_TRUE(s.notes.gesture == NoteGesture::kClock);
  ASSERT_TRUE(s.notes.voices == NoteVoices::kNote);
  ASSERT_TRUE(s.notes.mono);
  ASSERT_TRUE(s.notes.sustain == NoteSustain::kLatch);
  ASSERT_TRUE(s.notes.channel == NoteChannel::kLead);
}

static void test_colors_preset_matches_quality() {
  auto s = preset_defaults(PresetId::kColors);
  ASSERT_EQ(s.chord.palette, 0u);  // CLSC
  ASSERT_TRUE(s.chord.order == ChordOrder::kFixed);
}

static void test_discover_preset_ranks_by_consonance() {
  auto s = preset_defaults(PresetId::kDiscover);
  ASSERT_TRUE(s.chord.order == ChordOrder::kRanking);
}

// Voicings has no field of its own left: a direction writes the Voicing
// parameter and nothing is retriggered, so its ChordParams are the plain ones.
static void test_voicings_preset_is_plain_chord_params() {
  auto s = preset_defaults(PresetId::kVoicings);
  ChordParams plain;
  ASSERT_EQ(s.chord.palette, plain.palette);
  ASSERT_TRUE(s.chord.order == ChordOrder::kFixed);
  ASSERT_FALSE(s.chord.retrigger);
}

static void test_bend_preset_matches_bend() {
  auto s = preset_defaults(PresetId::kBend);
  ASSERT_TRUE(s.cont.x_target == ContTarget::kBend);
  ASSERT_TRUE(s.cont.y_target == ContTarget::kVibrato);
  ASSERT_TRUE(s.cont.bend_snap == ContBendSnap::kChordTones);
}

static void test_macro_preset_sends_cc_both_axes() {
  auto s = preset_defaults(PresetId::kMacro);
  ASSERT_TRUE(s.cont.x_target == ContTarget::kCc);
  ASSERT_TRUE(s.cont.y_target == ContTarget::kCc);
}

// Flip is disabled: the name survives only as an alias of kCount, so it is
// out of range for kPresets and recalls nothing.
static void test_flip_is_a_disabled_alias_of_count() {
  ASSERT_TRUE(PresetId::kFlip == PresetId::kCount);
  JoyFamilyState fresh;
  auto s = preset_defaults(PresetId::kFlip);
  ASSERT_TRUE(s.param.up_down.target == fresh.param.up_down.target);
  ASSERT_TRUE(s.param.left_right.target == AxisTarget::kNone);
  ASSERT_TRUE(s.notes.source == fresh.notes.source);
  ASSERT_TRUE(s.cont.x_target == fresh.cont.x_target);
}

static void test_preset_defaults_only_touch_their_own_family() {
  // preset_defaults() returns a full JoyFamilyState; the other three
  // families in it must stay at their plain defaults, matching the
  // "editing one family never disturbs another" invariant this data model
  // exists to support.
  JoyFamilyState fresh;
  auto s = preset_defaults(PresetId::kBass);
  ASSERT_TRUE(s.chord.palette == fresh.chord.palette);
  ASSERT_TRUE(s.cont.x_target == fresh.cont.x_target);
  ASSERT_TRUE(s.param.up_down.target == fresh.param.up_down.target);
}

static void test_joystick_push_param_covers_every_preset() {
  contract::ParamId out;
  ASSERT_TRUE(joystick_push_param(PresetId::kLead, out)); ASSERT_TRUE(out == contract::ParamId::NotesSource);
  ASSERT_TRUE(joystick_push_param(PresetId::kBass, out)); ASSERT_TRUE(out == contract::ParamId::NotesSource);
  ASSERT_TRUE(joystick_push_param(PresetId::kDrone, out)); ASSERT_TRUE(out == contract::ParamId::NotesSource);
  ASSERT_TRUE(joystick_push_param(PresetId::kColors, out)); ASSERT_TRUE(out == contract::ParamId::ChordPalette);
  ASSERT_TRUE(joystick_push_param(PresetId::kDiscover, out)); ASSERT_TRUE(out == contract::ParamId::ChordPalette);
  ASSERT_FALSE(joystick_push_param(PresetId::kVoicings, out));
  ASSERT_FALSE(joystick_push_param(PresetId::kBend, out));
  ASSERT_FALSE(joystick_push_param(PresetId::kMacro, out));
  ASSERT_FALSE(joystick_push_param(PresetId::kFlip, out));
}

int main() {
  TEST_SUITE("JoyFamily — data model");
  RUN_TEST(test_eight_presets_in_family_order);
  RUN_TEST(test_no_preset_belongs_to_the_param_family);
  RUN_TEST(test_preset_names_match_the_factory_list);
  RUN_TEST(test_lead_preset_matches_melody_sub_a);
  RUN_TEST(test_bass_preset_matches_bassclock);
  RUN_TEST(test_drone_preset_matches_drone);
  RUN_TEST(test_apply_legacy_harmony_override_matches_old_harmony_preset);
  RUN_TEST(test_colors_preset_matches_quality);
  RUN_TEST(test_discover_preset_ranks_by_consonance);
  RUN_TEST(test_voicings_preset_is_plain_chord_params);
  RUN_TEST(test_bend_preset_matches_bend);
  RUN_TEST(test_macro_preset_sends_cc_both_axes);
  RUN_TEST(test_flip_is_a_disabled_alias_of_count);
  RUN_TEST(test_preset_defaults_only_touch_their_own_family);
  RUN_TEST(test_joystick_push_param_covers_every_preset);
  test_print_result();
  return g_fail ? 1 : 0;
}
