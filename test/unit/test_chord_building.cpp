// Tests: chord building, key resolution, inversions, voice leading, voicing spread.
#include "../framework.h"
#include "engine/engine.h"
#include "engine/progression.h"
#include "midi/router.h"
#include <algorithm>
#include <vector>

// ---------------------------------------------------------------------------
// CaptureSink — registered to a real Router; captures all MIDI events.
// (Router methods are non-virtual, so we must use TransportSink to intercept.)
// ---------------------------------------------------------------------------
struct CaptureSink : midi::TransportSink {
  struct Event { std::string type; uint8_t ch, note, vel; };
  std::vector<Event> events;

  void send_note_on(uint8_t ch, uint8_t n, uint8_t v) override {
    events.push_back({"on", ch, n, v});
  }
  void send_note_off(uint8_t ch, uint8_t n, uint8_t v) override {
    events.push_back({"off", ch, n, v});
  }
  void send_pitch_bend(uint8_t, int16_t) override {}
  void send_control_change(uint8_t, uint8_t, uint8_t) override {}

  std::vector<uint8_t> note_ons() const {
    std::vector<uint8_t> r;
    for (auto& e : events) if (e.type == "on") r.push_back(e.note);
    return r;
  }
  bool has_off(uint8_t ch, uint8_t note) const {
    for (auto& e : events) if (e.type == "off" && e.ch == ch && e.note == note) return true;
    return false;
  }
  void clear() { events.clear(); }
};

// ---------------------------------------------------------------------------
// Helper: fire a chord via the engine and return sorted note-on MIDI numbers.
// ---------------------------------------------------------------------------
static std::vector<uint8_t> chord_notes(const std::string& key,
                                         const std::string& scale,
                                         int degree,
                                         const std::string& quality,
                                         uint8_t inversion = 0) {
  midi::Router router;
  CaptureSink sink;
  router.register_sink(&sink);

  engine::Engine eng;
  eng.begin();
  eng.set_router(&router);
  eng.set_key(key);
  eng.set_scale(scale);
  eng.set_channel(0);

  engine::ProgressionStep step{};
  step.scale_degree  = static_cast<uint8_t>(degree);
  step.chord_quality = quality;
  step.inversion     = inversion;
  step.octave        = 0;
  step.velocity      = 100;
  step.transpose     = 0;
  step.bend_into_next = false;
  eng.progression().set_steps({step});

  g_test_time_us = 0;
  eng.trigger_step(0, engine::TriggerMode::kOneShot);
  g_test_time_us = 500 * 1000;  // advance 500 ms so all strum offsets fire
  esp_timer_fire_pending();

  auto notes = sink.note_ons();
  std::sort(notes.begin(), notes.end());
  return notes;
}

