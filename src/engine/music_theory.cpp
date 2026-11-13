#include "engine/music_theory.h"
#include "app/state.h"
#include "engine/engine.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace engine {

int key_offset_from_str(const char* k) {
  if (!k || !k[0]) return 0;
  struct Entry { const char* name; int off; };
  static constexpr Entry kMap[] = {
      {"C", 0},  {"C#", 1}, {"Db", 1}, {"D", 2},  {"D#", 3}, {"Eb", 3},
      {"E", 4},  {"F", 5},  {"F#", 6}, {"Gb", 6}, {"G", 7},  {"G#", 8},
      {"Ab", 8}, {"A", 9},  {"A#", 10}, {"Bb", 10}, {"B", 11},
  };
  for (const auto& e : kMap) {
    if (std::strcmp(k, e.name) == 0) return e.off;
  }
  return 0;
}

const char* key_str_from_offset(int off) {
  static constexpr const char* kSharps[12] = {
      "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
  };
  off %= 12;
  if (off < 0) off += 12;
  return kSharps[off];
}

void scale_steps_for_name(const char* scale_name, const int8_t** out_steps, size_t* out_len) {
  static constexpr int8_t kMajor[] = {0, 2, 4, 5, 7, 9, 11};
  static constexpr int8_t kNaturalMinor[] = {0, 2, 3, 5, 7, 8, 10};
  static constexpr int8_t kHarmonicMinor[] = {0, 2, 3, 5, 7, 8, 11};
  static constexpr int8_t kDorian[] = {0, 2, 3, 5, 7, 9, 10};
  static constexpr int8_t kMixolydian[] = {0, 2, 4, 5, 7, 9, 10};
  static constexpr int8_t kSus2[] = {0, 2, 4, 5, 7, 9, 11};
  static constexpr int8_t kSevenths[] = {0, 2, 4, 5, 7, 9, 11};

  if (!scale_name) scale_name = "Major";

  if (std::strcmp(scale_name, "Natural Minor") == 0) {
    *out_steps = kNaturalMinor; *out_len = 7; return;
  }
  if (std::strcmp(scale_name, "Harmonic Minor") == 0) {
    *out_steps = kHarmonicMinor; *out_len = 7; return;
  }
  if (std::strcmp(scale_name, "Dorian") == 0) {
    *out_steps = kDorian; *out_len = 7; return;
  }
  if (std::strcmp(scale_name, "Mixolydian") == 0) {
    *out_steps = kMixolydian; *out_len = 7; return;
  }
  if (std::strcmp(scale_name, "sus2") == 0 || std::strcmp(scale_name, "Sus2") == 0) {
    *out_steps = kSus2; *out_len = 7; return;
  }
  if (std::strcmp(scale_name, "Sevenths") == 0) {
    *out_steps = kSevenths; *out_len = 7; return;
  }

  *out_steps = kMajor;
  *out_len = 7;
}

int scale_degree_offset(const char* scale_name, uint8_t degree) {
  const int8_t* steps = nullptr;
  size_t nsteps = 0;
  scale_steps_for_name(scale_name, &steps, &nsteps);
  if (!steps || nsteps == 0) return 0;
  if (degree == 0) degree = 1;
  const size_t idx = static_cast<size_t>(degree - 1) % nsteps;
  return steps[idx];
}

int progression_step_root_offset(const app::Settings& s, const ProgressionStep& step) {
  const int key_off = key_offset_from_str(s.key);
  const int degree_off = scale_degree_offset(s.scale, step.scale_degree);
  int root_off = (key_off + degree_off + step.transpose) % 12;
  if (root_off < 0) root_off += 12;
  return root_off;
}

int progression_step_root_midi(const app::Settings& s,
                               const ProgressionStep& step,
                               int base_octave) {
  const int key_off = key_offset_from_str(s.key);
  const int degree_off = scale_degree_offset(s.scale, step.scale_degree);
  const int base_note = 12 * (base_octave + 1);
  return base_note + key_off + degree_off + step.transpose + (step.octave * 12);
}

const Chord* chord_for_quality(Engine& engine, const std::string& quality) {
  const auto& chords = engine.chord_library().chords();
  for (const auto& ch : chords) {
    if (ch.name == quality) return &ch;
  }
  return nullptr;
}

uint8_t clamp_midi_note(int note) {
  if (note < 0) return 0;
  if (note > 127) return 127;
  return static_cast<uint8_t>(note);
}

std::vector<uint8_t> progression_step_chord_notes(const app::Settings& s,
                                                   Engine& engine,
                                                   const ProgressionStep& step,
                                                   int base_octave) {
  std::vector<uint8_t> notes;
  const int root = progression_step_root_midi(s, step, base_octave);
  const Chord* chord = chord_for_quality(engine, step.chord_quality);
  if (!chord || chord->intervals.empty()) {
    notes.push_back(clamp_midi_note(root));
    return notes;
  }

  notes.reserve(chord->intervals.size());
  for (int8_t iv : chord->intervals) {
    notes.push_back(clamp_midi_note(root + iv));
  }

  apply_chord_inversion(notes, step.inversion);
  return notes;
}

