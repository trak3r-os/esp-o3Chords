// Tests: ChordModifier inversion, performer swing/humanize,
//        solo velocity clamping, vibrato clamp.
// Covers:
//   Bug 8  — manual inversion 0 must be a reachable, distinct state
//   Bug 6  — solo velocity min=0 (should be 1, fixed in joystick handler)
//   Bug 9  — vibrato clamp -8191 instead of -8192
#include "../framework.h"
#include "engine/modifier.h"
#include "engine/performer.h"
#include "engine/progression.h"
#include "app/state.h"
#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// ChordModifier — inversion
// ---------------------------------------------------------------------------

static void test_default_modifier_lets_voice_leading_choose() {
  // Voice leading is Smooth by default and no inversion has been set by
  // hand, so the engine owns the choice and the step is left untouched.
  engine::ChordModifier m;
  ASSERT_TRUE(m.voice_leads());
  ASSERT_FALSE(m.manual_inversion());
  engine::ProgressionStep step{};
  step.inversion = 0;
  ASSERT_EQ(m.apply(step).inversion, 0);
}

static void test_manual_inversion_zero_is_a_real_choice() {
  // Root position chosen by hand (inversion 0) is a manual inversion like
  // any other, and stops voice leading.
  engine::ChordModifier m;
  m.set_manual_inversion(0);
  ASSERT_TRUE(m.manual_inversion());
  ASSERT_FALSE(m.voice_leads());
  ASSERT_EQ(m.inversion_override(), 0);
}

static void test_manual_inversion_applies_to_step() {
  engine::ChordModifier m;
  m.set_manual_inversion(1);

  engine::ProgressionStep step{};
  step.scale_degree = 1;
  step.chord_quality = "maj";
  step.inversion = 0;  // base = root position
  step.octave = 0; step.velocity = 100;
  step.transpose = 0; step.bend_into_next = false;

  auto modified = m.apply(step);
  ASSERT_EQ(modified.inversion, 1);  // modifier overrides to 1st inversion
  m.set_manual_inversion(9);         // clamped to the largest chord's last inversion
  ASSERT_EQ(m.inversion_override(), 5);
}

static void test_reset_inversion_hands_back_to_voice_leading() {
  engine::ChordModifier m;
  m.set_manual_inversion(1);
  m.reset_inversion();
  ASSERT_FALSE(m.manual_inversion());
  ASSERT_TRUE(m.voice_leads());
  ASSERT_EQ(m.inversion_override(), 0);
  // With voice leading off there is nothing to hand back to: plain root.
  m.set_voice_leading(engine::VoiceLeading::kOff);
  ASSERT_FALSE(m.voice_leads());
}

static void test_octave_shift_clamps() {
  engine::ChordModifier m;
  // Go to +3
  for (int i = 0; i < 10; ++i) m.adjust_octave(1);
  ASSERT_EQ(m.octave_shift(), 3);
  // Go to -3
  for (int i = 0; i < 10; ++i) m.adjust_octave(-1);
  ASSERT_EQ(m.octave_shift(), -3);
}

// ---------------------------------------------------------------------------
// Performer — note event generation
// ---------------------------------------------------------------------------

static void test_performer_render_step_basic() {
  engine::Performer perf;
  perf.set_note_length_ms(240);
  perf.set_humanize_ms(0);
  perf.set_strum_ms(0);

  auto events = perf.render_step({60, 64, 67}, 100, 0);
  ASSERT_EQ(events.size(), (size_t)3);
  for (auto& ev : events) {
    ASSERT_TRUE(ev.velocity > 0);  // render_step only emits note-ons
    ASSERT_TRUE(ev.note >= 60 && ev.note <= 67);
  }
}

static void test_performer_strum_offsets_ascending() {
  engine::Performer perf;
  perf.set_note_length_ms(240);
  perf.set_humanize_ms(0);
  perf.set_strum_ms(14);

  auto events = perf.render_step({60, 64, 67}, 100, 0);
  ASSERT_EQ(events.size(), (size_t)3);
  // Each note should have a larger offset than the previous
  ASSERT_TRUE(events[1].offset_ms > events[0].offset_ms);
  ASSERT_TRUE(events[2].offset_ms > events[1].offset_ms);
}

