#include "app/config_codec.h"
#include "app/progression_modes.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace app::contract {
namespace {

bool valid_axis(const app::ParamAxis& a) {
  return a.target <= app::AxisTarget::kExpressionCc &&
         a.role <= app::AxisRole::kContinuous &&
         a.hold <= app::AxisHold::kSticky &&
         a.amount <= app::AxisAmount::kByDistance;
}

bool valid_joy_families(const app::JoyFamilyState& f) {
  const auto& n = f.notes;
  if (n.source > app::NoteSource::kPentatonic || n.gesture > app::NoteGesture::kWalk ||
      n.voices > app::NoteVoices::kChord || n.sustain > app::NoteSustain::kLatch ||
      n.velocity_mode > app::NoteVelocityMode::kByDistance ||
      n.channel > app::NoteChannel::kLead || n.octave < -2 || n.octave > 2)
    return false;
  const auto& q = f.chord;
  if (q.palette >= app::kQualityMapCount || q.order > app::ChordOrder::kRanking)
    return false;
  const auto& k = f.cont;
  if (k.x_target > app::ContTarget::kAftertouch || k.y_target > app::ContTarget::kAftertouch ||
      k.bend_snap > app::ContBendSnap::kOff)
    return false;
  const auto& p = f.param;
  if (!valid_axis(p.up_down) || !valid_axis(p.left_right) ||
      !valid_axis(p.nw_se) || !valid_axis(p.ne_sw))
    return false;
  return true;
}

}  // namespace

ValidationResult validate(const Config& c) {
  if (c.velocity == 0 || c.velocity > 127 || c.duration_ms < 20 || c.duration_ms > 4000)
    return {ValidationError::Range, 0};
  if (c.arp_octaves < 1 || c.arp_octaves > 4 || c.arp_rate > 3 || c.arp_pattern > 6)
    return {ValidationError::Range, 1};
  if (c.swing > 100 || c.humanize > 40 || c.chord_channel > 15 || c.lead_channel > 15)
    return {ValidationError::Range, 2};
  if (c.articulation > Articulation::Arp || c.usb_role > UsbRole::Host || c.clock_source > ClockSource::External ||
      c.joystick.recipe_id >= RecipeId::COUNT)
    return {ValidationError::InvalidEnum, 3};
  if (c.joystick.x_min >= c.joystick.x_max || c.joystick.y_min >= c.joystick.y_max)
    return {ValidationError::Range, 4};
  if (c.key > 11 || c.scale > 21 || c.joystick.curve > 2 ||
      c.solo_velocity == 0 || c.solo_velocity > 127 ||
      c.solo_note_length_ms < 20 || c.solo_note_length_ms > 4000 ||
      c.velocity_random > 30 || c.accent_pattern > 3 ||
      c.voice_leading_complexity > 100)
    return {ValidationError::Range, 6};
  if (!std::isfinite(c.bpm) || c.bpm < 20.0f || c.bpm > 300.0f || c.play_trigger > 2 ||
      c.arp_gate_percent < 1 || c.arp_gate_percent > 100 || c.strum_rhythm > 4 ||
      c.voice_leading > 2 || c.voicing_spread > 7 ||
      (c.bend_range_semitones != 2 && c.bend_range_semitones != 5 &&
       c.bend_range_semitones != 7 && c.bend_range_semitones != 12))
    return {ValidationError::Range, 5};
  if (c.roll_ms > 60 || c.roll_dir > 3)
    return {ValidationError::Range, 8};
  if (c.active_preset >= app::PresetId::kCount || !valid_joy_families(c.joy_families))
    return {ValidationError::InvalidEnum, 7};
  return {};
}

}  // namespace app::contract

namespace app::config {
namespace {

constexpr size_t kWireSize = 124;
// Bytes 81-82: roll_ms/roll_dir (same offsets as in the v3 layout, which
// migrate_v3() reads unchanged).
// Bytes 83-118: the four JoyFamilyState family parameter sets (v4 —
// ChordParams is 3 bytes here, where v3 stored 7 including explore/moves/
// apply/on_center, so this block is 4 bytes shorter than v3's). Byte 119:
// active_preset, followed by the CRC. Sized from the structs, not estimated.
constexpr size_t kJoyFamiliesOffset = 83;
constexpr size_t kActivePresetOffset = 119;
constexpr size_t kCrcOffset = 120;

uint32_t read32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
void write32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (i * 8)); }
uint16_t read16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
void write16(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v); p[1] = static_cast<uint8_t>(v >> 8); }
uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) { crc ^= p[i]; for (int j = 0; j < 8; ++j) crc = (crc >> 1) ^ (0xEDB88320u & (-(crc & 1))); }
  return ~crc;
}
void put_i16(uint8_t* p, int16_t v) { write16(p, static_cast<uint16_t>(v)); }
int16_t get_i16(const uint8_t* p) { return static_cast<int16_t>(read16(p)); }

