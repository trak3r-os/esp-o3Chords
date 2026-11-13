#pragma once

#include <cstdint>
#include "engine/progression.h"

namespace app {

// ============================================================================
// Enums
// ============================================================================

enum class JoystickPlayMode : uint8_t {
  kQuality    = 0,  // 8 dirs = chord qualities from QMap (modifier)
  kChordVel   = 1,  // 8 dirs = degrees, distance = velocity -> engine
  kBend       = 2,  // Sub A: classic pitch bend. Sub B: chord morph
  kSoloist    = 3,  // Sub A: scale walk. Sub B: chord tones -> engine
  kBassClock  = 4,  // 8 dirs = scale notes in clock layout (direct MIDI)
  kDiscover   = 5,  // W=suggested chord, other 7 dirs=qualities sorted by tension
  kArpControl = 6,  // Live arp param tweaker (rate/pattern/oct/gate)
  kInterval   = 7,  // 8 dirs = intervals from chord root (scale/chord+/power)
  kDrone      = 8,  // 8 dirs = toggle sustained drone notes (direct MIDI)
  kRhythm     = 9,  // 8 dirs = trigger chord with different perf feels
  kCCControl  = 10, // Continuous X/Y -> 2 MIDI CC values
  kPentatonic = 11, // Walk pentatonic scale (major/minor/blues)
  kVoicing    = 12, // 8 dirs = chord inversions & voicings (modifier)
  kEuclidean  = 13, // 8 dirs = Euclidean rhythm patterns, BPM-synced
  kStrumFeel  = 14, // 8 dirs = strum presets, retrigger active chord
  kHarmony    = 15, // 8 dirs = harmony intervals on joy channel
  kScaleExplore = 16, // 8 dirs = scale/mode switch (Ion/Dor/Phry etc.)
  kCOUNT      = 17
};

enum class KeyNavMode : uint8_t {
  kFifths = 0,
  kFourths = 1,
  kSemitone = 2,
  kCOUNT = 3,
};

enum class ProgressionMode : uint8_t {
  kMajor = 0,
  kNaturalMinor = 1,
  kHarmonicMinor = 2,
  kDorian = 3,
  kMixolydian = 4,
  kSus2 = 5,
  kSevenths = 6,
  kSalsa = 7,
  kBossaNova = 8,
  kBachata = 9,
  kReggaeton = 10,
  kJazz = 11,
  kNeoSoul = 12,
  kGospel = 13,
  kLofi = 14,
  kBlues = 15,
  kFunk = 16,
  kRnB = 17,
  kPop = 18,
  kTrap = 19,
  kMotown = 20,
  kFlamenco = 21,
  kCOUNT = 22,
};

// The full display name for the menu and the play screen (e.g. "Bossa Nova",
// "Neo Soul"). Distinct from what apply_progression_mode()
// (progression_modes.cpp) writes to Settings::scale: that field stays the
// narrow engine-facing scale name music_theory.cpp's scale_steps_for_name()
// actually branches on (Major / Natural Minor / Harmonic Minor / Dorian /
// Mixolydian, else Major), since genre presets deliberately run on the major
// scale's steps. Kept header-only (unlike apply_progression_mode) so it can
// be used from config_codec.cpp without pulling in engine::Engine.
inline const char* progression_mode_full_name(ProgressionMode mode) {
  switch (mode) {
    case ProgressionMode::kMajor: return "Major";
    case ProgressionMode::kNaturalMinor: return "Natural Minor";
    case ProgressionMode::kHarmonicMinor: return "Harmonic Minor";
    case ProgressionMode::kDorian: return "Dorian";
    case ProgressionMode::kMixolydian: return "Mixolydian";
    case ProgressionMode::kSus2: return "Sus2";
    case ProgressionMode::kSevenths: return "Sevenths";
    case ProgressionMode::kSalsa: return "Salsa";
    case ProgressionMode::kBossaNova: return "Bossa Nova";
    case ProgressionMode::kBachata: return "Bachata";
    case ProgressionMode::kReggaeton: return "Reggaeton";
    case ProgressionMode::kJazz: return "Jazz";
    case ProgressionMode::kNeoSoul: return "Neo Soul";
    case ProgressionMode::kGospel: return "Gospel";
    case ProgressionMode::kLofi: return "Lofi";
    case ProgressionMode::kBlues: return "Blues";
    case ProgressionMode::kFunk: return "Funk";
    case ProgressionMode::kRnB: return "R&B";
    case ProgressionMode::kPop: return "Pop";
    case ProgressionMode::kTrap: return "Trap";
    case ProgressionMode::kMotown: return "Motown";
    case ProgressionMode::kFlamenco: return "Flamenco";
    default: return "Major";
  }
}

// The scale name the engine resolves chord tones against for a mode
// (music_theory.cpp's scale_steps_for_name()). Only the five real scales have
// their own steps; Sus2/Sevenths and every genre preset run on Major's.
inline const char* progression_mode_engine_scale(ProgressionMode mode) {
  switch (mode) {
    case ProgressionMode::kNaturalMinor:
    case ProgressionMode::kHarmonicMinor:
    case ProgressionMode::kDorian:
    case ProgressionMode::kMixolydian:
      return progression_mode_full_name(mode);
    default: return "Major";
  }
}

// ============================================================================
// Joystick quality maps
// ============================================================================

struct JoystickQualityMap {
  const char* name;
  const char* qualities[8];
};

inline constexpr uint8_t kQualityMapCount = 13;

inline const JoystickQualityMap kQualityMaps[kQualityMapCount] = {
  {"CLSC", {"maj", "add9", "maj7", "sus4", "min", "dim", "sus2", "dom7"}},
  {"7THS", {"maj7", "dom7", "add9", "sus4", "min7", "half-dim7", "dim7", "aug"}},
  {"EXT9", {"add9", "maj9", "dom9", "dom13", "min9", "min11", "maj13", "dom7"}},
  {"6THS", {"maj6", "add9", "maj7", "sus4", "min6", "min7", "dom7", "sus2"}},
  {"COLR", {"sus2", "add9", "sus4", "maj7", "aug", "dim", "dom7", "min7"}},
  {"MjMn", {"maj", "maj7", "maj6", "add9", "min", "min7", "min6", "min9"}},
  {"BrDk", {"maj7", "add9", "aug", "sus4", "dim7", "half-dim7", "dim", "min7"}},
  {"TnRs", {"dom7", "aug", "sus4", "dim", "maj", "min", "sus2", "add9"}},
  {"DmSs", {"dom7", "dom9", "aug", "dim", "sus4", "sus2", "add9", "min7"}},
  {"JAZZ", {"maj7", "dom9", "min7", "half-dim7", "dim7", "min9", "dom7", "maj9"}},
  {"SOUL", {"min7", "dom9", "maj9", "add9", "min9", "dom7", "maj7", "min11"}},
  {"LATN", {"dom7", "min7", "maj7", "dim", "dom9", "aug", "add9", "min9"}},
  {"GOSP", {"maj7", "dom9", "maj9", "add9", "min7", "dim7", "min9", "dom7"}},
};

// ============================================================================
// Joystick inline helpers
// ============================================================================

// Clock mapping: converts joystick direction (0=E..7=SE) to degree index (0-7).
// N=0(I), NE=1(II), E=2(III), SE=3(IV), S=4(V), SW=5(VI), W=6(VII), NW=7(VIII)
inline constexpr uint8_t kClockMap[8] = {2, 1, 0, 7, 6, 5, 4, 3};

// Discover mode: direction -> tension slot index (0=least tense .. 7=most
// tense). All 8 directions are ranked, so every direction means the same
// kind of thing: N is the single most-consonant point, S the single
// least-consonant, and each side of the compass mirrors the other stepping
// down from N to S.
// Layout: N=0, NE=1, NW=2, E=3, W=4, SW=5, SE=6, S=7
//         -------- semicircle from consonant (top) to dissonant (bottom) --------
inline constexpr uint8_t kDiscoverMap[8] = {
  3,  // dir 0 = E  -> tension slot 3 (medium)
  1,  // dir 1 = NE -> tension slot 1 (low)
  0,  // dir 2 = N  -> tension slot 0 (most consonant)
  2,  // dir 3 = NW -> tension slot 2 (low-)
  4,  // dir 4 = W  -> tension slot 4 (medium+)
  5,  // dir 5 = SW -> tension slot 5 (high-)
  7,  // dir 6 = S  -> tension slot 7 (least consonant)
  6,  // dir 7 = SE -> tension slot 6 (high)
};

// Flat cycle: advance to next or previous mode.
inline JoystickPlayMode next_joystick_mode(JoystickPlayMode current) {
  return static_cast<JoystickPlayMode>(
      (static_cast<uint8_t>(current) + 1) % static_cast<uint8_t>(JoystickPlayMode::kCOUNT));
}

inline JoystickPlayMode prev_joystick_mode(JoystickPlayMode current) {
  return static_cast<JoystickPlayMode>(
      (static_cast<uint8_t>(current) + static_cast<uint8_t>(JoystickPlayMode::kCOUNT) - 1)
      % static_cast<uint8_t>(JoystickPlayMode::kCOUNT));
}

// Modes that route through the engine pipeline (arp/strum).
inline bool joystick_engine_mode(JoystickPlayMode m) {
  return m == JoystickPlayMode::kChordVel ||
         m == JoystickPlayMode::kSoloist ||
         m == JoystickPlayMode::kInterval ||
         m == JoystickPlayMode::kRhythm ||
         m == JoystickPlayMode::kPentatonic ||
         m == JoystickPlayMode::kEuclidean ||
         m == JoystickPlayMode::kStrumFeel;
}

}  // namespace app