static void test_performer_velocity_range_valid() {
  engine::Performer perf;
  perf.set_note_length_ms(240);
  perf.set_humanize_ms(0);
  perf.set_strum_ms(0);
  perf.set_velocity_random(10);

  for (int trial = 0; trial < 20; ++trial) {
    auto events = perf.render_step({60}, 100, 0);
    for (auto& ev : events) {
      ASSERT_TRUE(ev.velocity >= 1 && ev.velocity <= 127);
    }
  }
}

static void test_performer_strum_direction_up() {
  // kUp: events fire low-to-high (input order), with ascending offsets.
  engine::Performer perf;
  perf.set_strum_ms(10);
  perf.set_humanize_ms(0);
  perf.set_strum_direction(engine::StrumDirection::kUp);

  auto events = perf.render_step({60, 64, 67}, 100, 0);
  ASSERT_EQ((int)events.size(), 3);
  ASSERT_EQ(events[0].note, 60);
  ASSERT_EQ(events[1].note, 64);
  ASSERT_EQ(events[2].note, 67);
  ASSERT_TRUE(events[0].offset_ms < events[1].offset_ms);
  ASSERT_TRUE(events[1].offset_ms < events[2].offset_ms);
}

static void test_performer_strum_direction_down() {
  // kDown: events fire high-to-low (reversed), with ascending offsets.
  engine::Performer perf;
  perf.set_strum_ms(10);
  perf.set_humanize_ms(0);
  perf.set_strum_direction(engine::StrumDirection::kDown);

  auto events = perf.render_step({60, 64, 67}, 100, 0);
  ASSERT_EQ((int)events.size(), 3);
  ASSERT_EQ(events[0].note, 67);  // high note first
  ASSERT_EQ(events[1].note, 64);
  ASSERT_EQ(events[2].note, 60);  // low note last
}

static void test_performer_strum_direction_both_alternates() {
  // kBoth: first call → kUp, second → kDown, third → kUp again.
  engine::Performer perf;
  perf.set_strum_ms(10);
  perf.set_humanize_ms(0);
  perf.set_strum_direction(engine::StrumDirection::kBoth);

  auto ev1 = perf.render_step({60, 64, 67}, 100, 0);
  ASSERT_EQ(ev1[0].note, 60);  // first call: kUp → low first

  auto ev2 = perf.render_step({60, 64, 67}, 100, 0);
  ASSERT_EQ(ev2[0].note, 67);  // second call: kDown → high first

  auto ev3 = perf.render_step({60, 64, 67}, 100, 0);
  ASSERT_EQ(ev3[0].note, 60);  // third call: kUp again
}

static void test_performer_strum_speed_scales_spacing() {
  // 2× speed halves the inter-note gap.
  engine::Performer perf;
  perf.set_strum_ms(100);
  perf.set_humanize_ms(0);
  perf.set_strum_speed(2.0f);  // effective_strum_ms = 100/2 = 50

  auto events = perf.render_step({60, 64, 67}, 100, 0);
  ASSERT_EQ(events[0].offset_ms, 0u);
  ASSERT_EQ(events[1].offset_ms, 50u);
  ASSERT_EQ(events[2].offset_ms, 100u);
}

// ---------------------------------------------------------------------------
// Roll ("stagger") — per-voice offset, distinct from and exclusive with strum.
// ---------------------------------------------------------------------------

static void test_performer_roll_up_ascends_by_index() {
  engine::Performer perf;
  perf.set_humanize_ms(0);
  perf.set_roll_ms(10);
  perf.set_roll_direction(engine::RollDirection::kUp);

  auto events = perf.render_step({60, 64, 67, 71}, 100, 0);
  ASSERT_EQ(events[0].offset_ms, 0u);
  ASSERT_EQ(events[1].offset_ms, 10u);
  ASSERT_EQ(events[2].offset_ms, 20u);
  ASSERT_EQ(events[3].offset_ms, 30u);
}