void write_joy_families(uint8_t* b, const app::JoyFamilyState& f) {
  size_t i = kJoyFamiliesOffset;
  const auto& n = f.notes;
  b[i++] = static_cast<uint8_t>(n.source); b[i++] = static_cast<uint8_t>(n.gesture);
  b[i++] = static_cast<uint8_t>(n.voices); b[i++] = static_cast<uint8_t>(n.sustain);
  b[i++] = n.mono; b[i++] = static_cast<uint8_t>(n.octave);
  b[i++] = static_cast<uint8_t>(n.velocity_mode); b[i++] = n.velocity_curve;
  b[i++] = static_cast<uint8_t>(n.channel);
  const auto& q = f.chord;
  b[i++] = q.palette; b[i++] = static_cast<uint8_t>(q.order); b[i++] = q.retrigger;
  const auto& k = f.cont;
  b[i++] = static_cast<uint8_t>(k.x_target); b[i++] = static_cast<uint8_t>(k.y_target);
  b[i++] = k.x_cc; b[i++] = k.y_cc; b[i++] = static_cast<uint8_t>(k.bend_snap);
  b[i++] = k.curve; b[i++] = k.deadzone;
  const app::ParamAxis* axes[4] = {&f.param.up_down, &f.param.left_right, &f.param.nw_se, &f.param.ne_sw};
  for (auto* a : axes) {
    b[i++] = static_cast<uint8_t>(a->target); b[i++] = static_cast<uint8_t>(a->role);
    b[i++] = static_cast<uint8_t>(a->hold); b[i++] = static_cast<uint8_t>(a->amount);
  }
  b[i++] = f.param.retrigger_on_change;
}

// Bytes 0-80 mean the same thing in v2 and v3 — v3 only appended fields
// after byte 80 — so both decode() and migrate_v2() share this. Bytes 81-82
// (roll_ms/roll_dir) are NOT included here even though they postdate v2's
// byte 80 boundary too: a real v2 blob's bytes 81-91 are unspecified padding
// (roll didn't exist yet), so migrate_v2() must not read them as roll data —
// only decode() (v3) does, right after calling this.
void read_common_fields(const uint8_t* b, contract::Config& c) {
  c.articulation = static_cast<contract::Articulation>(b[8]); c.velocity = b[9]; c.duration_ms = read16(b + 10); c.key = b[12]; c.scale = b[13]; c.arp_rate = b[14]; c.arp_pattern = b[15]; c.arp_octaves = b[16]; c.strum_feel = b[17]; c.swing = b[18]; c.humanize = b[19]; c.chord_channel = b[20]; c.lead_channel = b[21]; c.usb_role = static_cast<contract::UsbRole>(b[22]); c.ble_enabled = b[23]; c.joystick.recipe_id = static_cast<contract::RecipeId>(b[24]); c.joystick.x_target = b[25]; c.joystick.y_target = b[26]; c.joystick.curve = b[27]; c.joystick.keypad_as_joy = b[28]; c.joystick.x_min = get_i16(b + 29); c.joystick.x_max = get_i16(b + 31); c.joystick.y_min = get_i16(b + 33); c.joystick.y_max = get_i16(b + 35); c.revision = read32(b + 37);
  c.clock_source = static_cast<contract::ClockSource>(b[41]);
  std::memcpy(&c.bpm, b + 42, sizeof(c.bpm));
  std::memcpy(c.key_name, b + 46, sizeof(c.key_name)); c.key_name[sizeof(c.key_name)-1] = '\0';
  std::memcpy(c.scale_name, b + 50, sizeof(c.scale_name)); c.scale_name[sizeof(c.scale_name)-1] = '\0';
  c.play_trigger = b[66]; c.arp_gate_percent = b[67]; c.strum_rhythm = b[68];
  c.voice_leading = b[69]; c.voicing_spread = b[70]; c.bend_range_semitones = b[71];
  c.joystick_rotation = b[72]; c.joystick_flip_h = b[73];
  c.solo_velocity = b[74]; c.solo_note_length_ms = read16(b + 75);
  c.velocity_random = b[77]; c.accent_pattern = b[78]; c.voice_leading_complexity = b[79];
  c.joystick_flip_v = b[80];
}

}  // namespace

