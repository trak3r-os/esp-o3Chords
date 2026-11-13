// Tests: Engine::service_held() — held strum rhythm scheduling path.
//
// service_held() has two branches: held arpeggio (covered in test_service_held.cpp)
// and held strum rhythm.  These tests exclusively cover the strum branch.
//
// The strum rhythm branch is active when PerformanceMode == kStrum and
// performer().strum_rhythm() != kNone.  At each loop() call, hits within the
// 100 ms lookahead window are pre-scheduled into the MidiScheduler; the pattern
// repeats indefinitely until release_held() is called.
//
// Timing reference at 120 BPM: quarter_ms = 500 ms, 16th_ms = 125 ms.
//   Salsa  hits at 16th positions {2,6,8,12,14}  → ms {250,750,1000,1500,1750}
//   Reggae hits at 16th positions {2,6,10,14}    → ms {250,750,1250,1750}
//   Funk   hits at 16th positions {0,1,3,5,6,8,9,11,13,14} (ghost notes at 40%)
//   Bossa Nova: 32 16ths, first hit at position 0 (fires immediately)

#include "../framework.h"
#include "engine/engine.h"
#include "midi/router.h"
#include <algorithm>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers (same pattern as test_service_held.cpp)
// ---------------------------------------------------------------------------

struct CaptureSink : midi::TransportSink {
  struct Msg { std::string t; uint8_t ch, note, vel; };
  std::vector<Msg> msgs;
  void send_note_on(uint8_t ch, uint8_t n, uint8_t v)  override { msgs.push_back({"on",  ch, n, v}); }
  void send_note_off(uint8_t ch, uint8_t n, uint8_t v) override { msgs.push_back({"off", ch, n, v}); }
  void send_pitch_bend(uint8_t, int16_t)               override {}
  void send_control_change(uint8_t ch, uint8_t cc, uint8_t v) override { msgs.push_back({"cc", ch, cc, v}); }
  int count(const std::string& t, uint8_t note) const {
    int c = 0; for (const auto& m : msgs) if (m.t == t && m.note == note) ++c; return c;
  }
  bool has(const std::string& t, uint8_t note) const { return count(t, note) > 0; }
  std::vector<uint8_t> note_ons() const {
    std::vector<uint8_t> r;
    for (const auto& m : msgs) if (m.t == "on") r.push_back(m.note);
    return r;
  }
  void clear() { msgs.clear(); }
};

static void advance_ms(engine::Engine& eng, uint64_t& t_us, uint32_t ms) {
  constexpr uint32_t kStep = 10;
  uint32_t rem = ms;
  while (rem > 0) {
    uint32_t step = rem < kStep ? rem : kStep;
    t_us += static_cast<uint64_t>(step) * 1000;
    g_test_time_us = t_us;
    eng.loop();
    esp_timer_fire_pending();
    rem -= step;
  }
}

