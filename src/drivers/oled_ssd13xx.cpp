#include "drivers/oled_ssd13xx.h"
#include "drivers/oled_framebuffer.h"

#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace drivers {

static const char* TAG = "OLED";

static bool should_log_i2c_warning() {
  static int64_t next_log_us = 0;
  const int64_t now = esp_timer_get_time();
  if (now < next_log_us) return false;
  next_log_us = now + 1000000;
  return true;
}

bool OledSsd13xx::begin(const OledConfig& cfg) {
  if (!cfg.width || cfg.width > 128 || !cfg.height ||
      cfg.height > 64 || cfg.height % 8 != 0) return false;
  cfg_ = cfg;
  if (initialized_) return true;
  valid_pages_ = 0;

  // Basic SSD1306-compatible init sequence; works with many SSD1315 modules.
  bool ok = true;
  ok &= write_cmd(0xAE);               // display off
  ok &= write_cmd2(0xD5, 0x80);        // clock
  ok &= write_cmd2(0xA8, cfg_.height - 1);  // multiplex
  ok &= write_cmd2(0xD3, 0x00);        // display offset
  ok &= write_cmd(0x40);               // start line
  ok &= write_cmd2(0x8D, 0x14);        // charge pump
  ok &= write_cmd2(0x20, 0x02);        // page mode, matching flush page/column commands
  ok &= write_cmd(cfg_.flip_x ? 0xA0 : 0xA1); // segment remap
  ok &= write_cmd(cfg_.flip_y ? 0xC0 : 0xC8); // com scan direction
  ok &= write_cmd2(0xDA, 0x12);        // com pins
  ok &= write_cmd2(0x81, 0xCF);        // contrast
  ok &= write_cmd2(0xD9, 0xF1);        // precharge
  ok &= write_cmd2(0xDB, 0x40);        // vcom detect
  ok &= write_cmd(0xA4);               // display follows RAM
  ok &= write_cmd(0xA6);               // normal (not inverted)
  ok &= write_cmd(0xAF);               // display on

  if (!ok) {
    ESP_LOGW(TAG, "OLED init failed (addr=0x%02X)", cfg_.addr);
    return false;
  }

  clear();
  initialized_ = true;
  if (!flush()) {
    initialized_ = false;
    ESP_LOGW(TAG, "OLED init flush failed (addr=0x%02X)", cfg_.addr);
    return false;
  }
  ESP_LOGI(TAG, "OLED init OK (addr=0x%02X %ux%u)", cfg_.addr, cfg_.width, cfg_.height);
  return true;
}

bool OledSsd13xx::write_cmd(uint8_t cmd) {
  uint8_t out[2] = {0x00, cmd};
  esp_err_t err = i2c_master_write_to_device(cfg_.port, cfg_.addr, out, sizeof(out), pdMS_TO_TICKS(50));
  if (err != ESP_OK) {
    if (should_log_i2c_warning()) {
      ESP_LOGW(TAG, "cmd 0x%02X failed: %s", cmd, esp_err_to_name(err));
    }
    return false;
  }
  return true;
}

bool OledSsd13xx::write_cmd2(uint8_t cmd, uint8_t arg) {
  uint8_t out[3] = {0x00, cmd, arg};
  esp_err_t err = i2c_master_write_to_device(cfg_.port, cfg_.addr, out, sizeof(out), pdMS_TO_TICKS(50));
  if (err != ESP_OK) {
    if (should_log_i2c_warning()) {
      ESP_LOGW(TAG, "cmd2 0x%02X failed: %s", cmd, esp_err_to_name(err));
    }
    return false;
  }
  return true;
}

