#include "drivers/keypad_matrix.h"

#include "esp_timer.h"
#include "rom/ets_sys.h"

namespace drivers {

static uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

bool KeypadMatrix::begin(const KeypadMatrixConfig& cfg) {
  cfg_ = cfg;

  // Rows: inputs with pull-ups
  gpio_config_t row_conf = {};
  row_conf.intr_type = GPIO_INTR_DISABLE;
  row_conf.mode = GPIO_MODE_INPUT;
  row_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  row_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  row_conf.pin_bit_mask = 0;
  for (auto r : cfg_.rows) {
    row_conf.pin_bit_mask |= (1ULL << r);
  }
  gpio_config(&row_conf);

  // Cols: open-drain outputs, released (high-impedance) by default. Only the
  // column being scanned is pulled LOW; the rows' pull-ups do the rest. With
  // push-pull columns, two keys held in the same row shorted the scanned
  // (LOW) column to an idle (HIGH) one through the row line - the row then
  // sat near mid-rail and both keys read unreliably.
  gpio_config_t col_conf = {};
  col_conf.intr_type = GPIO_INTR_DISABLE;
  col_conf.mode = GPIO_MODE_OUTPUT_OD;
  col_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  col_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  col_conf.pin_bit_mask = 0;
  for (auto c : cfg_.cols) {
    col_conf.pin_bit_mask |= (1ULL << c);
  }
  gpio_config(&col_conf);
  for (auto c : cfg_.cols) {
    gpio_set_level(c, 1);
  }

  // Init states
  const uint32_t t = now_ms();
  for (auto& k : keys_) {
    k = {};
    k.last_raw_change_ms = t;
  }

  initialized_ = true;
  return true;
}

uint16_t KeypadMatrix::scan_raw() {
  uint16_t mask = 0;

  // Drive one column LOW at a time; pressed key pulls corresponding row LOW.
  for (int col = 0; col < 4; ++col) {
    uint8_t rows_mask = 0;
    for (int c = 0; c < 4; ++c) {
      gpio_set_level(cfg_.cols[c], c == col ? 0 : 1);
    }

    if (cfg_.settle_us) {
      ets_delay_us(cfg_.settle_us);
    }

    for (int row = 0; row < 4; ++row) {
      int level = gpio_get_level(cfg_.rows[row]);
      if (level == 0) {
        rows_mask |= static_cast<uint8_t>(1u << row);
        uint8_t index = static_cast<uint8_t>(row * 4 + col);
        mask |= (1u << index);
      }
    }

    // Cache what we saw for this column (diagnostics).
    last_col_rows_[col] = rows_mask;
  }

  // Restore cols HIGH.
  for (auto c : cfg_.cols) {
    gpio_set_level(c, 1);
  }

  return mask;
}

void KeypadMatrix::poll(std::vector<KeypadEvent>& out_events) {
  if (!initialized_) return;

  out_events.clear();
  const uint32_t t = now_ms();
  const uint16_t raw_mask = scan_raw();
  last_raw_mask_ = raw_mask;

  for (uint8_t i = 0; i < 16; ++i) {
    const bool raw = (raw_mask >> i) & 0x1;
    auto& k = keys_[i];

    if (raw != k.raw) {
      k.raw = raw;
      k.last_raw_change_ms = t;
    }

    if (k.stable != k.raw) {
      if ((t - k.last_raw_change_ms) >= cfg_.debounce_ms) {
        k.stable = k.raw;
        out_events.push_back(KeypadEvent{i, k.stable});
      }
    }
  }
}

bool KeypadMatrix::is_pressed(uint8_t index) const {
  if (index >= 16) return false;
  return keys_[index].stable;
}

uint16_t KeypadMatrix::raw_mask() const { return last_raw_mask_; }

uint16_t KeypadMatrix::stable_mask() const {
  uint16_t mask = 0;
  for (uint8_t i = 0; i < 16; ++i) {
    if (keys_[i].stable) mask |= (1u << i);
  }
  return mask;
}

uint8_t KeypadMatrix::col_rows_mask(uint8_t col) const {
  if (col >= 4) return 0;
  return last_col_rows_[col];
}

}  // namespace drivers
