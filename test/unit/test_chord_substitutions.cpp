// Tests: engine::compute_substitution() — all 8 joystick-direction substitutions.
//
// compute_substitution() is wired for Discover mode and has 0% test coverage.
// It reads the current chord degree from engine.progression() and applies one
// of 8 transforms based on joystick direction:
//   Dir 0 (E):  Tritone sub      — root + 6, dom7
//   Dir 1 (NE): Secondary dom    — root + 7, dom7
//   Dir 2 (N):  Relative         — major→relative minor (+9), minor→relative major (+3)
//   Dir 3 (NW): Borrowed         — same root, swap major↔minor
//   Dir 4 (W):  Dim passing      — root + 1, dim7
//   Dir 5 (SW): Augmented        — same root, aug
//   Dir 6 (S):  Pedal cycle      — maj→dom7→maj7→min7 (cycles)
//   Dir 7 (SE): Chromatic mediant— root + 4, same quality
//
// All tests use key=C, scale=Major, kMajor progression.
// Degree 5 (G major, root_pc=7) is used for the directional tests so every
// direction produces a distinct, checkable result.

#include "../framework.h"
#include "engine/chord_substitutions.h"
#include "engine/engine.h"
#include "app/progression_modes.h"
#include "app/state.h"

using engine::SubstitutionResult;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void setup_engine(engine::Engine& eng, app::Settings& s) {
  eng.begin();
  std::snprintf(s.key,   sizeof(s.key),   "C");
  std::snprintf(s.scale, sizeof(s.scale), "Major");
  s.velocity = 100;
  app::apply_progression_mode(eng, app::ProgressionMode::kMajor, s);
}

static SubstitutionResult sub(engine::Engine& eng, const app::Settings& s,
                               uint8_t degree, uint8_t direction) {
  return engine::compute_substitution(s, eng, degree, direction);
}

// ---------------------------------------------------------------------------
// Direction tests (degree 5 = G major, root_pc = 7 in C major)
// ---------------------------------------------------------------------------

static void test_sub_tritone_e() {
  // Dir 0: root + 6 = (7+6)%12 = 1 (C#), quality = dom7
  engine::Engine eng; app::Settings s{};
  setup_engine(eng, s);
  auto r = sub(eng, s, 5, 0);
  ASSERT_STREQ(r.root_name.c_str(), "C#");
  ASSERT_STREQ(r.quality.c_str(), "dom7");
}

static void test_sub_secondary_dominant_ne() {
  // Dir 1: root + 7 = (7+7)%12 = 2 (D), quality = dom7  (V of V)
  engine::Engine eng; app::Settings s{};
  setup_engine(eng, s);
  auto r = sub(eng, s, 5, 1);
  ASSERT_STREQ(r.root_name.c_str(), "D");
  ASSERT_STREQ(r.quality.c_str(), "dom7");
}

static void test_sub_relative_major_to_minor_n() {
  // Dir 2 on a major chord: root + 9 = (7+9)%12 = 4 (E), quality = min
  // G major → E minor (relative minor)
  engine::Engine eng; app::Settings s{};
  setup_engine(eng, s);
  auto r = sub(eng, s, 5, 2);
  ASSERT_STREQ(r.root_name.c_str(), "E");
  ASSERT_STREQ(r.quality.c_str(), "min");
}

static void test_sub_relative_minor_to_major_n() {
  // Dir 2 on a minor chord (degree 6 = A minor, root_pc = 9):
  // root + 3 = (9+3)%12 = 0 (C), quality = maj  (relative major)
  engine::Engine eng; app::Settings s{};
  setup_engine(eng, s);
  auto r = sub(eng, s, 6, 2);
  ASSERT_STREQ(r.root_name.c_str(), "C");
  ASSERT_STREQ(r.quality.c_str(), "maj");
}

static void test_sub_borrowed_major_nw() {
  // Dir 3 on major chord (deg 5, G major): root stays G (key_str=G),
  // quality flips to min (borrowed parallel minor)
  engine::Engine eng; app::Settings s{};
  setup_engine(eng, s);
  auto r = sub(eng, s, 5, 3);
  ASSERT_STREQ(r.root_name.c_str(), "G");
  ASSERT_STREQ(r.quality.c_str(), "min");
}

