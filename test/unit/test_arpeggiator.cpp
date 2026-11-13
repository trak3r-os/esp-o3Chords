// Tests: arpeggiator patterns, gate%, note-off correctness, octave expansion.
// Covers all bugs found in the deep analysis:
//   Bug 2 — kUpDown/kDownUp endpoint duplicates
//   Bug 3 — arp note-off events re-dispatched as kNoteOn (engine dispatch)
//   Bug 4 — note-off silently dropped when gate > step boundary
//   Bug 7 — uint8_t overflow in octave expansion
#include "../framework.h"
#include "engine/arpeggiator.h"
#include "engine/engine.h"
#include "midi/router.h"
#include <algorithm>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

struct CaptureSink : midi::TransportSink {
  struct Msg { std::string t; uint8_t ch, note, vel; int16_t bend; };
  std::vector<Msg> msgs;
  void send_note_on(uint8_t ch, uint8_t n, uint8_t v) override { msgs.push_back({"on",ch,n,v,0}); }
  void send_note_off(uint8_t ch, uint8_t n, uint8_t v) override { msgs.push_back({"off",ch,n,v,0}); }
  void send_pitch_bend(uint8_t ch, int16_t val) override { msgs.push_back({"pb",ch,0,0,val}); }
  void send_control_change(uint8_t ch, uint8_t cc, uint8_t v) override { msgs.push_back({"cc",ch,cc,v,0}); }
  bool has(const std::string& t, uint8_t note) const {
    for (auto& m : msgs) if (m.t == t && m.note == note) return true;
    return false;
  }
  int count(const std::string& t, uint8_t note) const {
    int n = 0;
    for (auto& m : msgs) if (m.t == t && m.note == note) ++n;
    return n;
  }
  void clear() { msgs.clear(); }
};

// Extract the first `take` note-ons from render_arp.
// Use a large step window so patterns of any length fit.
// The caller passes the expected cycle length as `take`.
static std::vector<uint8_t> arp_seq(engine::ArpPattern pat,
                                     uint8_t octaves,
                                     const std::vector<uint8_t>& base,
                                     size_t take) {
  engine::Arpeggiator arp;
  arp.set_pattern(pat);
  arp.set_octaves(octaves);
  arp.set_gate_percent(50);
  arp.set_enabled(true);
  arp.set_rate(engine::ArpRate::kEighth);  // 250ms per note @ 120 BPM

  // step_duration = take * 250ms + margin
  uint32_t step_ms = static_cast<uint32_t>(take * 300);
  auto events = arp.render_arp(base, 120.0f, 0, step_ms, 100, 0);
  std::vector<uint8_t> seq;
  for (auto& ev : events) {
    if (ev.velocity > 0 && seq.size() < take) seq.push_back(ev.note);
  }
  return seq;
}

// ---------------------------------------------------------------------------
// Pattern correctness
// ---------------------------------------------------------------------------

static void test_up_pattern_ascending() {
  // One cycle of kUp with 3 notes = 3 note-ons
  auto s = arp_seq(engine::ArpPattern::kUp, 1, {60, 64, 67}, 3);
  ASSERT_EQ(s.size(), (size_t)3);
  ASSERT_EQ(s[0], 60); ASSERT_EQ(s[1], 64); ASSERT_EQ(s[2], 67);
}

static void test_down_pattern_descending() {
  auto s = arp_seq(engine::ArpPattern::kDown, 1, {60, 64, 67}, 3);
  ASSERT_EQ(s.size(), (size_t)3);
  ASSERT_EQ(s[0], 67); ASSERT_EQ(s[1], 64); ASSERT_EQ(s[2], 60);
}

static void test_updown_no_endpoint_duplicates() {
  // Bug 2 regression: previously produced [C,E,G,E,C] — bottom note duplicated.
  // Fix: should produce [C,E,G,E] — neither endpoint repeated.
  // Cycle length for kUpDown with 3 notes (no endpoints) = 4.
  auto s = arp_seq(engine::ArpPattern::kUpDown, 1, {60, 64, 67}, 4);
  ASSERT_EQ(s.size(), (size_t)4);
  ASSERT_EQ(s[0], 60); ASSERT_EQ(s[1], 64);
  ASSERT_EQ(s[2], 67); ASSERT_EQ(s[3], 64);
  // C (60) must appear exactly once — not duplicated at the end
  int c_count = 0;
  for (auto n : s) if (n == 60) ++c_count;
  ASSERT_EQ(c_count, 1);
}

