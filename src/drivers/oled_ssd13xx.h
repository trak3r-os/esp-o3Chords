#pragma once

#include <cstdint>
#include <cstddef>

#include "driver/i2c.h"

namespace drivers {

struct OledConfig {
  i2c_port_t port = I2C_NUM_0;
  uint8_t addr = 0x3C;
  uint8_t width = 128;
  uint8_t height = 64;
  bool flip_x = false;
  bool flip_y = false;
  uint8_t col_offset = 0;  // SH1106 (1.3") = 2; SSD1306/SSD1315 (0.96") = 0
};

class OledSsd13xx {
 public:
  bool begin(const OledConfig& cfg);

  void clear();
  void draw_text(uint8_t x, uint8_t page, const char* text, bool invert = false);
  void draw_text_2x(uint8_t x, uint8_t page, const char* text, bool invert = false);
  void draw_text_line(uint8_t line, const char* text, bool invert = false);
  void draw_hline(uint8_t x0, uint8_t x1, uint8_t y);
  void draw_vline(uint8_t x, uint8_t y0, uint8_t y1);
  void fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool on = true);
  void fill_line(uint8_t page, uint8_t value = 0xFF);
  void reset();
  bool flush();

  // Read-only access to the paged pixel buffer, for tests (golden framebuffer
  // snapshots, pixel-level text assertions). Never used by production code.
  const uint8_t* framebuffer() const { return buf_; }
  uint8_t framebuffer_width() const { return cfg_.width; }
  uint8_t framebuffer_height() const { return cfg_.height; }

 private:
  bool write_cmd(uint8_t cmd);
  bool write_cmd2(uint8_t cmd, uint8_t arg);
  bool write_data(const uint8_t* data, size_t len);

  OledConfig cfg_{};
  bool initialized_ = false;
  static constexpr size_t kBufSize = 128 * 64 / 8;
  uint8_t buf_[kBufSize] = {};
  uint8_t sent_[kBufSize] = {};
  uint8_t valid_pages_ = 0;
};

}  // namespace drivers
