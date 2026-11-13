#include "../framework.h"
#include "app/input_context.h"
#include "app/input_joystick.h"
#include "app/config_codec.h"
#include "app/parameters.h"
#include "app/state.h"
#include "engine/engine.h"
#include "engine/note_ledger.h"
#include "midi/router.h"

#include <vector>

struct Capture : midi::TransportSink {
  int ons = 0;
  int offs = 0;
  void send_note_on(uint8_t, uint8_t, uint8_t) override { ++ons; }
  void send_note_off(uint8_t, uint8_t, uint8_t) override { ++offs; }
  void send_pitch_bend(uint8_t, int16_t) override {}
  void send_control_change(uint8_t, uint8_t, uint8_t) override {}
};

struct LedgerRouterSink : engine::NoteLedgerSink {
  midi::Router& router;
  explicit LedgerRouterSink(midi::Router& r) : router(r) {}
  void note_on(uint8_t, uint8_t ch, uint8_t n, uint8_t v) override { router.note_on(ch, n, v); }
  void note_off(uint8_t, uint8_t ch, uint8_t n) override { router.note_off(ch, n, 0); }
};

static void test_drone_engine_shared_ledger_is_transactional() {
  Capture capture;
  midi::Router router;
  router.register_sink(&capture);
  LedgerRouterSink ledger_sink(router);
  engine::NoteLedger ledger(&ledger_sink);
  engine::Engine engine;
  engine.begin();
  engine.set_router(&router);
  engine.set_note_ledger(&ledger);

  app::AppState state;
  state.sanitize();
  midi::BleMidi ble(router);
  midi::UsbDeviceMidi usb(router);
  app::JoystickState joystick;
  app::UiNavState nav;
  std::vector<engine::ProgressionStep> play_steps;
  app::InputContext ctx{state, engine, router, ble, usb, joystick, nav, play_steps, 0, &ledger};

  engine::ProgressionStep step{};
  step.scale_degree = 1;
  step.chord_quality = "maj";
  step.velocity = 100;
  engine.progression().set_steps({step});

  g_test_time_us = 0;
  engine.play_notes({60}, 100);
  g_test_time_us = 10 * 1000;
  esp_timer_fire_pending();
  ASSERT_EQ(ledger.active_tokens(), 1);

  // 30 active owners leave fewer slots than the three-note Drone requires.
  app::contract::VoiceToken fillers[29]{};
  for (uint8_t i = 0; i < 29; ++i)
    ASSERT_TRUE(ledger.acquire({0x9000, 0, 0, static_cast<uint8_t>(i), 90, 1}, fillers[i]));
  ASSERT_EQ(ledger.active_tokens(), 30);
  const int ons_before = capture.ons;

  auto config = state.config();
  config.active_preset = app::PresetId::kDrone;
  app::config::recall_preset(config.joy_families, config.active_preset);
  state.set_config(config);
  app::config::apply_to_settings(config, state.settings());
  // Drone's Voices=chord is the preset default now (no more drone_sub_mode
  // runtime toggle) — the "maj" step already gives the three-note triad.
  joystick.keypad_dir = -1;
  joystick.last_dir = -1;
  app::dispatch_keypad_joy_direction(ctx, 0);

  ASSERT_EQ(capture.ons, ons_before);
  ASSERT_EQ(ledger.active_tokens(), 30);
  ASSERT_EQ(joystick.drone_active_mask, 0);
  ASSERT_EQ(joystick.drone_chords[0].size(), size_t(0));
  ASSERT_EQ(ledger.generation_for(0x0200), 2u);

  // Free one slot: the next valid gesture admits all three notes and keeps
  // the Engine's already active voice intact.
  ASSERT_TRUE(ledger.release(fillers[28]));
  joystick.keypad_dir = -1;
  joystick.last_dir = -1;
  app::dispatch_keypad_joy_direction(ctx, 0);
  ASSERT_EQ(joystick.drone_chords[0].size(), size_t(3));
  ASSERT_NE(joystick.drone_active_mask & 1u, 0u);
  ASSERT_EQ(ledger.active_tokens(), 32);
  ASSERT_EQ(capture.ons, ons_before + 3);
}

int main() {
  TEST_SUITE("Drone/Engine admission through the shared note ledger");
  RUN_TEST(test_drone_engine_shared_ledger_is_transactional);
  return g_fail;
}
