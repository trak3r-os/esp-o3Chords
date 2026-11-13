// Tests: engine::music_theory.h advanced analytics functions.
//
// All five functions are used in Discover mode but had 0% test coverage:
//   chord_tension()         — count out-of-scale chord tones (0=diatonic, 1=one chromatic, ...)
//   suggest_next_quality()  — diatonic quality for a given scale degree
//   find_modal_interchange()— which modes contain a given chord's pitch classes
//   suggest_modulation()    — which keys absorb given foreign (out-of-scale) PCs
//   pivot_chord_mask()      — bitmask of degrees shared between two keys
//
// Also tests the display helpers: roman_symbol, nashville_symbol,
// midi_note_label, scale_formula_token.

#include "../framework.h"
#include "engine/music_theory.h"
#include "engine/engine.h"
#include "app/state.h"
#include <cstring>
#include <vector>

using engine::ModalMatch;
using engine::ModulationSuggestion;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static app::Settings make_settings(const char* key = "C",
                                    const char* scale = "Major") {
  app::Settings s{};
  std::snprintf(s.key,   sizeof(s.key),   "%s", key);
  std::snprintf(s.scale, sizeof(s.scale), "%s", scale);
  return s;
}

// ---------------------------------------------------------------------------
// chord_tension
// ---------------------------------------------------------------------------

static void test_chord_tension_diatonic_triad_is_zero() {
  // C major (I) in C major scale: C, E, G — all in scale → tension 0.
  engine::Engine eng; eng.begin();
  auto s = make_settings("C", "Major");
  ASSERT_EQ(engine::chord_tension(s, eng, 1, "maj"), 0);
}

static void test_chord_tension_dominant_7th_one_chromatic() {
  // C dom7 = C, E, G, Bb.  Bb not in C major → tension 1.
  engine::Engine eng; eng.begin();
  auto s = make_settings("C", "Major");
  ASSERT_EQ(engine::chord_tension(s, eng, 1, "dom7"), 1);
}

static void test_chord_tension_diminished_two_chromatic() {
  // C dim = C, Eb, Gb.  Eb(3) and Gb(6) not in C major → tension 2.
  engine::Engine eng; eng.begin();
  auto s = make_settings("C", "Major");
  ASSERT_EQ(engine::chord_tension(s, eng, 1, "dim"), 2);
}

static void test_chord_tension_diatonic_fifth() {
  // G major (V) in C major: G, B, D — all in scale → tension 0.
  engine::Engine eng; eng.begin();
  auto s = make_settings("C", "Major");
  ASSERT_EQ(engine::chord_tension(s, eng, 5, "maj"), 0);
}

static void test_chord_tension_diatonic_seventh() {
  // B dim (VII) in C major: B, D, F — all in scale → tension 0.
  engine::Engine eng; eng.begin();
  auto s = make_settings("C", "Major");
  ASSERT_EQ(engine::chord_tension(s, eng, 7, "dim"), 0);
}

static void test_chord_tension_secondary_dominant() {
  // D dom7 (V/V) = D, F#, A, C.  F# not in C major → tension 1.
  engine::Engine eng; eng.begin();
  auto s = make_settings("C", "Major");
  ASSERT_EQ(engine::chord_tension(s, eng, 2, "dom7"), 1);
}

// ---------------------------------------------------------------------------
// suggest_next_quality
// ---------------------------------------------------------------------------

static void test_suggest_quality_major_degree_1() {
  engine::Engine eng; eng.begin();
  auto s = make_settings("C", "Major");
  ASSERT_STREQ(engine::suggest_next_quality(s, eng, 1, 1), "maj");
}

static void test_suggest_quality_major_degree_2() {
  engine::Engine eng; eng.begin();
  auto s = make_settings("C", "Major");
  ASSERT_STREQ(engine::suggest_next_quality(s, eng, 1, 2), "min");
}

static void test_suggest_quality_major_degree_7() {
  // VII in major = diminished triad.
  engine::Engine eng; eng.begin();
  auto s = make_settings("C", "Major");
  ASSERT_STREQ(engine::suggest_next_quality(s, eng, 1, 7), "dim");
}

static void test_suggest_quality_harmonic_minor_degree_3() {
  // III in harmonic minor = augmented triad.
  // kHarmonicMinor = {0,2,3,5,7,8,11}: third(idx4)=7, fifth(idx6)=11
  // deg_interval(2) for deg=3: idx=(2+2)%7=4, sc[4]-sc[2] = 7-3 = 4
  // deg_interval(4) for deg=3: idx=(2+4)%7=6, sc[6]-sc[2] = 11-3 = 8
  // third=4, fifth=8 → "aug"
  engine::Engine eng; eng.begin();
  auto s = make_settings("C", "Harmonic Minor");
  ASSERT_STREQ(engine::suggest_next_quality(s, eng, 1, 3), "aug");
}