// ---------------------------------------------------------------------------
// Helper: fire a chord under a given voicing style and return sorted notes.
// ---------------------------------------------------------------------------
static std::vector<uint8_t> chord_notes_voiced(const std::string& quality,
                                               engine::VoicingSpread voicing,
                                               int8_t octave = 0, int8_t transpose = 0) {
  midi::Router router;
  CaptureSink sink;
  router.register_sink(&sink);

  engine::Engine eng;
  eng.begin();
  eng.set_router(&router);
  eng.set_key("C");
  eng.set_scale("Major");
  eng.set_channel(0);
  eng.modifier().set_voicing_spread(voicing);

  engine::ProgressionStep step{};
  step.scale_degree = 1;
  step.chord_quality = quality;
  step.inversion = 0;
  step.octave = octave;
  step.velocity = 100;
  step.transpose = transpose;
  step.bend_into_next = false;
  eng.progression().set_steps({step});

  g_test_time_us = 0;
  eng.trigger_step(0, engine::TriggerMode::kOneShot);
  g_test_time_us = 500 * 1000;
  esp_timer_fire_pending();

  auto notes = sink.note_ons();
  std::sort(notes.begin(), notes.end());
  return notes;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_c_major_root_position() {
  auto n = chord_notes("C", "Major", 1, "maj");
  ASSERT_EQ(n.size(), (size_t)3);
  ASSERT_EQ(n[0], 60); ASSERT_EQ(n[1], 64); ASSERT_EQ(n[2], 67);
}
static void test_c_minor_root_position() {
  auto n = chord_notes("C", "Major", 1, "min");
  ASSERT_EQ(n.size(), (size_t)3);
  ASSERT_EQ(n[0], 60); ASSERT_EQ(n[1], 63); ASSERT_EQ(n[2], 67);
}
static void test_g_major_root_position() {
  auto n = chord_notes("G", "Major", 1, "maj");
  ASSERT_EQ(n.size(), (size_t)3);
  ASSERT_EQ(n[0], 67); ASSERT_EQ(n[1], 71); ASSERT_EQ(n[2], 74);
}
static void test_f_major_root_position() {
  auto n = chord_notes("F", "Major", 1, "maj");
  ASSERT_EQ(n.size(), (size_t)3);
  ASSERT_EQ(n[0], 65); ASSERT_EQ(n[1], 69); ASSERT_EQ(n[2], 72);
}
static void test_bb_major_root_position() {
  auto n = chord_notes("Bb", "Major", 1, "maj");
  ASSERT_EQ(n.size(), (size_t)3);
  ASSERT_EQ(n[0], 70); ASSERT_EQ(n[1], 74); ASSERT_EQ(n[2], 77);
}
static void test_scale_degree_ii_minor_in_c() {
  auto n = chord_notes("C", "Major", 2, "min");
  ASSERT_EQ(n.size(), (size_t)3);
  ASSERT_EQ(n[0], 62); ASSERT_EQ(n[1], 65); ASSERT_EQ(n[2], 69);
}
static void test_scale_degree_v_in_c() {
  auto n = chord_notes("C", "Major", 5, "maj");
  ASSERT_EQ(n.size(), (size_t)3);
  ASSERT_EQ(n[0], 67); ASSERT_EQ(n[1], 71); ASSERT_EQ(n[2], 74);
}
static void test_first_inversion() {
  // auto-inversion is on by default and picks 0 when no previous chord.
  // We must disable it and set a manual inversion via the modifier.
  // Manual first inversion.
  midi::Router router; CaptureSink sink; router.register_sink(&sink);
  engine::Engine eng; eng.begin(); eng.set_router(&router); eng.set_channel(0);
  eng.set_key("C"); eng.set_scale("Major");
  eng.modifier().set_manual_inversion(1);

  engine::ProgressionStep step{};
  step.scale_degree = 1; step.chord_quality = "maj";
  step.inversion = 0; step.octave = 0; step.velocity = 100;
  step.transpose = 0; step.bend_into_next = false;
  eng.progression().set_steps({step});

  g_test_time_us = 0;
  eng.trigger_step(0, engine::TriggerMode::kOneShot);
  g_test_time_us = 500 * 1000; esp_timer_fire_pending();

  auto n = sink.note_ons();
  std::sort(n.begin(), n.end());
  // C major first inversion: E4=64, G4=67, C5=72
  ASSERT_EQ(n.size(), (size_t)3);
  ASSERT_EQ(n[0], 64); ASSERT_EQ(n[1], 67); ASSERT_EQ(n[2], 72);
}
static void test_second_inversion() {
  // Manual second inversion.
  midi::Router router; CaptureSink sink; router.register_sink(&sink);
  engine::Engine eng; eng.begin(); eng.set_router(&router); eng.set_channel(0);
  eng.set_key("C"); eng.set_scale("Major");
  eng.modifier().set_manual_inversion(2);

  engine::ProgressionStep step{};
  step.scale_degree = 1; step.chord_quality = "maj";
  step.inversion = 0; step.octave = 0; step.velocity = 100;
  step.transpose = 0; step.bend_into_next = false;
  eng.progression().set_steps({step});

  g_test_time_us = 0;
  eng.trigger_step(0, engine::TriggerMode::kOneShot);
  g_test_time_us = 500 * 1000; esp_timer_fire_pending();

  auto n = sink.note_ons();
  std::sort(n.begin(), n.end());
  // C major second inversion: G4=67, C5=72, E5=76
  ASSERT_EQ(n.size(), (size_t)3);
  ASSERT_EQ(n[0], 67); ASSERT_EQ(n[1], 72); ASSERT_EQ(n[2], 76);
}
static void test_maj7_chord() {
  auto n = chord_notes("C", "Major", 1, "maj7");
  ASSERT_EQ(n.size(), (size_t)4);
  ASSERT_EQ(n[0], 60); ASSERT_EQ(n[1], 64); ASSERT_EQ(n[2], 67); ASSERT_EQ(n[3], 71);
}
static void test_dom7_chord() {
  auto n = chord_notes("C", "Major", 1, "dom7");
  ASSERT_EQ(n.size(), (size_t)4);
  ASSERT_EQ(n[0], 60); ASSERT_EQ(n[1], 64); ASSERT_EQ(n[2], 67); ASSERT_EQ(n[3], 70);
}
static void test_min7_chord() {
  auto n = chord_notes("C", "Major", 1, "min7");
  ASSERT_EQ(n.size(), (size_t)4);
  ASSERT_EQ(n[0], 60); ASSERT_EQ(n[1], 63); ASSERT_EQ(n[2], 67); ASSERT_EQ(n[3], 70);
}
// ---------------------------------------------------------------------------
// Voicing spread — eight styles against a worked reference table (Cmaj7).
// ---------------------------------------------------------------------------
static void test_voicing_close() {
  auto n = chord_notes_voiced("maj7", engine::VoicingSpread::kClose);
  ASSERT_EQ(n.size(), (size_t)4);
  ASSERT_EQ(n[0], 60); ASSERT_EQ(n[1], 64); ASSERT_EQ(n[2], 67); ASSERT_EQ(n[3], 71);
}
static void test_voicing_default_unchanged() {
  auto n = chord_notes_voiced("maj7", engine::VoicingSpread::kDefault);
  ASSERT_EQ(n.size(), (size_t)4);
  ASSERT_EQ(n[0], 60); ASSERT_EQ(n[1], 64); ASSERT_EQ(n[2], 67); ASSERT_EQ(n[3], 71);
}
static void test_voicing_drop2() {
  auto n = chord_notes_voiced("maj7", engine::VoicingSpread::kDrop2);
  ASSERT_EQ(n.size(), (size_t)4);
  ASSERT_EQ(n[0], 55); ASSERT_EQ(n[1], 60); ASSERT_EQ(n[2], 64); ASSERT_EQ(n[3], 71);
}
static void test_voicing_drop3() {
  auto n = chord_notes_voiced("maj7", engine::VoicingSpread::kDrop3);
  ASSERT_EQ(n.size(), (size_t)4);
  ASSERT_EQ(n[0], 52); ASSERT_EQ(n[1], 60); ASSERT_EQ(n[2], 67); ASSERT_EQ(n[3], 71);
}
static void test_voicing_open() {
  auto n = chord_notes_voiced("maj7", engine::VoicingSpread::kOpen);
  ASSERT_EQ(n.size(), (size_t)4);
  ASSERT_EQ(n[0], 48); ASSERT_EQ(n[1], 55); ASSERT_EQ(n[2], 64); ASSERT_EQ(n[3], 71);
}
static void test_voicing_shell() {
  auto n = chord_notes_voiced("maj7", engine::VoicingSpread::kShell);
  ASSERT_EQ(n.size(), (size_t)3);
  ASSERT_EQ(n[0], 60); ASSERT_EQ(n[1], 71); ASSERT_EQ(n[2], 76);
}
static void test_voicing_rootless() {
  auto n = chord_notes_voiced("maj7", engine::VoicingSpread::kRootless);
  ASSERT_EQ(n.size(), (size_t)4);
  ASSERT_EQ(n[0], 64); ASSERT_EQ(n[1], 67); ASSERT_EQ(n[2], 71); ASSERT_EQ(n[3], 74);
}
static void test_voicing_guitar() {
  auto n = chord_notes_voiced("maj7", engine::VoicingSpread::kGuitar);
  ASSERT_EQ(n.size(), (size_t)4);
  ASSERT_EQ(n[0], 48); ASSERT_EQ(n[1], 55); ASSERT_EQ(n[2], 59); ASSERT_EQ(n[3], 64);
}
static void test_voicing_eight_styles_produce_eight_distinct_note_sets() {
  static constexpr engine::VoicingSpread kStyles[] = {
      engine::VoicingSpread::kClose,   engine::VoicingSpread::kDefault,
      engine::VoicingSpread::kDrop2,   engine::VoicingSpread::kDrop3,
      engine::VoicingSpread::kOpen,    engine::VoicingSpread::kShell,
      engine::VoicingSpread::kRootless, engine::VoicingSpread::kGuitar,
  };
  // maj9 (a five-note chord) rather than maj7: its ninth pushes the span past
  // an octave, so Close actually folds something and isn't a Default-alike.
  std::vector<std::vector<uint8_t>> sets;
  for (auto style : kStyles) sets.push_back(chord_notes_voiced("maj9", style));
  for (size_t i = 0; i < sets.size(); ++i)
    for (size_t j = i + 1; j < sets.size(); ++j)
      ASSERT_TRUE(sets[i] != sets[j]);
}

// Triads lack a seventh: Drop3 falls back to Drop2, Shell is left unchanged
// (root+third+fifth), and Rootless drops the root without adding a ninth.
static void test_voicing_triad_fallbacks() {
  auto drop2 = chord_notes_voiced("maj", engine::VoicingSpread::kDrop2);
  auto drop3 = chord_notes_voiced("maj", engine::VoicingSpread::kDrop3);
  ASSERT_TRUE(drop2 == drop3);

  auto shell = chord_notes_voiced("maj", engine::VoicingSpread::kShell);
  ASSERT_EQ(shell.size(), (size_t)3);
  ASSERT_EQ(shell[0], 60); ASSERT_EQ(shell[1], 64); ASSERT_EQ(shell[2], 67);

  auto rootless = chord_notes_voiced("maj", engine::VoicingSpread::kRootless);
  ASSERT_EQ(rootless.size(), (size_t)2);
  ASSERT_EQ(rootless[0], 64); ASSERT_EQ(rootless[1], 67);
}

// Five or more notes: the style applies to the lowest four voices only; any
// extension on top (here maj9's ninth) survives untouched.
static void test_voicing_five_note_chord_leaves_extension_untouched() {
  auto n = chord_notes_voiced("maj9", engine::VoicingSpread::kDrop2);
  ASSERT_EQ(n.size(), (size_t)5);
  ASSERT_EQ(n[0], 55); ASSERT_EQ(n[1], 60); ASSERT_EQ(n[2], 64);
  ASSERT_EQ(n[3], 71); ASSERT_EQ(n[4], 74);  // 74 = the built ninth, untouched.
}

// Rootless derives its ninth from the chord root + scale; if that candidate
// falls outside clamp_midi_note()'s range, it is omitted rather than clamped.
static void test_voicing_rootless_omits_out_of_range_ninth() {
  // Root at 116: the seventh (127) is the highest in-range chord tone, but
  // the derived ninth (116+12+2=130) overflows and must be omitted, not
  // clamped to 127 (which would collide with the seventh already there).
  auto n = chord_notes_voiced("maj7", engine::VoicingSpread::kRootless,
                              /*octave=*/4, /*transpose=*/8);
  ASSERT_EQ(n.size(), (size_t)3);  // third, fifth, seventh only — no ninth.
  ASSERT_EQ(n[0], 120); ASSERT_EQ(n[1], 123); ASSERT_EQ(n[2], 127);
}

// ---------------------------------------------------------------------------
// Roll ("stagger") — gate survives the offset, and release cancels
// not-yet-fired note-ons rather than only silencing what already sounded.
// ---------------------------------------------------------------------------
static void test_roll_gate_is_relative_to_each_voices_own_offset() {
  midi::Router router;
  CaptureSink sink;
  router.register_sink(&sink);

  engine::Engine eng;
  eng.begin();
  eng.set_router(&router);
  eng.set_key("C");
  eng.set_scale("Major");
  eng.set_channel(0);
  eng.performer().set_roll_ms(20);
  eng.performer().set_roll_direction(engine::RollDirection::kUp);
  eng.performer().set_note_length_ms(50);

  engine::ProgressionStep step{};
  step.scale_degree = 1; step.chord_quality = "maj7";
  step.inversion = 0; step.octave = 0; step.velocity = 100;
  step.transpose = 0; step.bend_into_next = false;
  eng.progression().set_steps({step});

  g_test_time_us = 0;
  eng.trigger_step(0, engine::TriggerMode::kOneShot);  // C4 E4 G4 B4 = 60,64,67,71.

  // Voice 0 (60) fires at +0ms, gates off at +50ms; voice 1 (64) fires at
  // +20ms, gates off at +70ms — not simultaneously with voice 0's note-off.
  g_test_time_us = 55 * 1000; esp_timer_fire_pending();
  ASSERT_TRUE(sink.has_off(0, 60));
  ASSERT_FALSE(sink.has_off(0, 64));

  g_test_time_us = 500 * 1000; esp_timer_fire_pending();
  ASSERT_TRUE(sink.has_off(0, 64));
  ASSERT_TRUE(sink.has_off(0, 67));
  ASSERT_TRUE(sink.has_off(0, 71));
}

static void test_roll_release_cancels_pending_note_ons() {
  midi::Router router;
  CaptureSink sink;
  router.register_sink(&sink);

  engine::Engine eng;
  eng.begin();
  eng.set_router(&router);
  eng.set_key("C");
  eng.set_scale("Major");
  eng.set_channel(0);
  eng.performer().set_roll_ms(20);
  eng.performer().set_roll_direction(engine::RollDirection::kUp);

  engine::ProgressionStep step{};
  step.scale_degree = 1; step.chord_quality = "maj7";
  step.inversion = 0; step.octave = 0; step.velocity = 100;
  step.transpose = 0; step.bend_into_next = false;
  eng.progression().set_steps({step});

  g_test_time_us = 0;
  eng.trigger_step(0, engine::TriggerMode::kHeld);  // C4 E4 G4 B4 = 60,64,67,71.

  // 25ms in: voices at offsets 0 and 20 have fired; 40 and 60 have not.
  g_test_time_us = 25 * 1000; esp_timer_fire_pending();
  auto ons = sink.note_ons();
  ASSERT_TRUE(std::find(ons.begin(), ons.end(), 60) != ons.end());
  ASSERT_TRUE(std::find(ons.begin(), ons.end(), 64) != ons.end());
  ASSERT_TRUE(std::find(ons.begin(), ons.end(), 67) == ons.end());
  ASSERT_TRUE(std::find(ons.begin(), ons.end(), 71) == ons.end());

  // Release mid-roll: pending note-ons must be cancelled outright, not just
  // followed by a note-off once they'd have sounded.
  eng.release_held();
  g_test_time_us = 500 * 1000; esp_timer_fire_pending();

  ons = sink.note_ons();
  ASSERT_TRUE(std::find(ons.begin(), ons.end(), 67) == ons.end());
  ASSERT_TRUE(std::find(ons.begin(), ons.end(), 71) == ons.end());
  ASSERT_TRUE(sink.has_off(0, 60));
  ASSERT_TRUE(sink.has_off(0, 64));
}

static void test_all_keys_produce_valid_midi() {
  static const char* keys[] = {
    "C","C#","Db","D","D#","Eb","E","F","F#","Gb","G","G#","Ab","A","A#","Bb","B"
  };
  for (auto* k : keys) {
    auto n = chord_notes(k, "Major", 1, "maj");
    ASSERT_EQ(n.size(), (size_t)3);
    for (auto note : n) ASSERT_TRUE(note <= 127);
  }
}
static void test_all_scale_degrees() {
  static const char* qualities[] = {"maj","min","min","maj","maj","min","dim"};
  for (int deg = 1; deg <= 7; ++deg) {
    auto n = chord_notes("C", "Major", deg, qualities[deg-1]);
    ASSERT_TRUE(n.size() >= 2);
    for (auto note : n) ASSERT_TRUE(note >= 21 && note <= 108);
  }
}
static void test_channel_used_for_note_on() {
  midi::Router router;
  CaptureSink sink;
  router.register_sink(&sink);

  engine::Engine eng;
  eng.begin();
  eng.set_router(&router);
  eng.set_channel(3);

  engine::ProgressionStep step{};
  step.scale_degree = 1; step.chord_quality = "maj";
  step.inversion = 0; step.octave = 0; step.velocity = 100;
  step.transpose = 0; step.bend_into_next = false;
  eng.progression().set_steps({step});

  g_test_time_us = 0;
  eng.trigger_step(0, engine::TriggerMode::kOneShot);
  g_test_time_us = 500 * 1000;
  esp_timer_fire_pending();

  ASSERT_TRUE(!sink.events.empty());
  for (auto& ev : sink.events)
    ASSERT_EQ(ev.ch, 3);
}
static void test_all_notes_off_silences() {
  midi::Router router;
  CaptureSink sink;
  router.register_sink(&sink);

  engine::Engine eng;
  eng.begin();
  eng.set_router(&router);
  eng.set_channel(0);

  engine::ProgressionStep step{};
  step.scale_degree = 1; step.chord_quality = "maj";
  step.inversion = 0; step.octave = 0; step.velocity = 100;
  step.transpose = 0; step.bend_into_next = false;
  eng.progression().set_steps({step});

  g_test_time_us = 0;
  eng.trigger_step(0, engine::TriggerMode::kHeld);
  g_test_time_us = 50 * 1000;
  esp_timer_fire_pending();

  auto ons = sink.note_ons();
  ASSERT_TRUE(!ons.empty());

  sink.clear();
  eng.all_notes_off();

  for (auto n : ons) {
    bool found = false;
    for (auto& ev : sink.events)
      if (ev.type == "off" && ev.note == n) { found = true; break; }
    ASSERT_TRUE(found);
  }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Voice leading: how consecutive chords connect (Engine::voice_chord).
// ---------------------------------------------------------------------------
struct Progression {
  midi::Router router; CaptureSink sink; engine::Engine eng;
  explicit Progression(engine::VoiceLeading vl, const char* key = "C") {
    router.register_sink(&sink);
    eng.begin(); eng.set_router(&router); eng.set_channel(0);
    eng.set_key(key); eng.set_scale("Major");
    eng.modifier().set_voice_leading(vl);
    static const char* kQuality[7] = {"maj", "min", "min", "maj", "maj", "min", "dim"};
    std::vector<engine::ProgressionStep> steps;
    for (uint8_t d = 1; d <= 7; ++d) {
      engine::ProgressionStep step{};
      step.scale_degree = d; step.chord_quality = kQuality[d - 1]; step.velocity = 100;
      steps.push_back(step);
    }
    eng.progression().set_steps(steps);
  }
  // Sounds scale degree `degree` and returns its notes, low to high.
  std::vector<uint8_t> play(int degree) {
    sink.clear();
    g_test_time_us += 1000 * 1000;
    eng.trigger_step(static_cast<size_t>(degree - 1), engine::TriggerMode::kOneShot);
    g_test_time_us += 100 * 1000; esp_timer_fire_pending();
    auto n = sink.note_ons();
    std::sort(n.begin(), n.end());
    return n;
  }
};

static void test_voice_leading_off_plays_every_chord_as_written() {
  Progression p(engine::VoiceLeading::kOff);
  ASSERT_TRUE(p.play(1) == (std::vector<uint8_t>{60, 64, 67}));
  ASSERT_TRUE(p.play(4) == (std::vector<uint8_t>{65, 69, 72}));
  ASSERT_TRUE(p.play(5) == (std::vector<uint8_t>{67, 71, 74}));
  ASSERT_TRUE(p.play(1) == (std::vector<uint8_t>{60, 64, 67}));
}

static void test_voice_leading_smooth_takes_the_nearest_inversion() {
  // The textbook I-IV-V-I: C E G, C F A, B D G, C E G.
  Progression p(engine::VoiceLeading::kSmooth);
  ASSERT_TRUE(p.play(1) == (std::vector<uint8_t>{60, 64, 67}));  // first chord: root position
  ASSERT_TRUE(p.play(4) == (std::vector<uint8_t>{60, 65, 69}));
  ASSERT_EQ(p.eng.sounding_inversion(), 2);
  ASSERT_TRUE(p.play(5) == (std::vector<uint8_t>{59, 62, 67}));
  ASSERT_EQ(p.eng.sounding_inversion(), 1);
  ASSERT_TRUE(p.play(1) == (std::vector<uint8_t>{60, 64, 67}));
  ASSERT_EQ(p.eng.sounding_inversion(), 0);
}

static void test_voice_leading_common_holds_shared_notes() {
  Progression p(engine::VoiceLeading::kCommon);
  ASSERT_TRUE(p.play(1) == (std::vector<uint8_t>{60, 64, 67}));
  ASSERT_TRUE(p.play(6) == (std::vector<uint8_t>{60, 64, 69}));  // C and E stay put
}

static void test_voice_leading_never_drifts_out_of_the_register() {
  // However long the progression, every bass stays within half an octave of
  // the tonic and the tonic chord comes back where it started.
  Progression p(engine::VoiceLeading::kSmooth);
  const auto first = p.play(1);
  for (int round = 0; round < 6; ++round)
    for (int degree : {2, 3, 4, 5, 6, 7, 5, 4, 6, 2, 5, 1}) {
      const auto n = p.play(degree);
      ASSERT_EQ(n.size(), (size_t)3);
      ASSERT_TRUE(n.front() >= 54 && n.front() <= 66);
    }
  ASSERT_TRUE(p.play(5).front() >= 54);
  const auto last = p.play(1);
  ASSERT_TRUE(last.front() >= 54 && last.back() <= 79);
}

static void test_octave_change_moves_the_register_and_stays_there() {
  // The reported fault: after an octave change the next chord drifted back
  // toward the old register. The octave must win, for every later chord.
  Progression p(engine::VoiceLeading::kSmooth);
  p.play(1); p.play(4);
  p.eng.modifier().adjust_octave(1);
  p.eng.shift_prev_chord(12);
  ASSERT_TRUE(p.play(5) == (std::vector<uint8_t>{71, 74, 79}));
  ASSERT_TRUE(p.play(1) == (std::vector<uint8_t>{72, 76, 79}));
  ASSERT_TRUE(p.play(4) == (std::vector<uint8_t>{72, 77, 81}));
  // Even with no memory carried over, the register follows the octave.
  p.eng.modifier().adjust_octave(-2);
  p.eng.clear_prev_chord();
  ASSERT_TRUE(p.play(1) == (std::vector<uint8_t>{48, 52, 55}));
}

static void test_manual_inversion_overrides_voice_leading() {
  Progression p(engine::VoiceLeading::kSmooth);
  p.play(1);
  p.eng.modifier().set_manual_inversion(1);
  ASSERT_TRUE(p.play(4) == (std::vector<uint8_t>{69, 72, 77}));  // F first inversion, as written
  ASSERT_TRUE(p.play(5) == (std::vector<uint8_t>{71, 74, 79}));
  p.eng.modifier().reset_inversion();                            // back to voice leading
  const auto n = p.play(1);
  ASSERT_TRUE(n.front() >= 54 && n.front() <= 66);
}

static void test_voice_leading_follows_the_key() {
  // The register window is anchored on the key's tonic, not on C.
  Progression p(engine::VoiceLeading::kSmooth, "G");
  ASSERT_TRUE(p.play(1) == (std::vector<uint8_t>{67, 71, 74}));
  ASSERT_TRUE(p.play(4) == (std::vector<uint8_t>{67, 72, 76}));
  ASSERT_TRUE(p.play(5) == (std::vector<uint8_t>{66, 69, 74}));
}

static void test_rootless_keeps_a_ninth_the_chord_already_has() {
  // dom9 already contains its ninth; Rootless must not add a second one.
  auto n = chord_notes_voiced("dom9", engine::VoicingSpread::kRootless);
  for (size_t i = 1; i < n.size(); ++i) ASSERT_TRUE(n[i] != n[i - 1]);
}

int main() {
  TEST_SUITE("Chord Building");
  RUN_TEST(test_c_major_root_position);
  RUN_TEST(test_c_minor_root_position);
  RUN_TEST(test_g_major_root_position);
  RUN_TEST(test_f_major_root_position);
  RUN_TEST(test_bb_major_root_position);
  RUN_TEST(test_scale_degree_ii_minor_in_c);
  RUN_TEST(test_scale_degree_v_in_c);
  RUN_TEST(test_first_inversion);
  RUN_TEST(test_second_inversion);
  RUN_TEST(test_maj7_chord);
  RUN_TEST(test_dom7_chord);
  RUN_TEST(test_min7_chord);

  TEST_SUITE("Voicing spread — eight styles");
  RUN_TEST(test_voicing_close);
  RUN_TEST(test_voicing_default_unchanged);
  RUN_TEST(test_voicing_drop2);
  RUN_TEST(test_voicing_drop3);
  RUN_TEST(test_voicing_open);
  RUN_TEST(test_voicing_shell);
  RUN_TEST(test_voicing_rootless);
  RUN_TEST(test_voicing_guitar);
  RUN_TEST(test_voicing_eight_styles_produce_eight_distinct_note_sets);
  RUN_TEST(test_voicing_triad_fallbacks);
  RUN_TEST(test_voicing_five_note_chord_leaves_extension_untouched);
  RUN_TEST(test_voicing_rootless_omits_out_of_range_ninth);

  TEST_SUITE("Voice leading");
  RUN_TEST(test_voice_leading_off_plays_every_chord_as_written);
  RUN_TEST(test_voice_leading_smooth_takes_the_nearest_inversion);
  RUN_TEST(test_voice_leading_common_holds_shared_notes);
  RUN_TEST(test_voice_leading_never_drifts_out_of_the_register);
  RUN_TEST(test_octave_change_moves_the_register_and_stays_there);
  RUN_TEST(test_manual_inversion_overrides_voice_leading);
  RUN_TEST(test_voice_leading_follows_the_key);
  RUN_TEST(test_rootless_keeps_a_ninth_the_chord_already_has);

  TEST_SUITE("Roll (stagger)");
  RUN_TEST(test_roll_gate_is_relative_to_each_voices_own_offset);
  RUN_TEST(test_roll_release_cancels_pending_note_ons);

  TEST_SUITE("Chord Building");
  RUN_TEST(test_all_keys_produce_valid_midi);
  RUN_TEST(test_all_scale_degrees);
  RUN_TEST(test_channel_used_for_note_on);
  RUN_TEST(test_all_notes_off_silences);

  test_print_result();
  return (g_fail > 0) ? 1 : 0;
}
