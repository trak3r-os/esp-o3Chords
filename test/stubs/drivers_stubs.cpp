// Stub implementations of hardware driver classes for native host builds.
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include "joy_stub.h"
#include "drivers/joystick_analog.h"
#include "drivers/keypad_matrix.h"
#include "drivers/rotary_encoder.h"
#include "drivers/gpio_buttons.h"
#include "drivers/oled_ssd13xx.h"
#include "drivers/oled_framebuffer.h"
#include "drivers/i2c_bus.h"
#include "drivers/storage.h"

#include <cstring>
#include <vector>

// Joystick stub state — tests set via stub:: namespace (see joy_stub.h).
namespace stub {
  int16_t joy_x   = 0;
  int16_t joy_y   = 0;
  int8_t  joy_dir = -1;
  bool    joy_btn = false;
  bool oled_flushed = false;
  std::vector<std::string>* oled_trace = nullptr;
  const uint8_t* oled_active_buf = nullptr;
  uint8_t oled_active_width = 0;
  uint8_t oled_active_height = 0;

  bool screen_contains(const char* needle) {
    if (!oled_active_buf || !needle || !*needle) return false;
    const uint8_t width = oled_active_width;
    const size_t pages = oled_active_height / 8;
    const size_t chars = std::strlen(needle);

    // 1x text: 6 bytes/char (5 glyph columns + 1px gap), one page tall.
    {
      const size_t pat_len = chars * 6;
      std::vector<uint8_t> pat(pat_len, 0);
      drivers::fb::draw_text(pat.data(), static_cast<uint8_t>(pat_len), 8, 0, 0, needle, false);
      for (size_t page = 0; page < pages; ++page) {
        const uint8_t* row = oled_active_buf + page * width;
        for (size_t col = 0; col + pat_len <= width; ++col) {
          bool normal = true, inverted = true;
          for (size_t i = 0; i < pat_len && (normal || inverted); ++i) {
            if (row[col + i] != pat[i]) normal = false;
            if (row[col + i] != static_cast<uint8_t>(pat[i] ^ 0xFF)) inverted = false;
          }
          if (normal || inverted) return true;
        }
      }
    }

    // 2x text: 12 bytes/char, spans two adjacent pages. Never drawn inverted
    // in this codebase, so only the normal polarity is checked.
    if (pages >= 2) {
      const uint8_t pat_width = static_cast<uint8_t>(chars * 12);
      std::vector<uint8_t> pat(static_cast<size_t>(pat_width) * 2, 0);
      drivers::fb::draw_text_2x(pat.data(), pat_width, 16, 0, 0, needle, false);
      for (size_t page = 0; page + 1 < pages; ++page) {
        const uint8_t* row0 = oled_active_buf + page * width;
        const uint8_t* row1 = oled_active_buf + (page + 1) * width;
        for (size_t col = 0; col + pat_width <= width; ++col) {
          bool match = true;
          for (size_t i = 0; i < pat_width && match; ++i) {
            if (row0[col + i] != pat[i] || row1[col + i] != pat[pat_width + i]) match = false;
          }
          if (match) return true;
        }
      }
    }
    return false;
  }
}

