// Tests: app::apply_progression_mode() — all 22 preset chord progressions.
//
// apply_progression_mode() writes two things:
//   1. s.scale  — the scale name string (e.g. "Major", "Natural Minor")
//   2. engine.progression().steps() — 7 ProgressionStep objects (degree + quality + velocity)
//
// A bug here produces silently wrong chords: the engine just plays whatever steps it
// receives with no error.  These tests verify degrees, qualities, transpose values,
// and velocity propagation for every mode.

#include "../framework.h"
#include "app/progression_modes.h"
#include "app/state.h"
#include "app/types.h"
#include "engine/engine.h"
#include "engine/music_theory.h"
#include <vector>
#include <string>

using namespace app;
using engine::ProgressionStep;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::vector<ProgressionStep> apply_and_get(ProgressionMode mode, uint8_t vel = 100) {
  engine::Engine eng;
  eng.begin();
  Settings s{};
  s.velocity = vel;
  apply_progression_mode(eng, mode, s);
  return eng.progression().steps();
}

static std::string get_scale_string(ProgressionMode mode) {
  engine::Engine eng;
  eng.begin();
  Settings s{};
  s.velocity = 100;
  apply_progression_mode(eng, mode, s);
  return std::string(s.scale);
}

// ---------------------------------------------------------------------------
// Tests — step count
// ---------------------------------------------------------------------------

static void test_all_modes_produce_exactly_7_steps() {
  // Every mode must populate exactly 7 steps.
  static const ProgressionMode all_modes[] = {
    ProgressionMode::kMajor,
    ProgressionMode::kNaturalMinor,
    ProgressionMode::kHarmonicMinor,
    ProgressionMode::kDorian,
    ProgressionMode::kMixolydian,
    ProgressionMode::kSus2,
    ProgressionMode::kSevenths,
    ProgressionMode::kSalsa,
    ProgressionMode::kBossaNova,
    ProgressionMode::kBachata,
    ProgressionMode::kReggaeton,
    ProgressionMode::kJazz,
    ProgressionMode::kNeoSoul,
    ProgressionMode::kGospel,
    ProgressionMode::kLofi,
    ProgressionMode::kBlues,
    ProgressionMode::kFunk,
    ProgressionMode::kRnB,
    ProgressionMode::kPop,
    ProgressionMode::kTrap,
    ProgressionMode::kMotown,
    ProgressionMode::kFlamenco,
  };
  for (auto mode : all_modes) {
    auto steps = apply_and_get(mode);
    ASSERT_EQ(static_cast<int>(steps.size()), 7);
  }
}

// CHORD/Colors applies one quality override to every keypad destination.  The
// seven roots must therefore be unique independently of their displayed base
// qualities, or two differently-labelled positions can sound identical.
static void test_all_modes_have_7_distinct_sounding_roots() {
  static const ProgressionMode all_modes[] = {
    ProgressionMode::kMajor, ProgressionMode::kNaturalMinor,
    ProgressionMode::kHarmonicMinor, ProgressionMode::kDorian,
    ProgressionMode::kMixolydian, ProgressionMode::kSus2,
    ProgressionMode::kSevenths, ProgressionMode::kSalsa,
    ProgressionMode::kBossaNova, ProgressionMode::kBachata,
    ProgressionMode::kReggaeton, ProgressionMode::kJazz,
    ProgressionMode::kNeoSoul, ProgressionMode::kGospel,
    ProgressionMode::kLofi, ProgressionMode::kBlues,
    ProgressionMode::kFunk, ProgressionMode::kRnB,
    ProgressionMode::kPop, ProgressionMode::kTrap,
    ProgressionMode::kMotown, ProgressionMode::kFlamenco,
  };
  for (auto mode : all_modes) {
    engine::Engine eng;
    eng.begin();
    Settings settings{};
    settings.velocity = 100;
    apply_progression_mode(eng, mode, settings);
    bool roots[12] = {};
    for (const auto& step : eng.progression().steps()) {
      const int root = engine::progression_step_root_offset(settings, step);
      ASSERT_FALSE(roots[root]);
      roots[root] = true;
    }
  }
}

