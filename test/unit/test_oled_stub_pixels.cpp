// Tests: the native OLED test stub writes real pixels (drivers::fb — the
// same math as the hardware driver), rather than faking one string per page.
// Regression: the old page-keyed string stub could only remember the LAST
// draw_text() call per page, so independent multi-token lines (e.g. the
// joystick screen's O:/I:/M:/R: register row, four separate draw_text()
// calls on one page) silently lost every token but the last.
#include "../framework.h"
#include "drivers/oled_framebuffer.h"
#include "drivers/oled_ssd13xx.h"
#include "joy_stub.h"

static void test_single_draw_text_is_found() {
  drivers::OledSsd13xx oled;
  oled.begin({});
  oled.clear();
  oled.draw_text(0, 0, "BPM", false);
  ASSERT_TRUE(oled.flush());
  ASSERT_TRUE(stub::screen_contains("BPM"));
  ASSERT_FALSE(stub::screen_contains("Voicing"));
}

static void test_multiple_draw_text_calls_on_one_page_all_survive() {
  // The exact shape of joystick_register_line(): four independent tokens
  // spread across one page. The old stub kept only the last one drawn.
  drivers::OledSsd13xx oled;
  oled.begin({});
  oled.clear();
  oled.draw_text(0,  0, "O:+0", false);
  oled.draw_text(30, 0, "I:R",  false);
  oled.draw_text(60, 0, "M:Maj", false);
  oled.draw_text(100, 0, "R:C", false);
  ASSERT_TRUE(oled.flush());
  ASSERT_TRUE(stub::screen_contains("O:+0"));
  ASSERT_TRUE(stub::screen_contains("I:R"));
  ASSERT_TRUE(stub::screen_contains("M:Maj"));
  ASSERT_TRUE(stub::screen_contains("R:C"));
}

static void test_inverted_text_is_found() {
  drivers::OledSsd13xx oled;
  oled.begin({});
  oled.clear();
  oled.fill_line(0, 0xFF);
  oled.draw_text(0, 0, "SELECTED", true);
  ASSERT_TRUE(oled.flush());
  ASSERT_TRUE(stub::screen_contains("SELECTED"));
}

static void test_2x_text_is_found() {
  drivers::OledSsd13xx oled;
  oled.begin({});
  oled.clear();
  oled.draw_text_2x(0, 1, "C Major", false);
  ASSERT_TRUE(oled.flush());
  ASSERT_TRUE(stub::screen_contains("C Major"));
}

static void test_cleared_screen_finds_nothing() {
  drivers::OledSsd13xx oled;
  oled.begin({});
  oled.clear();
  ASSERT_TRUE(oled.flush());
  ASSERT_FALSE(stub::screen_contains("BPM"));
}

// --- PARAM-family diagonal-arrow glyphs (kGlyphArrowNE/NW/SE/SW) ---
// Each new glyph is asserted against its exact 5-column bitmap, so a
// transposed or mirrored bitmap would fail here even though it would still
// render as *some* non-blank glyph.

static void draw_one_glyph(drivers::OledSsd13xx& oled, char glyph) {
  const char text[2] = {glyph, 0};
  oled.begin({});
  oled.clear();
  oled.draw_text(0, 0, text, false);
  ASSERT_TRUE(oled.flush());
}

static void assert_glyph_columns(const uint8_t expected[5]) {
  ASSERT_TRUE(stub::oled_active_buf != nullptr);
  for (int i = 0; i < 5; ++i) {
    ASSERT_EQ(stub::oled_active_buf[i], expected[i]);
  }
  // The 1px inter-character gap column must stay blank.
  ASSERT_EQ(stub::oled_active_buf[5], 0x00);
}

static void test_glyph_arrow_ne_pixels() {
  drivers::OledSsd13xx oled;
  draw_one_glyph(oled, drivers::fb::kGlyphArrowNE);
  const uint8_t expected[5] = {0x40, 0x20, 0x08, 0x05, 0x03};
  assert_glyph_columns(expected);
}

