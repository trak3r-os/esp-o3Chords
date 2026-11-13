#pragma once

#include "driver/gpio.h"

#include <cstdint>
#include <vector>

namespace drivers {

enum class RotaryEventType : uint8_t {
  kTurn,
  kClick,
  kLongClick,
};

struct RotaryEvent {
  RotaryEventType type = RotaryEventType::kTurn;
  int8_t delta = 0;
};

struct RotaryEncoderConfig {
  gpio_num_t pin_a = GPIO_NUM_NC;
  gpio_num_t pin_b = GPIO_NUM_NC;
  gpio_num_t pin_sw = GPIO_NUM_NC;
  bool invert_direction = false;
  uint8_t edges_per_detent = 4;
  uint32_t button_debounce_ms = 25;
  uint32_t long_press_ms = 650;
};

class RotaryEncoder {
 public:
  bool begin(const RotaryEncoderConfig& cfg);
  void poll(std::vector<RotaryEvent>& events, uint32_t now_ms);

 private:
  static void isr_trampoline(void* arg);
  void handle_edge_isr();
  uint8_t read_ab_state() const;
  bool read_button_pressed() const;

  RotaryEncoderConfig cfg_;
  bool ok_ = false;
  uint8_t last_ab_state_ = 0;
  int32_t edge_accum_ = 0;
  int32_t detent_accum_ = 0;
  uint32_t last_edge_ms_ = 0;
  static constexpr uint32_t kDetentRealignMs = 200;

  bool button_raw_ = false;
  bool button_stable_ = false;
  bool button_long_sent_ = false;
  uint32_t button_raw_changed_ms_ = 0;
  uint32_t button_pressed_ms_ = 0;
};

}  // namespace drivers
