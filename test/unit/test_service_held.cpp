// Tests: Engine::service_held() — the continuous tick-driven arp/strum path.
// This is a completely separate scheduling code path from the OneShot dispatch
// in play_step(), and was previously untested.
//
// service_held() is called from Engine::loop() with the tick count from
// MidiClock::update().  The internal clock (mode=kInternal) synthesizes
// 24-PPQ ticks from elapsed time.  We advance g_test_time_us and call
// engine.loop() to drive it.
#include "../framework.h"
#include "engine/engine.h"
#include "engine/rhythm_pattern.h"
#include "midi/router.h"
#include <algorithm>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

struct CaptureSink : midi::TransportSink {
  struct Msg { std::string t; uint8_t ch, note, vel; };
  std::vector<Msg> msgs;
  void send_note_on(uint8_t ch, uint8_t n, uint8_t v) override { msgs.push_back({"on",ch,n,v}); }
  void send_note_off(uint8_t ch, uint8_t n, uint8_t v) override { msgs.push_back({"off",ch,n,v}); }
  void send_pitch_bend(uint8_t, int16_t) override {}
  void send_control_change(uint8_t ch, uint8_t cc, uint8_t v) override { msgs.push_back({"cc",ch,cc,v}); }
  int count(const std::string& t, uint8_t note) const {
    int c = 0; for (auto& m : msgs) if (m.t==t && m.note==note) ++c; return c;
  }
  bool has(const std::string& t, uint8_t note) const { return count(t,note)>0; }
  std::vector<uint8_t> note_ons() const {
    std::vector<uint8_t> r;
    for (auto& m : msgs) if (m.t=="on") r.push_back(m.note);
    return r;
  }
  void clear() { msgs.clear(); }
};

// Advance time in small 10ms steps, calling engine.loop() + fire_pending each tick.
// This mirrors the real firmware's ~100Hz main loop and allows the internal clock
// to accumulate 24-PPQ ticks gradually rather than all at once.  A single large
// jump produces correct tick counts but the scheduler only arms its timer once, so
// only the first scheduled event fires — subsequent notes get dispatched in the
// wrong order.  10ms steps keep the behaviour identical to on-device.
static void advance_ms(engine::Engine& eng, uint64_t& t_us, uint32_t ms) {
  constexpr uint32_t kStepMs = 10;
  uint32_t remaining = ms;
  while (remaining > 0) {
    uint32_t step = remaining < kStepMs ? remaining : kStepMs;
    t_us += static_cast<uint64_t>(step) * 1000;
    g_test_time_us = t_us;
    eng.loop();
    esp_timer_fire_pending();
    remaining -= step;
  }
}

