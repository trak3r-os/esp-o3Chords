#pragma once

#include <stdint.h>

#include "driver/adc.h"

namespace drivers {

struct JoystickAnalogConfig {
  adc_unit_t adc_unit = ADC_UNIT_1;
  adc_channel_t adc_channel_x = ADC_CHANNEL_0;
  adc_channel_t adc_channel_y = ADC_CHANNEL_1;

  int pin_x = -1;      // For logging only
  int pin_y = -1;      // For logging only
  int pin_sw = -1;     // Digital pin for button/switch

  // ADC calibration
  uint16_t x_center = 2048;   // 12-bit center (0-4095)
  uint16_t y_center = 2048;
  uint16_t deadzone = 400;    // Deadzone radius

  // Direction thresholds
  uint16_t threshold_outer = 1200;  // Minimum distance from center for direction
};

enum class CalibrationResult : uint8_t { Ready, Sampling, Saved, Unstable, Error };

class JoystickAnalog {
 public:
  bool begin(const JoystickAnalogConfig& config = {});
  int8_t read_direction();  // Returns 0-7 for 8 directions, -1 for center
  bool read_button();       // Returns true if button pressed
  void get_xy(int16_t& x, int16_t& y);  // Filtered, centered, neutral-clamped ADC units

  // The application layer works in a fixed +/-kAxisFullScale position range,
  // independent of ADC resolution. get_xy() reports centred 12-bit ADC units
  // (roughly +/-2000 at the end stops), so every consumer goes through this
  // one conversion. kAdcReach sits short of the theoretical 2048 so a stick
  // that is slightly off-centre, or that never quite hits the rail, still
  // reaches full scale in every direction.
  static constexpr int16_t kAxisFullScale = 512;
  static constexpr int16_t kAdcReach = 1850;
  static constexpr int16_t normalize_axis(int16_t centered_adc) {
    const int32_t v = static_cast<int32_t>(centered_adc) * kAxisFullScale / kAdcReach;
    return static_cast<int16_t>(v > kAxisFullScale ? kAxisFullScale : v < -kAxisFullScale ? -kAxisFullScale : v);
  }
  void get_xy_normalized(int16_t& x, int16_t& y) {
    get_xy(x, y);
    x = normalize_axis(x);
    y = normalize_axis(y);
  }
  void start_calibration();
  CalibrationResult poll_calibration(uint8_t& progress);
  void cancel_calibration() { calibrating_ = false; }
  uint16_t center_x() const { return config_.x_center; }
  uint16_t center_y() const { return config_.y_center; }
  uint16_t neutral_zone() const { return neutral_zone_; }

 private:
  bool read_raw(int& x, int& y);
  JoystickAnalogConfig config_{};
  bool ready_ = false;
  bool filter_valid_ = false;
  int32_t filtered_x_ = 0, filtered_y_ = 0;
  uint32_t last_sample_ms_ = 0;
  uint16_t neutral_zone_ = 48;
  bool calibrating_ = false;
  uint32_t calibration_start_ = 0, calibration_sample_ms_ = 0;
  uint32_t sum_x_ = 0, sum_y_ = 0;
  uint16_t samples_ = 0;
  int min_x_ = 4095, max_x_ = 0, min_y_ = 4095, max_y_ = 0;
  uint8_t last_direction_ = 255;
  bool engaged_ = false;  // past the outer threshold; released a little inside it
  uint32_t last_change_ms_ = 0;
};

}  // namespace drivers
