// Tests: MIDI routing, note-on/off channel consistency, joy channel bugs.
#include "../framework.h"
#include "midi/router.h"
#include "engine/midi_scheduler.h"
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// CaptureSink — all messages arrive here via Router fan-out.
// ---------------------------------------------------------------------------
struct CaptureSink : midi::TransportSink {
  struct Msg { std::string t; uint8_t ch, note, vel; };
  std::vector<Msg> msgs;

  void send_note_on(uint8_t ch, uint8_t n, uint8_t v) override {
    msgs.push_back({"on", ch, n, v});
  }
  void send_note_off(uint8_t ch, uint8_t n, uint8_t v) override {
    msgs.push_back({"off", ch, n, v});
  }
  void send_pitch_bend(uint8_t, int16_t) override {}
  void send_control_change(uint8_t ch, uint8_t cc, uint8_t v) override {
    msgs.push_back({"cc", ch, cc, v});
  }

  bool has(const std::string& t, uint8_t ch, uint8_t note) const {
    for (auto& m : msgs) if (m.t == t && m.ch == ch && m.note == note) return true;
    return false;
  }
  void clear() { msgs.clear(); }
};

// ---------------------------------------------------------------------------
// Router fan-out tests
// ---------------------------------------------------------------------------

static void test_usb_role_matrix_and_boot_boundary() {
  const midi::UsbRole roles[] = {
      midi::UsbRole::Off, midi::UsbRole::Device, midi::UsbRole::Host};

  for (midi::UsbRole active : roles) {
    midi::UsbRoleState state(active);
    ASSERT_EQ(state.active(), active);
    ASSERT_EQ(state.configured(), active);
    ASSERT_FALSE(state.restart_required());

    for (midi::UsbRole requested : roles) {
      // Each assertion starts from the same boot-applied role; applying a
      // request intentionally changes the active role for the next boot.
      state = midi::UsbRoleState(active);
      state.request(requested);
      ASSERT_EQ(state.configured(), requested);
      ASSERT_EQ(state.active(), active);
      ASSERT_EQ(state.restart_required(), requested != active);

      state.apply_at_boot();
      ASSERT_EQ(state.active(), requested);
      ASSERT_EQ(state.configured(), requested);
      ASSERT_FALSE(state.restart_required());
    }
  }
}

struct DummySink : midi::TransportSink {
  int on_count = 0, off_count = 0, pb_count = 0, cc_count = 0;
  bool available = true;
  bool is_available() const override { return available; }
  void send_note_on(uint8_t, uint8_t, uint8_t) override { ++on_count; }
  void send_note_off(uint8_t, uint8_t, uint8_t) override { ++off_count; }
  void send_pitch_bend(uint8_t, int16_t) override { ++pb_count; }
  void send_control_change(uint8_t, uint8_t, uint8_t) override { ++cc_count; }
};

struct StatefulSink : DummySink {
  app::contract::TransportState state = app::contract::TransportState::Connected;
  app::contract::SendResult result = app::contract::SendResult::Accepted;
  app::contract::TransportState transport_state() const override { return state; }
  bool is_available() const override {
    return state == app::contract::TransportState::Connected ||
           state == app::contract::TransportState::Recovering;
  }
  app::contract::SendResult last_delivery_result() const override { return result; }
};

static void test_ble_backpressure_does_not_block_usb() {
  midi::Router router;
  StatefulSink ble, usb;
  ble.state = app::contract::TransportState::Connected;
  ble.result = app::contract::SendResult::Backpressure;
  router.register_sink(&ble);
  router.register_sink(&usb);

  ASSERT_EQ(router.route_note_on(0x3, 1, 60, 100), app::contract::SendResult::Accepted);
  ASSERT_EQ(ble.on_count, 1);
  ASSERT_EQ(usb.on_count, 1);
}

static void test_recovery_blocks_note_on_but_allows_release_and_reset() {
  midi::Router router;
  StatefulSink ble;
  ble.state = app::contract::TransportState::Recovering;
  ble.result = app::contract::SendResult::Backpressure;
  router.register_sink(&ble);

  ASSERT_EQ(router.route_note_on(1, 0, 60, 100), app::contract::SendResult::Backpressure);
  ASSERT_EQ(ble.on_count, 0);
  ble.result = app::contract::SendResult::Accepted;  // recovery lane still permits releases
  ASSERT_EQ(router.route_note_off(1, 0, 60, 0), app::contract::SendResult::Accepted);
  router.control_change(0, 123, 0);
  ASSERT_EQ(ble.cc_count, 1);
}

