#pragma once

#include <cstddef>
#include <cstdint>

// Pure, hardware-free pixel math for the SSD13xx-family 128x(8k) paged
// framebuffer. Shared by the real driver (oled_ssd13xx.cpp) and the native
// test stub (test/stubs/drivers_stubs.cpp) so both ever write exactly the
// same bits — the native test build has no separate, drift-prone copy of
// this math, which is what makes it safe to assert against pixels in tests.
namespace drivers::fb {

// 5x7 font, ASCII 0x20..0x7E plus 4 diagonal-arrow glyphs above the ASCII
// range (see kGlyphArrowNE etc. below). Each glyph is 5 columns, LSB = top
// pixel.
extern const uint8_t kFont5x7[];
constexpr int kFontFirstChar = 0x20;
constexpr int kFontLastChar = 0x82;
constexpr int kGlyphWidth = 5;    // + 1px gap = 6px advance per character
constexpr int kCharAdvance = 6;

// PARAM-family axis-label glyphs for the two diagonal joystick axes (NE/SW
// and NW/SE). Cardinal up/down/left/right axis labels reuse the ASCII '^',
// 'v', '<', '>' glyphs, so only the 4 diagonals need their own glyphs. These
// are raw indices into kFont5x7 above the ASCII range, not Unicode code
// points — never pass these through anything that treats `char` as Unicode.
constexpr char kGlyphArrowNE = 0x7F;
constexpr char kGlyphArrowNW = static_cast<char>(0x80);
constexpr char kGlyphArrowSE = static_cast<char>(0x81);
constexpr char kGlyphArrowSW = static_cast<char>(0x82);

void clear(uint8_t* buf, size_t size);

// `buf` is a paged framebuffer: `width` bytes per page, one bit per pixel
// row within the page (LSB = top row of the page).
void draw_text(uint8_t* buf, uint8_t width, uint8_t height, uint8_t x,
               uint8_t page, const char* text, bool invert);
void draw_text_2x(uint8_t* buf, uint8_t width, uint8_t height, uint8_t x,
                  uint8_t page, const char* text, bool invert);
void draw_hline(uint8_t* buf, uint8_t width, uint8_t height, uint8_t x0,
                uint8_t x1, uint8_t y);
void draw_vline(uint8_t* buf, uint8_t width, uint8_t height, uint8_t x,
                uint8_t y0, uint8_t y1);
void fill_rect(uint8_t* buf, uint8_t width, uint8_t height, uint8_t x,
              uint8_t y, uint8_t w, uint8_t h, bool on);
void fill_line(uint8_t* buf, uint8_t width, uint8_t height, uint8_t page,
              uint8_t value);

}  // namespace drivers::fb
