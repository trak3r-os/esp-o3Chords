#include "drivers/joystick_analog.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cmath>
#include <algorithm>
#include "nvs.h"

namespace drivers {

static adc_oneshot_unit_handle_t adc_handle = nullptr;


bool JoystickAnalog::begin(const JoystickAnalogConfig& config) {
  ready_ = false;
  config_ = config;

  // Configure button pin as input with pull-up
  if (config.pin_sw >= 0) {
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pin_bit_mask = 1ULL << config.pin_sw;
    gpio_config(&io_conf);
  }

  // Initialize ADC for analog joystick
  adc_oneshot_unit_init_cfg_t init_config = {};
  init_config.unit_id = config.adc_unit;

  esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc_handle);
  if (ret != ESP_OK) {
    ESP_LOGE("Joystick", "Failed to initialize ADC unit: %d", ret);
    return false;
  }

  // Configure ADC channels
  adc_oneshot_chan_cfg_t chan_config = {
    .atten = ADC_ATTEN_DB_12,     // 0-3.3V range (3.3V joystick, no voltage divider)
    .bitwidth = ADC_BITWIDTH_12,   // 12-bit resolution (0-4095)
  };

  ret = adc_oneshot_config_channel(adc_handle, config.adc_channel_x, &chan_config);
  if (ret != ESP_OK) {
    ESP_LOGE("Joystick", "Failed to config X channel: %d", ret);
    return false;
  }

  ret = adc_oneshot_config_channel(adc_handle, config.adc_channel_y, &chan_config);
  if (ret != ESP_OK) {
    ESP_LOGE("Joystick", "Failed to config Y channel: %d", ret);
    return false;
  }

  ESP_LOGI("Joystick", "Analog joystick initialized (unit=%d chX=%d chY=%d SW=GPIO%d)",
           (int)config.adc_unit, (int)config.adc_channel_x, (int)config.adc_channel_y, config.pin_sw);

  // Calibration is hardware-specific and independent of musical presets.
  nvs_handle_t handle;
  if (nvs_open("joy_cal", NVS_READONLY, &handle) == ESP_OK) {
    uint16_t data[4] = {};
    size_t size = sizeof(data);
    if (nvs_get_blob(handle, "center_v1", data, &size) == ESP_OK && size == sizeof(data) && data[0] == 1 &&
        data[1] >= 512 && data[1] <= 3583 && data[2] >= 512 && data[2] <= 3583 && data[3] >= 24 && data[3] <= 160) {
      config_.x_center = data[1]; config_.y_center = data[2]; neutral_zone_ = data[3];
    }
    nvs_close(handle);
  }
  ready_ = true;
  filter_valid_ = false;
  return true;
}

bool JoystickAnalog::read_raw(int& x, int& y) {
  if (!ready_ || !adc_handle) return false;
  int sx = 0, sy = 0;
  for (int i = 0; i < 4; ++i) {
    int rx, ry;
    if (adc_oneshot_read(adc_handle, config_.adc_channel_x, &rx) != ESP_OK ||
        adc_oneshot_read(adc_handle, config_.adc_channel_y, &ry) != ESP_OK) return false;
    sx += rx; sy += ry;
  }
  x = sx / 4; y = sy / 4;
  return true;
}

void JoystickAnalog::get_xy(int16_t& x, int16_t& y) {
  const uint32_t now = esp_timer_get_time() / 1000;
  // All consumers in an input pass share one sample and one filter update.
  if (!filter_valid_ || now - last_sample_ms_ >= 8) {
    int rx, ry;
    last_sample_ms_ = now;
    if (read_raw(rx, ry)) {
      if (!filter_valid_) { filtered_x_ = rx * 256; filtered_y_ = ry * 256; }
      else { filtered_x_ += (rx * 256 - filtered_x_) / 4; filtered_y_ += (ry * 256 - filtered_y_) / 4; }
      filter_valid_ = true;
    }
  }
  if (!filter_valid_) { x = y = 0; return; }
  auto centered = [this](int v) -> int16_t {
    if (std::abs(v) <= neutral_zone_) return 0;
    return v > 0 ? v - neutral_zone_ : v + neutral_zone_;
  };
  x = centered(filtered_x_ / 256 - config_.x_center);
  y = centered(filtered_y_ / 256 - config_.y_center);
}

void JoystickAnalog::start_calibration() {
  calibrating_ = true;
  calibration_start_ = esp_timer_get_time() / 1000;
  calibration_sample_ms_ = calibration_start_;
  samples_ = 0; sum_x_ = sum_y_ = 0;
  min_x_ = min_y_ = 4095; max_x_ = max_y_ = 0;
}