// Migration table for v2 blobs: maps each v2 RecipeId straight onto its v4
// preset (the v3-only presets kHarmony/kArp/kFeel/kDynamics don't exist in
// v4, so there is no intermediate stop). Sub-modes that lived in runtime
// JoystickState (not on the wire) aren't decidable here — Melody picks sub A
// (Gesture=walk) and Drone picks note. Interval/Harmony additionally need
// apply_legacy_harmony_override() on top of kDrone's factory defaults
// (migrate_v2()'s caller applies it) — recall_preset() alone would give
// kDrone's own sound (chord drone on scale degrees), not the mono latched
// interval drone Harmony actually was.
app::PresetId preset_for_recipe_v2(contract::RecipeId r) {
  using R = contract::RecipeId;
  switch (r) {
    case R::ChordVel:      return app::PresetId::kDrone;     // + momentary, not captured by a preset alone
    case R::Melody:        return app::PresetId::kLead;      // sub A default
    case R::Pentatonic:    return app::PresetId::kLead;
    case R::BassClock:     return app::PresetId::kBass;
    case R::Interval:      return app::PresetId::kDrone;     // was kHarmony
    case R::Harmony:       return app::PresetId::kDrone;     // was kHarmony
    case R::Drone:         return app::PresetId::kDrone;
    case R::Euclidean:     return app::PresetId::kLead;      // removed; falls back to NOTES/Lead
    case R::Quality:       return app::PresetId::kColors;
    case R::Discover:      return app::PresetId::kDiscover;
    case R::Voicing:       return app::PresetId::kVoicings;
    case R::ScaleExplore:  return app::PresetId::kColors;    // explore=qualities only
    case R::Bend:          return app::PresetId::kBend;
    case R::Expression:    return app::PresetId::kMacro;
    case R::Rhythm:        return app::PresetId::kMacro;     // Flip removed
    case R::StrumFeel:     return app::PresetId::kMacro;     // Flip removed
    case R::ArpControl:    return app::PresetId::kMacro;     // Flip removed
    default:               return app::PresetId::kLead;
  }
}

namespace {

void read_joy_families(const uint8_t* b, app::JoyFamilyState& f) {
  size_t i = kJoyFamiliesOffset;
  auto& n = f.notes;
  n.source = static_cast<app::NoteSource>(b[i++]); n.gesture = static_cast<app::NoteGesture>(b[i++]);
  n.voices = static_cast<app::NoteVoices>(b[i++]); n.sustain = static_cast<app::NoteSustain>(b[i++]);
  n.mono = b[i++]; n.octave = static_cast<int8_t>(b[i++]);
  n.velocity_mode = static_cast<app::NoteVelocityMode>(b[i++]); n.velocity_curve = b[i++];
  n.channel = static_cast<app::NoteChannel>(b[i++]);
  auto& q = f.chord;
  q.palette = b[i++]; q.order = static_cast<app::ChordOrder>(b[i++]); q.retrigger = b[i++];
  auto& k = f.cont;
  k.x_target = static_cast<app::ContTarget>(b[i++]); k.y_target = static_cast<app::ContTarget>(b[i++]);
  k.x_cc = b[i++]; k.y_cc = b[i++]; k.bend_snap = static_cast<app::ContBendSnap>(b[i++]);
  k.curve = b[i++]; k.deadzone = b[i++];
  app::ParamAxis* axes[4] = {&f.param.up_down, &f.param.left_right, &f.param.nw_se, &f.param.ne_sw};
  for (auto* a : axes) {
    a->target = static_cast<app::AxisTarget>(b[i++]); a->role = static_cast<app::AxisRole>(b[i++]);
    a->hold = static_cast<app::AxisHold>(b[i++]); a->amount = static_cast<app::AxisAmount>(b[i++]);
  }
  f.param.retrigger_on_change = b[i++];
}

}  // namespace