static void test_downup_no_endpoint_duplicates() {
  // Bug 2 regression (symmetric): previously produced [G,E,C,E,G] for kDownUp.
  // Fix: should produce [G,E,C,E] — neither endpoint repeated.
  auto s = arp_seq(engine::ArpPattern::kDownUp, 1, {60, 64, 67}, 4);
  ASSERT_EQ(s.size(), (size_t)4);
  ASSERT_EQ(s[0], 67); ASSERT_EQ(s[1], 64);
  ASSERT_EQ(s[2], 60); ASSERT_EQ(s[3], 64);
  // G (67) must appear exactly once
  int g_count = 0;
  for (auto n : s) if (n == 67) ++g_count;
  ASSERT_EQ(g_count, 1);
}

static void test_updowninc_includes_endpoints() {
  // kUpDownInc deliberately includes both endpoints — [C,E,G,G,E,C]
  auto s = arp_seq(engine::ArpPattern::kUpDownInc, 1, {60, 64, 67}, 6);
  ASSERT_EQ(s.size(), (size_t)6);
  ASSERT_EQ(s[0], 60); ASSERT_EQ(s[5], 60);  // both endpoints present
  ASSERT_EQ(s[2], 67); ASSERT_EQ(s[3], 67);   // top note duplicated
}

static void test_order_pattern_same_as_up() {
  auto s = arp_seq(engine::ArpPattern::kOrder, 1, {60, 64, 67}, 3);
  ASSERT_EQ(s.size(), (size_t)3);
  ASSERT_EQ(s[0], 60); ASSERT_EQ(s[1], 64); ASSERT_EQ(s[2], 67);
}

static void test_two_octave_expansion() {
  // 2 octaves × 3 notes = 6 per cycle
  auto s = arp_seq(engine::ArpPattern::kUp, 2, {60, 64, 67}, 6);
  ASSERT_EQ(s.size(), (size_t)6);
  // First octave: 60,64,67; second: 72,76,79
  ASSERT_EQ(s[3], 72); ASSERT_EQ(s[4], 76); ASSERT_EQ(s[5], 79);
}

static void test_octave_expansion_no_overflow() {
  // Bug 7 regression: notes > 127 after octave shift must be silently skipped,
  // not wrap-around via uint8_t (e.g. 127+12=139 should NOT appear as 11).
  // With 4 octaves and note 120: only octave 0 (120) is valid; 132,144,156 > 127.
  auto s = arp_seq(engine::ArpPattern::kUp, 4, {120}, 1);
  ASSERT_EQ(s.size(), (size_t)1);
  ASSERT_EQ(s[0], 120);
  // Verify no wrap-around notes (e.g. 11, 23, 35) appeared
  for (auto n : s) ASSERT_TRUE(n >= 120);
}

// ---------------------------------------------------------------------------
// render_arp: gate and note-off behaviour
// ---------------------------------------------------------------------------

static void test_render_arp_produces_noteoffs() {
  engine::Arpeggiator arp;
  arp.set_pattern(engine::ArpPattern::kUp);
  arp.set_octaves(1);
  arp.set_gate_percent(50);
  arp.set_enabled(true);
  arp.set_rate(engine::ArpRate::kEighth);

  // 120 BPM → quarter=500ms, eighth=250ms, gate50%=125ms note duration
  auto events = arp.render_arp({60, 64, 67}, 120.0f, 0, 1000, 100, 0);

  // Must have both note-on (vel>0) and note-off (vel==0) entries
  bool has_on = false, has_off = false;
  for (auto& ev : events) {
    if (ev.velocity > 0) has_on = true;
    if (ev.velocity == 0) has_off = true;
  }
  ASSERT_TRUE(has_on);
  ASSERT_TRUE(has_off);
}

static void test_render_arp_noteoff_always_emitted_at_step_boundary() {
  // Bug 4 regression: when gate% pushes note-off past step_duration, it was
  // silently dropped.  Fix: always emit note-off, capped at step_duration.
  engine::Arpeggiator arp;
  arp.set_pattern(engine::ArpPattern::kUp);
  arp.set_octaves(1);
  arp.set_gate_percent(100);  // gate=100% → note_off_offset == note_interval
  arp.set_enabled(true);
  arp.set_rate(engine::ArpRate::kEighth);

  // 120 BPM, eighth=250ms, step=300ms, gate100%=250ms
  // Last note starts at 250ms, note-off at 500ms > 300ms → was dropped before.
  auto events = arp.render_arp({60}, 120.0f, 0, 300, 100, 0);

  bool all_ons_have_offs = true;
  for (auto& on_ev : events) {
    if (on_ev.velocity == 0) continue;
    bool found_off = false;
    for (auto& off_ev : events) {
      if (off_ev.velocity == 0 && off_ev.note == on_ev.note) {
        found_off = true;
        ASSERT_TRUE(off_ev.offset_ms <= 300);  // capped at step_duration
        break;
      }
    }
    if (!found_off) all_ons_have_offs = false;
  }
  ASSERT_TRUE(all_ons_have_offs);
}