// Build an engine wired for held arp, internal clock.
static void setup_arp_engine(engine::Engine& eng, midi::Router& router,
                              CaptureSink& sink,
                              engine::ArpPattern pat = engine::ArpPattern::kUp,
                              engine::ArpRate   rate = engine::ArpRate::kEighth) {
  router.register_sink(&sink);
  eng.begin();
  eng.set_router(&router);
  eng.set_channel(0);
  eng.set_key("C");
  eng.set_scale("Major");
  eng.set_bpm(120.0f);

  // Internal clock, so ticks are synthesised from elapsed time.
  eng.midi_clock().set_mode(engine::ClockMode::kInternal);
  eng.midi_clock().set_internal_bpm(120.0f);

  eng.set_performance_mode(engine::PerformanceMode::kArp);
  eng.arpeggiator().set_enabled(true);
  eng.arpeggiator().set_rate(rate);
  eng.arpeggiator().set_pattern(pat);
  eng.arpeggiator().set_gate_percent(50);
  eng.arpeggiator().set_octaves(1);

  engine::ProgressionStep step{};
  step.scale_degree = 1; step.chord_quality = "maj";
  step.inversion = 0; step.octave = 0; step.velocity = 100;
  step.transpose = 0; step.bend_into_next = false;
  eng.progression().set_steps({step});
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_held_arp_schedules_notes_on_ticks() {
  // service_held with kHeld trigger: ticking the internal clock should
  // produce note-on events on the scheduler timeline.
  midi::Router router;
  CaptureSink sink;
  engine::Engine eng;
  setup_arp_engine(eng, router, sink);

  uint64_t t = 0;
  g_test_time_us = 0;

  // Initialise the clock (first loop call returns 0 ticks).
  eng.loop();

  // Trigger held step — sets up held_active_, held_is_arp_, arp sequence.
  eng.trigger_step(0, engine::TriggerMode::kHeld);
  ASSERT_TRUE(eng.held_active());
  ASSERT_TRUE(eng.held_is_arp());

  // At 120 BPM, kEighth = 250ms per step, 12 ticks per step.
  // Advance 300ms — should fire at least one arp step.
  advance_ms(eng, t, 300);

  // At least one note-on must have been scheduled and fired.
  ASSERT_FALSE(sink.note_ons().empty());
}

static void test_external_arp_does_not_attack_without_lock() {
  midi::Router router; CaptureSink sink; engine::Engine eng;
  setup_arp_engine(eng, router, sink);
  eng.midi_clock().set_mode(engine::ClockMode::kExternal);
  uint64_t t = 0; g_test_time_us = 0; eng.loop();
  eng.trigger_step(0, engine::TriggerMode::kHeld);
  advance_ms(eng, t, 300);
  ASSERT_TRUE(sink.note_ons().empty());
}

static void test_external_arp_relock_rebases_without_burst() {
  midi::Router router; CaptureSink sink; engine::Engine eng;
  setup_arp_engine(eng, router, sink);
  eng.midi_clock().set_mode(engine::ClockMode::kExternal);
  uint64_t t = 0; g_test_time_us = 0; eng.loop();
  eng.trigger_step(0, engine::TriggerMode::kHeld);
  // Start and lock the external clock, then establish one scheduled attack.
  eng.midi_clock().feed_byte(0xFA, engine::ClockSource::kUsb);
  for (int i = 0; i < 50; ++i) {
    g_test_time_us += 20833; eng.midi_clock().feed_byte(0xF8, engine::ClockSource::kUsb);
    eng.loop(); esp_timer_fire_pending();
  }
  sink.clear();
  // Lose the clock; the long gap must not be replayed as a burst after relock.
  g_test_time_us += 2'100'000; eng.loop(); esp_timer_fire_pending();
  for (int i = 0; i < 3; ++i) {
    g_test_time_us += 20833; eng.midi_clock().feed_byte(0xF8, engine::ClockSource::kUsb);
    eng.loop(); esp_timer_fire_pending();
  }
  ASSERT_LE(static_cast<int>(sink.note_ons().size()), 1);
}

static void test_held_arp_release_stops_notes() {
  midi::Router router;
  CaptureSink sink;
  engine::Engine eng;
  setup_arp_engine(eng, router, sink);

  uint64_t t = 0;
  g_test_time_us = 0;
  eng.loop();  // init clock

  eng.trigger_step(0, engine::TriggerMode::kHeld);

  // Let a few notes fire.
  advance_ms(eng, t, 600);
  ASSERT_FALSE(sink.note_ons().empty());

  // Release — must call all_notes_off and clear held state.
  eng.release_held();
  ASSERT_FALSE(eng.held_active());
}

static void test_held_arp_fires_first_note_immediately() {
  // play_step_held pre-loads held_arp_tick_acc_ = ticks_per_step so the very
  // first note fires on the first service_held call without waiting for a full
  // tick interval.  This is intentional: players expect immediate sound on key
  // press, not a gap equal to one arp step.
  midi::Router router;
  CaptureSink sink;
  engine::Engine eng;
  setup_arp_engine(eng, router, sink);

  g_test_time_us = 0;
  eng.loop();  // init clock

  eng.trigger_step(0, engine::TriggerMode::kHeld);

  // Advance just 1ms (essentially zero) — the pre-loaded accumulator fires
  // the first note at the beginning of the phase.
  uint64_t t = 0;
  advance_ms(eng, t, 1);

  // Exactly 1 note fires (the immediately-pre-scheduled first step).
  auto ons = sink.note_ons();
  ASSERT_EQ((int)ons.size(), 1);
}

static void test_held_arp_cycles_through_chord_notes() {
  // Verify the arp cycles through the C major chord notes (60, 64, 67).
  midi::Router router;
  CaptureSink sink;
  engine::Engine eng;
  setup_arp_engine(eng, router, sink, engine::ArpPattern::kUp, engine::ArpRate::kEighth);

  uint64_t t = 0;
  g_test_time_us = 0;
  eng.loop();

  // Disable auto-inversion so we get root position C major.
  eng.modifier().set_manual_inversion(0);

  eng.trigger_step(0, engine::TriggerMode::kHeld);

  // 3 arp steps = 3 * 250ms = 750ms — enough for one full cycle.
  advance_ms(eng, t, 900);

  auto ons = sink.note_ons();
  ASSERT_TRUE(ons.size() >= 3);

  // The first three note-ons must be C4, E4, G4 in ascending order.
  ASSERT_EQ(ons[0], 60);  // C4
  ASSERT_EQ(ons[1], 64);  // E4
  ASSERT_EQ(ons[2], 67);  // G4
}

static void test_held_arp_up_pattern_cycles_correctly() {
  // After a full cycle the sequence repeats from the beginning.
  midi::Router router;
  CaptureSink sink;
  engine::Engine eng;
  setup_arp_engine(eng, router, sink, engine::ArpPattern::kUp, engine::ArpRate::kEighth);

  uint64_t t = 0;
  g_test_time_us = 0;
  eng.loop();
  eng.modifier().set_manual_inversion(0);

  eng.trigger_step(0, engine::TriggerMode::kHeld);

  // 7 steps to see two full cycles of a 3-note chord.
  advance_ms(eng, t, 1800);

  auto ons = sink.note_ons();
  ASSERT_TRUE(ons.size() >= 6);
  // First two cycles must both be [60, 64, 67].
  ASSERT_EQ(ons[0], 60); ASSERT_EQ(ons[1], 64); ASSERT_EQ(ons[2], 67);
  ASSERT_EQ(ons[3], 60); ASSERT_EQ(ons[4], 64); ASSERT_EQ(ons[5], 67);
}

static void test_held_arp_each_noteon_has_noteoff() {
  // Every note-on from service_held must eventually be followed by a note-off.
  midi::Router router;
  CaptureSink sink;
  engine::Engine eng;
  setup_arp_engine(eng, router, sink);

  uint64_t t = 0;
  g_test_time_us = 0;
  eng.loop();
  eng.trigger_step(0, engine::TriggerMode::kHeld);

  // Run for 1 full second.
  advance_ms(eng, t, 1000);

  // Release and fire any pending timers.
  eng.release_held();
  eng.all_notes_off();
  advance_ms(eng, t, 500);

  // For every note-on, there must be at least one matching note-off.
  for (auto& m : sink.msgs) {
    if (m.t == "on" && m.vel > 0) {
      ASSERT_TRUE(sink.has("off", m.note));
    }
  }
}

static void test_held_arp_index_advances_monotonically() {
  // The arp index must advance by 1 per tick-gate, wrapping modulo length.
  midi::Router router;
  CaptureSink sink;
  engine::Engine eng;
  setup_arp_engine(eng, router, sink, engine::ArpPattern::kUp, engine::ArpRate::kEighth);

  uint64_t t = 0;
  g_test_time_us = 0;
  eng.loop();
  eng.modifier().set_manual_inversion(0);

  eng.trigger_step(0, engine::TriggerMode::kHeld);

  // 5 steps.
  advance_ms(eng, t, 1300);

  auto ons = sink.note_ons();
  ASSERT_TRUE(ons.size() >= 5);

  // Expected sequence for 3-note kUp chord: 60,64,67,60,64,...
  static const uint8_t kSeq[] = {60, 64, 67};
  for (size_t i = 0; i < std::min(ons.size(), (size_t)6); ++i) {
    ASSERT_EQ(ons[i], kSeq[i % 3]);
  }
}

static void test_held_arp_no_duplicate_noteons_on_same_note() {
  // service_held must not fire note-on for the same note twice without an
  // intervening note-off — the MidiScheduler would accumulate phantom entries.
  midi::Router router;
  CaptureSink sink;
  engine::Engine eng;
  setup_arp_engine(eng, router, sink, engine::ArpPattern::kUp, engine::ArpRate::kEighth);

  uint64_t t = 0;
  g_test_time_us = 0;
  eng.loop();
  eng.modifier().set_manual_inversion(0);

  eng.trigger_step(0, engine::TriggerMode::kHeld);
  advance_ms(eng, t, 1500);
  advance_ms(eng, t, 500);  // extra time for note-offs to fire

  // Track whether any note-on follows another note-on without a note-off
  // in between for the same MIDI note.
  std::vector<bool> active(128, false);
  bool double_on = false;
  for (auto& m : sink.msgs) {
    if (m.t == "on" && m.vel > 0) {
      if (active[m.note]) { double_on = true; break; }
      active[m.note] = true;
    } else if (m.t == "off") {
      active[m.note] = false;
    }
  }
  ASSERT_FALSE(double_on);
}

static void test_held_arp_quarter_rate_slower_than_eighth() {
  // kQuarter = 24 ticks per step vs kEighth = 12 ticks per step.
  // At 120 BPM, quarter = 500ms, eighth = 250ms.
  // In the same wall-clock time, eighth should produce ~2x more steps.

  auto run_and_count = [](engine::ArpRate rate, uint32_t ms) -> size_t {
    midi::Router router;
    CaptureSink sink;
    engine::Engine eng;
    setup_arp_engine(eng, router, sink, engine::ArpPattern::kUp, rate);
    uint64_t t = 0;
    g_test_time_us = 0;
    eng.loop();
    eng.trigger_step(0, engine::TriggerMode::kHeld);
    advance_ms(eng, t, ms);
    return sink.note_ons().size();
  };

  size_t quarter_steps = run_and_count(engine::ArpRate::kQuarter, 1200);
  size_t eighth_steps  = run_and_count(engine::ArpRate::kEighth,  1200);

  ASSERT_TRUE(eighth_steps > quarter_steps);
}

static void test_held_arp_channel_matches_engine_channel() {
  midi::Router router;
  CaptureSink sink;
  engine::Engine eng;
  setup_arp_engine(eng, router, sink);
  eng.set_channel(5);

  uint64_t t = 0;
  g_test_time_us = 0;
  eng.loop();
  eng.trigger_step(0, engine::TriggerMode::kHeld);
  advance_ms(eng, t, 600);

  for (auto& m : sink.msgs) {
    if (m.t == "on" || m.t == "off") ASSERT_EQ(m.ch, 5);
  }
}

// ---------------------------------------------------------------------------
// Strum rhythm guards — service_held with rhythm mode
// ---------------------------------------------------------------------------

static void setup_strum_engine(engine::Engine& eng, midi::Router& router,
                                CaptureSink& sink) {
  router.register_sink(&sink);
  eng.begin();
  eng.set_router(&router);
  eng.set_channel(0);
  eng.set_key("C");
  eng.set_scale("Major");
  eng.set_bpm(120.0f);
  eng.midi_clock().set_mode(engine::ClockMode::kInternal);
  eng.midi_clock().set_internal_bpm(120.0f);
  eng.set_performance_mode(engine::PerformanceMode::kStrum);
  eng.performer().set_strum_ms(14);
  eng.performer().set_note_length_ms(240);
  eng.performer().set_strum_rhythm(engine::StrumRhythm::kSalsa);

  engine::ProgressionStep step{};
  step.scale_degree = 1; step.chord_quality = "maj";
  step.inversion = 0; step.octave = 0; step.velocity = 100;
  step.transpose = 0; step.bend_into_next = false;
  eng.progression().set_steps({step});
}

static void test_held_strum_rhythm_schedules_hits() {
  // service_held with strum_rhythm mode must schedule note events.
  midi::Router router;
  CaptureSink sink;
  engine::Engine eng;
  setup_strum_engine(eng, router, sink);

  uint64_t t = 0;
  g_test_time_us = 0;
  eng.loop();  // init clock

  eng.trigger_step(0, engine::TriggerMode::kHeld);
  ASSERT_TRUE(eng.held_active());

  // Advance 2 seconds — salsa pattern repeats several times
  advance_ms(eng, t, 2000);

  // Must have produced some note-ons from the rhythm pattern
  ASSERT_FALSE(sink.note_ons().empty());
}

static void test_held_strum_rhythm_release_stops_scheduling() {
  midi::Router router;
  CaptureSink sink;
  engine::Engine eng;
  setup_strum_engine(eng, router, sink);

  uint64_t t = 0;
  g_test_time_us = 0;
  eng.loop();

  eng.trigger_step(0, engine::TriggerMode::kHeld);
  advance_ms(eng, t, 500);

  eng.release_held();
  ASSERT_FALSE(eng.held_active());

  // After release, advancing time should not produce more note-ons
  int ons_before_release = (int)sink.note_ons().size();
  sink.msgs.clear();
  advance_ms(eng, t, 1000);
  int ons_after = sink.note_ons().size();
  ASSERT_EQ(ons_after, 0);
  (void)ons_before_release;
}

static void test_held_strum_rhythm_zero_16th_ms_guard() {
  // Guard: `if (held_rhythm_16th_ms_ == 0) return;`
  // This fires when BPM is impossibly high or clock is not synced
  // and held_rhythm_16th_ms_ was never set.
  // We verify the guard by disabling clock sync (engine.loop() not called)
  // so the rhythm ms value is never calculated.
  midi::Router router;
  CaptureSink sink;
  engine::Engine eng;
  setup_strum_engine(eng, router, sink);

  // Do NOT call eng.loop() — clock is not synced, held_rhythm_16th_ms_ stays 0
  // Trigger and advance time: without the guard this would be an infinite loop.
  g_test_time_us = 0;
  eng.trigger_step(0, engine::TriggerMode::kHeld);

  // Advance a bit — the guard should prevent scheduling without crashing
  uint64_t t = 0;
  advance_ms(eng, t, 100);
  // No assertion needed beyond "no crash or infinite loop"
}

static void test_held_arp_empty_sequence_guard() {
  // If the arp sequence is empty when service_held runs, the code must
  // not crash on `% size` where size=0.
  // This can happen if trigger_step fires on a step with no chord notes.
  midi::Router router;
  CaptureSink sink;
  engine::Engine eng;

  router.register_sink(&sink);
  eng.begin();
  eng.set_router(&router);
  eng.set_bpm(120.0f);
  eng.midi_clock().set_mode(engine::ClockMode::kInternal);
  eng.midi_clock().set_internal_bpm(120.0f);
  eng.set_performance_mode(engine::PerformanceMode::kArp);
  eng.arpeggiator().set_enabled(true);
  eng.arpeggiator().set_rate(engine::ArpRate::kEighth);
  eng.arpeggiator().set_pattern(engine::ArpPattern::kUp);

  // Empty progression — no steps → no arp sequence built
  eng.progression().set_steps({});

  uint64_t t = 0;
  g_test_time_us = 0;
  eng.loop();

  // trigger_step on empty progression — should not crash
  eng.trigger_step(0, engine::TriggerMode::kHeld);

  // Advance time — the held_arp_sequence_.empty() guard should fire
  advance_ms(eng, t, 500);
  // Reaching here = no crash = test passes
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
  TEST_SUITE("service_held — held arp scheduling");
  RUN_TEST(test_held_arp_schedules_notes_on_ticks);
  RUN_TEST(test_external_arp_does_not_attack_without_lock);
  RUN_TEST(test_external_arp_relock_rebases_without_burst);
  RUN_TEST(test_held_arp_release_stops_notes);
  RUN_TEST(test_held_arp_fires_first_note_immediately);
  RUN_TEST(test_held_arp_cycles_through_chord_notes);
  RUN_TEST(test_held_arp_up_pattern_cycles_correctly);
  RUN_TEST(test_held_arp_each_noteon_has_noteoff);
  RUN_TEST(test_held_arp_index_advances_monotonically);
  RUN_TEST(test_held_arp_no_duplicate_noteons_on_same_note);
  RUN_TEST(test_held_arp_quarter_rate_slower_than_eighth);
  RUN_TEST(test_held_arp_channel_matches_engine_channel);

  TEST_SUITE("service_held — strum rhythm scheduling");
  RUN_TEST(test_held_strum_rhythm_schedules_hits);
  RUN_TEST(test_held_strum_rhythm_release_stops_scheduling);
  RUN_TEST(test_held_strum_rhythm_zero_16th_ms_guard);
  RUN_TEST(test_held_arp_empty_sequence_guard);

  test_print_result();
  return (g_fail > 0) ? 1 : 0;
}
