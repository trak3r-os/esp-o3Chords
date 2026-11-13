#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace app { struct Settings; }

namespace engine {

class Engine;
struct Chord;
struct ProgressionStep;

int key_offset_from_str(const char* k);
const char* key_str_from_offset(int off);

void scale_steps_for_name(const char* scale_name, const int8_t** out_steps, size_t* out_len);
int scale_degree_offset(const char* scale_name, uint8_t degree);

int progression_step_root_offset(const app::Settings& s, const ProgressionStep& step);
int progression_step_root_midi(const app::Settings& s, const ProgressionStep& step,
                               int base_octave = 4);

const Chord* chord_for_quality(Engine& engine, const std::string& quality);
uint8_t clamp_midi_note(int note);

// Select an actual chord tone as bass, anchored to this chord's register.
// Raising each lower tone as far as needed also handles 9ths/11ths/13ths;
// rotating a list once per inversion can leave the wrong note at the bottom.
template <typename Notes>
void apply_chord_inversion(Notes& notes, uint8_t inversion) {
  if (notes.empty()) return;
  std::sort(notes.begin(), notes.end());
  const int bass = notes[std::min<size_t>(inversion, notes.size() - 1)];
  for (auto& note : notes) {
    int raised = note;
    while (raised < bass) raised += 12;
    note = static_cast<uint8_t>(std::min(raised, 127));
  }
  std::sort(notes.begin(), notes.end());
}

std::vector<uint8_t> progression_step_chord_notes(const app::Settings& s, Engine& engine,
                                                   const ProgressionStep& step,
                                                   int base_octave = 4);
std::vector<uint8_t> scale_notes_midi(const app::Settings& s, int base_octave = 4);

bool quality_is_minor(const std::string& q);
bool quality_is_dim(const std::string& q);
bool quality_is_aug(const std::string& q);
const char* quality_short(const std::string& q);

// Count how many chord tones fall outside the current scale.
// Returns 0 (fully diatonic), 1 (one chromatic), 2+ (high tension).
uint8_t chord_tension(const app::Settings& s, Engine& engine,
                      uint8_t scale_degree, const std::string& quality);

// Suggest a chord quality for a given degree based on common voice-leading rules.
// Returns a quality string from the chord library (e.g. "maj", "min7").
//
// Dead code: nothing in the firmware calls it (kDiscoverMap ranks all 8
// directions rather than reserving one for a suggested next chord). Left in
// place deliberately rather than removed.
const char* suggest_next_quality(const app::Settings& s, Engine& engine,
                                 uint8_t current_degree, uint8_t target_degree);

const char* roman_degree(uint8_t degree);
void roman_symbol(uint8_t degree, const std::string& quality, char* out, size_t out_sz);
void nashville_symbol(uint8_t degree, const std::string& quality, char* out, size_t out_sz);

void midi_note_label(uint8_t note, char* out, size_t out_len);

const char* scale_formula_token(int semitone);

// ---------------------------------------------------------------------------
// Modal interchange: given a chord's pitch classes, find which mode(s) of the
// current scale contain all chord tones.  Returns up to max_results mode names
// sorted by number of shared tones (best first).
//
// Example: playing bVII (Bb) in C major → returns "Mixolydian" because the
// Mixolydian mode of C (C D E F G A Bb) contains that Bb.
// ---------------------------------------------------------------------------
struct ModalMatch {
  const char* mode_name;   // e.g. "Dorian", "Mixolydian"
  uint8_t shared_tones;    // How many chord PCs are in this mode (higher = better fit)
  uint8_t foreign_tones;   // How many chord PCs are NOT in this mode
};

// Returns number of matches written to out[] (0 if none).
size_t find_modal_interchange(const app::Settings& s,
                              const uint8_t* chord_pcs, size_t num_pcs,
                              ModalMatch* out, size_t max_results);

// ---------------------------------------------------------------------------
// Modulation suggestion: given foreign notes (out-of-scale pitch classes),
// find the nearest key(s) where those notes become diatonic.
// ---------------------------------------------------------------------------
struct ModulationSuggestion {
  const char* target_key;  // e.g. "Eb", "G"
  uint8_t covered;         // How many of the foreign notes this key covers
  uint8_t distance;        // Semitone distance from current key (0-6, folded)
};

// Returns number of suggestions written to out[] (0 if none).
size_t suggest_modulation(const app::Settings& s,
                          const uint8_t* foreign_pcs, size_t num_foreign,
                          ModulationSuggestion* out, size_t max_results);

// ---------------------------------------------------------------------------
// Pivot chords: find scale degrees that are diatonic in both the current key
// and a target key.  Returns bitmask of degrees 1-7 (bit 0 = degree 1).
// ---------------------------------------------------------------------------
uint8_t pivot_chord_mask(const app::Settings& s, const char* target_key);

}  // namespace engine