std::vector<uint8_t> scale_notes_midi(const app::Settings& s, int base_octave) {
  std::vector<uint8_t> notes;
  const int8_t* steps = nullptr;
  size_t nsteps = 0;
  scale_steps_for_name(s.scale, &steps, &nsteps);
  if (!steps || nsteps == 0) return notes;

  const int key_off = key_offset_from_str(s.key);
  const int base_note = 12 * (base_octave + 1);
  notes.reserve(nsteps + 1);
  for (size_t i = 0; i < nsteps; ++i) {
    notes.push_back(clamp_midi_note(base_note + key_off + steps[i]));
  }
  notes.push_back(clamp_midi_note(base_note + key_off + 12));
  return notes;
}

bool quality_is_minor(const std::string& q) {
  return q.rfind("min", 0) == 0;
}

bool quality_is_dim(const std::string& q) {
  return (q.find("dim") != std::string::npos);
}

bool quality_is_aug(const std::string& q) {
  return q.rfind("aug", 0) == 0;
}

const char* quality_short(const std::string& q) {
  if (q == "half-dim7") return "hdim7";
  return q.c_str();
}

const char* roman_degree(uint8_t degree) {
  static const char* kRomans[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
  if (degree >= 1 && degree <= 7) return kRomans[degree - 1];
  return "?";
}

void roman_symbol(uint8_t degree, const std::string& quality, char* out, size_t out_sz) {
  if (!out || out_sz == 0) return;
  const char* base = roman_degree(degree);
  std::snprintf(out, out_sz, "%s", base);

  const bool lower = quality_is_minor(quality) || quality_is_dim(quality);
  if (lower) {
    for (size_t i = 0; out[i] != '\0'; ++i) {
      if (out[i] >= 'A' && out[i] <= 'Z') out[i] = static_cast<char>(out[i] - 'A' + 'a');
    }
  }

  if (quality_is_dim(quality)) {
    const size_t n = std::strlen(out);
    if (n + 1 < out_sz) { out[n] = 'o'; out[n + 1] = '\0'; }
  } else if (quality_is_aug(quality)) {
    const size_t n = std::strlen(out);
    if (n + 1 < out_sz) { out[n] = '+'; out[n + 1] = '\0'; }
  }
}

void nashville_symbol(uint8_t degree, const std::string& quality, char* out, size_t out_sz) {
  if (!out || out_sz == 0) return;
  // Nashville: number + quality suffix
  const char* suffix = "";
  if (quality_is_dim(quality))       suffix = "dim";
  else if (quality_is_aug(quality))  suffix = "+";
  else if (quality_is_minor(quality)) {
    if (quality.find("7") != std::string::npos)  suffix = "m7";
    else if (quality.find("9") != std::string::npos)  suffix = "m9";
    else if (quality.find("11") != std::string::npos) suffix = "m11";
    else if (quality.find("6") != std::string::npos)  suffix = "m6";
    else suffix = "m";
  } else {
    // Major-type qualities
    if (quality == "dom7")       suffix = "7";
    else if (quality == "maj7")  suffix = "M7";
    else if (quality == "dom9")  suffix = "9";
    else if (quality == "maj9")  suffix = "M9";
    else if (quality == "dom13") suffix = "13";
    else if (quality == "maj13") suffix = "M13";
    else if (quality == "add9")  suffix = "add9";
    else if (quality == "sus2")  suffix = "sus2";
    else if (quality == "sus4")  suffix = "sus4";
    else if (quality == "maj6")  suffix = "6";
    else if (quality == "half-dim7") suffix = "hdim7";
    // "maj" → no suffix (just the number)
  }
  std::snprintf(out, out_sz, "%u%s", (unsigned)degree, suffix);
}

void midi_note_label(uint8_t note, char* out, size_t out_len) {
  if (!out || out_len == 0) return;
  const char* key = key_str_from_offset(note % 12);
  const int octave = static_cast<int>(note) / 12 - 1;
  std::snprintf(out, out_len, "%s%d", key, octave);
}

const char* scale_formula_token(int semitone) {
  switch ((semitone % 12 + 12) % 12) {
    case 0: return "1";   case 1: return "b2";  case 2: return "2";
    case 3: return "b3";  case 4: return "3";   case 5: return "4";
    case 6: return "b5";  case 7: return "5";   case 8: return "b6";
    case 9: return "6";   case 10: return "b7"; case 11: return "7";
    default: return "?";
  }
}

uint8_t chord_tension(const app::Settings& s, Engine& engine,
                      uint8_t scale_degree, const std::string& quality) {
  // Build the set of scale pitch classes.
  const int8_t* sc_steps = nullptr;
  size_t sc_len = 0;
  scale_steps_for_name(s.scale, &sc_steps, &sc_len);
  if (!sc_steps || sc_len == 0) return 0;

  const int key_off = key_offset_from_str(s.key);
  bool in_scale[12] = {};
  for (size_t i = 0; i < sc_len; ++i)
    in_scale[(key_off + sc_steps[i]) % 12] = true;

  // Get root pitch class for this degree.
  const int deg_off = scale_degree_offset(s.scale, scale_degree);
  const int root_pc = (key_off + deg_off) % 12;

  // Look up chord intervals.
  const Chord* ch = chord_for_quality(engine, quality);
  if (!ch) return 0;

  uint8_t out_count = 0;
  for (int8_t iv : ch->intervals) {
    int pc = ((root_pc + iv) % 12 + 12) % 12;
    if (!in_scale[pc]) ++out_count;
  }
  return out_count;
}

const char* suggest_next_quality(const app::Settings& s, Engine& engine,
                                 uint8_t current_degree, uint8_t target_degree) {
  (void)current_degree;

  // Return the natural diatonic quality for the target degree.
  // Build a triad on the target degree using scale intervals and match
  // it against the chord library.
  const int8_t* sc_steps = nullptr;
  size_t sc_len = 0;
  scale_steps_for_name(s.scale, &sc_steps, &sc_len);
  if (!sc_steps || sc_len == 0) return "maj";

  // Intervals of the triad built on target_degree (1-based).
  auto deg_interval = [&](uint8_t deg_offset) -> int {
    size_t idx = static_cast<size_t>((target_degree - 1 + deg_offset) % sc_len);
    size_t base_idx = static_cast<size_t>((target_degree - 1) % sc_len);
    int iv = sc_steps[idx] - sc_steps[base_idx];
    if (iv < 0) iv += 12;
    return iv;
  };

  int third = deg_interval(2);  // 3rd above root
  int fifth = deg_interval(4);  // 5th above root

  // Match common triad shapes.
  if (third == 4 && fifth == 7) return "maj";
  if (third == 3 && fifth == 7) return "min";
  if (third == 3 && fifth == 6) return "dim";
  if (third == 4 && fifth == 8) return "aug";
  if (third == 2 && fifth == 7) return "sus2";
  if (third == 5 && fifth == 7) return "sus4";

  // Fallback: check 7th for richer suggestion.
  int seventh = deg_interval(6);
  if (third == 4 && fifth == 7 && seventh == 11) return "maj7";
  if (third == 3 && fifth == 7 && seventh == 10) return "min7";
  if (third == 4 && fifth == 7 && seventh == 10) return "dom7";
  if (third == 3 && fifth == 6 && seventh == 9)  return "half-dim7";

  return "maj";
}

// ---------------------------------------------------------------------------
// Modal interchange
// ---------------------------------------------------------------------------

// All 7 diatonic modes as semitone patterns from root.
static constexpr int8_t kModeSteps[8][7] = {
    {0, 2, 4, 5, 7, 9, 11},  // Ionian (Major)
    {0, 2, 3, 5, 7, 9, 10},  // Dorian
    {0, 1, 3, 5, 7, 8, 10},  // Phrygian
    {0, 2, 4, 6, 7, 9, 11},  // Lydian
    {0, 2, 4, 5, 7, 9, 10},  // Mixolydian
    {0, 2, 3, 5, 7, 8, 10},  // Aeolian (Natural Minor)
    {0, 1, 3, 5, 6, 8, 10},  // Locrian
    {0, 2, 3, 5, 7, 8, 11},  // Harmonic Minor
};
static constexpr const char* kModeNames[8] = {
    "Ionian", "Dorian", "Phrygian", "Lydian",
    "Mixolydian", "Aeolian", "Locrian", "Harmonic Minor"
};

size_t find_modal_interchange(const app::Settings& s,
                              const uint8_t* chord_pcs, size_t num_pcs,
                              ModalMatch* out, size_t max_results) {
  if (!out || max_results == 0 || num_pcs == 0) return 0;

  const int key_off = key_offset_from_str(s.key);

  // Build pitch-class set for the chord.
  bool chord_set[12] = {};
  for (size_t i = 0; i < num_pcs && i < 12; ++i) {
    chord_set[chord_pcs[i] % 12] = true;
  }

  // Try each mode rooted on the current key.
  ModalMatch matches[8];
  size_t nm = 0;

  for (int m = 0; m < 8; ++m) {
    bool mode_set[12] = {};
    for (int i = 0; i < 7; ++i) {
      mode_set[(key_off + kModeSteps[m][i]) % 12] = true;
    }

    uint8_t shared = 0, foreign = 0;
    for (int pc = 0; pc < 12; ++pc) {
      if (!chord_set[pc]) continue;
      if (mode_set[pc]) ++shared;
      else ++foreign;
    }

    // Only include modes that contain at least some chord tones.
    if (shared > 0) {
      matches[nm++] = {kModeNames[m], shared, foreign};
    }
  }

  // Sort by shared tones descending, then foreign ascending.
  for (size_t i = 0; i < nm; ++i) {
    for (size_t j = i + 1; j < nm; ++j) {
      bool swap = (matches[j].shared_tones > matches[i].shared_tones) ||
                  (matches[j].shared_tones == matches[i].shared_tones &&
                   matches[j].foreign_tones < matches[i].foreign_tones);
      if (swap) std::swap(matches[i], matches[j]);
    }
  }

  const size_t count = (nm < max_results) ? nm : max_results;
  for (size_t i = 0; i < count; ++i) out[i] = matches[i];
  return count;
}

// ---------------------------------------------------------------------------
// Modulation suggestion
// ---------------------------------------------------------------------------
size_t suggest_modulation(const app::Settings& s,
                          const uint8_t* foreign_pcs, size_t num_foreign,
                          ModulationSuggestion* out, size_t max_results) {
  if (!out || max_results == 0 || num_foreign == 0) return 0;

  const int current_key = key_offset_from_str(s.key);

  // Get the current scale's interval pattern.
  const int8_t* sc_steps = nullptr;
  size_t sc_len = 0;
  scale_steps_for_name(s.scale, &sc_steps, &sc_len);
  if (!sc_steps || sc_len == 0) return 0;

  // Build foreign PC set.
  bool foreign_set[12] = {};
  for (size_t i = 0; i < num_foreign && i < 12; ++i) {
    foreign_set[foreign_pcs[i] % 12] = true;
  }

  // Try all 12 transpositions of the current scale pattern.
  ModulationSuggestion candidates[12];
  size_t nc = 0;

  for (int root = 0; root < 12; ++root) {
    if (root == current_key) continue;  // Skip current key.

    bool key_set[12] = {};
    for (size_t i = 0; i < sc_len; ++i) {
      key_set[(root + sc_steps[i]) % 12] = true;
    }

    uint8_t covered = 0;
    for (int pc = 0; pc < 12; ++pc) {
      if (foreign_set[pc] && key_set[pc]) ++covered;
    }
    if (covered == 0) continue;

    int raw_dist = std::abs(root - current_key);
    if (raw_dist > 6) raw_dist = 12 - raw_dist;  // Fold to 0-6.

    candidates[nc++] = {key_str_from_offset(root), covered,
                        static_cast<uint8_t>(raw_dist)};
  }

  // Sort by covered descending, then distance ascending.
  for (size_t i = 0; i < nc; ++i) {
    for (size_t j = i + 1; j < nc; ++j) {
      bool swap = (candidates[j].covered > candidates[i].covered) ||
                  (candidates[j].covered == candidates[i].covered &&
                   candidates[j].distance < candidates[i].distance);
      if (swap) std::swap(candidates[i], candidates[j]);
    }
  }

  const size_t count = (nc < max_results) ? nc : max_results;
  for (size_t i = 0; i < count; ++i) out[i] = candidates[i];
  return count;
}

// ---------------------------------------------------------------------------
// Pivot chord mask
// ---------------------------------------------------------------------------
uint8_t pivot_chord_mask(const app::Settings& s, const char* target_key) {
  const int8_t* sc_steps = nullptr;
  size_t sc_len = 0;
  scale_steps_for_name(s.scale, &sc_steps, &sc_len);
  if (!sc_steps || sc_len == 0) return 0;

  const int src_key = key_offset_from_str(s.key);
  const int tgt_key = key_offset_from_str(target_key);

  // Build target key's pitch-class set (same scale pattern, different root).
  bool target_set[12] = {};
  for (size_t i = 0; i < sc_len; ++i) {
    target_set[(tgt_key + sc_steps[i]) % 12] = true;
  }

  // For each degree in the source key, check if a triad built on that
  // degree is fully contained in the target key's pitch-class set.
  uint8_t mask = 0;
  for (uint8_t deg = 1; deg <= 7 && deg <= sc_len; ++deg) {
    size_t base_idx = deg - 1;
    // Build triad: root, 3rd, 5th (scale degrees: base, base+2, base+4).
    bool all_in_target = true;
    for (int t = 0; t < 3; ++t) {
      size_t idx = (base_idx + static_cast<size_t>(t) * 2) % sc_len;
      int pc = (src_key + sc_steps[idx]) % 12;
      if (!target_set[pc]) { all_in_target = false; break; }
    }
    if (all_in_target) mask |= (1u << (deg - 1));
  }

  return mask;
}

}  // namespace engine