// Overwrite only the family the chosen preset belongs to, leaving the other
// three untouched — that's what lets cycling the Recipe slot recall a preset
// without wiping tuning in the families you aren't looking at.
void recall_preset(app::JoyFamilyState& state, app::PresetId preset) {
  if (preset >= app::PresetId::kCount) return;
  const auto defaults = app::preset_defaults(preset);
  switch (app::kPresets[static_cast<size_t>(preset)].family) {
    case app::JoyFamily::kNotes: state.notes = defaults.notes; break;
    case app::JoyFamily::kChord: state.chord = defaults.chord; break;
    case app::JoyFamily::kCont:  state.cont = defaults.cont; break;
    case app::JoyFamily::kParam: state.param = defaults.param; break;
    default: break;
  }
}

size_t encoded_size() { return kWireSize; }

bool encode(const contract::Config& c, uint8_t* b, size_t cap, size_t& written) {
  written = 0;
  if (!b || cap < kWireSize || contract::validate(c).error != contract::ValidationError::None) return false;
  std::memset(b, 0, kWireSize);
  write32(b, kMagic); write16(b + 4, kVersion); write16(b + 6, kWireSize);
  b[8] = static_cast<uint8_t>(c.articulation); b[9] = c.velocity; write16(b + 10, c.duration_ms);
  b[12] = c.key; b[13] = c.scale; b[14] = c.arp_rate; b[15] = c.arp_pattern; b[16] = c.arp_octaves;
  b[17] = c.strum_feel; b[18] = c.swing; b[19] = c.humanize; b[20] = c.chord_channel; b[21] = c.lead_channel;
  b[22] = static_cast<uint8_t>(c.usb_role); b[23] = c.ble_enabled;
  b[41] = static_cast<uint8_t>(c.clock_source);
  b[24] = static_cast<uint8_t>(c.joystick.recipe_id); b[25] = c.joystick.x_target; b[26] = c.joystick.y_target; b[27] = c.joystick.curve; b[28] = c.joystick.keypad_as_joy;
  put_i16(b + 29, c.joystick.x_min); put_i16(b + 31, c.joystick.x_max); put_i16(b + 33, c.joystick.y_min); put_i16(b + 35, c.joystick.y_max);
  b[37] = static_cast<uint8_t>(c.revision); b[38] = static_cast<uint8_t>(c.revision >> 8); b[39] = static_cast<uint8_t>(c.revision >> 16); b[40] = static_cast<uint8_t>(c.revision >> 24);
  std::memcpy(b + 42, &c.bpm, sizeof(c.bpm));
  std::memcpy(b + 46, c.key_name, sizeof(c.key_name));
  std::memcpy(b + 50, c.scale_name, sizeof(c.scale_name));
  b[66] = c.play_trigger; b[67] = c.arp_gate_percent; b[68] = c.strum_rhythm;
  b[69] = c.voice_leading; b[70] = c.voicing_spread; b[71] = c.bend_range_semitones;
  b[72] = c.joystick_rotation; b[73] = c.joystick_flip_h;
  b[74] = c.solo_velocity; write16(b + 75, c.solo_note_length_ms);
  b[77] = c.velocity_random; b[78] = c.accent_pattern; b[79] = c.voice_leading_complexity;
  b[80] = c.joystick_flip_v;
  b[81] = c.roll_ms; b[82] = c.roll_dir;
  write_joy_families(b, c.joy_families);
  b[kActivePresetOffset] = static_cast<uint8_t>(c.active_preset);
  write32(b + kCrcOffset, crc32(b, kCrcOffset)); written = kWireSize; return true;
}

