#include "drivers/gpio_buttons.h"

#include "esp_log.h"

namespace drivers {
namespace {

constexpr const char* TAG = "Buttons";

}  // namespace

bool GpioButtons::begin(const GpioButtonsConfig& cfg) {
  if (!valid_pin(cfg.button1) || !valid_pin(cfg.button2)) {
    ESP_LOGW(TAG, "UI buttons disabled; missing pin assignment");
    return false;
  }

  cfg_ = cfg;
  gpio_config_t conf = {};
  conf.intr_type = GPIO_INTR_DISABLE;
  conf.mode = GPIO_MODE_INPUT;
  conf.pin_bit_mask = (1ULL << cfg_.button1) | (1ULL << cfg_.button2);
  conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  conf.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&conf);

  buttons_[0].pin = cfg_.button1;
  buttons_[0].id = GpioButtonId::kButton1;
  buttons_[1].pin = cfg_.button2;
  buttons_[1].id = GpioButtonId::kButton2;

  for (auto& button : buttons_) {
    button.raw = read_pressed(button.pin);
    button.stable = button.raw;
    button.long_sent = button.stable;  // held at power-up is not a long press
  }

  ok_ = true;
  ESP_LOGI(TAG, "UI buttons initialized B1=GPIO%d B2=GPIO%d",
           (int)cfg_.button1, (int)cfg_.button2);
  return true;
}

void GpioButtons::poll(std::vector<GpioButtonEvent>& events, uint32_t now_ms) {
  events.clear();
  if (!ok_) return;
  for (auto& button : buttons_) {
    poll_one(button, events, now_ms);
  }
}

void GpioButtons::poll_one(State& state, std::vector<GpioButtonEvent>& events,
                           uint32_t now_ms) {
  const bool raw = read_pressed(state.pin);
  if (raw != state.raw) {
    state.raw = raw;
    state.raw_changed_ms = now_ms;
  }

  if ((now_ms - state.raw_changed_ms) >= cfg_.debounce_ms &&
      state.stable != state.raw) {
    state.stable = state.raw;
    if (state.stable) {
      state.pressed_ms = now_ms;
      state.long_sent = false;
    } else {
      if (!state.long_sent) {
        events.push_back({state.id, GpioButtonEventType::kClick});
      }
      state.long_sent = false;
    }
  }

  if (state.stable && !state.long_sent &&
      (now_ms - state.pressed_ms) >= cfg_.long_press_ms) {
    state.long_sent = true;
    events.push_back({state.id, GpioButtonEventType::kLongClick});
  }
}

bool GpioButtons::valid_pin(gpio_num_t pin) {
  return pin != GPIO_NUM_NC;
}

bool GpioButtons::read_pressed(gpio_num_t pin) {
  return gpio_get_level(pin) == 0;
}

}  // namespace drivers