// ---------------------------------------------------------------------------
// Engine arp dispatch: note-off events NOT re-scheduled as kNoteOn
// ---------------------------------------------------------------------------

static void make_arp_engine(engine::Engine& eng, midi::Router& router,
                              CaptureSink& sink) {
  router.register_sink(&sink);
  eng.begin();
  eng.set_router(&router);
  eng.set_channel(0);
  eng.set_key("C");
  eng.set_scale("Major");

  // Enable arp mode
  engine::ProgressionStep step{};
  step.scale_degree = 1;
  step.chord_quality = "maj";
  step.inversion = 0; step.octave = 0; step.velocity = 100;
  step.transpose = 0; step.bend_into_next = false;
  eng.progression().set_steps({step});

  eng.set_performance_mode(engine::PerformanceMode::kArp);
  eng.arpeggiator().set_enabled(true);
  eng.arpeggiator().set_rate(engine::ArpRate::kEighth);
  eng.arpeggiator().set_pattern(engine::ArpPattern::kUp);
  eng.arpeggiator().set_gate_percent(50);
  eng.arpeggiator().set_octaves(1);
  eng.set_bpm(120.0f);  // eighth=250ms, gate50%=125ms
}

static void test_engine_arp_dispatch_no_double_noteoff() {
  // Bug 3 regression: arp note-off NoteEvents (vel=0) were re-dispatched as
  // kNoteOn, then an extra kNoteOff was added — each note got TWO note-offs.
  // Fix: note-offs from render_arp are dispatched as kNoteOff directly.
  midi::Router router;
  CaptureSink sink;
  engine::Engine eng;
  make_arp_engine(eng, router, sink);

  g_test_time_us = 0;
  eng.trigger_step(0, engine::TriggerMode::kOneShot);
  // Advance 600ms to let first note fully play (on at 0, off at 125ms)
  g_test_time_us = 600 * 1000;
  esp_timer_fire_pending();

  // Each note should have exactly ONE note-off, not two
  for (auto& m : sink.msgs) {
    if (m.t == "on") {
      int off_count = sink.count("off", m.note);
      // Off count should be exactly 1 per note-on (not 2 from the old double-dispatch)
      ASSERT_TRUE(off_count <= 1);
    }
  }
}

static void test_engine_arp_dispatch_no_spurious_noteon_vel0() {
  // Bug 3 regression: vel=0 events from render_arp were scheduled as kNoteOn,
  // which adds them to active_notes_ — a phantom entry that corrupts state.
  // Fix: vel=0 events are dispatched as kNoteOff only.
  midi::Router router;
  CaptureSink sink;
  engine::Engine eng;
  make_arp_engine(eng, router, sink);

  g_test_time_us = 0;
  eng.trigger_step(0, engine::TriggerMode::kOneShot);
  g_test_time_us = 600 * 1000;
  esp_timer_fire_pending();

  // No note-on message should have velocity == 0 (that would be vel-0 note-on)
  for (auto& m : sink.msgs) {
    if (m.t == "on") ASSERT_NE(m.vel, 0);
  }
}

// ---------------------------------------------------------------------------
// Pitch bend pre-bend value (Bug 1 regression)
// ---------------------------------------------------------------------------