static void test_sub_borrowed_minor_nw() {
  // Dir 3 on minor chord (deg 2, D minor, root_pc=2): quality flips to maj
  engine::Engine eng; app::Settings s{};
  setup_engine(eng, s);
  auto r = sub(eng, s, 2, 3);
  ASSERT_STREQ(r.root_name.c_str(), "D");
  ASSERT_STREQ(r.quality.c_str(), "maj");
}

static void test_sub_dim_passing_w() {
  // Dir 4: root + 1 = (7+1)%12 = 8 (G#), quality = dim7
  engine::Engine eng; app::Settings s{};
  setup_engine(eng, s);
  auto r = sub(eng, s, 5, 4);
  ASSERT_STREQ(r.root_name.c_str(), "G#");
  ASSERT_STREQ(r.quality.c_str(), "dim7");
}

static void test_sub_augmented_sw() {
  // Dir 5: root stays G, quality = aug
  engine::Engine eng; app::Settings s{};
  setup_engine(eng, s);
  auto r = sub(eng, s, 5, 5);
  ASSERT_STREQ(r.root_name.c_str(), "G");
  ASSERT_STREQ(r.quality.c_str(), "aug");
}

static void test_sub_pedal_cycles_maj_to_dom7_s() {
  // Dir 6: kPedalCycle = {maj, dom7, maj7, min7}.
  // Degree 5 chord quality = "maj" → next = "dom7".
  engine::Engine eng; app::Settings s{};
  setup_engine(eng, s);
  auto r = sub(eng, s, 5, 6);
  ASSERT_STREQ(r.root_name.c_str(), "G");
  ASSERT_STREQ(r.quality.c_str(), "dom7");
}

static void test_sub_chromatic_mediant_se() {
  // Dir 7: root + 4 = (7+4)%12 = 11 (B), same quality as original (maj)
  engine::Engine eng; app::Settings s{};
  setup_engine(eng, s);
  auto r = sub(eng, s, 5, 7);
  ASSERT_STREQ(r.root_name.c_str(), "B");
  ASSERT_STREQ(r.quality.c_str(), "maj");
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

static void test_sub_invalid_degree_0_returns_default() {
  engine::Engine eng; app::Settings s{};
  setup_engine(eng, s);
  auto r = sub(eng, s, 0, 0);
  // Must not crash; returns a valid (if arbitrary) default
  ASSERT_FALSE(r.root_name.empty());
  ASSERT_FALSE(r.quality.empty());
}

static void test_sub_notes_are_populated() {
  // For every direction on degree 1 (C major), result.notes must not be empty.
  engine::Engine eng; app::Settings s{};
  setup_engine(eng, s);
  for (uint8_t dir = 0; dir < 8; ++dir) {
    auto r = sub(eng, s, 1, dir);
    ASSERT_TRUE(!r.notes.empty());
  }
}

static void test_sub_degree_1_tritone_is_f_sharp() {
  // C major (root=0), dir 0: root + 6 = 6 (F#), dom7
  engine::Engine eng; app::Settings s{};
  setup_engine(eng, s);
  auto r = sub(eng, s, 1, 0);
  ASSERT_STREQ(r.root_name.c_str(), "F#");
  ASSERT_STREQ(r.quality.c_str(), "dom7");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
  TEST_SUITE("compute_substitution — all 8 directions");
  RUN_TEST(test_sub_tritone_e);
  RUN_TEST(test_sub_secondary_dominant_ne);
  RUN_TEST(test_sub_relative_major_to_minor_n);
  RUN_TEST(test_sub_relative_minor_to_major_n);
  RUN_TEST(test_sub_borrowed_major_nw);
  RUN_TEST(test_sub_borrowed_minor_nw);
  RUN_TEST(test_sub_dim_passing_w);
  RUN_TEST(test_sub_augmented_sw);
  RUN_TEST(test_sub_pedal_cycles_maj_to_dom7_s);
  RUN_TEST(test_sub_chromatic_mediant_se);
  RUN_TEST(test_sub_invalid_degree_0_returns_default);
  RUN_TEST(test_sub_notes_are_populated);
  RUN_TEST(test_sub_degree_1_tritone_is_f_sharp);

  test_print_result();
  return (g_fail > 0) ? 1 : 0;
}