DecodeResult decode(const uint8_t* b, size_t size, contract::Config& out) {
  if (!b || size < 8) return {DecodeStatus::Corrupt, 0};
  if (read32(b) != kMagic) return {DecodeStatus::Unsupported, read16(b + 4)};
  if (read16(b + 4) != kVersion) return {DecodeStatus::Unsupported, read16(b + 4)};
  if (size != kWireSize || read16(b + 6) != kWireSize || read32(b + kCrcOffset) != crc32(b, kCrcOffset))
    return {DecodeStatus::Corrupt, kVersion};
  contract::Config c{};
  read_common_fields(b, c);
  c.roll_ms = b[81]; c.roll_dir = b[82];
  read_joy_families(b, c.joy_families);
  c.active_preset = static_cast<app::PresetId>(b[kActivePresetOffset]);
  // Preset 8 was Flip in the previous v4 layout. Keep the rest of the saved
  // configuration and fall back to the last available continuous recipe.
  if (b[kActivePresetOffset] == 8) c.active_preset = app::PresetId::kMacro;
  if (contract::validate(c).error != contract::ValidationError::None) return {DecodeStatus::Corrupt, kVersion};
  out = c; return {DecodeStatus::Valid, kVersion};
}

DecodeResult migrate_v2(const uint8_t* b, size_t size, contract::Config& out) {
  constexpr uint16_t kVersionV2 = 2;
  constexpr size_t kWireSizeV2 = 96;
  constexpr size_t kCrcOffsetV2 = 92;
  if (!b || size < 8) return {DecodeStatus::Corrupt, 0};
  if (read32(b) != kMagic) return {DecodeStatus::Unsupported, read16(b + 4)};
  if (read16(b + 4) != kVersionV2) return {DecodeStatus::Unsupported, read16(b + 4)};
  if (size != kWireSizeV2 || read16(b + 6) != kWireSizeV2 || read32(b + kCrcOffsetV2) != crc32(b, kCrcOffsetV2))
    return {DecodeStatus::Corrupt, kVersionV2};
  contract::Config c{};
  read_common_fields(b, c);
  c.active_preset = preset_for_recipe_v2(c.joystick.recipe_id);
  c.joy_families = {};
  recall_preset(c.joy_families, c.active_preset);
  if (c.joystick.recipe_id == contract::RecipeId::Interval ||
      c.joystick.recipe_id == contract::RecipeId::Harmony) {
    app::apply_legacy_harmony_override(c.joy_families.notes);
  }
  if (contract::validate(c).error != contract::ValidationError::None) return {DecodeStatus::Corrupt, kVersionV2};
  out = c; return {DecodeStatus::Valid, kVersionV2};
}

namespace {
// v3's PresetId numbering, frozen here since the live app::PresetId enum
// drops kHarmony/kArp/kFeel/kDynamics in v4 and every value after them
// shifts down — migrate_v3() must decode a stored byte against this OLD
// layout, never the current enum.
enum class PresetIdV3 : uint8_t {
  kLead = 0, kBass = 1, kHarmony = 2, kDrone = 3,
  kColors = 4, kDiscover = 5, kVoicings = 6,
  kBend = 7, kMacro = 8,
  kArp = 9, kFeel = 10, kDynamics = 11, kFlip = 12,
  kCount = 13,
};

// v3 -> v4 preset table: kHarmony folds into kDrone (with
// apply_legacy_harmony_override() applied by the caller); kArp/kFeel/
// kDynamics and kFlip fall back to Macro, since Flip is disabled.
app::PresetId preset_v3_to_v4(PresetIdV3 v3) {
  switch (v3) {
    case PresetIdV3::kLead:     return app::PresetId::kLead;
    case PresetIdV3::kBass:     return app::PresetId::kBass;
    case PresetIdV3::kHarmony:  return app::PresetId::kDrone;
    case PresetIdV3::kDrone:    return app::PresetId::kDrone;
    case PresetIdV3::kColors:   return app::PresetId::kColors;
    case PresetIdV3::kDiscover: return app::PresetId::kDiscover;
    case PresetIdV3::kVoicings: return app::PresetId::kVoicings;
    case PresetIdV3::kBend:     return app::PresetId::kBend;
    case PresetIdV3::kMacro:    return app::PresetId::kMacro;
    case PresetIdV3::kArp: case PresetIdV3::kFeel:
    case PresetIdV3::kDynamics: case PresetIdV3::kFlip:
      return app::PresetId::kMacro;
    default: return app::PresetId::kLead;
  }
}
}  // namespace