CalibrationResult JoystickAnalog::poll_calibration(uint8_t& progress) {
  if (!calibrating_) return CalibrationResult::Ready;
  if (!ready_) { calibrating_ = false; return CalibrationResult::Error; }
  const uint32_t now = esp_timer_get_time() / 1000;
  const uint32_t elapsed = now - calibration_start_;
  progress = static_cast<uint8_t>(std::min<uint32_t>(100, elapsed * 100 / 2000));
  // One second to release the stick, then a full second of stationary samples.
  if (elapsed < 1000) return CalibrationResult::Sampling;
  if (elapsed < 2000) {
    if (now - calibration_sample_ms_ < 10) return CalibrationResult::Sampling;
    calibration_sample_ms_ = now;
    int x, y;
    if (!read_raw(x, y)) { calibrating_ = false; return CalibrationResult::Error; }
    sum_x_ += x; sum_y_ += y; ++samples_;
    min_x_ = std::min(min_x_, x); max_x_ = std::max(max_x_, x);
    min_y_ = std::min(min_y_, y); max_y_ = std::max(max_y_, y);
    return CalibrationResult::Sampling;
  }
  calibrating_ = false;
  if (samples_ < 20) return CalibrationResult::Error;
  const uint16_t x = sum_x_ / samples_, y = sum_y_ / samples_;
  const int span = std::max(max_x_ - min_x_, max_y_ - min_y_);
  // The centre must leave every direction enough travel to pass the outer
  // threshold; a stick held off-centre during calibration would otherwise be
  // "saved" with one side permanently dead.
  const int reach = static_cast<int>(config_.threshold_outer) + 160;  // + the widest neutral zone
  const auto centred = [reach](uint16_t v) { return v >= reach && v <= 4095 - reach; };
  if (span > 120 || !centred(x) || !centred(y)) return CalibrationResult::Unstable;
  const uint16_t zone = static_cast<uint16_t>(std::clamp(span + 24, 32, 160));
  nvs_handle_t handle;
  if (nvs_open("joy_cal", NVS_READWRITE, &handle) != ESP_OK) return CalibrationResult::Error;
  const uint16_t data[] = {1, x, y, zone};
  const bool saved = nvs_set_blob(handle, "center_v1", data, sizeof(data)) == ESP_OK &&
      nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  if (!saved) return CalibrationResult::Error;
  config_.x_center = x; config_.y_center = y; neutral_zone_ = zone;
  filter_valid_ = false;
  last_direction_ = 255;
  last_change_ms_ = now;
  return CalibrationResult::Saved;
}

int8_t JoystickAnalog::read_direction() {
  int16_t x, y;
  get_xy(x, y);

  // Calculate distance from center
  float distance = sqrtf(x * x + y * y);

  // Check if within deadzone
  if (distance < config_.deadzone) {
    last_direction_ = 255;  // Center position
    engaged_ = false;
    return -1;
  }

  // A direction engages at the outer threshold and lets go a little inside
  // it. With one threshold, a stick resting right on it flickered between a
  // direction and centre, re-triggering notes on every flicker.
  const float threshold = engaged_ ? config_.threshold_outer * 0.8f : config_.threshold_outer;
  if (distance < threshold) {
    engaged_ = false;
    return -1;
  }
  engaged_ = true;

  // Calculate angle (0-360 degrees)
  // atan2f returns -PI to +PI, we convert to 0-360
  float angle_rad = atan2f(-y, x);  // Negate Y for screen coordinates
  float angle_deg = angle_rad * 180.0f / M_PI;
  if (angle_deg < 0) {
    angle_deg += 360.0f;
  }

  // Map to 8 directions (0-7)
  // Direction 0: RIGHT (E) = 0°
  // Direction 1: NORTHEAST  = 45°
  // Direction 2: UP (N)     = 90°
  // Direction 3: NORTHWEST  = 135°
  // Direction 4: LEFT (W)   = 180°
  // Direction 5: SOUTHWEST  = 225°
  // Direction 6: DOWN (S)   = 270°
  // Direction 7: SOUTHEAST  = 315°
  int8_t direction = (int8_t)((angle_deg + 22.5f) / 45.0f) % 8;

  // Debouncing: only change direction if enough time has passed
  constexpr uint32_t kDebounceMs = 200;
  uint32_t now_ms = esp_timer_get_time() / 1000;

  if (direction != last_direction_) {
    if (now_ms - last_change_ms_ >= kDebounceMs) {
      last_direction_ = direction;
      last_change_ms_ = now_ms;
      return direction;
    } else {
      // Not enough time passed, return previous direction
      return last_direction_ == 255 ? -1 : last_direction_;
    }
  }

  return direction;
}

bool JoystickAnalog::read_button() {
  if (config_.pin_sw < 0) {
    return false;
  }

  // Button connects to GND when pressed, so LOW = pressed
  return gpio_get_level((gpio_num_t)config_.pin_sw) == 0;
}

}  // namespace drivers