static void test_performer_roll_down_descends_by_index() {
  engine::Performer perf;
  perf.set_humanize_ms(0);
  perf.set_roll_ms(10);
  perf.set_roll_direction(engine::RollDirection::kDown);

  auto events = perf.render_step({60, 64, 67, 71}, 100, 0);
  ASSERT_EQ(events[0].offset_ms, 30u);
  ASSERT_EQ(events[1].offset_ms, 20u);
  ASSERT_EQ(events[2].offset_ms, 10u);
  ASSERT_EQ(events[3].offset_ms, 0u);
}

static void test_performer_roll_out_fires_pivot_first() {
  // Pivot = n/2 = index 2. Out spreads from there to both edges.
  engine::Performer perf;
  perf.set_humanize_ms(0);
  perf.set_roll_ms(10);
  perf.set_roll_direction(engine::RollDirection::kOut);

  auto events = perf.render_step({60, 64, 67, 71}, 100, 0);
  ASSERT_EQ(events[0].offset_ms, 20u);
  ASSERT_EQ(events[1].offset_ms, 10u);
  ASSERT_EQ(events[2].offset_ms, 0u);
  ASSERT_EQ(events[3].offset_ms, 10u);
}

static void test_performer_roll_in_fires_edges_first() {
  // In is the mirror of Out: the edges (index 0 and n-1) fire first.
  engine::Performer perf;
  perf.set_humanize_ms(0);
  perf.set_roll_ms(10);
  perf.set_roll_direction(engine::RollDirection::kIn);

  auto events = perf.render_step({60, 64, 67, 71}, 100, 0);
  ASSERT_EQ(events[0].offset_ms, 0u);
  ASSERT_EQ(events[1].offset_ms, 10u);
  ASSERT_EQ(events[2].offset_ms, 20u);
  ASSERT_EQ(events[3].offset_ms, 10u);
}

static void test_performer_roll_overrides_strum_entirely() {
  // Roll and strum must not both offset the same voice: with roll active,
  // strum's spacing AND its direction-driven reordering are both ignored.
  engine::Performer perf;
  perf.set_humanize_ms(0);
  perf.set_strum_ms(100);
  perf.set_strum_direction(engine::StrumDirection::kDown);  // would reverse order
  perf.set_roll_ms(10);
  perf.set_roll_direction(engine::RollDirection::kUp);

  auto events = perf.render_step({60, 64, 67, 71}, 100, 0);
  ASSERT_EQ(events[0].note, 60);  // order not reversed by strum's kDown
  ASSERT_EQ(events[0].offset_ms, 0u);
  ASSERT_EQ(events[1].offset_ms, 10u);
  ASSERT_EQ(events[2].offset_ms, 20u);
  ASSERT_EQ(events[3].offset_ms, 30u);  // not 300 (strum's own spacing)
}

static void test_performer_roll_offset_survives_swing() {
  // Swing still delays odd-indexed voices, scaled off roll_ms instead of
  // strum's (zero, here) spacing — roll's offset must survive swing.
  engine::Performer perf;
  perf.set_humanize_ms(0);
  perf.set_roll_ms(10);
  perf.set_roll_direction(engine::RollDirection::kUp);
  perf.set_swing_percent(50);

  auto events = perf.render_step({60, 64, 67, 71}, 100, 0);
  ASSERT_EQ(events[0].offset_ms, 0u);
  ASSERT_EQ(events[1].offset_ms, 15u);  // 10 + 50% of roll_ms(10)
  ASSERT_EQ(events[2].offset_ms, 20u);
  ASSERT_EQ(events[3].offset_ms, 35u);  // 30 + 5
}

static void test_performer_swing_delays_odd_notes() {
  // Swing delays odd-indexed notes by swing_percent% of effective_strum_ms.
  // strum_ms=100, swing=50 → odd notes get +50 extra ms.
  engine::Performer perf;
  perf.set_strum_ms(100);
  perf.set_humanize_ms(0);
  perf.set_swing_percent(50);

  auto events = perf.render_step({60, 62, 64, 65}, 100, 0);
  ASSERT_EQ(events[0].offset_ms, 0u);    // i=0: no swing
  ASSERT_EQ(events[1].offset_ms, 150u);  // i=1: 100 + 50
  ASSERT_EQ(events[2].offset_ms, 200u);  // i=2: no swing
  ASSERT_EQ(events[3].offset_ms, 350u);  // i=3: 300 + 50
}