// v3 -> v4. Unlike migrate_v2() (which never had real per-family state to
// preserve and just recalls factory defaults), a v3 blob's joy_families are
// real, possibly user-tuned values, so this reads them field by field rather
// than resetting to preset_defaults() — only the four ChordParams fields v4
// does not store (explore, moves, apply, on_center) are discarded outright,
// and only a legacy Harmony preset gets an explicit override on top of what
// was actually stored.
DecodeResult migrate_v3(const uint8_t* b, size_t size, contract::Config& out) {
  constexpr uint16_t kVersionV3 = 3;
  constexpr size_t kWireSizeV3 = 128;
  constexpr size_t kJoyFamiliesOffsetV3 = 83;
  constexpr size_t kActivePresetOffsetV3 = 123;
  constexpr size_t kCrcOffsetV3 = 124;
  if (!b || size < 8) return {DecodeStatus::Corrupt, 0};
  if (read32(b) != kMagic) return {DecodeStatus::Unsupported, read16(b + 4)};
  if (read16(b + 4) != kVersionV3) return {DecodeStatus::Unsupported, read16(b + 4)};
  if (size != kWireSizeV3 || read16(b + 6) != kWireSizeV3 || read32(b + kCrcOffsetV3) != crc32(b, kCrcOffsetV3))
    return {DecodeStatus::Corrupt, kVersionV3};

  contract::Config c{};
  read_common_fields(b, c);
  c.roll_ms = b[81]; c.roll_dir = b[82];

  size_t i = kJoyFamiliesOffsetV3;
  auto& n = c.joy_families.notes;
  n.source = static_cast<app::NoteSource>(b[i++]); n.gesture = static_cast<app::NoteGesture>(b[i++]);
  n.voices = static_cast<app::NoteVoices>(b[i++]); n.sustain = static_cast<app::NoteSustain>(b[i++]);
  n.mono = b[i++]; n.octave = static_cast<int8_t>(b[i++]);
  n.velocity_mode = static_cast<app::NoteVelocityMode>(b[i++]); n.velocity_curve = b[i++];
  n.channel = static_cast<app::NoteChannel>(b[i++]);
  // v3 chord block: explore, moves, palette, order, apply, retrigger,
  // on_center. explore/moves/apply/on_center are discarded; only
  // palette/order/retrigger exist in v4.
  auto& q = c.joy_families.chord;
  i += 2;  // skip explore, moves
  q.palette = b[i++];
  q.order = static_cast<app::ChordOrder>(b[i++]);
  i += 1;  // skip apply
  q.retrigger = b[i++];
  i += 1;  // skip on_center
  auto& k = c.joy_families.cont;
  k.x_target = static_cast<app::ContTarget>(b[i++]); k.y_target = static_cast<app::ContTarget>(b[i++]);
  k.x_cc = b[i++]; k.y_cc = b[i++]; k.bend_snap = static_cast<app::ContBendSnap>(b[i++]);
  k.curve = b[i++]; k.deadzone = b[i++];
  app::ParamAxis* axes[4] = {&c.joy_families.param.up_down, &c.joy_families.param.left_right,
                             &c.joy_families.param.nw_se, &c.joy_families.param.ne_sw};
  for (auto* a : axes) {
    a->target = static_cast<app::AxisTarget>(b[i++]); a->role = static_cast<app::AxisRole>(b[i++]);
    a->hold = static_cast<app::AxisHold>(b[i++]); a->amount = static_cast<app::AxisAmount>(b[i++]);
  }
  c.joy_families.param.retrigger_on_change = b[i++];

  const uint8_t v3_preset = b[kActivePresetOffsetV3];
  c.active_preset = preset_v3_to_v4(static_cast<PresetIdV3>(v3_preset));
  if (static_cast<PresetIdV3>(v3_preset) == PresetIdV3::kHarmony) {
    app::apply_legacy_harmony_override(c.joy_families.notes);
  }

  if (contract::validate(c).error != contract::ValidationError::None) return {DecodeStatus::Corrupt, kVersionV3};
  out = c; return {DecodeStatus::Valid, kVersionV3};
}

namespace {
constexpr const char* kKeyNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// Legacy Settings carry the key and scale as strings ("F#", "Bb", "Dorian").
uint8_t key_index_from_name(const char* name) {
  static constexpr int8_t kLetter[7] = {9, 11, 0, 2, 4, 5, 7};  // A..G
  const char letter = name ? name[0] : 'C';
  if (letter < 'A' || letter > 'G') return 0;
  int pitch = kLetter[letter - 'A'];
  if (name[1] == '#') ++pitch;
  else if (name[1] == 'b') --pitch;
  return static_cast<uint8_t>((pitch + 12) % 12);
}

uint8_t scale_index_from_name(const char* name) {
  for (uint8_t i = 0; i < static_cast<uint8_t>(app::ProgressionMode::kCOUNT); ++i)
    if (std::strcmp(name, app::progression_mode_full_name(static_cast<app::ProgressionMode>(i))) == 0) return i;
  return 0;
}
}  // namespace

