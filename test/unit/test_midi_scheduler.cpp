// Tests: MidiScheduler event ordering, timing, all_notes_off, set_channel.
#include "../framework.h"
#include "engine/midi_scheduler.h"
#include "engine/engine.h"
#include "midi/router.h"
#include <vector>
#include <string>

// CaptureSink registered to a real Router — intercepts every MIDI message.
struct CaptureSink : midi::TransportSink {
  struct Msg { std::string t; uint8_t ch, note, vel; uint64_t at_us; };
  std::vector<Msg> msgs;

  void send_note_on(uint8_t ch, uint8_t n, uint8_t v) override {
    msgs.push_back({"on", ch, n, v, g_test_time_us});
  }
  void send_note_off(uint8_t ch, uint8_t n, uint8_t v) override {
    msgs.push_back({"off", ch, n, v, g_test_time_us});
  }
  void send_pitch_bend(uint8_t ch, int16_t val) override {
    msgs.push_back({"pb", ch, 0, (uint8_t)(val & 0x7F), g_test_time_us});
  }
  void send_control_change(uint8_t ch, uint8_t cc, uint8_t val) override {
    msgs.push_back({"cc", ch, cc, val, g_test_time_us});
  }

  bool has(const std::string& t, uint8_t ch, uint8_t note) const {
    for (auto& m : msgs) if (m.t == t && m.ch == ch && m.note == note) return true;
    return false;
  }
  void clear() { msgs.clear(); }
};

struct LedgerRouterSink : engine::NoteLedgerSink {
  midi::Router& router;
  explicit LedgerRouterSink(midi::Router& r) : router(r) {}
  void note_on(uint8_t, uint8_t ch, uint8_t n, uint8_t v) override { router.note_on(ch, n, v); }
  void note_off(uint8_t, uint8_t ch, uint8_t n) override { router.note_off(ch, n, 0); }
};

// Convenience wrappers
static engine::ScheduledEvent make_on(uint64_t ms, uint8_t n, uint8_t vel = 100) {
  engine::ScheduledEvent e{};
  e.due_ms = ms; e.type = engine::EventType::kNoteOn; e.note = n; e.velocity = vel;
  return e;
}
static engine::ScheduledEvent make_off(uint64_t ms, uint8_t n) {
  engine::ScheduledEvent e{};
  e.due_ms = ms; e.type = engine::EventType::kNoteOff; e.note = n; e.velocity = 0;
  return e;
}
static engine::ScheduledEvent make_pb(uint64_t ms, int16_t bend) {
  engine::ScheduledEvent e{};
  e.due_ms = ms; e.type = engine::EventType::kPitchBend; e.bend = bend;
  return e;
}

// ---------------------------------------------------------------------------
// Timing & Ordering
// ---------------------------------------------------------------------------

static void test_single_noteon_fires_at_due_time() {
  midi::Router router; CaptureSink sink; router.register_sink(&sink);
  engine::MidiScheduler sched; sched.start(&router, 0);

  g_test_time_us = 0;
  sched.schedule(make_on(100, 60));

  g_test_time_us = 50 * 1000; esp_timer_fire_pending();
  ASSERT_FALSE(sink.has("on", 0, 60));

  g_test_time_us = 100 * 1000; esp_timer_fire_pending();
  ASSERT_TRUE(sink.has("on", 0, 60));
}

static void test_events_ordered_ascending_by_time() {
  midi::Router router; CaptureSink sink; router.register_sink(&sink);
  engine::MidiScheduler sched; sched.start(&router, 0);

  g_test_time_us = 0;
  sched.schedule(make_on(200, 62));
  sched.schedule(make_on(100, 60));
  sched.schedule(make_on(300, 64));

  g_test_time_us = 100 * 1000; esp_timer_fire_pending();
  ASSERT_TRUE(sink.has("on", 0, 60));
  ASSERT_FALSE(sink.has("on", 0, 62)); ASSERT_FALSE(sink.has("on", 0, 64));

  g_test_time_us = 200 * 1000; esp_timer_fire_pending();
  ASSERT_TRUE(sink.has("on", 0, 62));
  ASSERT_FALSE(sink.has("on", 0, 64));

  g_test_time_us = 300 * 1000; esp_timer_fire_pending();
  ASSERT_TRUE(sink.has("on", 0, 64));
}