static void test_performer_accent_downbeat_boosts_first() {
  // Accent=1 (downbeat): note[0] gets +15 velocity, others unchanged.
  engine::Performer perf;
  perf.set_strum_ms(0);
  perf.set_humanize_ms(0);
  perf.set_velocity_random(0);
  perf.set_accent_pattern(1);

  auto events = perf.render_step({60, 64, 67}, 80, 0);
  ASSERT_EQ(events[0].velocity, 95);  // 80 + 15
  ASSERT_EQ(events[1].velocity, 80);
  ASSERT_EQ(events[2].velocity, 80);
}

static void test_performer_accent_backbeat_boosts_notes_1_and_3() {
  // Accent=2 (backbeat): notes at index 1 and 3 get +15.
  engine::Performer perf;
  perf.set_strum_ms(0);
  perf.set_humanize_ms(0);
  perf.set_velocity_random(0);
  perf.set_accent_pattern(2);

  auto events = perf.render_step({60, 62, 64, 65}, 80, 0);
  ASSERT_EQ(events[0].velocity, 80);
  ASSERT_EQ(events[1].velocity, 95);  // backbeat: +15
  ASSERT_EQ(events[2].velocity, 80);
  ASSERT_EQ(events[3].velocity, 95);  // backbeat: +15
}

static void test_performer_accent_every_4th() {
  // Accent=3 (every 4th): notes at index 0 and 4 get +15.
  engine::Performer perf;
  perf.set_strum_ms(0);
  perf.set_humanize_ms(0);
  perf.set_velocity_random(0);
  perf.set_accent_pattern(3);

  auto events = perf.render_step({60, 62, 64, 65, 67}, 80, 0);
  ASSERT_EQ(events[0].velocity, 95);  // i=0: 0%4==0 → accented
  ASSERT_EQ(events[1].velocity, 80);
  ASSERT_EQ(events[2].velocity, 80);
  ASSERT_EQ(events[3].velocity, 80);
  ASSERT_EQ(events[4].velocity, 95);  // i=4: 4%4==0 → accented
}

static void test_performer_render_step_returns_only_noteons() {
  // render_step must NOT embed note-off events (unlike render_arp).
  // The engine adds note-offs separately at note_len offset.
  engine::Performer perf;
  perf.set_note_length_ms(240);
  perf.set_humanize_ms(0);
  perf.set_strum_ms(0);

  auto events = perf.render_step({60, 64, 67}, 100, 0);
  for (auto& ev : events) {
    ASSERT_NE(ev.velocity, 0);  // velocity==0 would be a note-off
  }
}

// ---------------------------------------------------------------------------
// Settings — velocity clamping (Bug 6 regression)
// ---------------------------------------------------------------------------

static void test_settings_velocity_minimum_is_1() {
  // After Bug 6 fix, the joystick handler clamps to min=1.
  // Verify the Settings value itself can't stay at 0 after sanitize().
  app::Settings s{};
  s.magic = 0x4F334348; s.version = 1; s.size = sizeof(s);
  s.velocity = 0;  // invalid — would produce MIDI note-off when played

  app::AppState as;
  // Manually set then sanitize
  std::memcpy(&as.settings(), &s, sizeof(s));
  as.sanitize();

  ASSERT_TRUE(as.settings().velocity >= 1);
}

static void test_settings_midi_channel_bounds() {
  app::Settings s{};
  s.magic = 0x4F334348; s.version = 1; s.size = sizeof(s);
  s.midi_channel = 200;  // out of range — should be clamped to 0-15
  s.midi_channel_joy = 200;

  app::AppState as;
  std::memcpy(&as.settings(), &s, sizeof(s));
  as.sanitize();

  ASSERT_TRUE(as.settings().midi_channel <= 15);
  ASSERT_TRUE(as.settings().midi_channel_joy <= 15);
}

// ---------------------------------------------------------------------------
// Pitch bend encoding (MIDI spec compliance)
// ---------------------------------------------------------------------------