void sync_names(contract::Config& c) {
  std::snprintf(c.key_name, sizeof(c.key_name), "%s", kKeyNames[c.key % 12]);
  const auto mode = c.scale < static_cast<uint8_t>(app::ProgressionMode::kCOUNT)
      ? static_cast<app::ProgressionMode>(c.scale) : app::ProgressionMode::kMajor;
  std::snprintf(c.scale_name, sizeof(c.scale_name), "%s", app::progression_mode_full_name(mode));
}

contract::Config factory_defaults() { return from_settings(Settings{}); }

MigrationStatus migrate_v1_result(const Settings& s, contract::Config& out) {
  static_assert(sizeof(Settings) == 112, "ESP32 v1 Settings layout changed");
  if (s.magic != 0x4F334348 || s.version != 1 || s.size != 112 || !std::isfinite(s.bpm) ||
      !std::memchr(s.key, '\0', sizeof(s.key)) || !std::memchr(s.scale, '\0', sizeof(s.scale)) ||
      !std::memchr(s.ble_device_name, '\0', sizeof(s.ble_device_name))) return MigrationStatus::Corrupt;
  if (s.mode == PerformanceMode::kHarp) return MigrationStatus::Unsupported;
  contract::Config c{}; c.velocity = s.velocity; c.duration_ms = s.note_length_ms; c.arp_rate = s.arp_rate; c.arp_pattern = s.arp_pattern; c.arp_octaves = s.arp_octaves;
  c.bpm = s.bpm; c.key = key_index_from_name(s.key); c.scale = scale_index_from_name(s.scale); sync_names(c);
  recall_preset(c.joy_families, c.active_preset);
  c.play_trigger = static_cast<uint8_t>(s.play_trigger); c.arp_gate_percent = s.arp_gate_percent; c.strum_rhythm = s.strum_rhythm;
  if (s.mode == PerformanceMode::kStrum) c.articulation = contract::Articulation::Strum; else if (s.mode != PerformanceMode::kChord) c.articulation = contract::Articulation::Arp;
  if (s.mode == PerformanceMode::kArp2Oct) c.arp_octaves = 2;
  c.swing = s.swing_percent; c.humanize = s.humanize_ms; c.chord_channel = s.midi_channel; c.lead_channel = s.midi_channel_joy; c.ble_enabled = s.ble_midi_enabled;
  c.strum_feel = s.strum_ms; c.solo_velocity = s.solo_velocity; c.solo_note_length_ms = s.solo_note_length_ms;
  c.velocity_random = s.velocity_random; c.accent_pattern = s.accent_pattern;
  c.voice_leading_complexity = s.voice_leading_complexity;
  c.usb_role = s.usb_host_midi_enabled ? contract::UsbRole::Host : (s.usb_device_midi_enabled ? contract::UsbRole::Device : contract::UsbRole::Off);
  c.voice_leading = s.voice_leading; c.voicing_spread = s.voicing_spread; c.bend_range_semitones = s.bend_range_semitones;
  c.joystick_rotation = s.joystick_rotation; c.joystick_flip_h = s.joystick_flip_h; c.joystick_flip_v = s.joystick_flip_v;
  if (contract::validate(c).error != contract::ValidationError::None) return MigrationStatus::Corrupt;
  out = c;
  return MigrationStatus::Valid;
}

bool migrate_v1(const Settings& s, contract::Config& out) {
  return migrate_v1_result(s, out) == MigrationStatus::Valid;
}