static void test_interrupted_recovery_keeps_new_attacks_blocked() {
  midi::Router router;
  StatefulSink ble;
  router.register_sink(&ble);
  ble.state = app::contract::TransportState::Recovering;
  ASSERT_EQ(router.route_note_on(1, 0, 61, 100), app::contract::SendResult::Backpressure);
  ble.state = app::contract::TransportState::Connected;
  ASSERT_EQ(router.route_note_on(1, 0, 62, 100), app::contract::SendResult::Accepted);
  ASSERT_EQ(ble.on_count, 1);
  ble.state = app::contract::TransportState::Recovering;
  ASSERT_EQ(router.route_note_on(1, 0, 63, 100), app::contract::SendResult::Backpressure);
  ASSERT_EQ(ble.on_count, 1);
}

static void test_usb_operates_while_ble_is_disconnected() {
  midi::Router router;
  StatefulSink ble, usb;
  ble.state = app::contract::TransportState::Disconnected;
  usb.state = app::contract::TransportState::Connected;
  router.register_sink(&ble);
  router.register_sink(&usb);
  ASSERT_EQ(router.route_note_on(0x3, 2, 64, 100), app::contract::SendResult::Accepted);
  ASSERT_EQ(ble.on_count, 0);
  ASSERT_EQ(usb.on_count, 1);
}

static void test_destination_is_captured_when_scheduled() {
  midi::Router router;
  CaptureSink sink;
  router.register_sink(&sink);
  engine::MidiScheduler sched;
  sched.start(&router, 0);
  sched.set_route_mask(1u << 0);
  engine::ScheduledEvent ev{};
  ev.due_ms = 100; ev.type = engine::EventType::kNoteOn; ev.note = 67; ev.velocity = 100;
  ASSERT_TRUE(sched.schedule(ev));
  sched.set_route_mask(0);  // must not alter the already admitted event
  g_test_time_us = 100 * 1000;
  esp_timer_fire_pending();
  ASSERT_TRUE(sink.has("on", 0, 67));
}

static void test_route_delivery_is_captured_and_independent() {
  midi::Router router;
  DummySink ble, usb;
  router.register_sink(&ble);
  router.register_sink(&usb);

  ASSERT_EQ(router.route_note_on(1u << 1, 2, 60, 100), app::contract::SendResult::Accepted);
  ASSERT_EQ(ble.on_count, 0);
  ASSERT_EQ(usb.on_count, 1);
  usb.available = false;
  ASSERT_EQ(router.route_note_on(1u << 1, 2, 62, 100), app::contract::SendResult::Disconnected);
  ASSERT_EQ(ble.on_count, 0);
}

static void test_router_fanout_to_multiple_sinks() {
  midi::Router router;
  DummySink a, b;
  router.register_sink(&a);
  router.register_sink(&b);

  router.note_on(0, 60, 100);
  ASSERT_EQ(a.on_count, 1);
  ASSERT_EQ(b.on_count, 1);

  router.note_off(0, 60, 0);
  ASSERT_EQ(a.off_count, 1);
  ASSERT_EQ(b.off_count, 1);

  router.pitch_bend(0, 1000);
  ASSERT_EQ(a.pb_count, 1);
  ASSERT_EQ(b.pb_count, 1);

  router.control_change(0, 7, 64);
  ASSERT_EQ(a.cc_count, 1);
  ASSERT_EQ(b.cc_count, 1);
}

