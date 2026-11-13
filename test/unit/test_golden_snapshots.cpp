// Golden framebuffer snapshot harness: a smoke test against the static boot
// screen, proving the plumbing works (render -> dump -> compare/update).
// Per-screen goldens for the play screen, the joystick family bodies and the
// edit screens live in test_production_ui.cpp.
#include "../framework.h"
#include "golden.h"
#include "ui/production_render.h"

#include <cstdio>

static void test_boot_screen_matches_golden() {
  drivers::OledSsd13xx oled;
  oled.begin({});
  ASSERT_TRUE(ui::render_boot_screen(oled));
  std::string message;
  const bool ok = golden::check("boot_screen", golden::dump_screen(oled), message);
  if (!ok) std::fprintf(stderr, "  %s\n", message.c_str());
  ASSERT_TRUE(ok);
}

// A screen that changes must produce a different dump — otherwise the golden
// comparison would be vacuously true no matter what render_boot_screen() did.
static void test_dump_screen_is_sensitive_to_pixel_changes() {
  drivers::OledSsd13xx oled;
  oled.begin({});
  oled.clear();
  const auto blank = golden::dump_screen(oled);
  oled.fill_rect(10, 10, 5, 5);
  const auto filled = golden::dump_screen(oled);
  ASSERT_TRUE(blank != filled);
}

int main() {
  TEST_SUITE("Golden framebuffer snapshots");
  RUN_TEST(test_boot_screen_matches_golden);
  RUN_TEST(test_dump_screen_is_sensitive_to_pixel_changes);
  test_print_result();
  return g_fail ? 1 : 0;
}