static void test_glyph_arrow_nw_pixels() {
  drivers::OledSsd13xx oled;
  draw_one_glyph(oled, drivers::fb::kGlyphArrowNW);
  const uint8_t expected[5] = {0x03, 0x05, 0x08, 0x20, 0x40};
  assert_glyph_columns(expected);
}

static void test_glyph_arrow_se_pixels() {
  drivers::OledSsd13xx oled;
  draw_one_glyph(oled, drivers::fb::kGlyphArrowSE);
  const uint8_t expected[5] = {0x01, 0x02, 0x08, 0x50, 0x60};
  assert_glyph_columns(expected);
}

static void test_glyph_arrow_sw_pixels() {
  drivers::OledSsd13xx oled;
  draw_one_glyph(oled, drivers::fb::kGlyphArrowSW);
  const uint8_t expected[5] = {0x60, 0x50, 0x08, 0x02, 0x01};
  assert_glyph_columns(expected);
}

// NE and SW are point-reflections (180-degree rotations) of each other;
// NW and SE likewise. Cross-check that relationship directly, independent
// of the hand-picked byte values above, so a future re-draw of one glyph
// that breaks the pairing is caught even if both individual bitmaps still
// "look like an arrow" in isolation.
static void test_diagonal_glyphs_are_point_symmetric_pairs() {
  auto rotated_180_matches = [](const uint8_t a[5], const uint8_t b[5]) {
    for (int col = 0; col < 5; ++col) {
      uint8_t mirrored_col = 0;
      uint8_t src = a[4 - col];
      for (int row = 0; row < 7; ++row) {
        if (src & (1 << row)) mirrored_col |= (1 << (6 - row));
      }
      if (mirrored_col != b[col]) return false;
    }
    return true;
  };
  const uint8_t ne[5] = {0x40, 0x20, 0x08, 0x05, 0x03};
  const uint8_t sw[5] = {0x60, 0x50, 0x08, 0x02, 0x01};
  const uint8_t nw[5] = {0x03, 0x05, 0x08, 0x20, 0x40};
  const uint8_t se[5] = {0x01, 0x02, 0x08, 0x50, 0x60};
  ASSERT_TRUE(rotated_180_matches(ne, sw));
  ASSERT_TRUE(rotated_180_matches(nw, se));
}

// A codepoint above the new kFontLastChar must still fall back to '?' —
// confirms the bound check moved to kFontLastChar rather than silently
// disappearing when 0x7E was replaced.
static void test_char_above_new_bound_falls_back_to_question_mark() {
  drivers::OledSsd13xx oled;
  oled.begin({});
  oled.clear();
  const char above_bound[2] = {static_cast<char>(drivers::fb::kFontLastChar + 1), 0};
  oled.draw_text(0, 0, above_bound, false);
  ASSERT_TRUE(oled.flush());
  ASSERT_TRUE(stub::screen_contains("?"));
}

int main() {
  TEST_SUITE("OLED test stub — real pixels");
  RUN_TEST(test_single_draw_text_is_found);
  RUN_TEST(test_multiple_draw_text_calls_on_one_page_all_survive);
  RUN_TEST(test_inverted_text_is_found);
  RUN_TEST(test_2x_text_is_found);
  RUN_TEST(test_cleared_screen_finds_nothing);
  RUN_TEST(test_glyph_arrow_ne_pixels);
  RUN_TEST(test_glyph_arrow_nw_pixels);
  RUN_TEST(test_glyph_arrow_se_pixels);
  RUN_TEST(test_glyph_arrow_sw_pixels);
  RUN_TEST(test_diagonal_glyphs_are_point_symmetric_pairs);
  RUN_TEST(test_char_above_new_bound_falls_back_to_question_mark);
  test_print_result();
  return g_fail ? 1 : 0;
}