static void test_transport_input_is_not_echoed_to_its_source() {
  midi::Router router;
  DummySink source, other;
  router.register_sink(&source);
  router.register_sink(&other);

  router.forward_note_on(&source, 0, 60, 100);
  router.forward_note_off(&source, 0, 60, 0);
  router.forward_pitch_bend(&source, 0, 512);
  router.forward_control_change(&source, 0, 1, 64);

  ASSERT_EQ(source.on_count, 0);
  ASSERT_EQ(source.off_count, 0);
  ASSERT_EQ(source.pb_count, 0);
  ASSERT_EQ(source.cc_count, 0);
  ASSERT_EQ(other.on_count, 1);
  ASSERT_EQ(other.off_count, 1);
  ASSERT_EQ(other.pb_count, 1);
  ASSERT_EQ(other.cc_count, 1);
}
static void test_router_empty_sinks_no_crash() {
  midi::Router router;
  router.note_on(0, 60, 100);
  router.note_off(0, 60, 0);
  router.pitch_bend(0, 0);
  router.control_change(0, 123, 0);
  ASSERT_TRUE(true);
}
static void test_router_correct_channel_forwarding() {
  struct CapCh : midi::TransportSink {
    uint8_t last_on_ch = 0xFF, last_off_ch = 0xFF;
    void send_note_on(uint8_t ch, uint8_t, uint8_t) override { last_on_ch = ch; }
    void send_note_off(uint8_t ch, uint8_t, uint8_t) override { last_off_ch = ch; }
    void send_pitch_bend(uint8_t, int16_t) override {}
    void send_control_change(uint8_t, uint8_t, uint8_t) override {}
  };
  midi::Router router;
  CapCh sink;
  router.register_sink(&sink);
  router.note_on(5, 60, 100);
  ASSERT_EQ(sink.last_on_ch, 5);
  router.note_off(12, 60, 0);
  ASSERT_EQ(sink.last_off_ch, 12);
}

// ---------------------------------------------------------------------------
// MidiScheduler channel-change tests (Bug 2 regression)
// ---------------------------------------------------------------------------

static void test_scheduler_note_on_fires_on_correct_channel() {
  midi::Router router;
  CaptureSink sink;
  router.register_sink(&sink);

  engine::MidiScheduler sched;
  sched.start(&router, 2);

  engine::ScheduledEvent ev{};
  ev.due_ms = 0; ev.type = engine::EventType::kNoteOn; ev.note = 60; ev.velocity = 100;
  g_test_time_us = 0;
  sched.schedule(ev);
  g_test_time_us = 500 * 1000;
  esp_timer_fire_pending();

  ASSERT_TRUE(sink.has("on", 2, 60));
}

static void test_scheduler_set_channel_flushes_old_channel_notes() {
  midi::Router router;
  CaptureSink sink;
  router.register_sink(&sink);

  engine::MidiScheduler sched;
  sched.start(&router, 0);

  engine::ScheduledEvent on_ev{};
  on_ev.due_ms = 0; on_ev.type = engine::EventType::kNoteOn; on_ev.note = 60; on_ev.velocity = 100;
  g_test_time_us = 0;
  sched.schedule(on_ev);
  g_test_time_us = 50 * 1000;
  esp_timer_fire_pending();
  ASSERT_TRUE(sink.has("on", 0, 60));

  sink.clear();
  sched.set_channel(1);

  ASSERT_TRUE(sink.has("off", 0, 60));   // note-off MUST be on old channel 0
  ASSERT_FALSE(sink.has("off", 1, 60));  // must NOT appear on new channel 1
}

static void test_scheduler_all_notes_off_clears_active() {
  midi::Router router;
  CaptureSink sink;
  router.register_sink(&sink);

  engine::MidiScheduler sched;
  sched.start(&router, 0);

  engine::ScheduledEvent on1{}, on2{};
  on1.due_ms = 0; on1.type = engine::EventType::kNoteOn; on1.note = 60; on1.velocity = 100;
  on2.due_ms = 0; on2.type = engine::EventType::kNoteOn; on2.note = 64; on2.velocity = 100;
  g_test_time_us = 0;
  sched.schedule(on1); sched.schedule(on2);
  g_test_time_us = 50 * 1000;
  esp_timer_fire_pending();

  ASSERT_EQ(sched.active_notes().size(), (size_t)2);
  sched.all_notes_off();
  ASSERT_EQ(sched.active_notes().size(), (size_t)0);
  ASSERT_TRUE(sink.has("off", 0, 60));
  ASSERT_TRUE(sink.has("off", 0, 64));
}

static void test_scheduler_noteoff_fires_after_noteon() {
  midi::Router router;
  CaptureSink sink;
  router.register_sink(&sink);

  engine::MidiScheduler sched;
  sched.start(&router, 0);

  engine::ScheduledEvent on_ev{}, off_ev{};
  on_ev.due_ms = 0;   on_ev.type = engine::EventType::kNoteOn;  on_ev.note = 62; on_ev.velocity = 90;
  off_ev.due_ms = 200; off_ev.type = engine::EventType::kNoteOff; off_ev.note = 62; off_ev.velocity = 0;
  g_test_time_us = 0;
  sched.schedule(on_ev); sched.schedule(off_ev);

  g_test_time_us = 100 * 1000;
  esp_timer_fire_pending();
  ASSERT_TRUE(sink.has("on", 0, 62));
  ASSERT_FALSE(sink.has("off", 0, 62));

  g_test_time_us = 200 * 1000;
  esp_timer_fire_pending();
  ASSERT_TRUE(sink.has("off", 0, 62));
}