static void test_noteon_before_noteoff() {
  midi::Router router; CaptureSink sink; router.register_sink(&sink);
  engine::MidiScheduler sched; sched.start(&router, 0);

  g_test_time_us = 0;
  sched.schedule(make_on(0, 60));
  sched.schedule(make_off(240, 60));

  g_test_time_us = 10 * 1000; esp_timer_fire_pending();
  ASSERT_TRUE(sink.has("on", 0, 60));
  ASSERT_FALSE(sink.has("off", 0, 60));

  g_test_time_us = 240 * 1000; esp_timer_fire_pending();
  ASSERT_TRUE(sink.has("off", 0, 60));
}

static void test_schedule_batch() {
  midi::Router router; CaptureSink sink; router.register_sink(&sink);
  engine::MidiScheduler sched; sched.start(&router, 0);

  engine::ScheduledEvent batch[4] = {
    make_on(0, 60), make_on(10, 64), make_on(20, 67), make_off(240, 60)
  };
  g_test_time_us = 0;
  sched.schedule_batch(batch, 4);

  g_test_time_us = 30 * 1000; esp_timer_fire_pending();
  ASSERT_TRUE(sink.has("on", 0, 60)); ASSERT_TRUE(sink.has("on", 0, 64)); ASSERT_TRUE(sink.has("on", 0, 67));
  ASSERT_FALSE(sink.has("off", 0, 60));

  g_test_time_us = 240 * 1000; esp_timer_fire_pending();
  ASSERT_TRUE(sink.has("off", 0, 60));
}

// ---------------------------------------------------------------------------
// all_notes_off / clear
// ---------------------------------------------------------------------------

static void test_all_notes_off_sends_noteoff_for_active() {
  midi::Router router; CaptureSink sink; router.register_sink(&sink);
  engine::MidiScheduler sched; sched.start(&router, 0);

  g_test_time_us = 0;
  sched.schedule(make_on(0, 60)); sched.schedule(make_on(0, 64)); sched.schedule(make_on(0, 67));
  g_test_time_us = 50 * 1000; esp_timer_fire_pending();

  ASSERT_EQ(sched.active_notes().size(), (size_t)3);
  sink.clear();
  sched.all_notes_off();

  ASSERT_TRUE(sink.has("off", 0, 60)); ASSERT_TRUE(sink.has("off", 0, 64)); ASSERT_TRUE(sink.has("off", 0, 67));
  ASSERT_EQ(sched.active_notes().size(), (size_t)0);
}

static void test_source_cleanup_preserves_other_owner() {
  midi::Router router; CaptureSink sink; router.register_sink(&sink);
  LedgerRouterSink ledger_sink(router);
  engine::NoteLedger ledger(&ledger_sink);
  engine::MidiScheduler producer_a;
  engine::MidiScheduler producer_b;
  producer_a.start(&router, 0); producer_b.start(&router, 0);
  producer_a.set_source(10); producer_b.set_source(20);
  producer_a.set_ledger(&ledger); producer_b.set_ledger(&ledger);

  // Two producers share one physical voice.  B changes channel and must not
  // panic A; only the final owner may emit the physical Note Off.
  g_test_time_us = 0;
  producer_a.schedule(make_on(0, 60));
  producer_b.schedule(make_on(0, 60));
  g_test_time_us = 10 * 1000; esp_timer_fire_pending();
  ASSERT_EQ(sink.msgs.size(), size_t(1));
  ASSERT_TRUE(sink.has("on", 0, 60));

  sink.clear();
  producer_b.set_channel(3);
  ASSERT_FALSE(sink.has("off", 0, 60));
  producer_a.all_notes_off();
  ASSERT_TRUE(sink.has("off", 0, 60));
}

static void test_panic_invalidates_queued_attack() {
  midi::Router router; CaptureSink sink; router.register_sink(&sink);
  LedgerRouterSink ledger_sink(router);
  engine::NoteLedger ledger(&ledger_sink);
  engine::MidiScheduler sched; sched.start(&router, 0); sched.set_ledger(&ledger);

  g_test_time_us = 0;
  ASSERT_TRUE(sched.schedule(make_on(100, 60)));
  ledger.panic();
  g_test_time_us = 100 * 1000;
  esp_timer_fire_pending();

  ASSERT_FALSE(sink.has("on", 0, 60));
}

static void test_source_invalidation_preserves_other_source() {
  midi::Router router; CaptureSink sink; router.register_sink(&sink);
  LedgerRouterSink ledger_sink(router);
  engine::NoteLedger ledger(&ledger_sink);
  engine::MidiScheduler a; engine::MidiScheduler b;
  a.start(&router, 0); b.start(&router, 0);
  a.set_source(10); b.set_source(20);
  a.set_ledger(&ledger); b.set_ledger(&ledger);
  g_test_time_us = 0;
  a.schedule(make_on(100, 60)); b.schedule(make_on(100, 64));
  ledger.invalidate_source(10);
  g_test_time_us = 100 * 1000; esp_timer_fire_pending();
  ASSERT_FALSE(sink.has("on", 0, 60));
  ASSERT_TRUE(sink.has("on", 0, 64));
}

