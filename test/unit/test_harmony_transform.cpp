#include "../framework.h"
#include "app/harmony_transform.h"

using namespace app::harmony;

static void test_tension_maps_preserve_boundaries() {
  ASSERT_TRUE(quality_for_xy(TensionMap::Diatonic, 0.0f, 0.0f, 0.1f) == nullptr);
  ASSERT_STREQ(quality_for_xy(TensionMap::Diatonic, 1.0f, 1.0f, 0.3f), "add9");
  ASSERT_STREQ(quality_for_xy(TensionMap::Altered, -1.0f, -1.0f, 0.9f), "maj9");
  ASSERT_EQ(tension_slot_for_xy(-1.0f, -1.0f, 0.9f), 12);
}

static void test_voicing_direction_recipe_mapping() {
  // Each of the eight directions names a distinct engine::VoicingSpread style.
  ASSERT_EQ(voicing_for_direction(2), 1); // N=Default
  ASSERT_EQ(voicing_for_direction(1), 0); // NE=Close
  ASSERT_EQ(voicing_for_direction(0), 2); // E=Drop2
  ASSERT_EQ(voicing_for_direction(7), 3); // SE=Drop3
  ASSERT_EQ(voicing_for_direction(6), 4); // S=Open
  ASSERT_EQ(voicing_for_direction(5), 5); // SW=Shell
  ASSERT_EQ(voicing_for_direction(4), 6); // W=Rootless
  ASSERT_EQ(voicing_for_direction(3), 7); // NW=Guitar
  ASSERT_EQ(voicing_for_direction(-1), 1); // Invalid falls back to Default.
}

static void test_voicing_direction_map_is_a_bijection() {
  bool seen[8] = {};
  for (int8_t dir = 0; dir < 8; ++dir) {
    const uint8_t style = voicing_for_direction(dir);
    ASSERT_TRUE(style < 8);
    ASSERT_FALSE(seen[style]);
    seen[style] = true;
  }
}

static void test_scale_fallback_order() {
  ASSERT_STREQ(scale_for_direction(0), "Ionian");
  ASSERT_STREQ(scale_for_direction(5), "Aeolian");
  ASSERT_STREQ(scale_for_direction(7), "Harmonic Minor");
  ASSERT_STREQ(scale_for_direction(99), "Ionian");
}

int main() {
  TEST_SUITE("Harmony transformation primitives");
  RUN_TEST(test_tension_maps_preserve_boundaries);
  RUN_TEST(test_voicing_direction_recipe_mapping);
  RUN_TEST(test_voicing_direction_map_is_a_bijection);
  RUN_TEST(test_scale_fallback_order);
  test_print_result();
  return g_fail ? 1 : 0;
}