namespace drivers {

// --- JoystickAnalog ---
bool JoystickAnalog::begin(const JoystickAnalogConfig&) { return true; }
int8_t JoystickAnalog::read_direction() { return stub::joy_dir; }
bool JoystickAnalog::read_button() { return stub::joy_btn; }
void JoystickAnalog::get_xy(int16_t& x, int16_t& y) { x = stub::joy_x; y = stub::joy_y; }

// --- KeypadMatrix ---
bool KeypadMatrix::begin(const KeypadMatrixConfig&) { return true; }
void KeypadMatrix::poll(std::vector<KeypadEvent>&) {}
uint16_t KeypadMatrix::raw_mask() const { return 0; }
uint16_t KeypadMatrix::stable_mask() const { return 0; }
uint8_t KeypadMatrix::col_rows_mask(uint8_t) const { return 0; }

// --- RotaryEncoder ---
bool RotaryEncoder::begin(const RotaryEncoderConfig&) { return true; }
void RotaryEncoder::poll(std::vector<RotaryEvent>&, uint32_t) {}
bool RotaryEncoder::read_button_pressed() const { return false; }

// --- GpioButtons ---
bool GpioButtons::begin(const GpioButtonsConfig&) { return true; }
void GpioButtons::poll(std::vector<GpioButtonEvent>&, uint32_t) {}
void GpioButtons::poll_one(State&, std::vector<GpioButtonEvent>&, uint32_t) {}
bool GpioButtons::read_pressed(gpio_num_t) { return false; }

// --- OledSsd13xx ---
// Only the hardware I2C transport is stubbed. Every drawing method below
// delegates to drivers::fb — the exact pixel math the real driver uses — so
// tests observe genuine pixels, not a fabrication of what drawing "should"
// have produced. See stub::screen_contains() for how tests read them back.
bool OledSsd13xx::begin(const OledConfig&) { return true; }
void OledSsd13xx::reset() {}
void OledSsd13xx::clear() { drivers::fb::clear(buf_, sizeof(buf_)); }
bool OledSsd13xx::flush() {
  stub::oled_flushed = true;
  stub::oled_active_buf = buf_;
  stub::oled_active_width = cfg_.width;
  stub::oled_active_height = cfg_.height;
  return true;
}
namespace {
void trace(const char* fmt, ...) {
  if (!stub::oled_trace) return;
  char line[96];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  stub::oled_trace->push_back(line);
}
}  // namespace
void OledSsd13xx::draw_text(uint8_t x, uint8_t page, const char* text, bool invert) {
  trace("p%u x%-3u %s \"%s\"", page, x, invert ? "inv" : "txt", text);
  drivers::fb::draw_text(buf_, cfg_.width, cfg_.height, x, page, text, invert);
}
void OledSsd13xx::draw_text_2x(uint8_t x, uint8_t page, const char* text, bool invert) {
  trace("p%u x%-3u %s \"%s\"", page, x, invert ? "INV2x" : "2x ", text);
  drivers::fb::draw_text_2x(buf_, cfg_.width, cfg_.height, x, page, text, invert);
}
void OledSsd13xx::draw_text_line(uint8_t line, const char* text, bool invert) {
  draw_text(0, line, text, invert);
}
void OledSsd13xx::draw_hline(uint8_t x0, uint8_t x1, uint8_t y) {
  trace("hline x%u..%u y%u", x0, x1, y);
  drivers::fb::draw_hline(buf_, cfg_.width, cfg_.height, x0, x1, y);
}
void OledSsd13xx::draw_vline(uint8_t x, uint8_t y0, uint8_t y1) {
  trace("vline x%u y%u..%u", x, y0, y1);
  drivers::fb::draw_vline(buf_, cfg_.width, cfg_.height, x, y0, y1);
}
void OledSsd13xx::fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool on) {
  trace("rect x%u y%u %ux%u %s", x, y, w, h, on ? "on" : "off");
  drivers::fb::fill_rect(buf_, cfg_.width, cfg_.height, x, y, w, h, on);
}
void OledSsd13xx::fill_line(uint8_t page, uint8_t value) {
  trace("p%u fill 0x%02X", page, value);
  drivers::fb::fill_line(buf_, cfg_.width, cfg_.height, page, value);
}
bool OledSsd13xx::write_cmd(uint8_t) { return true; }
bool OledSsd13xx::write_cmd2(uint8_t, uint8_t) { return true; }
bool OledSsd13xx::write_data(const uint8_t*, size_t) { return true; }

// --- I2cBus ---
bool I2cBus::begin(const I2cBusConfig&) { return true; }
void I2cBus::reset() {}

// --- Storage ---
bool Storage::begin() { return true; }
bool Storage::mount() { return true; }
bool Storage::save_settings(const void*, size_t) { return true; }
bool Storage::load_settings(void*, size_t) { return false; }
bool Storage::save_preset(const char*, const void*, size_t) { return true; }
bool Storage::load_preset(const char*, void*, size_t, size_t&) { return false; }
bool Storage::delete_preset(const char*) { return true; }
bool Storage::list_presets(char**, size_t, size_t& out_count) { out_count = 0; return true; }
bool Storage::factory_reset() { return true; }

}  // namespace drivers