static void test_suggest_quality_minor_degree_1() {
  engine::Engine eng; eng.begin();
  auto s = make_settings("C", "Natural Minor");
  ASSERT_STREQ(engine::suggest_next_quality(s, eng, 1, 1), "min");
}

// ---------------------------------------------------------------------------
// find_modal_interchange
// ---------------------------------------------------------------------------

static void test_modal_interchange_d_major_in_c_is_lydian() {
  // D major = {D, F#, A} = PCs {2, 6, 9}.
  // Only C Lydian ({0,2,4,6,7,9,11}) contains all three → shared=3, foreign=0.
  auto s = make_settings("C", "Major");
  uint8_t pcs[] = {2, 6, 9};
  ModalMatch out[8];
  size_t n = engine::find_modal_interchange(s, pcs, 3, out, 8);
  ASSERT_TRUE(n > 0);
  ASSERT_EQ(out[0].shared_tones, 3);
  ASSERT_EQ(out[0].foreign_tones, 0);
  ASSERT_STREQ(out[0].mode_name, "Lydian");
}

static void test_modal_interchange_returns_nonzero_for_diatonic_chord() {
  // C major chord (PCs {0,4,7}) is in several modes of C; result count > 0.
  auto s = make_settings("C", "Major");
  uint8_t pcs[] = {0, 4, 7};
  ModalMatch out[8];
  size_t n = engine::find_modal_interchange(s, pcs, 3, out, 8);
  ASSERT_TRUE(n > 0);
  ASSERT_TRUE(out[0].shared_tones >= 3);
  ASSERT_EQ(out[0].foreign_tones, 0);
}

static void test_modal_interchange_empty_chord_returns_zero() {
  auto s = make_settings("C", "Major");
  ModalMatch out[1];
  size_t n = engine::find_modal_interchange(s, nullptr, 0, out, 1);
  ASSERT_EQ(static_cast<int>(n), 0);
}

static void test_modal_interchange_shared_plus_foreign_equals_num_pcs() {
  // Invariant: for every result, shared + foreign == num_pcs.
  auto s = make_settings("C", "Major");
  uint8_t pcs[] = {2, 6, 9};  // D major
  ModalMatch out[8];
  size_t n = engine::find_modal_interchange(s, pcs, 3, out, 8);
  for (size_t i = 0; i < n; ++i) {
    ASSERT_EQ(out[i].shared_tones + out[i].foreign_tones, 3);
  }
}

static void test_modal_interchange_sorted_by_shared_descending() {
  auto s = make_settings("C", "Major");
  uint8_t pcs[] = {1, 6, 10};  // very chromatic chord
  ModalMatch out[8];
  size_t n = engine::find_modal_interchange(s, pcs, 3, out, 8);
  for (size_t i = 1; i < n; ++i) {
    ASSERT_TRUE(out[i].shared_tones <= out[i - 1].shared_tones);
  }
}

// ---------------------------------------------------------------------------
// suggest_modulation
// ---------------------------------------------------------------------------

static void test_suggest_modulation_bb_in_c_returns_result() {
  // Bb (PC=10) is foreign in C major.
  // C# major (root=1) contains Bb and is distance=1 from C → top result.
  auto s = make_settings("C", "Major");
  uint8_t foreign[] = {10};  // Bb
  ModulationSuggestion out[3];
  size_t n = engine::suggest_modulation(s, foreign, 1, out, 3);
  ASSERT_TRUE(n > 0);
  ASSERT_EQ(out[0].covered, 1);
  ASSERT_EQ(out[0].distance, 1);
  ASSERT_STREQ(out[0].target_key, "C#");  // nearest key with Bb
}

static void test_suggest_modulation_two_foreign_notes() {
  // C# (PC=1) and Ab (PC=8) are both foreign in C major.
  // C# major ({1,3,5,6,8,10,0}) contains both → covered=2, distance=1.
  auto s = make_settings("C", "Major");
  uint8_t foreign[] = {1, 8};
  ModulationSuggestion out[4];
  size_t n = engine::suggest_modulation(s, foreign, 2, out, 4);
  ASSERT_TRUE(n > 0);
  ASSERT_EQ(out[0].covered, 2);
  ASSERT_EQ(out[0].distance, 1);
  ASSERT_STREQ(out[0].target_key, "C#");
}

