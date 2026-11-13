#include "engine/chord_substitutions.h"
#include "engine/engine.h"
#include "engine/music_theory.h"
#include "app/state.h"

#include <algorithm>

namespace engine {

SubstitutionResult compute_substitution(const app::Settings& s,
                                         Engine& engine,
                                         uint8_t degree,
                                         uint8_t direction) {
  SubstitutionResult result;
  if (degree < 1 || degree > 7) {
    result.root_name = "C";
    result.quality = "maj";
    return result;
  }

  // Get current chord's root offset
  const auto& steps = engine.progression().steps();
  if (static_cast<size_t>(degree - 1) >= steps.size()) {
    result.root_name = "C";
    result.quality = "maj";
    return result;
  }

  const auto& step = steps[degree - 1];
  const int orig_root_off = progression_step_root_offset(s, step);

  int sub_root_off = orig_root_off;
  std::string sub_quality = step.chord_quality;

  switch (direction) {
    case 0: // E: Tritone sub — root + 6 semitones, dom7
      sub_root_off = (orig_root_off + 6) % 12;
      sub_quality = "dom7";
      break;
    case 1: // NE: Secondary dominant — V of current degree (root + 7), dom7
      sub_root_off = (orig_root_off + 7) % 12;
      sub_quality = "dom7";
      break;
    case 2: // N: Relative minor/major — ±3 semitones, swap quality
      if (quality_is_minor(step.chord_quality)) {
        sub_root_off = (orig_root_off + 3) % 12;
        sub_quality = "maj";
      } else {
        sub_root_off = (orig_root_off + 9) % 12; // -3 mod 12
        sub_quality = "min";
      }
      break;
    case 3: // NW: Borrowed chord — same root, swap major↔minor
      if (quality_is_minor(step.chord_quality)) {
        sub_quality = "maj";
      } else {
        sub_quality = "min";
      }
      break;
    case 4: // W: Diminished passing — half-step above root, dim7
      sub_root_off = (orig_root_off + 1) % 12;
      sub_quality = "dim7";
      break;
    case 5: // SW: Augmented — same root, aug
      sub_quality = "aug";
      break;
    case 6: // S: Pedal — same root, cycle quality (maj→7→maj7→min7)
    {
      static const char* kPedalCycle[] = {"maj", "dom7", "maj7", "min7"};
      static constexpr int kPedalCount = 4;
      int current = 0;
      for (int i = 0; i < kPedalCount; ++i) {
        if (step.chord_quality == kPedalCycle[i]) { current = i; break; }
      }
      sub_quality = kPedalCycle[(current + 1) % kPedalCount];
      break;
    }
    case 7: // SE: Chromatic mediant — root + 4 semitones, same quality type
      sub_root_off = (orig_root_off + 4) % 12;
      // Keep same quality family
      break;
  }

  result.root_name = key_str_from_offset(sub_root_off);
  result.quality = sub_quality;

  // Build MIDI notes
  const Chord* chord = chord_for_quality(engine, sub_quality);
  const int base_note = 60 + sub_root_off;  // C4 octave
  if (chord && !chord->intervals.empty()) {
    result.notes.reserve(chord->intervals.size());
    for (int8_t iv : chord->intervals) {
      result.notes.push_back(clamp_midi_note(base_note + iv));
    }
  } else {
    result.notes.push_back(clamp_midi_note(base_note));
  }

  return result;
}

}  // namespace engine
