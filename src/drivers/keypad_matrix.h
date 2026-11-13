#pragma once

#include <cstdint>
#include <vector>

#include "driver/gpio.h"

namespace drivers {

struct KeypadEvent {
  uint8_t index = 0;   // 0..15, row-major (row*4 + col)
  bool pressed = false;
};

struct KeypadMatrixConfig {
  gpio_num_t rows[4] = {GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC};
  gpio_num_t cols[4] = {GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC};
  uint32_t debounce_ms = 25;
  uint32_t settle_us = 30;
};

class KeypadMatrix {
 public:
  bool begin(const KeypadMatrixConfig& cfg);
  void poll(std::vector<KeypadEvent>& out_events);

  bool is_pressed(uint8_t index) const;
  uint16_t raw_mask() const;
  uint16_t stable_mask() const;
  uint8_t col_rows_mask(uint8_t col) const;

 private:
  struct KeyState {
    bool raw = false;
    bool stable = false;
    uint32_t last_raw_change_ms = 0;
  };

  uint16_t scan_raw();

  KeypadMatrixConfig cfg_{};
  bool initialized_ = false;
  uint16_t last_raw_mask_ = 0;
  uint8_t last_col_rows_[4] = {};
  KeyState keys_[16] = {};
};

}  // namespace drivers