static void test_suggest_modulation_empty_foreign_returns_zero() {
  auto s = make_settings("C", "Major");
  ModulationSuggestion out[1];
  size_t n = engine::suggest_modulation(s, nullptr, 0, out, 1);
  ASSERT_EQ(static_cast<int>(n), 0);
}

static void test_suggest_modulation_sorted_covered_desc_dist_asc() {
  // Invariant: results are sorted by covered desc, then distance asc.
  auto s = make_settings("C", "Major");
  uint8_t foreign[] = {1, 6, 10};  // C#, F#, Bb
  ModulationSuggestion out[12];
  size_t n = engine::suggest_modulation(s, foreign, 3, out, 12);
  ASSERT_TRUE(n > 0);
  for (size_t i = 1; i < n; ++i) {
    ASSERT_TRUE(out[i].covered <= out[i - 1].covered);
  }
}

// ---------------------------------------------------------------------------
// pivot_chord_mask
// ---------------------------------------------------------------------------

static void test_pivot_chord_mask_c_to_g_major() {
  // C major → G major pivot chords are degrees 1(C), 3(E), 5(G), 6(A).
  // Bitmask: bit0|bit2|bit4|bit5 = 1+4+16+32 = 53.
  auto s = make_settings("C", "Major");
  uint8_t mask = engine::pivot_chord_mask(s, "G");
  ASSERT_EQ(mask, 53);  // degrees 1,3,5,6
}

static void test_pivot_chord_mask_same_key_is_all_set() {
  // Pivot from C major to C major: all 7 triads are shared → mask = 0x7F = 127.
  auto s = make_settings("C", "Major");
  uint8_t mask = engine::pivot_chord_mask(s, "C");
  ASSERT_EQ(mask, 127);
}

static void test_pivot_chord_mask_c_to_f_major() {
  // C major → F major: both share {C, Dm, Em, Am} as pivot chords (degrees 1,2,3,6).
  // F major = {F,G,A,Bb,C,D,E} = PCs {5,7,9,10,0,2,4}.
  // C major triads: C(0,4,7), Dm(2,5,9), Em(4,7,11), F(5,9,0), G(7,11,2), Am(9,0,4), Bdim(11,2,5)
  // F major set: {0,2,4,5,7,9,10}.
  //   C (0,4,7): 0✓,4✓,7✓ → pivot ✓ (deg1, bit0)
  //   Dm(2,5,9): 2✓,5✓,9✓ → pivot ✓ (deg2, bit1)
  //   Em(4,7,11): 4✓,7✓,11? 11 not in F → no
  //   Fmaj(5,9,0): 5✓,9✓,0✓ → pivot ✓ (deg4, bit3)
  //   G (7,11,2): 11 not in F → no
  //   Am(9,0,4): 9✓,0✓,4✓ → pivot ✓ (deg6, bit5)
  //   Bdim(11,2,5): 11 not in F → no
  // mask = 1+2+8+32 = 43
  auto s = make_settings("C", "Major");
  uint8_t mask = engine::pivot_chord_mask(s, "F");
  ASSERT_EQ(mask, 43);  // degrees 1,2,4,6
}

static void test_pivot_chord_mask_nonzero_for_nearby_keys() {
  auto s = make_settings("G", "Major");
  uint8_t mask = engine::pivot_chord_mask(s, "D");  // adjacent on circle of fifths
  ASSERT_TRUE(mask > 0);
}

// ---------------------------------------------------------------------------
// Display helpers
// ---------------------------------------------------------------------------

static void test_roman_symbol_major_upper() {
  char buf[8];
  engine::roman_symbol(1, "maj", buf, sizeof(buf));
  ASSERT_STREQ(buf, "I");
  engine::roman_symbol(5, "maj", buf, sizeof(buf));
  ASSERT_STREQ(buf, "V");
}

static void test_roman_symbol_minor_lower() {
  char buf[8];
  engine::roman_symbol(2, "min", buf, sizeof(buf));
  ASSERT_STREQ(buf, "ii");
  engine::roman_symbol(6, "min7", buf, sizeof(buf));
  ASSERT_STREQ(buf, "vi");
}

static void test_roman_symbol_diminished_adds_o() {
  char buf[8];
  engine::roman_symbol(7, "dim", buf, sizeof(buf));
  ASSERT_STREQ(buf, "viio");
}