// ---------------------------------------------------------------------------
// Tests — scale modes: chord qualities
// ---------------------------------------------------------------------------

static void test_major_chord_qualities() {
  auto s = apply_and_get(ProgressionMode::kMajor);
  // I, IV, V = maj; II, III, VI = min; VII = dim
  ASSERT_EQ(s[0].scale_degree, 1); ASSERT_STREQ(s[0].chord_quality.c_str(), "maj");
  ASSERT_EQ(s[1].scale_degree, 2); ASSERT_STREQ(s[1].chord_quality.c_str(), "min");
  ASSERT_EQ(s[2].scale_degree, 3); ASSERT_STREQ(s[2].chord_quality.c_str(), "min");
  ASSERT_EQ(s[3].scale_degree, 4); ASSERT_STREQ(s[3].chord_quality.c_str(), "maj");
  ASSERT_EQ(s[4].scale_degree, 5); ASSERT_STREQ(s[4].chord_quality.c_str(), "maj");
  ASSERT_EQ(s[5].scale_degree, 6); ASSERT_STREQ(s[5].chord_quality.c_str(), "min");
  ASSERT_EQ(s[6].scale_degree, 7); ASSERT_STREQ(s[6].chord_quality.c_str(), "dim");
}

static void test_natural_minor_chord_qualities() {
  auto s = apply_and_get(ProgressionMode::kNaturalMinor);
  // i=min, ii=dim, III=maj, iv=min, v=min, VI=maj, VII=maj
  ASSERT_STREQ(s[0].chord_quality.c_str(), "min");
  ASSERT_STREQ(s[1].chord_quality.c_str(), "dim");
  ASSERT_STREQ(s[2].chord_quality.c_str(), "maj");
  ASSERT_STREQ(s[3].chord_quality.c_str(), "min");
  ASSERT_STREQ(s[4].chord_quality.c_str(), "min");
  ASSERT_STREQ(s[5].chord_quality.c_str(), "maj");
  ASSERT_STREQ(s[6].chord_quality.c_str(), "maj");
}

static void test_harmonic_minor_chord_qualities() {
  auto s = apply_and_get(ProgressionMode::kHarmonicMinor);
  // i=min, ii=dim, III=aug, iv=min, V=maj, VI=maj, vii=dim
  ASSERT_STREQ(s[0].chord_quality.c_str(), "min");
  ASSERT_STREQ(s[1].chord_quality.c_str(), "dim");
  ASSERT_STREQ(s[2].chord_quality.c_str(), "aug");
  ASSERT_STREQ(s[3].chord_quality.c_str(), "min");
  ASSERT_STREQ(s[4].chord_quality.c_str(), "maj");
  ASSERT_STREQ(s[5].chord_quality.c_str(), "maj");
  ASSERT_STREQ(s[6].chord_quality.c_str(), "dim");
}

static void test_dorian_chord_qualities() {
  auto s = apply_and_get(ProgressionMode::kDorian);
  // i=min, ii=min, III=maj, IV=maj, v=min, vi=dim, VII=maj
  ASSERT_STREQ(s[0].chord_quality.c_str(), "min");
  ASSERT_STREQ(s[1].chord_quality.c_str(), "min");
  ASSERT_STREQ(s[2].chord_quality.c_str(), "maj");
  ASSERT_STREQ(s[3].chord_quality.c_str(), "maj");
  ASSERT_STREQ(s[4].chord_quality.c_str(), "min");
  ASSERT_STREQ(s[5].chord_quality.c_str(), "dim");
  ASSERT_STREQ(s[6].chord_quality.c_str(), "maj");
}