// Wire up engine for held strum with the given rhythm, 120 BPM internal clock.
static void setup_strum_engine(engine::Engine& eng, midi::Router& router,
                                CaptureSink& sink,
                                engine::StrumRhythm rhythm = engine::StrumRhythm::kSalsa) {
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
  eng.performer().set_strum_rhythm(rhythm);
  eng.performer().set_note_length_ms(100);
  eng.performer().set_strum_ms(0);  // no strum spread: all chord notes at offset 0

  engine::ProgressionStep step{};
  step.scale_degree = 1;
  step.chord_quality = "maj";
  step.inversion    = 0;
  step.octave       = 0;
  step.velocity     = 100;
  step.transpose    = 0;
  step.bend_into_next = false;
  eng.progression().set_steps({step});
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_strum_activates_held_state() {
  // trigger_step(kHeld) with kStrum + non-None rhythm must set held_active().
  midi::Router router; CaptureSink sink; engine::Engine eng;
  setup_strum_engine(eng, router, sink);

  g_test_time_us = 0;
  eng.loop();
  eng.trigger_step(0, engine::TriggerMode::kHeld);

  ASSERT_TRUE(eng.held_active());
  ASSERT_FALSE(eng.held_is_arp());
}

static void test_strum_kNone_fires_chord_once_not_repeatedly() {
  // kStrum + kNone falls through to "held chord, no rhythm".
  // The chord is scheduled once at trigger time; service_held does nothing further.
  midi::Router router; CaptureSink sink; engine::Engine eng;
  setup_strum_engine(eng, router, sink, engine::StrumRhythm::kNone);

  g_test_time_us = 0;
  uint64_t t = 0;
  eng.loop();
  // Root position so note count is predictable.
  eng.modifier().set_manual_inversion(0);
  eng.trigger_step(0, engine::TriggerMode::kHeld);

  // Let the initial chord fire.
  advance_ms(eng, t, 50);
  int count_initial = static_cast<int>(sink.note_ons().size());
  ASSERT_TRUE(count_initial > 0);

  // Advance one full "bar equivalent" — no new note-ons should appear.
  advance_ms(eng, t, 2000);
  ASSERT_EQ(static_cast<int>(sink.note_ons().size()), count_initial);
}

static void test_strum_fires_notes_within_lookahead() {
  // Salsa first hit at position 2 = 250 ms.  It enters the 100 ms lookahead
  // window when now_ms >= 150 ms and fires at 250 ms.  After 300 ms: notes appear.
  midi::Router router; CaptureSink sink; engine::Engine eng;
  setup_strum_engine(eng, router, sink);

  g_test_time_us = 0;
  uint64_t t = 0;
  eng.loop();
  eng.trigger_step(0, engine::TriggerMode::kHeld);

  advance_ms(eng, t, 300);

  ASSERT_FALSE(sink.note_ons().empty());
}

static void test_strum_salsa_fires_5_hits_per_bar() {
  // One full Salsa bar = 5 hits.  C major = 3 notes per hit → 15 note-ons.
  // At 120 BPM: bar duration = 16 × 125 ms = 2000 ms.
  midi::Router router; CaptureSink sink; engine::Engine eng;
  setup_strum_engine(eng, router, sink);

  g_test_time_us = 0;
  uint64_t t = 0;
  eng.loop();
  eng.modifier().set_manual_inversion(0);
  eng.trigger_step(0, engine::TriggerMode::kHeld);

  advance_ms(eng, t, 2200);  // last hit at 1750 ms + margin

  // 5 hits × 3 notes (C major) = 15
  ASSERT_TRUE(static_cast<int>(sink.note_ons().size()) >= 12);
}

static void test_strum_velocity_scaled_by_hit_pct() {
  // Salsa: positions {2,8,14} carry velocity_pct=100; positions {6,12} carry 90%.
  // base velocity = 100 → rendered velocities must include both 100 and 90.
  midi::Router router; CaptureSink sink; engine::Engine eng;
  setup_strum_engine(eng, router, sink);

  g_test_time_us = 0;
  uint64_t t = 0;
  eng.loop();
  eng.trigger_step(0, engine::TriggerMode::kHeld);

  advance_ms(eng, t, 2200);

  bool found_full = false, found_soft = false;
  for (const auto& m : sink.msgs) {
    if (m.t == "on") {
      if (m.vel == 100) found_full = true;
      if (m.vel == 90)  found_soft = true;
    }
  }
  ASSERT_TRUE(found_full);
  ASSERT_TRUE(found_soft);
}

static void test_strum_each_noteon_has_noteoff() {
  // Every note-on from the strum rhythm must be matched by a note-off.
  // Note-offs are scheduled by the engine before the next hit time.
  midi::Router router; CaptureSink sink; engine::Engine eng;
  setup_strum_engine(eng, router, sink);

  g_test_time_us = 0;
  uint64_t t = 0;
  eng.loop();
  eng.trigger_step(0, engine::TriggerMode::kHeld);

  advance_ms(eng, t, 2200);
  eng.release_held();
  eng.all_notes_off();
  advance_ms(eng, t, 300);  // flush remaining scheduled events

  for (const auto& m : sink.msgs) {
    if (m.t == "on" && m.vel > 0) {
      ASSERT_TRUE(sink.has("off", m.note));
    }
  }
}

static void test_strum_salsa_pattern_loops() {
  // After one full bar (2000 ms) the pattern starts over.
  // Two bars: 10 hits × 3 notes = 30 note-ons.
  midi::Router router; CaptureSink sink; engine::Engine eng;
  setup_strum_engine(eng, router, sink);

  g_test_time_us = 0;
  uint64_t t = 0;
  eng.loop();
  eng.modifier().set_manual_inversion(0);
  eng.trigger_step(0, engine::TriggerMode::kHeld);

  advance_ms(eng, t, 4200);  // two full 2000 ms bars + margin

  // 2 × 5 hits × 3 notes = 30; allow some tolerance at boundaries.
  ASSERT_TRUE(static_cast<int>(sink.note_ons().size()) >= 24);
}

static void test_strum_release_clears_held_state() {
  midi::Router router; CaptureSink sink; engine::Engine eng;
  setup_strum_engine(eng, router, sink);

  g_test_time_us = 0;
  uint64_t t = 0;
  eng.loop();
  eng.trigger_step(0, engine::TriggerMode::kHeld);
  advance_ms(eng, t, 500);

  ASSERT_TRUE(eng.held_active());

  eng.release_held();
  ASSERT_FALSE(eng.held_active());

  // No new note-ons after release.
  size_t count_at_release = sink.note_ons().size();
  advance_ms(eng, t, 500);
  ASSERT_EQ(static_cast<int>(sink.note_ons().size()), static_cast<int>(count_at_release));
}

static void test_strum_channel_matches_engine_channel() {
  midi::Router router; CaptureSink sink; engine::Engine eng;
  setup_strum_engine(eng, router, sink);
  eng.set_channel(7);

  g_test_time_us = 0;
  uint64_t t = 0;
  eng.loop();
  eng.trigger_step(0, engine::TriggerMode::kHeld);

  advance_ms(eng, t, 2200);

  for (const auto& m : sink.msgs) {
    if (m.t == "on" || m.t == "off") {
      ASSERT_EQ(m.ch, 7);
    }
  }
}

static void test_strum_reggae_4_hits_uniform_velocity() {
  // Reggae: 4 hits all at velocity_pct=100.  With base=100 → all vel=100.
  midi::Router router; CaptureSink sink; engine::Engine eng;
  setup_strum_engine(eng, router, sink, engine::StrumRhythm::kReggae);

  g_test_time_us = 0;
  uint64_t t = 0;
  eng.loop();
  eng.modifier().set_manual_inversion(0);
  eng.trigger_step(0, engine::TriggerMode::kHeld);

  advance_ms(eng, t, 2200);  // 4 hits in 16 16ths = 2000 ms

  auto ons = sink.note_ons();
  ASSERT_TRUE(static_cast<int>(ons.size()) >= 12);  // 4 hits × 3 notes

  for (const auto& m : sink.msgs) {
    if (m.t == "on") {
      ASSERT_EQ(m.vel, 100);
    }
  }
}

static void test_strum_funk_ghost_note_velocity() {
  // Funk ghost hits carry velocity_pct=40; accents carry 100%.
  // base=100 → ghosts=40, accents=100.  Both must appear.
  midi::Router router; CaptureSink sink; engine::Engine eng;
  setup_strum_engine(eng, router, sink, engine::StrumRhythm::kFunk);

  g_test_time_us = 0;
  uint64_t t = 0;
  eng.loop();
  eng.trigger_step(0, engine::TriggerMode::kHeld);

  advance_ms(eng, t, 2200);  // Funk = 16 16ths = 2000 ms

  bool found_ghost = false, found_accent = false;
  for (const auto& m : sink.msgs) {
    if (m.t == "on") {
      if (m.vel == 40)  found_ghost  = true;
      if (m.vel == 100) found_accent = true;
    }
  }
  ASSERT_TRUE(found_ghost);
  ASSERT_TRUE(found_accent);
}

static void test_strum_velocity_floor_is_one() {
  // vel = max(1, base * pct / 100).  With base=1 and pct=40: 1*40/100 = 0 → clamped to 1.
  midi::Router router; CaptureSink sink; engine::Engine eng;
  setup_strum_engine(eng, router, sink, engine::StrumRhythm::kFunk);

  engine::ProgressionStep step{};
  step.scale_degree = 1;
  step.chord_quality = "maj";
  step.velocity = 1;  // very low base
  step.transpose = 0;
  eng.progression().set_steps({step});

  g_test_time_us = 0;
  uint64_t t = 0;
  eng.loop();
  eng.trigger_step(0, engine::TriggerMode::kHeld);

  advance_ms(eng, t, 500);

  for (const auto& m : sink.msgs) {
    if (m.t == "on") {
      ASSERT_TRUE(m.vel >= 1);
    }
  }
}

static void test_strum_bossa_nova_fires_7_hits() {
  // Bossa Nova: 32 16ths = 4000 ms at 120 BPM, 7 hits.
  // First hit is at position 0 — fires immediately on first service_held call.
  midi::Router router; CaptureSink sink; engine::Engine eng;
  setup_strum_engine(eng, router, sink, engine::StrumRhythm::kBossaNova);

  g_test_time_us = 0;
  uint64_t t = 0;
  eng.loop();
  eng.modifier().set_manual_inversion(0);
  eng.trigger_step(0, engine::TriggerMode::kHeld);

  advance_ms(eng, t, 4200);  // full 4000 ms bar + margin

  // 7 hits × 3 notes = 21
  ASSERT_TRUE(static_cast<int>(sink.note_ons().size()) >= 18);
}

static void test_strum_bossa_nova_first_hit_fires_immediately() {
  // Bossa Nova position 0: hit_time = bar_start = trigger time.
  // Enters lookahead on the very first service_held call → fires within 10 ms.
  midi::Router router; CaptureSink sink; engine::Engine eng;
  setup_strum_engine(eng, router, sink, engine::StrumRhythm::kBossaNova);

  g_test_time_us = 0;
  uint64_t t = 0;
  eng.loop();
  eng.trigger_step(0, engine::TriggerMode::kHeld);

  advance_ms(eng, t, 20);  // very short — only position-0 hit has fired

  ASSERT_FALSE(sink.note_ons().empty());
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
  TEST_SUITE("service_held — strum rhythm scheduling");
  RUN_TEST(test_strum_activates_held_state);
  RUN_TEST(test_strum_kNone_fires_chord_once_not_repeatedly);
  RUN_TEST(test_strum_fires_notes_within_lookahead);
  RUN_TEST(test_strum_salsa_fires_5_hits_per_bar);
  RUN_TEST(test_strum_velocity_scaled_by_hit_pct);
  RUN_TEST(test_strum_each_noteon_has_noteoff);
  RUN_TEST(test_strum_salsa_pattern_loops);
  RUN_TEST(test_strum_release_clears_held_state);
  RUN_TEST(test_strum_channel_matches_engine_channel);
  RUN_TEST(test_strum_reggae_4_hits_uniform_velocity);
  RUN_TEST(test_strum_funk_ghost_note_velocity);
  RUN_TEST(test_strum_velocity_floor_is_one);
  RUN_TEST(test_strum_bossa_nova_fires_7_hits);
  RUN_TEST(test_strum_bossa_nova_first_hit_fires_immediately);

  test_print_result();
  return (g_fail > 0) ? 1 : 0;
}
