#include "../framework.h"
#include "app/voice_recipe.h"

using namespace app::voice;

static void test_articulation_policy() {
  ASSERT_EQ(trigger_mode(false), engine::TriggerMode::kOneShot);
  ASSERT_EQ(trigger_mode(true), engine::TriggerMode::kHeld);
}

// clock_allows_pulse() was removed along with Euclidean, its only consumer.
// The MIDI clock still drives the
// arpeggiator directly, untouched.

int main() {
  TEST_SUITE("Shared voice recipe policy");
  RUN_TEST(test_articulation_policy);
  test_print_result();
  return g_fail ? 1 : 0;
}