static void test_mixolydian_chord_qualities() {
  auto s = apply_and_get(ProgressionMode::kMixolydian);
  // I=maj, ii=min, iii=dim, IV=maj, v=min, vi=min, VII=maj
  ASSERT_STREQ(s[0].chord_quality.c_str(), "maj");
  ASSERT_STREQ(s[1].chord_quality.c_str(), "min");
  ASSERT_STREQ(s[2].chord_quality.c_str(), "dim");
  ASSERT_STREQ(s[3].chord_quality.c_str(), "maj");
  ASSERT_STREQ(s[4].chord_quality.c_str(), "min");
  ASSERT_STREQ(s[5].chord_quality.c_str(), "min");
  ASSERT_STREQ(s[6].chord_quality.c_str(), "maj");
}

static void test_sus2_all_steps_are_sus2() {
  auto s = apply_and_get(ProgressionMode::kSus2);
  for (int i = 0; i < 7; ++i) {
    ASSERT_STREQ(s[i].chord_quality.c_str(), "sus2");
  }
}

static void test_sevenths_specific_qualities() {
  auto s = apply_and_get(ProgressionMode::kSevenths);
  // Imaj7, IImin7, IIImin7, IVmaj7, Vdom7, VImin7, VIIhalf-dim7
  ASSERT_STREQ(s[0].chord_quality.c_str(), "maj7");
  ASSERT_STREQ(s[1].chord_quality.c_str(), "min7");
  ASSERT_STREQ(s[2].chord_quality.c_str(), "min7");
  ASSERT_STREQ(s[3].chord_quality.c_str(), "maj7");
  ASSERT_STREQ(s[4].chord_quality.c_str(), "dom7");
  ASSERT_STREQ(s[5].chord_quality.c_str(), "min7");
  ASSERT_STREQ(s[6].chord_quality.c_str(), "half-dim7");
}

// ---------------------------------------------------------------------------
// Tests — scale name written to Settings
// ---------------------------------------------------------------------------

static void test_scale_string_written_for_scale_modes() {
  ASSERT_STREQ(get_scale_string(ProgressionMode::kMajor).c_str(),        "Major");
  ASSERT_STREQ(get_scale_string(ProgressionMode::kNaturalMinor).c_str(), "Natural Minor");
  ASSERT_STREQ(get_scale_string(ProgressionMode::kHarmonicMinor).c_str(),"Harmonic Minor");
  ASSERT_STREQ(get_scale_string(ProgressionMode::kDorian).c_str(),       "Dorian");
  ASSERT_STREQ(get_scale_string(ProgressionMode::kMixolydian).c_str(),   "Mixolydian");
}

static void test_genre_modes_use_major_scale_string() {
  // Genre presets use the major scale for chord-tone resolution.
  // apply_progression_mode writes "Major" for any mode not in the explicit list.
  static const ProgressionMode genre_modes[] = {
    ProgressionMode::kSalsa, ProgressionMode::kJazz, ProgressionMode::kGospel,
    ProgressionMode::kLofi,  ProgressionMode::kBlues, ProgressionMode::kFunk,
    ProgressionMode::kPop,   ProgressionMode::kTrap,  ProgressionMode::kMotown,
    ProgressionMode::kFlamenco,
  };
  for (auto mode : genre_modes) {
    ASSERT_STREQ(get_scale_string(mode).c_str(), "Major");
  }
}

// ---------------------------------------------------------------------------
// Tests — velocity propagated from Settings
// ---------------------------------------------------------------------------

static void test_velocity_propagated_to_all_steps() {
  auto s64 = apply_and_get(ProgressionMode::kMajor, 64);
  for (const auto& step : s64) {
    ASSERT_EQ(step.velocity, 64);
  }

  auto s1 = apply_and_get(ProgressionMode::kJazz, 1);
  for (const auto& step : s1) {
    ASSERT_EQ(step.velocity, 1);
  }
}

// ---------------------------------------------------------------------------
// Tests — genre preset degrees and qualities
// ---------------------------------------------------------------------------