bool OledSsd13xx::write_data(const uint8_t* data, size_t len) {
  // Chunked writes: [0x40][data...]
  static constexpr size_t kChunk = 16;
  uint8_t out[1 + kChunk];
  out[0] = 0x40;

  size_t off = 0;
  while (off < len) {
    size_t n = len - off;
    if (n > kChunk) n = kChunk;
    std::memcpy(out + 1, data + off, n);
    esp_err_t err = i2c_master_write_to_device(cfg_.port, cfg_.addr, out, 1 + n, pdMS_TO_TICKS(50));
    if (err != ESP_OK) {
      if (should_log_i2c_warning()) {
        ESP_LOGW(TAG, "data write failed: %s", esp_err_to_name(err));
      }
      return false;
    }
    off += n;
  }
  return true;
}

void OledSsd13xx::clear() { fb::clear(buf_, sizeof(buf_)); }

void OledSsd13xx::draw_text(uint8_t x, uint8_t page, const char* text, bool invert) {
  fb::draw_text(buf_, cfg_.width, cfg_.height, x, page, text, invert);
}

void OledSsd13xx::draw_text_2x(uint8_t x, uint8_t page, const char* text, bool invert) {
  fb::draw_text_2x(buf_, cfg_.width, cfg_.height, x, page, text, invert);
}

void OledSsd13xx::draw_text_line(uint8_t line, const char* text, bool invert) {
  draw_text(0, line, text, invert);
}

void OledSsd13xx::draw_hline(uint8_t x0, uint8_t x1, uint8_t y) {
  fb::draw_hline(buf_, cfg_.width, cfg_.height, x0, x1, y);
}

void OledSsd13xx::draw_vline(uint8_t x, uint8_t y0, uint8_t y1) {
  fb::draw_vline(buf_, cfg_.width, cfg_.height, x, y0, y1);
}

void OledSsd13xx::fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool on) {
  fb::fill_rect(buf_, cfg_.width, cfg_.height, x, y, w, h, on);
}

void OledSsd13xx::fill_line(uint8_t page, uint8_t value) {
  fb::fill_line(buf_, cfg_.width, cfg_.height, page, value);
}

void OledSsd13xx::reset() {
  valid_pages_ = 0;
  initialized_ = false;
}

bool OledSsd13xx::flush() {
  if (!initialized_) return false;
  // A bus fault can disturb the controller's origin/address registers, not
  // just the page being written. Never trust any cached page after a fault.
  auto failed = [this]() { valid_pages_ = 0; return false; };
  // A periodic full-frame rewrite would add visible flashes and multiply I2C
  // traffic even when only a few pixels changed. A full refresh is necessary
  // only after init or a failed transfer, when valid_pages_ is invalidated.
  const bool full_refresh = valid_pages_ == 0;
  if (full_refresh) {
    valid_pages_ = 0;
    if (!write_cmd2(0x20, 0x02)) return failed();  // page addressing
    // Restore the visible origin together with the full-frame recovery.
    if (!write_cmd(0x40) || !write_cmd2(0xD3, 0x00)) return failed();
  }
  const uint8_t pages = cfg_.height / 8;
  for (uint8_t p = 0; p < pages; ++p) {
    const size_t base = p * cfg_.width;
    const uint8_t mask = static_cast<uint8_t>(1u << p);
    size_t first = 0, end = cfg_.width;
    if (valid_pages_ & mask) {
      while (first < end && buf_[base + first] == sent_[base + first]) ++first;
      if (first == end) continue;
      while (end > first && buf_[base + end - 1] == sent_[base + end - 1]) --end;
    }
    // Invalidate before I/O; any failure below invalidates the entire frame.
    valid_pages_ &= static_cast<uint8_t>(~mask);
    const uint8_t column = static_cast<uint8_t>(cfg_.col_offset + first);
    if (!write_cmd(static_cast<uint8_t>(0xB0 | p))) return failed();
    if (!write_cmd(static_cast<uint8_t>(column & 0x0F))) return failed();
    if (!write_cmd(static_cast<uint8_t>(0x10 | (column >> 4)))) return failed();
    if (!write_data(&buf_[base + first], end - first)) return failed();
    std::memcpy(&sent_[base + first], &buf_[base + first], end - first);
    valid_pages_ |= mask;
  }
  return true;
}

}  // namespace drivers
