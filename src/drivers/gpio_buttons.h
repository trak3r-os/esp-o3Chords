#pragma once

#include "driver/gpio.h"

#include <cstdint>
#include <vector>

namespace drivers {

enum class GpioButtonId : uint8_t {
  kButton1,
  kButton2,
};

enum class GpioButtonEventType : uint8_t {
  kClick,
  kLongClick,
};

struct GpioButtonEvent {
  GpioButtonId id = GpioButtonId::kButton1;
  GpioButtonEventType type = GpioButtonEventType::kClick;
};

struct GpioButtonsConfig {
  gpio_num_t button1 = GPIO_NUM_NC;
  gpio_num_t button2 = GPIO_NUM_NC;
  uint32_t debounce_ms = 25;
  uint32_t long_press_ms = 650;
};

class GpioButtons {
 public:
  bool begin(const GpioButtonsConfig& cfg);
  void poll(std::vector<GpioButtonEvent>& events, uint32_t now_ms);

 private:
  struct State {
    gpio_num_t pin = GPIO_NUM_NC;
    GpioButtonId id = GpioButtonId::kButton1;
    bool raw = false;
    bool stable = false;
    bool long_sent = false;
    uint32_t raw_changed_ms = 0;
    uint32_t pressed_ms = 0;
  };

  void poll_one(State& state, std::vector<GpioButtonEvent>& events,
                uint32_t now_ms);
  static bool valid_pin(gpio_num_t pin);
  static bool read_pressed(gpio_num_t pin);

  GpioButtonsConfig cfg_;
  bool ok_ = false;
  State buttons_[2];
};

}  // namespace drivers