static void test_salsa_degrees_and_qualities() {
  auto s = apply_and_get(ProgressionMode::kSalsa);
  // Keep the five original destinations, then fill the unused III and VII.
  ASSERT_EQ(s[0].scale_degree, 1); ASSERT_STREQ(s[0].chord_quality.c_str(), "maj7");
  ASSERT_EQ(s[1].scale_degree, 4); ASSERT_STREQ(s[1].chord_quality.c_str(), "dom7");
  ASSERT_EQ(s[2].scale_degree, 5); ASSERT_STREQ(s[2].chord_quality.c_str(), "dom7");
  ASSERT_EQ(s[3].scale_degree, 6); ASSERT_STREQ(s[3].chord_quality.c_str(), "min7");
  ASSERT_EQ(s[4].scale_degree, 2); ASSERT_STREQ(s[4].chord_quality.c_str(), "min7");
  ASSERT_EQ(s[5].scale_degree, 3); ASSERT_STREQ(s[5].chord_quality.c_str(), "dom7");
  ASSERT_EQ(s[6].scale_degree, 7); ASSERT_STREQ(s[6].chord_quality.c_str(), "maj7");
}

static void test_jazz_ii_v_i_progression() {
  auto s = apply_and_get(ProgressionMode::kJazz);
  // ii min7, V dom7, I maj7 (classic ii-V-I)
  ASSERT_EQ(s[0].scale_degree, 2); ASSERT_STREQ(s[0].chord_quality.c_str(), "min7");
  ASSERT_EQ(s[1].scale_degree, 5); ASSERT_STREQ(s[1].chord_quality.c_str(), "dom7");
  ASSERT_EQ(s[2].scale_degree, 1); ASSERT_STREQ(s[2].chord_quality.c_str(), "maj7");
  // turnaround: vi min7
  ASSERT_EQ(s[3].scale_degree, 6); ASSERT_STREQ(s[3].chord_quality.c_str(), "min7");
  // The repeated destinations fill unused roots so Color overrides stay unique.
  ASSERT_EQ(s[4].scale_degree, 3);
  ASSERT_EQ(s[5].scale_degree, 4);
  ASSERT_EQ(s[6].scale_degree, 7); ASSERT_STREQ(s[6].chord_quality.c_str(), "maj9");
}

static void test_blues_all_dom7() {
  auto s = apply_and_get(ProgressionMode::kBlues);
  for (const auto& step : s) {
    ASSERT_STREQ(step.chord_quality.c_str(), "dom7");
  }
}

static void test_pop_i_v_vi_iv() {
  auto s = apply_and_get(ProgressionMode::kPop);
  // I maj, V maj, VI min, IV maj — the canonical pop four-chord pattern
  ASSERT_EQ(s[0].scale_degree, 1); ASSERT_STREQ(s[0].chord_quality.c_str(), "maj");
  ASSERT_EQ(s[1].scale_degree, 5); ASSERT_STREQ(s[1].chord_quality.c_str(), "maj");
  ASSERT_EQ(s[2].scale_degree, 6); ASSERT_STREQ(s[2].chord_quality.c_str(), "min");
  ASSERT_EQ(s[3].scale_degree, 4); ASSERT_STREQ(s[3].chord_quality.c_str(), "maj");
}

// ---------------------------------------------------------------------------
// Tests — transpose field
// ---------------------------------------------------------------------------

static void test_funk_step4_has_minus1_transpose() {
  // Funk step index 3: degree 7, dom7, transpose=-1 (bVII borrowed chord)
  auto s = apply_and_get(ProgressionMode::kFunk);
  ASSERT_EQ(s[3].scale_degree, 7);
  ASSERT_STREQ(s[3].chord_quality.c_str(), "dom7");
  ASSERT_EQ(s[3].transpose, -1);
}

