#include "drivers/rotary_encoder.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

namespace drivers {
namespace {

constexpr const char* TAG = "Rotary";

portMUX_TYPE g_rotary_mux = portMUX_INITIALIZER_UNLOCKED;

int8_t transition_delta(uint8_t transition) {
  static constexpr int8_t kTable[16] = {
      0, -1,  1,  0,
      1,  0,  0, -1,
     -1,  0,  0,  1,
      0,  1, -1,  0,
  };
  return kTable[transition & 0x0F];
}

bool valid_pin(gpio_num_t pin) {
  return pin != GPIO_NUM_NC;
}

}  // namespace

bool RotaryEncoder::begin(const RotaryEncoderConfig& cfg) {
  if (!valid_pin(cfg.pin_a) || !valid_pin(cfg.pin_b) || !valid_pin(cfg.pin_sw)) {
    ESP_LOGW(TAG, "Rotary encoder disabled; missing pin assignment");
    return false;
  }

  cfg_ = cfg;
  if (cfg_.edges_per_detent == 0) cfg_.edges_per_detent = 4;

  gpio_config_t ab_conf = {};
  ab_conf.intr_type = GPIO_INTR_ANYEDGE;
  ab_conf.mode = GPIO_MODE_INPUT;
  ab_conf.pin_bit_mask = (1ULL << cfg_.pin_a) | (1ULL << cfg_.pin_b);
  ab_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  ab_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&ab_conf);

  gpio_config_t sw_conf = {};
  sw_conf.intr_type = GPIO_INTR_DISABLE;
  sw_conf.mode = GPIO_MODE_INPUT;
  sw_conf.pin_bit_mask = (1ULL << cfg_.pin_sw);
  sw_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  sw_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&sw_conf);

  last_ab_state_ = read_ab_state();
  button_raw_ = read_button_pressed();
  button_stable_ = button_raw_;
  // A button already down at power-up is not a press: without this the first
  // poll saw it "held" since time zero and fired a long click.
  button_long_sent_ = button_stable_;

  esp_err_t err = gpio_install_isr_service(0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "ISR service unavailable: %s", esp_err_to_name(err));
    return false;
  }

  gpio_isr_handler_add(cfg_.pin_a, &RotaryEncoder::isr_trampoline, this);
  gpio_isr_handler_add(cfg_.pin_b, &RotaryEncoder::isr_trampoline, this);

  ok_ = true;
  ESP_LOGI(TAG, "Rotary encoder initialized A=GPIO%d B=GPIO%d SW=GPIO%d",
           (int)cfg_.pin_a, (int)cfg_.pin_b, (int)cfg_.pin_sw);
  return true;
}

void RotaryEncoder::poll(std::vector<RotaryEvent>& events, uint32_t now_ms) {
  events.clear();
  if (!ok_) return;

  int32_t edge_delta = 0;
  portENTER_CRITICAL(&g_rotary_mux);
  edge_delta = edge_accum_;
  edge_accum_ = 0;
  portEXIT_CRITICAL(&g_rotary_mux);

  // Realign to the detent: a bounced or missed edge leaves a partial count
  // behind, which would make every later click register one edge early or
  // late (or the first click after it do nothing). Once the knob has rested,
  // a partial count is noise, not half a click.
  if (edge_delta != 0) {
    last_edge_ms_ = now_ms;
  } else if (detent_accum_ != 0 && (now_ms - last_edge_ms_) >= kDetentRealignMs) {
    detent_accum_ = 0;
  }

  detent_accum_ += edge_delta;
  while (detent_accum_ >= cfg_.edges_per_detent) {
    events.push_back({RotaryEventType::kTurn, 1});
    detent_accum_ -= cfg_.edges_per_detent;
  }
  while (detent_accum_ <= -static_cast<int32_t>(cfg_.edges_per_detent)) {
    events.push_back({RotaryEventType::kTurn, -1});
    detent_accum_ += cfg_.edges_per_detent;
  }

  const bool raw = read_button_pressed();
  if (raw != button_raw_) {
    button_raw_ = raw;
    button_raw_changed_ms_ = now_ms;
  }

  if ((now_ms - button_raw_changed_ms_) >= cfg_.button_debounce_ms &&
      button_stable_ != button_raw_) {
    button_stable_ = button_raw_;
    if (button_stable_) {
      button_pressed_ms_ = now_ms;
      button_long_sent_ = false;
    } else {
      if (!button_long_sent_) {
        events.push_back({RotaryEventType::kClick, 0});
      }
      button_long_sent_ = false;
    }
  }

  if (button_stable_ && !button_long_sent_ &&
      (now_ms - button_pressed_ms_) >= cfg_.long_press_ms) {
    button_long_sent_ = true;
    events.push_back({RotaryEventType::kLongClick, 0});
  }
}

void RotaryEncoder::isr_trampoline(void* arg) {
  static_cast<RotaryEncoder*>(arg)->handle_edge_isr();
}

void RotaryEncoder::handle_edge_isr() {
  const uint8_t state = read_ab_state();
  const uint8_t transition = static_cast<uint8_t>((last_ab_state_ << 2) | state);
  int8_t delta = transition_delta(transition);
  if (cfg_.invert_direction) delta = static_cast<int8_t>(-delta);
  last_ab_state_ = state;

  if (delta != 0) {
    portENTER_CRITICAL_ISR(&g_rotary_mux);
    edge_accum_ += delta;
    portEXIT_CRITICAL_ISR(&g_rotary_mux);
  }
}

uint8_t RotaryEncoder::read_ab_state() const {
  const uint8_t a = gpio_get_level(cfg_.pin_a) ? 1 : 0;
  const uint8_t b = gpio_get_level(cfg_.pin_b) ? 1 : 0;
  return static_cast<uint8_t>((a << 1) | b);
}

bool RotaryEncoder::read_button_pressed() const {
  return gpio_get_level(cfg_.pin_sw) == 0;
}

}  // namespace drivers
