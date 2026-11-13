// Tests: AppState::sanitize() — every Settings field is clamped on load.
// Verifies that a corrupted NVS blob never produces out-of-range values.
#include "../framework.h"
#include "app/state.h"
#include <cstring>

static app::AppState make_corrupt() {
  app::AppState as;
  auto& s = as.settings();
  // Set magic so sanitize() doesn't reset to defaults.
  s.magic   = 0x4F334348;
  s.version = 1;
  s.size    = sizeof(s);
  // Corrupt every numeric field to an extreme value.
  s.bpm                  = 9999.0f;
  s.midi_channel         = 200;
  s.midi_channel_joy     = 200;
  s.velocity             = 0;
  s.note_length_ms       = 0;
  s.strum_ms             = 255;
  s.humanize_ms          = 255;
  s.arp_rate             = 99;
  s.arp_pattern          = 99;
  s.arp_octaves          = 0;
  s.arp_gate_percent     = 0;
  s.strum_rhythm         = 99;
  s.clock_mode           = 99;
  s.joystick_rotation    = 99;
  s.solo_velocity        = 0;
  s.solo_note_length_ms  = 0;
  s.voice_leading        = 99;
  s.voicing_spread       = 99;
  s.swing_percent        = 200;
  s.velocity_random      = 200;
  s.accent_pattern       = 99;
  s.nashville_display    = 99;
  s.voice_leading_complexity = 200;
  static_cast<uint8_t&>(reinterpret_cast<uint8_t*>(&s.play_trigger)[0]) = 99;
  static_cast<uint8_t&>(reinterpret_cast<uint8_t*>(&s.mode)[0])         = 99;
  return as;
}

// ---------------------------------------------------------------------------

static void test_bpm_clamped_in_range() {
  auto as = make_corrupt(); as.sanitize();
  ASSERT_TRUE(as.settings().bpm >= 20.0f && as.settings().bpm <= 300.0f);
}
static void test_midi_channels_clamped_0_to_15() {
  auto as = make_corrupt(); as.sanitize();
  ASSERT_TRUE(as.settings().midi_channel     <= 15);
  ASSERT_TRUE(as.settings().midi_channel_joy <= 15);
}
static void test_velocity_clamped_1_to_127() {
  auto as = make_corrupt(); as.sanitize();
  ASSERT_TRUE(as.settings().velocity >= 1 && as.settings().velocity <= 127);
}
static void test_note_length_ms_clamped() {
  auto as = make_corrupt(); as.sanitize();
  ASSERT_TRUE(as.settings().note_length_ms >= 20 && as.settings().note_length_ms <= 4000);
}
static void test_strum_ms_clamped() {
  auto as = make_corrupt(); as.sanitize();
  ASSERT_TRUE(as.settings().strum_ms <= 240);
}
static void test_humanize_ms_clamped() {
  auto as = make_corrupt(); as.sanitize();
  ASSERT_TRUE(as.settings().humanize_ms <= 40);
}
static void test_arp_rate_clamped() {
  auto as = make_corrupt(); as.sanitize();
  ASSERT_TRUE(as.settings().arp_rate <= 3);
}
static void test_arp_pattern_clamped() {
  auto as = make_corrupt(); as.sanitize();
  ASSERT_TRUE(as.settings().arp_pattern <= 6);
}
static void test_arp_octaves_clamped_1_to_4() {
  auto as = make_corrupt(); as.sanitize();
  ASSERT_TRUE(as.settings().arp_octaves >= 1 && as.settings().arp_octaves <= 4);
}
static void test_arp_gate_percent_clamped_1_to_100() {
  auto as = make_corrupt(); as.sanitize();
  ASSERT_TRUE(as.settings().arp_gate_percent >= 1 && as.settings().arp_gate_percent <= 100);
}
static void test_clock_mode_clamped_0_to_2() {
  auto as = make_corrupt(); as.sanitize();
  ASSERT_TRUE(as.settings().clock_mode <= 2);
}
static void test_joystick_rotation_clamped_0_to_3() {
  auto as = make_corrupt(); as.sanitize();
  ASSERT_TRUE(as.settings().joystick_rotation <= 3);
}
static void test_solo_velocity_clamped_1_to_127() {
  auto as = make_corrupt(); as.sanitize();
  ASSERT_TRUE(as.settings().solo_velocity >= 1 && as.settings().solo_velocity <= 127);
}
static void test_voice_leading_clamped_0_to_2() {
  auto as = make_corrupt(); as.sanitize();
  ASSERT_TRUE(as.settings().voice_leading <= 2);
}
static void test_voicing_spread_clamped_0_to_7() {
  auto as = make_corrupt(); as.sanitize();
  ASSERT_TRUE(as.settings().voicing_spread <= 7);
}
static void test_swing_percent_clamped_0_to_100() {
  auto as = make_corrupt(); as.sanitize();
  ASSERT_TRUE(as.settings().swing_percent <= 100);
}
static void test_velocity_random_clamped_0_to_30() {
  auto as = make_corrupt(); as.sanitize();
  ASSERT_TRUE(as.settings().velocity_random <= 30);
}
static void test_accent_pattern_clamped_0_to_3() {
  auto as = make_corrupt(); as.sanitize();
  ASSERT_TRUE(as.settings().accent_pattern <= 3);
}
static void test_nashville_display_clamped_0_to_1() {
  auto as = make_corrupt(); as.sanitize();
  ASSERT_TRUE(as.settings().nashville_display <= 1);
}
static void test_performance_mode_clamped() {
  auto as = make_corrupt(); as.sanitize();
  ASSERT_TRUE(static_cast<uint8_t>(as.settings().mode) <= 4);
}
static void test_bad_magic_resets_to_defaults() {
  app::AppState as;
  as.settings().magic = 0xDEADBEEF;
  as.settings().bpm   = 9999.0f;
  as.sanitize();
  // After bad magic, entire struct is reset to defaults.
  ASSERT_TRUE(as.settings().bpm >= 20.0f && as.settings().bpm <= 300.0f);
  ASSERT_EQ(as.settings().magic, 0x4F334348u);
}
static void test_valid_settings_unchanged_after_sanitize() {
  app::AppState as;
  as.settings().magic   = 0x4F334348;
  as.settings().version = 1;
  as.settings().size    = sizeof(as.settings());
  as.settings().bpm     = 140.0f;
  as.settings().midi_channel = 7;
  as.settings().velocity = 90;
  as.sanitize();
  ASSERT_EQ((int)as.settings().bpm, 140);
  ASSERT_EQ(as.settings().midi_channel, 7);
  ASSERT_EQ(as.settings().velocity, 90);
}