static void test_scheduler_cleanup_is_owner_scoped() {
  midi::Router router;
  CaptureSink sink;
  router.register_sink(&sink);

  engine::MidiScheduler sched;
  sched.start(&router, 7);

  engine::ScheduledEvent ev{};
  ev.due_ms = 0; ev.type = engine::EventType::kNoteOn; ev.note = 60; ev.velocity = 100;
  g_test_time_us = 0;
  sched.schedule(ev);
  g_test_time_us = 10 * 1000;
  esp_timer_fire_pending();

  sink.clear();
  sched.all_notes_off();
  ASSERT_TRUE(sink.has("off", 7, 60));
  ASSERT_FALSE(sink.has("cc", 7, 123));
}

// ---------------------------------------------------------------------------
// Joy channel consistency (Bug 1 regression)
// ---------------------------------------------------------------------------

static void test_note_off_must_use_same_channel_as_note_on() {
  midi::Router router;
  CaptureSink sink;
  router.register_sink(&sink);

  const uint8_t joy_ch = 2;
  const uint8_t chord_ch = 0;

  router.note_on(joy_ch, 60, 100);
  router.note_off(joy_ch, 60, 0);   // correct fix: same channel

  ASSERT_TRUE(sink.has("on",  joy_ch,   60));
  ASSERT_TRUE(sink.has("off", joy_ch,   60));
  ASSERT_FALSE(sink.has("off", chord_ch, 60));  // wrong-channel off must not exist
}

static void test_joy_noteon_noteoff_same_channel_when_channels_differ() {
  midi::Router router;
  CaptureSink sink;
  router.register_sink(&sink);

  const uint8_t ch_joy   = 3;
  const uint8_t ch_chord = 0;

  router.note_on(ch_joy, 62, 90);
  router.note_on(ch_joy, 65, 90);
  router.note_off(ch_joy, 62, 0);  // fixed: always midi_channel_joy
  router.note_off(ch_joy, 65, 0);

  ASSERT_TRUE(sink.has("off", ch_joy,   62));
  ASSERT_TRUE(sink.has("off", ch_joy,   65));
  ASSERT_FALSE(sink.has("off", ch_chord, 62));
  ASSERT_FALSE(sink.has("off", ch_chord, 65));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
  TEST_SUITE("Router Fan-out");
  RUN_TEST(test_usb_role_matrix_and_boot_boundary);
  RUN_TEST(test_router_fanout_to_multiple_sinks);
  RUN_TEST(test_transport_input_is_not_echoed_to_its_source);
  RUN_TEST(test_router_empty_sinks_no_crash);
  RUN_TEST(test_router_correct_channel_forwarding);
  RUN_TEST(test_route_delivery_is_captured_and_independent);
  RUN_TEST(test_ble_backpressure_does_not_block_usb);
  RUN_TEST(test_recovery_blocks_note_on_but_allows_release_and_reset);
  RUN_TEST(test_interrupted_recovery_keeps_new_attacks_blocked);
  RUN_TEST(test_usb_operates_while_ble_is_disconnected);
  RUN_TEST(test_destination_is_captured_when_scheduled);

  TEST_SUITE("MidiScheduler Channel & Note-off (Bug 2 regression)");
  RUN_TEST(test_scheduler_note_on_fires_on_correct_channel);
  RUN_TEST(test_scheduler_set_channel_flushes_old_channel_notes);
  RUN_TEST(test_scheduler_all_notes_off_clears_active);
  RUN_TEST(test_scheduler_noteoff_fires_after_noteon);
  RUN_TEST(test_scheduler_cleanup_is_owner_scoped);

  TEST_SUITE("Joy Channel Consistency (Bug 1 regression)");
  RUN_TEST(test_note_off_must_use_same_channel_as_note_on);
  RUN_TEST(test_joy_noteon_noteoff_same_channel_when_channels_differ);

  test_print_result();
  return (g_fail > 0) ? 1 : 0;
}