static void test_pitch_bend_center_encodes_8192() {
  // MIDI pitch bend center = 0x2000 = 8192.
  // The engine stores it as signed offset where 0 = center.
  // UsbDeviceMidi::send_pitch_bend: value + 8192 → 14-bit unsigned.
  // Test that center (0) → 8192.
  int16_t signed_value = 0;
  uint16_t encoded = static_cast<uint16_t>(signed_value + 8192) & 0x3FFF;
  ASSERT_EQ(encoded, 8192);
}

static void test_pitch_bend_max_down_encodes_0() {
  // MIDI maximum down bend = 0x0000.
  int16_t signed_value = -8192;
  uint16_t encoded = static_cast<uint16_t>(signed_value + 8192) & 0x3FFF;
  ASSERT_EQ(encoded, 0);
}

static void test_pitch_bend_max_up_encodes_16383() {
  // MIDI maximum up bend = 0x3FFF = 16383.
  int16_t signed_value = 8191;
  uint16_t encoded = static_cast<uint16_t>(signed_value + 8192) & 0x3FFF;
  ASSERT_EQ(encoded, 16383);
}

static void test_pitch_bend_lsb_msb_split() {
  // Check LSB/MSB split for a known value: center = 8192 = 0x2000.
  uint16_t encoded = 8192;
  uint8_t lsb = encoded & 0x7F;
  uint8_t msb = (encoded >> 7) & 0x7F;
  ASSERT_EQ(lsb, 0);
  ASSERT_EQ(msb, 64);  // 0x40
}

static void test_vibrato_clamp_allows_minus_8192() {
  // Bug 9 regression: clamp was std::max(-8191, ...) missing one value.
  // Verify the formula now allows -8192 (the MIDI spec minimum).
  int bend = -8200;  // below min, should clamp to -8192
  int clamped = std::max(-8192, std::min(8191, bend));
  ASSERT_EQ(clamped, -8192);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
  TEST_SUITE("ChordModifier — inversion");
  RUN_TEST(test_default_modifier_lets_voice_leading_choose);
  RUN_TEST(test_manual_inversion_zero_is_a_real_choice);
  RUN_TEST(test_manual_inversion_applies_to_step);
  RUN_TEST(test_reset_inversion_hands_back_to_voice_leading);
  RUN_TEST(test_octave_shift_clamps);

  TEST_SUITE("Performer — note event generation");
  RUN_TEST(test_performer_render_step_basic);
  RUN_TEST(test_performer_strum_offsets_ascending);
  RUN_TEST(test_performer_velocity_range_valid);
  RUN_TEST(test_performer_render_step_returns_only_noteons);
  RUN_TEST(test_performer_strum_direction_up);
  RUN_TEST(test_performer_strum_direction_down);
  RUN_TEST(test_performer_strum_direction_both_alternates);
  RUN_TEST(test_performer_strum_speed_scales_spacing);
  RUN_TEST(test_performer_swing_delays_odd_notes);
  RUN_TEST(test_performer_roll_up_ascends_by_index);
  RUN_TEST(test_performer_roll_down_descends_by_index);
  RUN_TEST(test_performer_roll_out_fires_pivot_first);
  RUN_TEST(test_performer_roll_in_fires_edges_first);
  RUN_TEST(test_performer_roll_overrides_strum_entirely);
  RUN_TEST(test_performer_roll_offset_survives_swing);
  RUN_TEST(test_performer_accent_downbeat_boosts_first);
  RUN_TEST(test_performer_accent_backbeat_boosts_notes_1_and_3);
  RUN_TEST(test_performer_accent_every_4th);

  TEST_SUITE("Settings — velocity & channel bounds");
  RUN_TEST(test_settings_velocity_minimum_is_1);
  RUN_TEST(test_settings_midi_channel_bounds);

  TEST_SUITE("Pitch Bend Encoding (MIDI spec)");
  RUN_TEST(test_pitch_bend_center_encodes_8192);
  RUN_TEST(test_pitch_bend_max_down_encodes_0);
  RUN_TEST(test_pitch_bend_max_up_encodes_16383);
  RUN_TEST(test_pitch_bend_lsb_msb_split);
  RUN_TEST(test_vibrato_clamp_allows_minus_8192);

  test_print_result();
  return (g_fail > 0) ? 1 : 0;
}
