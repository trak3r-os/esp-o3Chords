#include "../framework.h"
#include "app/modulation_recipe.h"

using namespace app::modulation;

static void test_cc_defaults_and_overrides() {
  const auto custom = cc_pair_for_preset(0, 21, 22);
  ASSERT_EQ(custom.x, 21);
  ASSERT_EQ(custom.y, 22);
  ASSERT_EQ(cc_pair_for_preset(1, 21, 22).x, 1);
  ASSERT_EQ(cc_pair_for_preset(1, 21, 22).y, 11);
  ASSERT_EQ(cc_pair_for_preset(99, 21, 22).x, 21);
}

static void test_axis_mapping_and_curves() {
  ASSERT_EQ(axis_to_cc(0, 10), 63);
  ASSERT_EQ(axis_to_cc(512, 10), 127);
  ASSERT_EQ(axis_to_cc(5, 10), 63);
  ASSERT_TRUE(axis_to_cc(256, 0, Curve::Exponential) < axis_to_cc(256, 0));
  ASSERT_TRUE(axis_to_cc(256, 0, Curve::Logarithmic) > axis_to_cc(256, 0));
  ASSERT_EQ(axis_to_bend(0, 10), 0);
  // Full deflection is the full 14-bit bend range, whatever the deadzone.
  ASSERT_EQ(axis_to_bend(512, 10), 8191);
  ASSERT_EQ(axis_to_bend(-512, 10), -8192);
}

// A "Deadzone %" setting in axis units: percent of full scale, capped at 90%
// so some travel always remains.
static void test_deadzone_units_from_percent() {
  ASSERT_EQ(deadzone_units(0), 0);
  ASSERT_EQ(deadzone_units(10), 51);
  ASSERT_EQ(deadzone_units(50), 256);
  ASSERT_EQ(deadzone_units(90), 460);
  ASSERT_EQ(deadzone_units(100), 460);
  ASSERT_EQ(deadzone_units(255), 460);
}

// Inside the deadzone the output is neutral; past it the remaining travel is
// rescaled to the full range, so the first step out is one unit, not a jump
// of the deadzone's width, and the end stop is still full scale.
static void test_apply_deadzone_rescales_remaining_travel() {
  for (int16_t dz : {0, 51, 128, 256, 460}) {
    ASSERT_EQ(apply_deadzone(0, dz), 0);
    ASSERT_EQ(apply_deadzone(dz, dz), 0);
    ASSERT_EQ(apply_deadzone(static_cast<int16_t>(-dz), dz), 0);
    ASSERT_TRUE(apply_deadzone(static_cast<int16_t>(dz + 1), dz) >= 0);
    ASSERT_TRUE(apply_deadzone(static_cast<int16_t>(dz + 1), dz) <= 10);
    ASSERT_EQ(apply_deadzone(kAxisFullScale, dz), kAxisFullScale);
    ASSERT_EQ(apply_deadzone(static_cast<int16_t>(-kAxisFullScale), dz), -kAxisFullScale);
    // Symmetric and monotonic across the whole travel.
    int16_t previous = 0;
    for (int16_t v = 0; v <= kAxisFullScale; ++v) {
      const int16_t out = apply_deadzone(v, dz);
      ASSERT_TRUE(out >= previous);
      ASSERT_EQ(apply_deadzone(static_cast<int16_t>(-v), dz), -out);
      previous = out;
    }
  }
  // Beyond the rail is clamped, not extrapolated.
  ASSERT_EQ(apply_deadzone(2000, 51), kAxisFullScale);
  ASSERT_EQ(apply_deadzone(-2000, 51), -kAxisFullScale);
}

static void test_axis_to_cc_is_continuous_at_the_deadzone_edge_and_reaches_full_scale() {
  for (int16_t dz : {0, 51, 256}) {
    ASSERT_EQ(axis_to_cc(dz, dz), 63);
    ASSERT_EQ(axis_to_cc(static_cast<int16_t>(-dz), dz), 63);
    ASSERT_TRUE(axis_to_cc(static_cast<int16_t>(dz + 1), dz) - 63 <= 1);
    ASSERT_TRUE(63 - axis_to_cc(static_cast<int16_t>(-dz - 1), dz) <= 1);
    ASSERT_EQ(axis_to_cc(kAxisFullScale, dz), 127);
    ASSERT_EQ(axis_to_cc(static_cast<int16_t>(-kAxisFullScale), dz), 0);
    // No step anywhere along the travel is wider than the rescale itself.
    for (int16_t v = -kAxisFullScale; v < kAxisFullScale; ++v) {
      const int step = axis_to_cc(static_cast<int16_t>(v + 1), dz) - axis_to_cc(v, dz);
      ASSERT_TRUE(step >= 0);
      ASSERT_TRUE(step <= 1);
    }
  }
  // Every curve shares the same end points.
  for (auto curve : {Curve::Linear, Curve::Exponential, Curve::Logarithmic}) {
    ASSERT_EQ(axis_to_cc(kAxisFullScale, 51, curve), 127);
    ASSERT_EQ(axis_to_cc(static_cast<int16_t>(-kAxisFullScale), 51, curve), 0);
  }
}

static void test_axis_to_bend_is_continuous_at_the_deadzone_edge_and_reaches_full_scale() {
  for (int16_t dz : {0, 51, 256}) {
    ASSERT_EQ(axis_to_bend(dz, dz), 0);
    ASSERT_EQ(axis_to_bend(static_cast<int16_t>(-dz), dz), 0);
    // One axis unit past the edge is at most one rescaled unit of bend
    // (8192/512 per unit, stretched by full/(full - dz)).
    const int unit = 16 * kAxisFullScale / (kAxisFullScale - dz) + 16;
    ASSERT_TRUE(axis_to_bend(static_cast<int16_t>(dz + 1), dz) >= 0);
    ASSERT_TRUE(axis_to_bend(static_cast<int16_t>(dz + 1), dz) <= unit);
    ASSERT_TRUE(axis_to_bend(static_cast<int16_t>(-dz - 1), dz) >= -unit);
    ASSERT_EQ(axis_to_bend(kAxisFullScale, dz), 8191);
    ASSERT_EQ(axis_to_bend(static_cast<int16_t>(-kAxisFullScale), dz), -8192);
    for (int16_t v = -kAxisFullScale; v < kAxisFullScale; ++v) {
      const int step = axis_to_bend(static_cast<int16_t>(v + 1), dz) - axis_to_bend(v, dz);
      ASSERT_TRUE(step >= 0);
      ASSERT_TRUE(step <= unit);
    }
  }
}

static void test_restore_neutral_state() {
  const auto state = restore_state();
  ASSERT_EQ(state.cc_x, -1);
  ASSERT_EQ(state.cc_y, -1);
  ASSERT_EQ(state.bend, 0);
  ASSERT_TRUE(state.neutral_required);
}

int main() {
  TEST_SUITE("Modulation recipes");
  RUN_TEST(test_cc_defaults_and_overrides);
  RUN_TEST(test_axis_mapping_and_curves);
  RUN_TEST(test_deadzone_units_from_percent);
  RUN_TEST(test_apply_deadzone_rescales_remaining_travel);
  RUN_TEST(test_axis_to_cc_is_continuous_at_the_deadzone_edge_and_reaches_full_scale);
  RUN_TEST(test_axis_to_bend_is_continuous_at_the_deadzone_edge_and_reaches_full_scale);
  RUN_TEST(test_restore_neutral_state);
  test_print_result();
  return g_fail ? 1 : 0;
}
