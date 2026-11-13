#pragma once

#include "engine/progression.h"
#include <cstdint>
#include <string>

namespace engine {

enum class VoiceLeading : uint8_t {
  kOff = 0,      // Every chord as written: root position, or the manual inversion
  kSmooth,       // Each chord takes the inversion nearest the previous chord
  kCommon,       // As Smooth, but notes shared with the previous chord stay put first
};

// Eight distinct voicing styles applied to the lowest four voices of a chord
// (5+-note chords leave the extensions on top untouched). Index 1 (kDefault)
// must never move: stored `voicing_spread = 1` has always meant "unchanged".
enum class VoicingSpread : uint8_t {
  kClose = 0,    // All notes within 1 octave of bass
  kDefault,      // Standard voicing (as built) — keep at index 1
  kDrop2,        // Drop the second-from-top voice an octave
  kDrop3,        // Drop the third-from-top voice an octave (triads: falls back to Drop2)
  kOpen,         // Alternate voices dropped an octave (triads: falls back to Drop2)
  kShell,        // Root + 7th, 3rd raised an octave, 5th omitted (triads: unchanged)
  kRootless,     // Root omitted, 9th added above (triads: root omitted, no 9th)
  kGuitar,       // Root/5th/7th dropped an octave, 3rd left in place
};

// Canonical name for each style — the single place UI/joystick code should
// pull labels from, so the eight names never drift out of sync with the map
// in harmony_transform.cpp.
const char* voicing_spread_name(VoicingSpread v);

class ChordModifier {
 public:
  // Quality override - set by name from joystick quality maps
  void reset_quality();
  void set_quality(const char* quality);  // Set quality directly by name

  // Octave shifting - transpose by octaves
  void adjust_octave(int8_t delta);
  void reset_octave();

  // A manual inversion (keys 8/9) pins which chord tone is lowest and takes
  // the choice away from voice leading; reset_inversion() hands it back.
  void set_manual_inversion(uint8_t inversion);
  void reset_inversion();

  // Reset all modifications
  void reset();

  // Apply modifications to a progression step (non-destructive)
  ProgressionStep apply(const ProgressionStep& base) const;

  // Get current modifier state for display
  std::string get_quality_display() const;
  std::string get_octave_display() const;
  std::string get_inversion_display() const;

  // Voice leading
  void set_voice_leading(VoiceLeading vl) { voice_leading_ = vl; }
  VoiceLeading voice_leading() const { return voice_leading_; }

  // Voicing spread
  void set_voicing_spread(VoicingSpread vs) { voicing_spread_ = vs; }
  VoicingSpread voicing_spread() const { return voicing_spread_; }

  // Voice-leading complexity (0=smoothest, 100=most contrasting)
  void set_voice_leading_complexity(uint8_t c) { voice_leading_complexity_ = (c > 100) ? 100 : c; }
  uint8_t voice_leading_complexity() const { return voice_leading_complexity_; }

  std::string get_voice_leading_display() const;
  std::string get_voicing_spread_display() const;

  // Latch mode support
  void set_latch_mode(bool active) { latch_mode_active_ = active; }
  bool is_latched() const { return latch_mode_active_; }

  // Accessors
  const char* quality_name() const { return quality_name_; }
  int8_t octave_shift() const { return octave_shift_; }
  uint8_t inversion_override() const { return inversion_override_; }
  bool manual_inversion() const { return inversion_active_; }
  // True when the engine, not the player, picks each chord's inversion: voice
  // leading is on and no inversion has been set by hand (8/9).
  bool voice_leads() const {
    return voice_leading_ != VoiceLeading::kOff && !inversion_active_;
  }
  bool has_modifications() const;

 private:
  char quality_name_[16] = {};        // Active quality override name
  int8_t octave_shift_ = 0;          // -3 to +3 octaves
  uint8_t inversion_override_ = 0;   // 0-5, clamped to the chord's actual size
  bool quality_active_ = false;
  bool inversion_active_ = false;
  VoiceLeading voice_leading_ = VoiceLeading::kSmooth;
  VoicingSpread voicing_spread_ = VoicingSpread::kDefault;
  bool latch_mode_active_ = false;   // Joystick latch mode active
  uint8_t voice_leading_complexity_ = 0;  // 0=smoothest, 100=most contrasting
};

}  // namespace engine