static void test_lead_channel_is_independent_and_captured() {
  midi::Router router; CaptureSink sink; router.register_sink(&sink);
  engine::Engine eng; eng.begin(); eng.set_router(&router);
  eng.set_channel(1); eng.set_lead_channel(4);
  g_test_time_us = 0;
  eng.play_notes({60}, 100);
  g_test_time_us = 10 * 1000; esp_timer_fire_pending();
  ASSERT_TRUE(sink.has("on", 4, 60));
  sink.clear();
  eng.set_lead_channel(7);
  ASSERT_TRUE(sink.has("off", 4, 60));
  sink.clear();
  eng.play_notes({62}, 100);
  g_test_time_us = 20 * 1000; esp_timer_fire_pending();
  ASSERT_TRUE(sink.has("on", 7, 62));
}

static void test_clear_does_not_send_noteoffs() {
  midi::Router router; CaptureSink sink; router.register_sink(&sink);
  engine::MidiScheduler sched; sched.start(&router, 0);

  g_test_time_us = 0;
  sched.schedule(make_on(100, 60)); sched.schedule(make_off(300, 60));
  sched.clear();

  g_test_time_us = 400 * 1000; esp_timer_fire_pending();
  ASSERT_FALSE(sink.has("on", 0, 60)); ASSERT_FALSE(sink.has("off", 0, 60));
}

// ---------------------------------------------------------------------------
// set_channel (Bug 2 regression)
// ---------------------------------------------------------------------------

static void test_set_channel_flushes_active_notes_on_old_channel() {
  midi::Router router; CaptureSink sink; router.register_sink(&sink);
  engine::MidiScheduler sched; sched.start(&router, 0);

  g_test_time_us = 0;
  sched.schedule(make_on(0, 60)); sched.schedule(make_on(0, 64));
  g_test_time_us = 10 * 1000; esp_timer_fire_pending();
  ASSERT_EQ(sched.active_notes().size(), (size_t)2);

  sink.clear();
  sched.set_channel(3);

  // Note-offs must be on OLD channel 0
  ASSERT_TRUE(sink.has("off", 0, 60));
  ASSERT_TRUE(sink.has("off", 0, 64));
  // Must NOT appear on new channel 3
  ASSERT_FALSE(sink.has("off", 3, 60));
  ASSERT_FALSE(sink.has("off", 3, 64));
  ASSERT_EQ(sched.active_notes().size(), (size_t)0);
}

static void test_set_channel_same_channel_no_flush() {
  midi::Router router; CaptureSink sink; router.register_sink(&sink);
  engine::MidiScheduler sched; sched.start(&router, 2);

  g_test_time_us = 0;
  sched.schedule(make_on(0, 60));
  g_test_time_us = 10 * 1000; esp_timer_fire_pending();
  ASSERT_EQ(sched.active_notes().size(), (size_t)1);

  sink.clear();
  sched.set_channel(2);  // same channel — no flush

  ASSERT_EQ(sched.active_notes().size(), (size_t)1);
  ASSERT_FALSE(sink.has("off", 2, 60));
}

static void test_channel_change_cancels_pending_events() {
  midi::Router router; CaptureSink sink; router.register_sink(&sink);
  engine::MidiScheduler sched; sched.start(&router, 0);

  g_test_time_us = 0;
  sched.schedule(make_on(0, 62));
  sched.schedule(make_off(200, 62));
  g_test_time_us = 5 * 1000; esp_timer_fire_pending();

  sched.set_channel(5);  // flush old active note on ch 0
  sink.clear();

  g_test_time_us = 200 * 1000; esp_timer_fire_pending();
  ASSERT_FALSE(sink.has("off", 5, 62));  // old phrase was cancelled
}

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------

static void test_pitchbend_event_dispatched() {
  midi::Router router; CaptureSink sink; router.register_sink(&sink);
  engine::MidiScheduler sched; sched.start(&router, 0);

  g_test_time_us = 0;
  sched.schedule(make_pb(0, 4096));
  g_test_time_us = 10 * 1000; esp_timer_fire_pending();

  bool found = false;
  for (auto& m : sink.msgs) if (m.t == "pb") { found = true; break; }
  ASSERT_TRUE(found);
}

