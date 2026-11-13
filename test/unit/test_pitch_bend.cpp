// Tests: engine::bend_from_x() and engine::quantize_bend_to_chord().
//
// bend_from_x maps a joystick X axis value (with deadzone) to a signed MIDI
// pitch bend in [-8192, 8191].
//
// quantize_bend_to_chord snaps a raw bend to the nearest chord tone across
// octave extensions.  Given intervals (e.g. {0,4,7} for major) and
// bend_range semitones, it finds the closest tone to the raw bend target
// and returns the MIDI bend value that lands exactly on that tone.

#include "../framework.h"
#include "engine/pitch_bend.h"
#include <vector>

// ---------------------------------------------------------------------------
// bend_from_x
// ---------------------------------------------------------------------------

static void test_bend_from_x_zero_input() {
  ASSERT_EQ(engine::bend_from_x(0, 100), 0);
}

static void test_bend_from_x_within_deadzone() {
  // |x| <= deadzone → 0
  ASSERT_EQ(engine::bend_from_x(50, 100),  0);
  ASSERT_EQ(engine::bend_from_x(-50, 100), 0);
  ASSERT_EQ(engine::bend_from_x(100, 100), 0);  // at boundary: |100| <= 100
}

static void test_bend_from_x_just_outside_deadzone() {
  // |x| > deadzone → x*4
  int16_t v = engine::bend_from_x(101, 100);
  ASSERT_TRUE(v > 0);
  ASSERT_EQ(v, 101 * 4);
}

static void test_bend_from_x_clamped_positive() {
  // x*4 > 8191 → clamp to 8191
  ASSERT_EQ(engine::bend_from_x(2048, 0), 8191);  // 2048*4 = 8192 → 8191
  ASSERT_EQ(engine::bend_from_x(2049, 0), 8191);  // 2049*4 = 8196 → 8191
}

static void test_bend_from_x_clamped_negative() {
  // x*4 < -8192 → clamp to -8192
  ASSERT_EQ(engine::bend_from_x(-2048, 0), -8192);  // -2048*4 = -8192 (exact)
  ASSERT_EQ(engine::bend_from_x(-2049, 0), -8192);  // -2049*4 = -8196 → -8192
}

static void test_bend_from_x_linear_in_range() {
  // 100 * 4 = 400, well within range
  ASSERT_EQ(engine::bend_from_x(100, 0), 400);
  ASSERT_EQ(engine::bend_from_x(-100, 0), -400);
}

// ---------------------------------------------------------------------------
// quantize_bend_to_chord
// ---------------------------------------------------------------------------

static void test_quantize_bend_zero_returns_zero() {
  ASSERT_EQ(engine::quantize_bend_to_chord(0, {0, 4, 7}, 12, nullptr), 0);
}

static void test_quantize_bend_empty_intervals_returns_zero() {
  ASSERT_EQ(engine::quantize_bend_to_chord(4096, {}, 12, nullptr), 0);
}

static void test_quantize_bend_zero_range_returns_zero() {
  ASSERT_EQ(engine::quantize_bend_to_chord(4096, {0, 4, 7}, 0, nullptr), 0);
}

static void test_quantize_bend_snaps_out_snap_semitones() {
  // C major triad {0,4,7}, bend_range=12, raw=4096 → target = 6.0 st.
  // Nearest chord tone: 7 (fifth, dist=1) beats 4 (third, dist=2).
  // snap must equal 7.0.
  float snap = 0.0f;
  engine::quantize_bend_to_chord(4096, {0, 4, 7}, 12, &snap);
  ASSERT_EQ(static_cast<int>(snap), 7);
}

static void test_quantize_bend_full_up_snaps_to_octave() {
  // intervals={0,7}, bend_range=7, raw=8192 → target=7.0 st exactly.
  // tone at oct=0: {0,7}; 7 has dist=0 → wins.
  // bend = 7*8192/7 = 8192, clamped to 8191.
  float snap = 0.0f;
  int16_t result = engine::quantize_bend_to_chord(8192, {0, 7}, 7, &snap);
  ASSERT_EQ(static_cast<int>(snap), 7);
  ASSERT_EQ(result, 8191);
}

static void test_quantize_bend_negative_snaps_down() {
  // raw=-8192, intervals={0,4,7}, bend_range=12 → target=-12.0 st.
  // tone at oct=-1: {-12,-8,-5}. -12 has dist=0 → wins.
  // bend = -12*8192/12 = -8192.
  float snap = 0.0f;
  int16_t result = engine::quantize_bend_to_chord(-8192, {0, 4, 7}, 12, &snap);
  ASSERT_EQ(static_cast<int>(snap), -12);
  ASSERT_EQ(result, -8192);
}

static void test_quantize_bend_snaps_to_minor_third() {
  // intervals={0,3,7} (minor triad), bend_range=6, raw=4096 → target=3.0 st.
  // tone=3 (minor third) is exact: dist=0. Wins.
  // bend = 3*8192/6 = 4096.
  float snap = 0.0f;
  int16_t result = engine::quantize_bend_to_chord(4096, {0, 3, 7}, 6, &snap);
  ASSERT_EQ(static_cast<int>(snap), 3);
  ASSERT_EQ(result, 4096);
}

static void test_quantize_bend_null_snap_does_not_crash() {
  // out_snap_semitones may be nullptr — must not crash.
  engine::quantize_bend_to_chord(4096, {0, 4, 7}, 12, nullptr);
}

static void test_quantize_bend_result_within_midi_range() {
  // For any raw bend, output must stay in [-8192, 8191].
  const std::vector<int8_t> intervals = {0, 4, 7, 10};
  for (int16_t raw : {(int16_t)-8192, (int16_t)-4096, (int16_t)0,
                      (int16_t)4096, (int16_t)8191}) {
    int16_t result = engine::quantize_bend_to_chord(raw, intervals, 12, nullptr);
    ASSERT_TRUE(result >= -8192 && result <= 8191);
  }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
  TEST_SUITE("bend_from_x — joystick to pitch bend");
  RUN_TEST(test_bend_from_x_zero_input);
  RUN_TEST(test_bend_from_x_within_deadzone);
  RUN_TEST(test_bend_from_x_just_outside_deadzone);
  RUN_TEST(test_bend_from_x_clamped_positive);
  RUN_TEST(test_bend_from_x_clamped_negative);
  RUN_TEST(test_bend_from_x_linear_in_range);

  TEST_SUITE("quantize_bend_to_chord — chord-tone snap");
  RUN_TEST(test_quantize_bend_zero_returns_zero);
  RUN_TEST(test_quantize_bend_empty_intervals_returns_zero);
  RUN_TEST(test_quantize_bend_zero_range_returns_zero);
  RUN_TEST(test_quantize_bend_snaps_out_snap_semitones);
  RUN_TEST(test_quantize_bend_full_up_snaps_to_octave);
  RUN_TEST(test_quantize_bend_negative_snaps_down);
  RUN_TEST(test_quantize_bend_snaps_to_minor_third);
  RUN_TEST(test_quantize_bend_null_snap_does_not_crash);
  RUN_TEST(test_quantize_bend_result_within_midi_range);

  test_print_result();
  return (g_fail > 0) ? 1 : 0;
}
