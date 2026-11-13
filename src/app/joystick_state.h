#pragma once

#include "app/types.h"
#include "app/contracts/contracts.h"
#include "util/fixed_vec.h"

#include <array>
#include <cstdint>

namespace app {

struct JoystickState {
  // Core
  JoystickPlayMode mode = JoystickPlayMode::kQuality;
  uint8_t quality_map_index = 0;
  bool quality_retrigger = false;
  bool latch_active = false;
  int8_t latched_dir = -1;
  bool last_btn = false;
  int8_t last_dir = -1;
  // Physical and keypad-as-joy gestures have independent edge tracking.  A
  // release from one source must not synthesize a release for the other.
  int8_t physical_dir = -1;
  int8_t keypad_dir = -1;
  int16_t raw_x = 0;  // centered ADC X value for XY graph
  int16_t raw_y = 0;  // centered ADC Y value for XY graph

  // Pitch bend
  int16_t last_bend = 0;
  bool bend_neutral_required = false;
  bool recipe_neutral_required = false;
  float bend_snap_semitones = 0.0f;

  // Note playback: NOTES family, Sustain=momentary (direct-MIDI, self-managed).
  std::array<uint8_t, 8> note_active{};
  uint8_t note_active_count = 0;
  uint8_t note_channel = 0;
  bool note_one_shot = false;
  uint32_t note_off_due_ms = 0;
  std::array<contract::VoiceToken, 8> note_tokens{};
  uint8_t last_note = 60;
  int8_t note_octave = 0;

  // Walk state (NOTES, Gesture=walk)
  uint8_t walk_index = 0;

  // Browse state (CHORD active direction / degree tracking)
  uint8_t browse_index = 0;

  // Velocity-from-distance curve, read by NOTES momentary clock playback
  // (Velocity=byDistance).
  uint8_t chordvel_curve = 0;   // 0=linear, 1=exp, 2=log

  // NOTES family, Sustain=latch: one sustained note (or chord, if Voices=chord)
  // per direction. Mono=on means "only one direction latched at a time," not
  // a separate mechanism.
  uint8_t drone_active_mask = 0; // Bitmask: which of 8 dirs have active latch notes
  uint8_t drone_notes[8] = {};
  util::FixedVec<uint8_t, 8> drone_chords[8];
  util::FixedVec<contract::VoiceToken, 8> drone_tokens[8];
  uint8_t drone_channels[8] = {};

  // CONT family: last CC values sent per axis, for change-only dedup.
  int16_t cc_last_x = -1;
  int16_t cc_last_y = -1;

  // CHORD family, Moves=voicing.
  uint8_t voicing_selection = 0; // 0=root,1=1st inv,2=2nd inv,3=drop2,4=open,5=shell,6=spread,7=reset
  bool voicing_auto_reset = true;// Reset voicing when chord changes

  // CHORD family, Order=ranking: tension-sorted quality slots
  // (discover_rebuild_slots), one per compass direction (kDiscoverMap ranks
  // all 8 directions; none is reserved for a suggestion).
  const char* discover_slots[8] = {};   // 8 tension-sorted qualities (0=least tense)
  uint8_t discover_tension[8] = {};     // tension level per slot (0,1,2+)
  uint8_t discover_consonance[8] = {};  // consonance score per slot (0-100)

  // Modal interchange hint (set by discover_rebuild_slots)
  const char* modal_hint = nullptr;     // e.g. "Dorian" if borrowed chord detected

  // Improvements
  bool joy_vel_enabled = false;           // (C) velocity-from-distance
  bool keypad_as_joy = false;             // Use keypad 1-9 as joystick directions
  bool keypad_gesture_active = false;
  uint8_t voicing_last_degree = 0;        // (D) track chord changes
  uint8_t cc_dead_zone = 10;              // (F) center dead zone
  uint8_t cc_preset = 0;                  // (G) 0=Custom,1-4=presets
  uint8_t bend_vibrato_depth = 0;         // (I) from Y-axis

  // PARAM family: per-axis (up_down, left_right, nw_se, ne_sw) momentary
  // bookkeeping — the value a Hold=momentary axis must restore on release.
  int32_t param_saved_value[4] = {};
  bool param_axis_active[4] = {};
};

// Tension level thresholds for OLED display.
inline constexpr uint8_t kTensionGreen  = 0;  // fully diatonic
inline constexpr uint8_t kTensionYellow = 1;  // 1 chromatic tone
// anything >= 2 is red

}  // namespace app