static void test_active_notes_tracks_correctly() {
  midi::Router router; CaptureSink sink; router.register_sink(&sink);
  engine::MidiScheduler sched; sched.start(&router, 0);

  g_test_time_us = 0;
  sched.schedule(make_on(0, 60)); sched.schedule(make_on(0, 64));
  sched.schedule(make_off(200, 60));

  g_test_time_us = 50 * 1000; esp_timer_fire_pending();
  ASSERT_EQ(sched.active_notes().size(), (size_t)2);

  g_test_time_us = 200 * 1000; esp_timer_fire_pending();
  ASSERT_EQ(sched.active_notes().size(), (size_t)1);
  ASSERT_EQ(sched.active_notes()[0], 64);
}

static void test_overflow_rejects_whole_batch_and_silences() {
  midi::Router router; CaptureSink sink; router.register_sink(&sink);
  engine::MidiScheduler sched; sched.start(&router, 0);
  g_test_time_us = 0;
  ASSERT_TRUE(sched.schedule(make_on(0, 60)));
  engine::ScheduledEvent pending[127];
  for (auto& e : pending) e = make_pb(100, 100);
  ASSERT_TRUE(sched.schedule_batch(pending, 127));
  engine::ScheduledEvent pair[] = {make_on(100, 65), make_off(200, 65)};
  ASSERT_FALSE(sched.schedule_batch(pair, 2));
  ASSERT_TRUE(sink.has("off", 0, 60));
  ASSERT_EQ(sched.active_notes().size(), size_t(0));
  g_test_time_us = 300000; esp_timer_fire_pending();
  ASSERT_FALSE(sink.has("on", 0, 65));
}

static void test_active_capacity_never_emits_untracked_note() {
  midi::Router router; CaptureSink sink; router.register_sink(&sink);
  engine::MidiScheduler sched; sched.start(&router, 0);
  g_test_time_us = 0;
  for (uint8_t n = 0; n < 33; ++n) sched.schedule(make_on(100, n));
  g_test_time_us = 100000; esp_timer_fire_pending();
  ASSERT_FALSE(sink.has("on", 0, 32));
  for (uint8_t n = 0; n < 32; ++n) ASSERT_TRUE(sink.has("off", 0, n));
  ASSERT_EQ(sched.active_notes().size(), size_t(0));
}

static void test_equal_time_preserves_order_and_zero_velocity_releases() {
  midi::Router router; CaptureSink sink; router.register_sink(&sink);
  engine::MidiScheduler sched; sched.start(&router, 0);
  g_test_time_us = 0;
  engine::ScheduledEvent pair[] = {make_on(100, 60), make_on(100, 60, 0)};
  ASSERT_TRUE(sched.schedule_batch(pair, 2));
  g_test_time_us = 100000; esp_timer_fire_pending();
  ASSERT_EQ(sink.msgs.size(), size_t(2));
  ASSERT_TRUE(sink.msgs[0].t == "on");
  ASSERT_TRUE(sink.msgs[1].t == "off");
  ASSERT_EQ(sched.active_notes().size(), size_t(0));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
  TEST_SUITE("MidiScheduler — Timing & Ordering");
  RUN_TEST(test_single_noteon_fires_at_due_time);
  RUN_TEST(test_events_ordered_ascending_by_time);
  RUN_TEST(test_noteon_before_noteoff);
  RUN_TEST(test_schedule_batch);

  TEST_SUITE("MidiScheduler — all_notes_off / clear");
  RUN_TEST(test_all_notes_off_sends_noteoff_for_active);
  RUN_TEST(test_source_cleanup_preserves_other_owner);
  RUN_TEST(test_panic_invalidates_queued_attack);
  RUN_TEST(test_source_invalidation_preserves_other_source);
  RUN_TEST(test_lead_channel_is_independent_and_captured);
  RUN_TEST(test_clear_does_not_send_noteoffs);

  TEST_SUITE("MidiScheduler — set_channel (Bug 2 regression)");
  RUN_TEST(test_set_channel_flushes_active_notes_on_old_channel);
  RUN_TEST(test_set_channel_same_channel_no_flush);
  RUN_TEST(test_channel_change_cancels_pending_events);

  TEST_SUITE("MidiScheduler — misc events");
  RUN_TEST(test_pitchbend_event_dispatched);
  RUN_TEST(test_active_notes_tracks_correctly);

  RUN_TEST(test_overflow_rejects_whole_batch_and_silences);
  RUN_TEST(test_active_capacity_never_emits_untracked_note);
  RUN_TEST(test_equal_time_preserves_order_and_zero_velocity_releases);
  test_print_result();
  return (g_fail > 0) ? 1 : 0;
}