contract::Config from_settings(const Settings& s) {
  contract::Config c{};
  c.bpm = s.bpm;
  // Reverse-maps the legacy strings onto the indexes Config treats as the
  // authority (Scale walks all 22 ProgressionMode presets).
  c.key = key_index_from_name(s.key); c.scale = scale_index_from_name(s.scale); sync_names(c);
  // A Config built here has never had a preset recalled: without this the
  // default preset would run on the family structs' bare member defaults
  // instead of its own factory parameters.
  recall_preset(c.joy_families, c.active_preset);
  c.velocity = s.velocity; c.duration_ms = s.note_length_ms;
  c.articulation = s.mode == PerformanceMode::kStrum ? contract::Articulation::Strum
                   : s.mode == PerformanceMode::kChord ? contract::Articulation::Chord
                   : contract::Articulation::Arp;
  c.play_trigger = static_cast<uint8_t>(s.play_trigger);
  c.arp_rate = s.arp_rate; c.arp_pattern = s.arp_pattern; c.arp_octaves = s.arp_octaves;
  c.arp_gate_percent = s.arp_gate_percent; c.strum_rhythm = s.strum_rhythm;
  c.strum_feel = s.strum_ms; c.swing = s.swing_percent; c.humanize = s.humanize_ms;
  c.chord_channel = s.midi_channel; c.lead_channel = s.midi_channel_joy;
  c.usb_role = s.usb_host_midi_enabled ? contract::UsbRole::Host :
               (s.usb_device_midi_enabled ? contract::UsbRole::Device : contract::UsbRole::Off);
  c.ble_enabled = s.ble_midi_enabled; c.voice_leading = s.voice_leading;
  c.voicing_spread = s.voicing_spread; c.bend_range_semitones = s.bend_range_semitones;
  c.joystick_rotation = s.joystick_rotation; c.joystick_flip_h = s.joystick_flip_h; c.joystick_flip_v = s.joystick_flip_v;
  c.clock_source = static_cast<contract::ClockSource>(s.clock_mode);
  c.solo_velocity = s.solo_velocity; c.solo_note_length_ms = s.solo_note_length_ms;
  c.velocity_random = s.velocity_random; c.accent_pattern = s.accent_pattern;
  c.voice_leading_complexity = s.voice_leading_complexity;
  return c;
}

bool apply_to_settings(const contract::Config& c, Settings& s) {
  if (contract::validate(c).error != contract::ValidationError::None) return false;
  // Settings::scale is the engine-facing scale, not the display name: genre
  // modes ("Jazz", "Trap", ...) deliberately resolve chord tones on Major.
  s.bpm = c.bpm; std::snprintf(s.key, sizeof(s.key), "%s", kKeyNames[c.key % 12]);
  std::snprintf(s.scale, sizeof(s.scale), "%s",
                app::progression_mode_engine_scale(static_cast<app::ProgressionMode>(c.scale)));
  s.velocity = c.velocity; s.note_length_ms = c.duration_ms;
  s.mode = c.articulation == contract::Articulation::Strum ? PerformanceMode::kStrum
         : c.articulation == contract::Articulation::Arp ? PerformanceMode::kArp
         : PerformanceMode::kChord;
  s.play_trigger = static_cast<PlayTrigger>(c.play_trigger);
  s.arp_rate = c.arp_rate; s.arp_pattern = c.arp_pattern; s.arp_octaves = c.arp_octaves;
  s.arp_gate_percent = c.arp_gate_percent; s.strum_rhythm = c.strum_rhythm;
  s.strum_ms = c.strum_feel; s.swing_percent = c.swing; s.humanize_ms = c.humanize;
  s.solo_velocity = c.solo_velocity; s.solo_note_length_ms = c.solo_note_length_ms;
  s.velocity_random = c.velocity_random; s.accent_pattern = c.accent_pattern;
  s.voice_leading_complexity = c.voice_leading_complexity;
  s.midi_channel = c.chord_channel; s.midi_channel_joy = c.lead_channel;
  s.usb_host_midi_enabled = c.usb_role == contract::UsbRole::Host;
  s.usb_device_midi_enabled = c.usb_role == contract::UsbRole::Device;
  s.ble_midi_enabled = c.ble_enabled; s.voice_leading = c.voice_leading;
  s.voicing_spread = c.voicing_spread; s.bend_range_semitones = c.bend_range_semitones;
  s.joystick_rotation = c.joystick_rotation; s.joystick_flip_h = c.joystick_flip_h; s.joystick_flip_v = c.joystick_flip_v;
  s.clock_mode = static_cast<uint8_t>(c.clock_source);
  s.magic = 0x4F334348; s.version = 1; s.size = sizeof(Settings);
  return true;
}

}  // namespace app::config