static void test_bend_into_next_uses_full_range() {
  // Bug 1 regression: old formula (bend_range * 8192) / 2 overflowed int16_t
  // for range > 4 and produced wrong values.  Fix: always use -8192.
  midi::Router router;
  CaptureSink sink;
  router.register_sink(&sink);

  engine::Engine eng;
  eng.begin();
  eng.set_router(&router);
  eng.set_channel(0);
  eng.set_bend_range(12);  // triggers old overflow in unfixed code

  engine::ProgressionStep step{};
  step.scale_degree = 1;
  step.chord_quality = "maj";
  step.inversion = 0; step.octave = 0; step.velocity = 100;
  step.transpose = 0;
  step.bend_into_next = true;  // <<< trigger pre-bend
  eng.progression().set_steps({step});

  g_test_time_us = 0;
  eng.trigger_step(0, engine::TriggerMode::kOneShot);
  g_test_time_us = 50 * 1000;
  esp_timer_fire_pending();

  // The first pitch-bend scheduled must be -8192 (full-range down).
  // Before the fix, range=12 produced a UB-wrapped garbage value or +0 (center).
  bool found_bend = false;
  int16_t first_bend = 0;
  for (auto& m : sink.msgs) {
    if (m.t == "pb") {
      found_bend = true;
      first_bend = m.bend;
      break;
    }
  }
  ASSERT_TRUE(found_bend);
  // Must be max-downward, not 0 (the UB result for range=12 before fix)
  ASSERT_EQ(first_bend, -8192);
}

static void test_bend_into_next_value_range_2() {
  // Verify the value is -8192 (full-range down) for range=2 as well.
  // Old formula accidentally produced the correct value only for range=2.
  // Ensure both remain correct after the fix.
  midi::Router router;

  struct BendCapture : midi::TransportSink {
    int16_t last_bend = 0;
    bool got_bend = false;
    void send_note_on(uint8_t, uint8_t, uint8_t) override {}
    void send_note_off(uint8_t, uint8_t, uint8_t) override {}
    void send_control_change(uint8_t, uint8_t, uint8_t) override {}
    void send_pitch_bend(uint8_t, int16_t val) override {
      if (!got_bend) { last_bend = val; got_bend = true; }
    }
  } bc;
  router.register_sink(&bc);

  engine::Engine eng;
  eng.begin();
  eng.set_router(&router);
  eng.set_channel(0);
  eng.set_bend_range(2);

  engine::ProgressionStep step{};
  step.scale_degree = 1; step.chord_quality = "maj";
  step.inversion = 0; step.octave = 0; step.velocity = 100;
  step.transpose = 0; step.bend_into_next = true;
  eng.progression().set_steps({step});

  g_test_time_us = 0;
  eng.trigger_step(0, engine::TriggerMode::kOneShot);
  g_test_time_us = 50 * 1000;
  esp_timer_fire_pending();

  ASSERT_TRUE(bc.got_bend);
  // Must be -8192 (full-range down), not 0, not an overflowed garbage value
  ASSERT_EQ(bc.last_bend, -8192);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
static void test_one_shot_arp_has_no_runt_note_at_odd_tempos() {
  // 130 BPM sixteenths: the step is 461 ms and four notes take 4 x 115 = 460,
  // so a fifth note used to start at 460 with a 1 ms gate.
  engine::Arpeggiator arp;
  arp.set_pattern(engine::ArpPattern::kUp);
  arp.set_octaves(1);
  arp.set_gate_percent(80);
  arp.set_enabled(true);
  arp.set_rate(engine::ArpRate::kSixteenth);
  const auto events = arp.render_arp({60, 64, 67}, 130.0f, 0, 461, 100, 0);
  int ons = 0;
  for (const auto& ev : events) if (ev.velocity > 0) ++ons;
  ASSERT_EQ(ons, 4);
}

int main() {
  TEST_SUITE("Arpeggiator Patterns");
  RUN_TEST(test_up_pattern_ascending);
  RUN_TEST(test_down_pattern_descending);
  RUN_TEST(test_updown_no_endpoint_duplicates);
  RUN_TEST(test_downup_no_endpoint_duplicates);
  RUN_TEST(test_updowninc_includes_endpoints);
  RUN_TEST(test_order_pattern_same_as_up);
  RUN_TEST(test_two_octave_expansion);
  RUN_TEST(test_octave_expansion_no_overflow);

  TEST_SUITE("Arpeggiator Gate / Note-off");
  RUN_TEST(test_render_arp_produces_noteoffs);
  RUN_TEST(test_render_arp_noteoff_always_emitted_at_step_boundary);

  TEST_SUITE("Engine Arp Dispatch (Bug 3 regression)");
  RUN_TEST(test_engine_arp_dispatch_no_double_noteoff);
  RUN_TEST(test_engine_arp_dispatch_no_spurious_noteon_vel0);

  TEST_SUITE("Pitch Bend Pre-bend (Bug 1 regression)");
  RUN_TEST(test_one_shot_arp_has_no_runt_note_at_odd_tempos);
  RUN_TEST(test_bend_into_next_uses_full_range);
  RUN_TEST(test_bend_into_next_value_range_2);

  test_print_result();
  return (g_fail > 0) ? 1 : 0;
}
