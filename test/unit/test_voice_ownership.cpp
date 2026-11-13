#include "../framework.h"
#include "app/production_ui.h"
#include "app/config_codec.h"
#include "app/parameters.h"
#include "app/input_joystick.h"
#include "app/settings_bridge.h"
#include "app/state.h"
#include "app/joystick_state.h"
#include "engine/engine.h"
#include "engine/note_ledger.h"
#include "midi/router.h"
#include "midi/ble_midi.h"
#include "midi/usb_device_midi.h"
#include "esp_timer.h"

struct Capture final : midi::TransportSink {
  int ons = 0, offs = 0, last_channel = -1;
  void send_note_on(uint8_t c, uint8_t, uint8_t) override { ++ons; last_channel = c; }
  void send_note_off(uint8_t, uint8_t, uint8_t) override { ++offs; }
  void send_pitch_bend(uint8_t, int16_t) override {}
  void send_control_change(uint8_t, uint8_t, uint8_t) override {}
};

struct LedgerSink final : engine::NoteLedgerSink {
  midi::Router& router;
  explicit LedgerSink(midi::Router& r) : router(r) {}
  void note_on(uint8_t route, uint8_t c, uint8_t n, uint8_t v) override {
    router.route_note_on(1u << route, c, n, v);
  }
  void note_off(uint8_t route, uint8_t c, uint8_t n) override {
    router.route_note_off(1u << route, c, n, 0);
  }
};

struct Context {
  Capture capture;
  midi::Router router;
  LedgerSink sink{router};
  engine::NoteLedger ledger{&sink};
  app::AppState state;
  engine::Engine engine;
  midi::BleMidi ble{router};
  midi::UsbDeviceMidi usb{router};
  app::JoystickState joystick;
  app::UiNavState nav;
  std::vector<engine::ProgressionStep> steps;
  app::InputContext input{state, engine, router, ble, usb, joystick, nav, steps, 0, &ledger};
  app::prod::UiState ui;

  Context() {
    router.register_sink(&capture);
    state.sanitize();
    engine.begin();
    engine.set_router(&router);
    engine.set_note_ledger(&ledger);
    engine.set_route_mask(router.active_route_mask());
    engine.performer().set_humanize_ms(0);
    engine.performer().set_strum_ms(0);
    engine::ProgressionStep step{};
    step.scale_degree = 1;
    step.chord_quality = "maj";
    step.velocity = 100;
    steps.push_back(step);
    engine.progression().set_steps(steps);
    app::apply_settings(state, engine, ble, usb);
    app::prod::begin(ui, input);
  }

  void edit(app::contract::ParamId id, int8_t delta = 1) {
    ui.navigation_mode = app::contract::NavigationMode::Edit;
    ui.selected_parameter = id;
    app::prod::encoder_turn(input, ui, delta);
  }
};

static void lead_channel_zero_is_real_channel() {
  Context c;
  c.engine.set_channel(5);
  c.engine.set_lead_channel(0);
  c.engine.play_notes({60}, 100, engine::TriggerMode::kHeld);
  g_test_time_us += 50000;
  esp_timer_fire_pending();
  ASSERT_EQ(c.capture.last_channel, 0);
}

static void chord_does_not_release_lead() {
  Context c;
  c.engine.play_notes({90}, 100, engine::TriggerMode::kHeld);
  g_test_time_us += 50000;
  esp_timer_fire_pending();
  const int before = c.capture.offs;
  c.engine.trigger_step(0, engine::TriggerMode::kHeld);
  ASSERT_EQ(c.capture.offs - before, 0);
}

static void pre_panic_generation_is_rejected() {
  engine::NoteLedger ledger;
  ledger.invalidate_source(42);
  const auto old_generation = ledger.generation_for(42);
  ledger.panic();
  app::contract::VoiceToken token;
  ASSERT_FALSE(ledger.acquire({42, old_generation, 0, 60, 100, 1}, token));
}

static void triad_admission_is_atomic() {
  Context c;
  app::contract::VoiceToken token;
  for (int i = 0; i < 31; ++i)
    ASSERT_TRUE(c.ledger.acquire({999, 0, 0, static_cast<uint8_t>(i), 100, 1}, token));
  const int before = c.capture.ons;
  c.engine.play_notes({60, 64, 67}, 100, engine::TriggerMode::kHeld);
  g_test_time_us += 50000;
  esp_timer_fire_pending();
  ASSERT_EQ(c.capture.ons - before, 0);
  ASSERT_FALSE(c.engine.held_active());
}

static void lead_channel_edit_releases_drone_owner() {
  Context c;
  auto config = c.state.config();
  config.active_preset = app::PresetId::kDrone;
  app::config::recall_preset(config.joy_families, config.active_preset);
  c.state.set_config(config);
  app::config::apply_to_settings(config, c.state.settings());
  app::dispatch_keypad_joy_direction(c.input, 0);
  ASSERT_GT(c.ledger.active_tokens(), 0);
  const auto before = c.ledger.active_tokens();
  c.edit(app::contract::ParamId::LeadChannel);
  ASSERT_LT(c.ledger.active_tokens(), before);
}

int main() {
  TEST_SUITE("Voice ownership across chord, lead and drone");
  RUN_TEST(lead_channel_zero_is_real_channel);
  RUN_TEST(chord_does_not_release_lead);
  RUN_TEST(pre_panic_generation_is_rejected);
  RUN_TEST(triad_admission_is_atomic);
  RUN_TEST(lead_channel_edit_releases_drone_owner);
  test_print_result();
  return g_fail ? 1 : 0;
}