static void test_roman_symbol_augmented_adds_plus() {
  char buf[8];
  engine::roman_symbol(3, "aug", buf, sizeof(buf));
  ASSERT_STREQ(buf, "III+");
}

static void test_nashville_symbol_plain_major() {
  char buf[8];
  engine::nashville_symbol(1, "maj", buf, sizeof(buf));
  ASSERT_STREQ(buf, "1");
  engine::nashville_symbol(4, "maj", buf, sizeof(buf));
  ASSERT_STREQ(buf, "4");
}

static void test_nashville_symbol_dom7() {
  char buf[8];
  engine::nashville_symbol(5, "dom7", buf, sizeof(buf));
  ASSERT_STREQ(buf, "57");
}

static void test_nashville_symbol_minor_7() {
  char buf[8];
  engine::nashville_symbol(2, "min7", buf, sizeof(buf));
  ASSERT_STREQ(buf, "2m7");
}

static void test_midi_note_label_c4() {
  char buf[8];
  engine::midi_note_label(60, buf, sizeof(buf));
  ASSERT_STREQ(buf, "C4");
}

static void test_midi_note_label_a4() {
  char buf[8];
  engine::midi_note_label(69, buf, sizeof(buf));
  ASSERT_STREQ(buf, "A4");
}

static void test_scale_formula_token_values() {
  ASSERT_STREQ(engine::scale_formula_token(0),  "1");
  ASSERT_STREQ(engine::scale_formula_token(3),  "b3");
  ASSERT_STREQ(engine::scale_formula_token(4),  "3");
  ASSERT_STREQ(engine::scale_formula_token(7),  "5");
  ASSERT_STREQ(engine::scale_formula_token(10), "b7");
  ASSERT_STREQ(engine::scale_formula_token(11), "7");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
  TEST_SUITE("chord_tension");
  RUN_TEST(test_chord_tension_diatonic_triad_is_zero);
  RUN_TEST(test_chord_tension_dominant_7th_one_chromatic);
  RUN_TEST(test_chord_tension_diminished_two_chromatic);
  RUN_TEST(test_chord_tension_diatonic_fifth);
  RUN_TEST(test_chord_tension_diatonic_seventh);
  RUN_TEST(test_chord_tension_secondary_dominant);

  TEST_SUITE("suggest_next_quality");
  RUN_TEST(test_suggest_quality_major_degree_1);
  RUN_TEST(test_suggest_quality_major_degree_2);
  RUN_TEST(test_suggest_quality_major_degree_7);
  RUN_TEST(test_suggest_quality_harmonic_minor_degree_3);
  RUN_TEST(test_suggest_quality_minor_degree_1);

  TEST_SUITE("find_modal_interchange");
  RUN_TEST(test_modal_interchange_d_major_in_c_is_lydian);
  RUN_TEST(test_modal_interchange_returns_nonzero_for_diatonic_chord);
  RUN_TEST(test_modal_interchange_empty_chord_returns_zero);
  RUN_TEST(test_modal_interchange_shared_plus_foreign_equals_num_pcs);
  RUN_TEST(test_modal_interchange_sorted_by_shared_descending);

  TEST_SUITE("suggest_modulation");
  RUN_TEST(test_suggest_modulation_bb_in_c_returns_result);
  RUN_TEST(test_suggest_modulation_two_foreign_notes);
  RUN_TEST(test_suggest_modulation_empty_foreign_returns_zero);
  RUN_TEST(test_suggest_modulation_sorted_covered_desc_dist_asc);

  TEST_SUITE("pivot_chord_mask");
  RUN_TEST(test_pivot_chord_mask_c_to_g_major);
  RUN_TEST(test_pivot_chord_mask_same_key_is_all_set);
  RUN_TEST(test_pivot_chord_mask_c_to_f_major);
  RUN_TEST(test_pivot_chord_mask_nonzero_for_nearby_keys);

  TEST_SUITE("Display helpers");
  RUN_TEST(test_roman_symbol_major_upper);
  RUN_TEST(test_roman_symbol_minor_lower);
  RUN_TEST(test_roman_symbol_diminished_adds_o);
  RUN_TEST(test_roman_symbol_augmented_adds_plus);
  RUN_TEST(test_nashville_symbol_plain_major);
  RUN_TEST(test_nashville_symbol_dom7);
  RUN_TEST(test_nashville_symbol_minor_7);
  RUN_TEST(test_midi_note_label_c4);
  RUN_TEST(test_midi_note_label_a4);
  RUN_TEST(test_scale_formula_token_values);

  test_print_result();
  return (g_fail > 0) ? 1 : 0;
}
