#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Settable state for the JoystickAnalog stub.
// Tests set these before calling process_joystick() to inject specific
// X/Y values and direction into the analog joystick code paths.
namespace stub {
  extern int16_t joy_x;   // raw X returned by get_xy()
  extern int16_t joy_y;   // raw Y returned by get_xy()
  extern int8_t  joy_dir; // direction returned by read_direction() (-1=center)
  extern bool    joy_btn; // button state returned by read_button()
  extern bool oled_flushed;

  // The OLED test stub draws into a real paged pixel buffer (via
  // drivers::fb — the same math the hardware driver uses) rather than
  // faking text, so this is a genuine framebuffer, not a per-line string
  // capture. It's captured on flush(), pointing at the flushed OledSsd13xx
  // instance's own buffer.
  extern const uint8_t* oled_active_buf;
  extern uint8_t oled_active_width;
  extern uint8_t oled_active_height;

  // True if `needle` appears rendered anywhere on the last-flushed screen,
  // in either normal or inverted text, at 1x or 2x size. Searches for the
  // exact rendered pixel pattern (same font, same layout math) rather than
  // reconstructing text from pixels, so arbitrary x-alignment is not
  // ambiguous — it's a substring match on known bytes, not OCR.
  bool screen_contains(const char* needle);

  // Optional draw-call log: when non-null, every OLED primitive appends one
  // line describing itself (tools/screen_dump.cpp's --calls view).
  extern std::vector<std::string>* oled_trace;

  // Reset all joystick stub state to centered/released defaults.
  inline void joy_reset() { joy_x = 0; joy_y = 0; joy_dir = -1; joy_btn = false; }
  inline void oled_reset() { oled_flushed = false; oled_active_buf = nullptr; }
}
