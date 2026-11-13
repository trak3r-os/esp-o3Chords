#pragma once

#include <cstddef>
#include <cstdint>

// Forward-declared, not included: contracts.h includes this header (Config
// embeds JoyFamilyState), so pulling contracts.h in here would be circular.
namespace app::contract { enum class ParamId : uint16_t; }

// The joystick's parameter model: four families (code) plus eight factory
// presets (data). Each family keeps its own full parameter set at all times
// — switching preset only overwrites the parameters of the family that
// preset belongs to, so tuning in one family is never lost by visiting
// another. There is no physical joystick button on this hardware, so family
// selection is never a separate gesture: rotating the Recipe slot walks all
// presets in a fixed order grouped by family, and the family is whichever
// preset is current.
namespace app {

enum class JoyFamily : uint8_t { kNotes, kChord, kCont, kParam, kCount };

// ---------------------------------------------------------------------------
// NOTES — the joystick fires its own notes.
// ---------------------------------------------------------------------------
enum class NoteSource : uint8_t { kDegrees, kScale, kChordTones, kIntervals, kPentatonic };
enum class NoteGesture : uint8_t { kClock, kWalk };
enum class NoteVoices : uint8_t { kNote, kChord };
enum class NoteSustain : uint8_t { kMomentary, kLatch };
enum class NoteVelocityMode : uint8_t { kFixed, kByDistance };
enum class NoteChannel : uint8_t { kChord, kLead };

struct NotesParams {
  NoteSource source = NoteSource::kDegrees;
  NoteGesture gesture = NoteGesture::kClock;
  NoteVoices voices = NoteVoices::kNote;
  NoteSustain sustain = NoteSustain::kMomentary;
  bool mono = false;
  int8_t octave = 0;  // -2..+2
  NoteVelocityMode velocity_mode = NoteVelocityMode::kFixed;
  uint8_t velocity_curve = 0;  // 0=linear, 1=exp, 2=log
  NoteChannel channel = NoteChannel::kChord;
};

// ---------------------------------------------------------------------------
// CHORD — the joystick transforms the chord (writes into ChordModifier).
// ---------------------------------------------------------------------------
// What a direction changes is fixed per preset, not a field: Colors/Discover
// change quality, Voicings changes voicing. Direction changes are armed
// silently and applied by the next numbered-key chord; center release never
// undoes them. `order` and `retrigger` are per-preset constants, not exposed
// in the menu.
enum class ChordOrder : uint8_t { kFixed, kRanking };

struct ChordParams {
  uint8_t palette = 0;  // index into kQualityMaps
  ChordOrder order = ChordOrder::kFixed;
  bool retrigger = false;
};

// ---------------------------------------------------------------------------
// CONT — the joystick sends a value, reading position rather than direction.
// ---------------------------------------------------------------------------
enum class ContTarget : uint8_t { kBend, kVibrato, kCc, kAftertouch };
enum class ContBendSnap : uint8_t { kChordTones, kChromatic, kOff };

struct ContParams {
  ContTarget x_target = ContTarget::kBend;
  ContTarget y_target = ContTarget::kVibrato;
  uint8_t x_cc = 1;
  uint8_t y_cc = 11;
  ContBendSnap bend_snap = ContBendSnap::kChordTones;
  uint8_t curve = 0;
  uint8_t deadzone = 10;
};

// ---------------------------------------------------------------------------
// PARAM — the joystick edits settings live, one target per axis.
// ---------------------------------------------------------------------------
// Tier-1 targets already have a ParamId; tier-2 exist in Config, persisted
// and sanitized, but have no ParamId/UI row. This is the axis-target
// vocabulary, kept independent of ParamId since it must name tier-2 fields
// too.
enum class AxisTarget : uint8_t {
  kNone,
  kArticulation, kPlayTrigger, kVoicing, kArpPattern, kArpRate, kArpOctaves,
  kArpGate, kStrumFeel, kSwing, kHumanize, kVelocity, kDuration,
  kAccentPattern, kVelocityRandom, kVoiceLeading, kVoiceLeadingComplexity,
  kSoloVelocity, kSoloNoteLength,
  // A continuous-only target: streams the axis position as a MIDI CC
  // (expression, CC 11) rather than mapping it onto a Settings field.
  // Appended last so existing wire values keep their meaning.
  kExpressionCc,
};
enum class AxisRole : uint8_t { kDiscrete, kContinuous };
enum class AxisHold : uint8_t { kMomentary, kSticky };
enum class AxisAmount : uint8_t { kStep, kByDistance };

struct ParamAxis {
  AxisTarget target = AxisTarget::kNone;
  AxisRole role = AxisRole::kDiscrete;
  AxisHold hold = AxisHold::kMomentary;
  AxisAmount amount = AxisAmount::kStep;
};

struct ParamParams {
  ParamAxis up_down;     // up/down
  ParamAxis left_right;  // left/right
  ParamAxis nw_se;       // the diagonal pair through NW/SE
  ParamAxis ne_sw;       // the diagonal pair through NE/SW
  bool retrigger_on_change = false;
};

// All four families' state, always persisted together, so each family keeps
// its own parameter set.
struct JoyFamilyState {
  NotesParams notes;
  ChordParams chord;
  ContParams cont;
  ParamParams param;
};

// The eight selectable factory presets, grouped by family in the fixed order
// the Recipe slot rotates through. Flip is not offered; its numeric value
// remains as an alias for decoding/source compatibility only.
enum class PresetId : uint8_t {
  kLead, kBass, kDrone,      // NOTES
  kColors, kDiscover, kVoicings,   // CHORD
  kBend, kMacro,                   // CONT
  kCount,
  kFlip = kCount,                  // Disabled compatibility alias.
};

struct PresetInfo {
  PresetId id;
  JoyFamily family;
  const char* name;
};

extern const PresetInfo kPresets[static_cast<size_t>(PresetId::kCount)];

// The named preset's parameters, applied on top of default-constructed
// params for its family (an edited-then-recalled preset always starts from
// this, never from whatever the live state happened to be).
JoyFamilyState preset_defaults(PresetId id);

// The settings migration's Harmony -> Drone remap: forces the fields that
// made the stored Harmony recipe/preset distinct (mono latched notes on the
// lead channel), leaving whatever else was already set (source, gesture,
// octave, sustain, ...). Shared by migrate_v2() and migrate_v3()
// (config_codec.cpp), which both need to reproduce a legacy Harmony-family
// recipe/preset, since there is no PresetId::kHarmony.
void apply_legacy_harmony_override(NotesParams& notes);

// The one parameter (if any) worth a push on this preset's joystick screen:
// the encoder always starts on Recipe, and a push reaches at most one further
// value. Shared by the renderer (production_render.cpp's
// draw_joystick_header(), for row1's right-hand slot) and the PLAY-screen
// cursor (production_ui.cpp's joystick_cursor) — a single source of truth for
// which ParamId that is. Returns false (and leaves `out` unset) for CONT and
// PARAM, where no single axis beats the rest.
bool joystick_push_param(PresetId preset, contract::ParamId& out);

}  // namespace app