static void test_trap_borrowed_chords_have_minus1_transpose() {
  // The borrowed color is retained when repeated destinations are relocated.
  auto s = apply_and_get(ProgressionMode::kTrap);
  ASSERT_EQ(s[1].scale_degree, 6); ASSERT_EQ(s[1].transpose, -1);
  ASSERT_EQ(s[2].scale_degree, 7); ASSERT_EQ(s[2].transpose, -1);
  ASSERT_EQ(s[5].scale_degree, 1); ASSERT_EQ(s[5].transpose, -1);
  ASSERT_EQ(s[6].scale_degree, 2); ASSERT_EQ(s[6].transpose, -1);
  // Root-position steps have no transpose
  ASSERT_EQ(s[0].scale_degree, 1); ASSERT_EQ(s[0].transpose, 0);
}

static void test_flamenco_borrowed_chords_have_minus1_transpose() {
  // Flamenco keeps the -1 semitone borrowed color after root deduplication.
  auto s = apply_and_get(ProgressionMode::kFlamenco);
  ASSERT_EQ(s[1].scale_degree, 7); ASSERT_EQ(s[1].transpose, -1);
  ASSERT_EQ(s[2].scale_degree, 6); ASSERT_EQ(s[2].transpose, -1);
  ASSERT_EQ(s[5].scale_degree, 1); ASSERT_EQ(s[5].transpose, -1);
}

static void test_scale_modes_have_zero_transpose() {
  // Scale-based modes do not use transpose.
  for (auto mode : {ProgressionMode::kMajor, ProgressionMode::kNaturalMinor,
                    ProgressionMode::kHarmonicMinor, ProgressionMode::kDorian,
                    ProgressionMode::kMixolydian}) {
    auto s = apply_and_get(mode);
    for (const auto& step : s) {
      ASSERT_EQ(step.transpose, 0);
    }
  }
}

// ---------------------------------------------------------------------------
// Tests — degrees are sequential 1-7 for scale modes
// ---------------------------------------------------------------------------

static void test_scale_modes_degrees_are_1_to_7() {
  for (auto mode : {ProgressionMode::kMajor, ProgressionMode::kNaturalMinor,
                    ProgressionMode::kHarmonicMinor, ProgressionMode::kDorian,
                    ProgressionMode::kMixolydian, ProgressionMode::kSus2,
                    ProgressionMode::kSevenths}) {
    auto s = apply_and_get(mode);
    for (int i = 0; i < 7; ++i) {
      ASSERT_EQ(s[i].scale_degree, static_cast<uint8_t>(i + 1));
    }
  }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
  TEST_SUITE("apply_progression_mode — all 22 presets");
  RUN_TEST(test_all_modes_produce_exactly_7_steps);
  RUN_TEST(test_all_modes_have_7_distinct_sounding_roots);
  RUN_TEST(test_major_chord_qualities);
  RUN_TEST(test_natural_minor_chord_qualities);
  RUN_TEST(test_harmonic_minor_chord_qualities);
  RUN_TEST(test_dorian_chord_qualities);
  RUN_TEST(test_mixolydian_chord_qualities);
  RUN_TEST(test_sus2_all_steps_are_sus2);
  RUN_TEST(test_sevenths_specific_qualities);
  RUN_TEST(test_scale_string_written_for_scale_modes);
  RUN_TEST(test_genre_modes_use_major_scale_string);
  RUN_TEST(test_velocity_propagated_to_all_steps);
  RUN_TEST(test_salsa_degrees_and_qualities);
  RUN_TEST(test_jazz_ii_v_i_progression);
  RUN_TEST(test_blues_all_dom7);
  RUN_TEST(test_pop_i_v_vi_iv);
  RUN_TEST(test_funk_step4_has_minus1_transpose);
  RUN_TEST(test_trap_borrowed_chords_have_minus1_transpose);
  RUN_TEST(test_flamenco_borrowed_chords_have_minus1_transpose);
  RUN_TEST(test_scale_modes_have_zero_transpose);
  RUN_TEST(test_scale_modes_degrees_are_1_to_7);

  test_print_result();
  return (g_fail > 0) ? 1 : 0;
}