// ---------------------------------------------------------------------------
// Additional gap coverage: fields present in make_corrupt() but previously
// not tested against sanitize() output.
// ---------------------------------------------------------------------------

static void test_voice_leading_complexity_clamped_0_to_100() {
  // voice_leading_complexity: 0=smoothest, 100=most contrasting.
  // A corrupted blob with value 200 must be clamped on load.
  auto as = make_corrupt();
  as.sanitize();
  ASSERT_LE(as.settings().voice_leading_complexity, 100);
}


static void test_play_trigger_clamped_below_kCOUNT() {
  auto as = make_corrupt();
  as.sanitize();
  ASSERT_LT(static_cast<uint8_t>(as.settings().play_trigger),
            static_cast<uint8_t>(app::PlayTrigger::kCOUNT));
}


static void test_solo_note_length_ms_clamped() {
  auto as = make_corrupt();
  as.sanitize();
  ASSERT_GE(as.settings().solo_note_length_ms, 20);
  ASSERT_LE(as.settings().solo_note_length_ms, 4000);
}

static void test_size_and_version_coherent_after_sanitize() {
  // sanitize() sets version=1 and size=sizeof(Settings).
  auto as = make_corrupt();
  as.sanitize();
  ASSERT_EQ(as.settings().version, 1);
  ASSERT_EQ(as.settings().size, sizeof(app::Settings));
}

int main() {
  TEST_SUITE("AppState::sanitize() — all fields clamped");
  RUN_TEST(test_bpm_clamped_in_range);
  RUN_TEST(test_midi_channels_clamped_0_to_15);
  RUN_TEST(test_velocity_clamped_1_to_127);
  RUN_TEST(test_note_length_ms_clamped);
  RUN_TEST(test_strum_ms_clamped);
  RUN_TEST(test_humanize_ms_clamped);
  RUN_TEST(test_arp_rate_clamped);
  RUN_TEST(test_arp_pattern_clamped);
  RUN_TEST(test_arp_octaves_clamped_1_to_4);
  RUN_TEST(test_arp_gate_percent_clamped_1_to_100);
  RUN_TEST(test_clock_mode_clamped_0_to_2);
  RUN_TEST(test_joystick_rotation_clamped_0_to_3);
  RUN_TEST(test_solo_velocity_clamped_1_to_127);
  RUN_TEST(test_voice_leading_clamped_0_to_2);
  RUN_TEST(test_voicing_spread_clamped_0_to_7);
  RUN_TEST(test_swing_percent_clamped_0_to_100);
  RUN_TEST(test_velocity_random_clamped_0_to_30);
  RUN_TEST(test_accent_pattern_clamped_0_to_3);
  RUN_TEST(test_nashville_display_clamped_0_to_1);
  RUN_TEST(test_performance_mode_clamped);
  RUN_TEST(test_bad_magic_resets_to_defaults);
  RUN_TEST(test_valid_settings_unchanged_after_sanitize);
  RUN_TEST(test_voice_leading_complexity_clamped_0_to_100);
  RUN_TEST(test_play_trigger_clamped_below_kCOUNT);
  RUN_TEST(test_solo_note_length_ms_clamped);
  RUN_TEST(test_size_and_version_coherent_after_sanitize);
  test_print_result();
  return (g_fail > 0) ? 1 : 0;
}
