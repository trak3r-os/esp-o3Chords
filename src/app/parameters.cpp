#include "app/parameters.h"
#include "app/config_codec.h"
#include "app/types.h"
#include "engine/modifier.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace app::parameters {
namespace {
constexpr Descriptor kDescriptors[] = {
  {contract::ParamId::Velocity, "Velocity", "", 1, 127, 1, true, contract::ParameterFilter::Chord},
  {contract::ParamId::Duration, "Duration", "ms", 20, 4000, 10, true, contract::ParameterFilter::Chord},
  {contract::ParamId::Key, "Key", "", 0, 11, 1, false, contract::ParameterFilter::Chord},
  {contract::ParamId::Scale, "Scale", "", 0, 21, 1, false, contract::ParameterFilter::Chord},
  {contract::ParamId::Articulation, "Articulation", "", 0, 2, 1, true, contract::ParameterFilter::Chord, true, false, true},
  {contract::ParamId::ArpRate, "Arp rate", "", 0, 3, 1, true, contract::ParameterFilter::Chord, true, false, true},
  {contract::ParamId::ArpPattern, "Arp pattern", "", 0, 6, 1, false, contract::ParameterFilter::Chord, true, false, true},
  {contract::ParamId::ArpOctaves, "Arp octaves", "", 1, 4, 1, true, contract::ParameterFilter::Chord},
  {contract::ParamId::StrumFeel, "Strum feel", "", 0, 4, 1, true, contract::ParameterFilter::Chord},
  {contract::ParamId::Swing, "Swing", "%", 0, 100, 1, true, contract::ParameterFilter::Chord},
  {contract::ParamId::Humanize, "Humanize", "ms", 0, 40, 1, false, contract::ParameterFilter::Chord},
  {contract::ParamId::Voicing, "Voicing", "", 0, 7, 1, false, contract::ParameterFilter::Chord},
  {contract::ParamId::ChordChannel, "Chord channel", "", 0, 15, 1, false, contract::ParameterFilter::System},
  // "Recipe" walks the factory presets; the family is implied by the preset,
  // since this hardware has no joystick button to select a family
  // separately. Declared first among the Joystick-filtered rows (row order in
  // the menu follows this array's order) so it always renders as row 0: the
  // encoder always starts on Recipe.
  {contract::ParamId::JoystickRecipe, "Joystick recipe", "", 0, static_cast<int32_t>(app::PresetId::kCount) - 1, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::LeadChannel, "Lead channel", "", 0, 15, 1, false, contract::ParameterFilter::Joystick},
  // Vestigial JoystickAssignment fields (no preset uses them): still
  // readable/mutable for whatever addresses them directly (commands.cpp),
  // but hidden from the menu itself by visible() below — no screen shows
  // them, and CONT's own x_cc/y_cc/curve/deadzone supersede the
  // CC-target/curve pair.
  {contract::ParamId::JoystickXTarget, "Joystick X", "", 0, 255, 1, false, contract::ParameterFilter::Joystick},
  {contract::ParamId::JoystickYTarget, "Joystick Y", "", 0, 255, 1, false, contract::ParameterFilter::Joystick},
  {contract::ParamId::JoystickCurve, "Joystick curve", "", 0, 255, 1, false, contract::ParameterFilter::Joystick},
  // Applies regardless of which family/preset is active, so it belongs with
  // the other global input toggles (System), not inside any one family's
  // edit-menu block.
  {contract::ParamId::KeypadAsJoy, "Keypad as joy", "", 0, 1, 1, false, contract::ParameterFilter::System},
  {contract::ParamId::ClockSource, "Clock", "", 0, 2, 1, false, contract::ParameterFilter::System},
  {contract::ParamId::UsbRole, "USB role", "", 0, 2, 1, false, contract::ParameterFilter::System},
  {contract::ParamId::BleEnabled, "BLE", "", 0, 1, 1, false, contract::ParameterFilter::System},
  {contract::ParamId::Advanced, "Advanced", "", 0, 1, 1, false, contract::ParameterFilter::System, false, true},
  {contract::ParamId::Bpm, "BPM", "", 20, 300, 1, true, contract::ParameterFilter::Chord},
  {contract::ParamId::JoystickInvertX, "Joystick invert X", "", 0, 1, 1, false, contract::ParameterFilter::System},
  {contract::ParamId::JoystickInvertY, "Joystick invert Y", "", 0, 1, 1, false, contract::ParameterFilter::System},
  // Rows for the Config fields the play screen and NOTES edit menu
  // reference. Other Config fields (arp_gate_percent,
  // voice_leading_complexity, solo_note_length_ms, velocity_random,
  // accent_pattern, strum_rhythm, bend_range_semitones) stay row-less — no
  // screen prints them.
  {contract::ParamId::VoiceLeading, "Voice leading", "", 0, 2, 1, true, contract::ParameterFilter::Chord, true, false, true},
  {contract::ParamId::Roll, "Roll", "ms", 0, 60, 1, true, contract::ParameterFilter::Chord},
  {contract::ParamId::RollDir, "Roll direction", "", 0, 3, 1, false, contract::ParameterFilter::Chord, true, false, true},
  {contract::ParamId::PlayTrigger, "Trigger", "", 0, 2, 1, true, contract::ParameterFilter::Chord, true, false, true},
  {contract::ParamId::SoloVelocity, "Vel", "", 1, 127, 1, false, contract::ParameterFilter::Joystick},
  // Edit menu: one row per JoyFamilyState field, gated to the active family
  // by visible() (see family_for_param() below) so only one family's block
  // ever shows.
  {contract::ParamId::NotesSource, "Source", "", 0, 4, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::NotesGesture, "Gesture", "", 0, 1, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::NotesVoices, "Voices", "", 0, 1, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::NotesSustain, "Sustain", "", 0, 1, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::NotesMono, "Mono", "", 0, 1, 1, false, contract::ParameterFilter::Joystick},
  {contract::ParamId::NotesOctave, "Octave", "", -2, 2, 1, false, contract::ParameterFilter::Joystick},
  {contract::ParamId::NotesVelMode, "Vel mode", "", 0, 1, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::NotesChannel, "Channel", "", 0, 1, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::ChordPalette, "Palette", "", 0, static_cast<int32_t>(app::kQualityMapCount) - 1, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  // Order and Retrigger are per-preset constants — kept readable/mutable
  // (config_codec.cpp's migration, e.g.) but hidden from the menu by
  // visible() below, same treatment as the vestigial JoystickAssignment
  // fields above.
  {contract::ParamId::ChordOrder, "Order", "", 0, 1, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::ChordRetrigger, "Retrigger", "", 0, 1, 1, false, contract::ParameterFilter::Joystick},
  {contract::ParamId::ContXTarget, "X target", "", 0, 3, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::ContYTarget, "Y target", "", 0, 3, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::ContXCc, "X CC", "", 0, 127, 1, false, contract::ParameterFilter::Joystick},
  {contract::ParamId::ContYCc, "Y CC", "", 0, 127, 1, false, contract::ParameterFilter::Joystick},
  {contract::ParamId::ContBendSnap, "Bend snap", "", 0, 2, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::ContCurve, "Curve", "", 0, 2, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::ContDeadzone, "Deadzone", "%", 0, 100, 1, false, contract::ParameterFilter::Joystick},
  {contract::ParamId::ParamUpDownTarget, "^v Target", "", 0, 19, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::ParamUpDownRole, "^v Role", "", 0, 1, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::ParamUpDownHold, "^v Hold", "", 0, 1, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::ParamUpDownAmount, "^v Amount", "", 0, 1, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::ParamLeftRightTarget, "<> Target", "", 0, 19, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::ParamLeftRightRole, "<> Role", "", 0, 1, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::ParamLeftRightHold, "<> Hold", "", 0, 1, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::ParamLeftRightAmount, "<> Amount", "", 0, 1, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::ParamNwSeTarget, "NW Target", "", 0, 19, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::ParamNwSeRole, "NW Role", "", 0, 1, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::ParamNwSeHold, "NW Hold", "", 0, 1, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::ParamNwSeAmount, "NW Amount", "", 0, 1, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::ParamNeSwTarget, "NE Target", "", 0, 19, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::ParamNeSwRole, "NE Role", "", 0, 1, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::ParamNeSwHold, "NE Hold", "", 0, 1, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::ParamNeSwAmount, "NE Amount", "", 0, 1, 1, false, contract::ParameterFilter::Joystick, true, false, true},
  {contract::ParamId::ParamRetrigger, "Retrigger", "", 0, 1, 1, false, contract::ParameterFilter::Joystick},
};

// Family-scoped params (NOTES/CHORD/CONT/PARAM edit-menu rows) are gated by
// visible() to the Config's active family, so only one family's block shows
// at a time. Non-family Joystick rows (Recipe, Lead channel, Solo vel, the
// legacy Joystick X/Y/curve trio) return false here and stay universally
// visible under the Joystick filter.
bool family_for_param(contract::ParamId id, app::JoyFamily& out) {
  using P = contract::ParamId;
  if (id >= P::NotesSource && id <= P::NotesChannel) { out = app::JoyFamily::kNotes; return true; }
  if (id >= P::ChordPalette && id <= P::ChordRetrigger) { out = app::JoyFamily::kChord; return true; }
  if (id >= P::ContXTarget && id <= P::ContDeadzone) { out = app::JoyFamily::kCont; return true; }
  if (id >= P::ParamUpDownTarget && id <= P::ParamRetrigger) { out = app::JoyFamily::kParam; return true; }
  return false;
}

const char* preset_name(int32_t value) {
  return value >= 0 && value < static_cast<int32_t>(app::PresetId::kCount)
             ? app::kPresets[value].name : "Unknown";
}

const char* arp_rate_name(int32_t value) {
  static constexpr const char* kNames[] = {"Quarter", "Eighth", "Sixteenth", "Triplet"};
  return value >= 0 && value < 4 ? kNames[value] : "Unknown";
}

const char* arp_pattern_name(int32_t value) {
  static constexpr const char* kNames[] = {
      "Up", "Down", "Up-Down", "Down-Up", "Up-Down Inclusive", "Random", "Played Order"};
  return value >= 0 && value < 7 ? kNames[value] : "Unknown";
}

// Name tables for the JoyFamilyState enums. Each mirrors its enum's
// declaration order in joy_family.h exactly, so a value equals its index into
// these arrays.
const char* name_from(int32_t value, std::initializer_list<const char*> names) {
  const auto n = static_cast<int32_t>(names.size());
  return value >= 0 && value < n ? *(names.begin() + value) : "Unknown";
}
const char* note_source_name(int32_t v) { return name_from(v, {"Degrees", "Scale", "Chord tones", "Intervals", "Pentatonic"}); }
const char* note_gesture_name(int32_t v) { return name_from(v, {"Clock", "Walk"}); }
const char* note_voices_name(int32_t v) { return name_from(v, {"Note", "Chord"}); }
const char* note_sustain_name(int32_t v) { return name_from(v, {"Momentary", "Latch"}); }
const char* note_velmode_name(int32_t v) { return name_from(v, {"Fixed", "Distance"}); }
const char* note_channel_name(int32_t v) { return name_from(v, {"Chord", "Lead"}); }
const char* chord_order_name(int32_t v) { return name_from(v, {"Fixed", "Ranking"}); }
const char* chord_palette_name(int32_t v) {
  return v >= 0 && v < static_cast<int32_t>(app::kQualityMapCount) ? app::kQualityMaps[v].name : "Unknown";
}
const char* cont_target_name(int32_t v) { return name_from(v, {"Bend", "Vibrato", "CC", "Aftertouch"}); }
const char* cont_bendsnap_name(int32_t v) { return name_from(v, {"Chord tones", "Chromatic", "Off"}); }
const char* cont_curve_name(int32_t v) { return name_from(v, {"Linear", "Exp", "Log"}); }
const char* axis_target_name(int32_t v) {
  return name_from(v, {"None", "Style", "Trigger", "Voicing", "Arp pattern", "Arp rate", "Arp octaves",
                        "Arp gate", "Strum feel", "Swing", "Humanize", "Velocity", "Duration",
                        "Accent", "Vel random", "Voice leading", "VL complexity", "Solo vel",
                        "Solo length", "Expression CC"});
}
const char* axis_role_name(int32_t v) { return name_from(v, {"Discrete", "Continuous"}); }
const char* axis_hold_name(int32_t v) { return name_from(v, {"Momentary", "Sticky"}); }
const char* axis_amount_name(int32_t v) { return name_from(v, {"Step", "Distance"}); }

}

// AxisTarget -> ParamId for the tier-1 targets already in the registry.
// Lets a PARAM axis's value format through the real parameters::format()
// (names, units) instead of printing a bare integer.
bool axis_target_param_id(app::AxisTarget target, contract::ParamId& out) {
  using AT = app::AxisTarget;
  using P = contract::ParamId;
  switch (target) {
    case AT::kArticulation: out = P::Articulation; return true;
    case AT::kPlayTrigger:  out = P::PlayTrigger; return true;
    case AT::kVoicing:      out = P::Voicing; return true;
    case AT::kArpPattern:   out = P::ArpPattern; return true;
    case AT::kArpRate:      out = P::ArpRate; return true;
    case AT::kArpOctaves:   out = P::ArpOctaves; return true;
    case AT::kStrumFeel:    out = P::StrumFeel; return true;
    case AT::kSwing:        out = P::Swing; return true;
    case AT::kHumanize:     out = P::Humanize; return true;
    case AT::kVelocity:     out = P::Velocity; return true;
    case AT::kDuration:     out = P::Duration; return true;
    case AT::kVoiceLeading: out = P::VoiceLeading; return true;
    case AT::kSoloVelocity: out = P::SoloVelocity; return true;
    default: return false;  // tier-2: no ParamId (see format_axis_tier2_value).
  }
}

// Tier-2 targets exist in Config, persisted and sanitized, but have no
// ParamId/menu row — format the raw value directly instead of printing it
// unlabeled.
void format_axis_tier2_value(app::AxisTarget target, int32_t value, char* buf, size_t len) {
  using AT = app::AxisTarget;
  switch (target) {
    case AT::kArpGate: std::snprintf(buf, len, "%ld%%", static_cast<long>(value)); break;
    case AT::kAccentPattern: {
      static constexpr const char* kNames[] = {"Off", "Downbeat", "Backbeat", "Every 4th"};
      std::snprintf(buf, len, "%s", (value >= 0 && value < 4) ? kNames[value] : "?");
      break;
    }
    case AT::kVelocityRandom: std::snprintf(buf, len, "%ld", static_cast<long>(value)); break;
    case AT::kVoiceLeadingComplexity: std::snprintf(buf, len, "%ld%%", static_cast<long>(value)); break;
    case AT::kSoloNoteLength: std::snprintf(buf, len, "%ldms", static_cast<long>(value)); break;
    case AT::kExpressionCc: std::snprintf(buf, len, "CC"); break;
    default: std::snprintf(buf, len, "%ld", static_cast<long>(value)); break;
  }
}
const Descriptor* all(size_t& count) { count = sizeof(kDescriptors) / sizeof(kDescriptors[0]); return kDescriptors; }
const Descriptor* find(contract::ParamId id) { size_t n; const auto* ds = all(n); for (size_t i = 0; i < n; ++i) if (ds[i].id == id) return &ds[i]; return nullptr; }
int32_t clamp(const Descriptor& d, int32_t value) { return std::clamp(value, d.min, d.max); }
contract::Command set_command(const Descriptor& d, int32_t value) { return {contract::CommandKind::SetParameter, d.id, clamp(d, value), {}}; }

ReadResult read(const contract::Config& c, contract::ParamId id) {
  using P = contract::ParamId;
  switch (id) {
    case P::Velocity: return {true, c.velocity}; case P::Duration: return {true, c.duration_ms};
    case P::Key: return {true, c.key}; case P::Scale: return {true, c.scale};
    case P::Articulation: return {true, static_cast<int32_t>(c.articulation)};
    case P::ArpRate: return {true, c.arp_rate}; case P::ArpPattern: return {true, c.arp_pattern};
    case P::ArpOctaves: return {true, c.arp_octaves}; case P::StrumFeel: return {true, c.strum_feel};
    case P::Swing: return {true, c.swing}; case P::Humanize: return {true, c.humanize};
    case P::Voicing: return {true, c.voicing_spread};
    case P::ChordChannel: return {true, c.chord_channel}; case P::LeadChannel: return {true, c.lead_channel};
    case P::JoystickRecipe: return {true, static_cast<int32_t>(c.active_preset)};
    case P::JoystickXTarget: return {true, c.joystick.x_target}; case P::JoystickYTarget: return {true, c.joystick.y_target};
    case P::JoystickCurve: return {true, c.joystick.curve}; case P::KeypadAsJoy: return {true, c.joystick.keypad_as_joy ? 1 : 0}; case P::ClockSource: return {true, static_cast<int32_t>(c.clock_source)};
    case P::UsbRole: return {true, static_cast<int32_t>(c.usb_role)}; case P::BleEnabled: return {true, c.ble_enabled ? 1 : 0};
    case P::Bpm: return {true, static_cast<int32_t>(std::lround(c.bpm))};
    case P::JoystickInvertX: return {true, c.joystick_flip_h ? 1 : 0};
    case P::JoystickInvertY: return {true, c.joystick_flip_v ? 1 : 0};
    case P::VoiceLeading: return {true, c.voice_leading};
    case P::Roll: return {true, c.roll_ms};
    case P::RollDir: return {true, c.roll_dir};
    case P::PlayTrigger: return {true, c.play_trigger};
    case P::SoloVelocity: return {true, c.solo_velocity};
    case P::NotesSource: return {true, static_cast<int32_t>(c.joy_families.notes.source)};
    case P::NotesGesture: return {true, static_cast<int32_t>(c.joy_families.notes.gesture)};
    case P::NotesVoices: return {true, static_cast<int32_t>(c.joy_families.notes.voices)};
    case P::NotesSustain: return {true, static_cast<int32_t>(c.joy_families.notes.sustain)};
    case P::NotesMono: return {true, c.joy_families.notes.mono ? 1 : 0};
    case P::NotesOctave: return {true, c.joy_families.notes.octave};
    case P::NotesVelMode: return {true, static_cast<int32_t>(c.joy_families.notes.velocity_mode)};
    case P::NotesChannel: return {true, static_cast<int32_t>(c.joy_families.notes.channel)};
    case P::ChordPalette: return {true, c.joy_families.chord.palette};
    case P::ChordOrder: return {true, static_cast<int32_t>(c.joy_families.chord.order)};
    case P::ChordRetrigger: return {true, c.joy_families.chord.retrigger ? 1 : 0};
    case P::ContXTarget: return {true, static_cast<int32_t>(c.joy_families.cont.x_target)};
    case P::ContYTarget: return {true, static_cast<int32_t>(c.joy_families.cont.y_target)};
    case P::ContXCc: return {true, c.joy_families.cont.x_cc};
    case P::ContYCc: return {true, c.joy_families.cont.y_cc};
    case P::ContBendSnap: return {true, static_cast<int32_t>(c.joy_families.cont.bend_snap)};
    case P::ContCurve: return {true, c.joy_families.cont.curve};
    case P::ContDeadzone: return {true, c.joy_families.cont.deadzone};
    case P::ParamUpDownTarget: return {true, static_cast<int32_t>(c.joy_families.param.up_down.target)};
    case P::ParamUpDownRole: return {true, static_cast<int32_t>(c.joy_families.param.up_down.role)};
    case P::ParamUpDownHold: return {true, static_cast<int32_t>(c.joy_families.param.up_down.hold)};
    case P::ParamUpDownAmount: return {true, static_cast<int32_t>(c.joy_families.param.up_down.amount)};
    case P::ParamLeftRightTarget: return {true, static_cast<int32_t>(c.joy_families.param.left_right.target)};
    case P::ParamLeftRightRole: return {true, static_cast<int32_t>(c.joy_families.param.left_right.role)};
    case P::ParamLeftRightHold: return {true, static_cast<int32_t>(c.joy_families.param.left_right.hold)};
    case P::ParamLeftRightAmount: return {true, static_cast<int32_t>(c.joy_families.param.left_right.amount)};
    case P::ParamNwSeTarget: return {true, static_cast<int32_t>(c.joy_families.param.nw_se.target)};
    case P::ParamNwSeRole: return {true, static_cast<int32_t>(c.joy_families.param.nw_se.role)};
    case P::ParamNwSeHold: return {true, static_cast<int32_t>(c.joy_families.param.nw_se.hold)};
    case P::ParamNwSeAmount: return {true, static_cast<int32_t>(c.joy_families.param.nw_se.amount)};
    case P::ParamNeSwTarget: return {true, static_cast<int32_t>(c.joy_families.param.ne_sw.target)};
    case P::ParamNeSwRole: return {true, static_cast<int32_t>(c.joy_families.param.ne_sw.role)};
    case P::ParamNeSwHold: return {true, static_cast<int32_t>(c.joy_families.param.ne_sw.hold)};
    case P::ParamNeSwAmount: return {true, static_cast<int32_t>(c.joy_families.param.ne_sw.amount)};
    case P::ParamRetrigger: return {true, c.joy_families.param.retrigger_on_change ? 1 : 0};
    default: return {};
  }
}

bool visible(const contract::Config& c, contract::ParamId id, contract::ParameterFilter filter, bool /*advanced*/) {
  const auto* d = find(id);
  if (!d || d->action || d->filter != filter) return false;
  if (filter == contract::ParameterFilter::Joystick) {
    using P = contract::ParamId;
    // Vestigial JoystickAssignment fields — see their kDescriptors comment.
    if (id == P::JoystickXTarget || id == P::JoystickYTarget || id == P::JoystickCurve) return false;
    // Order and Retrigger: per-preset constants, not menu rows — see their
    // kDescriptors comment.
    if (id == P::ChordOrder || id == P::ChordRetrigger) return false;
    app::JoyFamily field_family;
    if (family_for_param(id, field_family)) {
      const auto active = app::kPresets[static_cast<size_t>(c.active_preset)].family;
      if (field_family != active) return false;
    }
  }
  return read(c, id).applicable;
}

bool mutate(contract::Config& c, contract::ParamId id, int32_t delta) {
  const auto* d = find(id); if (!d || !d->editable || d->action || delta == 0) return false;
  const auto r = read(c, id); if (!r.applicable) return false;
  int32_t next = r.value + delta * d->step;
  if (d->wraps) {
    if (next > d->max) next = d->min;
    if (next < d->min) next = d->max;
  } else next = clamp(*d, next);
  if (id == contract::ParamId::JoystickRecipe) {
    c.active_preset = static_cast<app::PresetId>(next);
    app::config::recall_preset(c.joy_families, c.active_preset);
  } else switch (id) {
    case contract::ParamId::Velocity: c.velocity = next; break; case contract::ParamId::Duration: c.duration_ms = next; break;
    case contract::ParamId::Key: c.key = next; break; case contract::ParamId::Scale: c.scale = next; break;
    case contract::ParamId::Articulation: c.articulation = static_cast<contract::Articulation>(next); break;
    case contract::ParamId::ArpRate: c.arp_rate = next; break; case contract::ParamId::ArpPattern: c.arp_pattern = next; break;
    case contract::ParamId::ArpOctaves: c.arp_octaves = next; break; case contract::ParamId::StrumFeel: c.strum_feel = next; break;
    case contract::ParamId::Swing: c.swing = next; break; case contract::ParamId::Humanize: c.humanize = next; break;
    case contract::ParamId::Voicing: c.voicing_spread = next; break;
    case contract::ParamId::ChordChannel: c.chord_channel = next; break; case contract::ParamId::LeadChannel: c.lead_channel = next; break;
    case contract::ParamId::JoystickXTarget: c.joystick.x_target = next; break; case contract::ParamId::JoystickYTarget: c.joystick.y_target = next; break;
    case contract::ParamId::JoystickCurve: c.joystick.curve = next; break; case contract::ParamId::KeypadAsJoy: c.joystick.keypad_as_joy = next != 0; break; case contract::ParamId::ClockSource: c.clock_source = static_cast<contract::ClockSource>(next); break;
    case contract::ParamId::UsbRole: c.usb_role = static_cast<contract::UsbRole>(next); break; case contract::ParamId::BleEnabled: c.ble_enabled = next != 0; break;
    case contract::ParamId::Bpm: c.bpm = static_cast<float>(next); break;
    case contract::ParamId::JoystickInvertX: c.joystick_flip_h = next != 0; break;
    case contract::ParamId::JoystickInvertY: c.joystick_flip_v = next != 0; break;
    case contract::ParamId::VoiceLeading: c.voice_leading = next; break;
    case contract::ParamId::Roll: c.roll_ms = next; break;
    case contract::ParamId::RollDir: c.roll_dir = next; break;
    case contract::ParamId::PlayTrigger: c.play_trigger = next; break;
    case contract::ParamId::SoloVelocity: c.solo_velocity = next; break;
    case contract::ParamId::NotesSource: c.joy_families.notes.source = static_cast<app::NoteSource>(next); break;
    case contract::ParamId::NotesGesture: c.joy_families.notes.gesture = static_cast<app::NoteGesture>(next); break;
    case contract::ParamId::NotesVoices: c.joy_families.notes.voices = static_cast<app::NoteVoices>(next); break;
    case contract::ParamId::NotesSustain: c.joy_families.notes.sustain = static_cast<app::NoteSustain>(next); break;
    case contract::ParamId::NotesMono: c.joy_families.notes.mono = next != 0; break;
    case contract::ParamId::NotesOctave: c.joy_families.notes.octave = static_cast<int8_t>(next); break;
    case contract::ParamId::NotesVelMode: c.joy_families.notes.velocity_mode = static_cast<app::NoteVelocityMode>(next); break;
    case contract::ParamId::NotesChannel: c.joy_families.notes.channel = static_cast<app::NoteChannel>(next); break;
    case contract::ParamId::ChordPalette: c.joy_families.chord.palette = next; break;
    case contract::ParamId::ChordOrder: c.joy_families.chord.order = static_cast<app::ChordOrder>(next); break;
    case contract::ParamId::ChordRetrigger: c.joy_families.chord.retrigger = next != 0; break;
    case contract::ParamId::ContXTarget: c.joy_families.cont.x_target = static_cast<app::ContTarget>(next); break;
    case contract::ParamId::ContYTarget: c.joy_families.cont.y_target = static_cast<app::ContTarget>(next); break;
    case contract::ParamId::ContXCc: c.joy_families.cont.x_cc = next; break;
    case contract::ParamId::ContYCc: c.joy_families.cont.y_cc = next; break;
    case contract::ParamId::ContBendSnap: c.joy_families.cont.bend_snap = static_cast<app::ContBendSnap>(next); break;
    case contract::ParamId::ContCurve: c.joy_families.cont.curve = next; break;
    case contract::ParamId::ContDeadzone: c.joy_families.cont.deadzone = next; break;
    case contract::ParamId::ParamUpDownTarget: c.joy_families.param.up_down.target = static_cast<app::AxisTarget>(next); break;
    case contract::ParamId::ParamUpDownRole: c.joy_families.param.up_down.role = static_cast<app::AxisRole>(next); break;
    case contract::ParamId::ParamUpDownHold: c.joy_families.param.up_down.hold = static_cast<app::AxisHold>(next); break;
    case contract::ParamId::ParamUpDownAmount: c.joy_families.param.up_down.amount = static_cast<app::AxisAmount>(next); break;
    case contract::ParamId::ParamLeftRightTarget: c.joy_families.param.left_right.target = static_cast<app::AxisTarget>(next); break;
    case contract::ParamId::ParamLeftRightRole: c.joy_families.param.left_right.role = static_cast<app::AxisRole>(next); break;
    case contract::ParamId::ParamLeftRightHold: c.joy_families.param.left_right.hold = static_cast<app::AxisHold>(next); break;
    case contract::ParamId::ParamLeftRightAmount: c.joy_families.param.left_right.amount = static_cast<app::AxisAmount>(next); break;
    case contract::ParamId::ParamNwSeTarget: c.joy_families.param.nw_se.target = static_cast<app::AxisTarget>(next); break;
    case contract::ParamId::ParamNwSeRole: c.joy_families.param.nw_se.role = static_cast<app::AxisRole>(next); break;
    case contract::ParamId::ParamNwSeHold: c.joy_families.param.nw_se.hold = static_cast<app::AxisHold>(next); break;
    case contract::ParamId::ParamNwSeAmount: c.joy_families.param.nw_se.amount = static_cast<app::AxisAmount>(next); break;
    case contract::ParamId::ParamNeSwTarget: c.joy_families.param.ne_sw.target = static_cast<app::AxisTarget>(next); break;
    case contract::ParamId::ParamNeSwRole: c.joy_families.param.ne_sw.role = static_cast<app::AxisRole>(next); break;
    case contract::ParamId::ParamNeSwHold: c.joy_families.param.ne_sw.hold = static_cast<app::AxisHold>(next); break;
    case contract::ParamId::ParamNeSwAmount: c.joy_families.param.ne_sw.amount = static_cast<app::AxisAmount>(next); break;
    case contract::ParamId::ParamRetrigger: c.joy_families.param.retrigger_on_change = next != 0; break;
    default: return false;
  }
  return true;
}

bool format(const contract::Config& c, contract::ParamId id, char* label, size_t ll, char* value, size_t vl) {
  const auto* d = find(id); const auto r = read(c, id); if (!d || !r.applicable || !label || !value || !ll || !vl) return false;
  std::snprintf(label, ll, "%s", d->label);
  if (id == contract::ParamId::Key) std::snprintf(value, vl, "%s", c.key_name);
  else if (id == contract::ParamId::Scale) std::snprintf(value, vl, "%s", c.scale_name);
  else if (id == contract::ParamId::ChordChannel || id == contract::ParamId::LeadChannel) std::snprintf(value, vl, "%ld", static_cast<long>(r.value + 1));
  else if (id == contract::ParamId::Bpm) std::snprintf(value, vl, "%ld BPM", static_cast<long>(r.value));
  else if (id == contract::ParamId::Articulation) std::snprintf(value, vl, "%s", r.value == 0 ? "Chord" : r.value == 1 ? "Strum" : "Arp");
  else if (id == contract::ParamId::Voicing) std::snprintf(value, vl, "%s",
      engine::voicing_spread_name(static_cast<engine::VoicingSpread>(std::clamp<int32_t>(r.value, 0, 7))));
  else if (id == contract::ParamId::ArpRate) std::snprintf(value, vl, "%s", arp_rate_name(r.value));
  else if (id == contract::ParamId::ArpPattern) std::snprintf(value, vl, "%s", arp_pattern_name(r.value));
  else if (id == contract::ParamId::ArpOctaves) std::snprintf(value, vl, "%ld %s", static_cast<long>(r.value), r.value == 1 ? "octave" : "octaves");
  else if (id == contract::ParamId::JoystickRecipe) std::snprintf(value, vl, "%s", preset_name(r.value));
  else if (id == contract::ParamId::KeypadAsJoy) std::snprintf(value, vl, "%s", r.value ? "Enabled" : "Disabled");
  else if (id == contract::ParamId::ClockSource) std::snprintf(value, vl, "%s", r.value == 0 ? "Internal" : r.value == 1 ? "Auto" : "External");
  else if (id == contract::ParamId::UsbRole) std::snprintf(value, vl, "%s", r.value == 0 ? "Off" : r.value == 1 ? "Device" : "Host");
  else if (id == contract::ParamId::BleEnabled) std::snprintf(value, vl, "%s", r.value ? "ON" : "OFF");
  else if (id == contract::ParamId::JoystickInvertX || id == contract::ParamId::JoystickInvertY) std::snprintf(value, vl, "%s", r.value ? "Inverted" : "Normal");
  else if (id == contract::ParamId::VoiceLeading) std::snprintf(value, vl, "%s", r.value == 0 ? "Off" : r.value == 1 ? "Smooth" : "Common");
  else if (id == contract::ParamId::RollDir) std::snprintf(value, vl, "%s", r.value == 0 ? "Up" : r.value == 1 ? "Down" : r.value == 2 ? "Out" : "In");
  else if (id == contract::ParamId::PlayTrigger) std::snprintf(value, vl, "%s", r.value == 0 ? "Shot" : r.value == 1 ? "Hold" : "Repeat");
  else if (id == contract::ParamId::NotesSource) std::snprintf(value, vl, "%s", note_source_name(r.value));
  else if (id == contract::ParamId::NotesGesture) std::snprintf(value, vl, "%s", note_gesture_name(r.value));
  else if (id == contract::ParamId::NotesVoices) std::snprintf(value, vl, "%s", note_voices_name(r.value));
  else if (id == contract::ParamId::NotesSustain) std::snprintf(value, vl, "%s", note_sustain_name(r.value));
  else if (id == contract::ParamId::NotesMono) std::snprintf(value, vl, "%s", r.value ? "On" : "Off");
  else if (id == contract::ParamId::NotesOctave) std::snprintf(value, vl, "%+ld", static_cast<long>(r.value));
  else if (id == contract::ParamId::NotesVelMode) std::snprintf(value, vl, "%s", note_velmode_name(r.value));
  else if (id == contract::ParamId::NotesChannel) std::snprintf(value, vl, "%s", note_channel_name(r.value));
  else if (id == contract::ParamId::ChordPalette) std::snprintf(value, vl, "%s", chord_palette_name(r.value));
  else if (id == contract::ParamId::ChordOrder) std::snprintf(value, vl, "%s", chord_order_name(r.value));
  else if (id == contract::ParamId::ChordRetrigger || id == contract::ParamId::ParamRetrigger)
    std::snprintf(value, vl, "%s", r.value ? "On" : "Off");
  else if (id == contract::ParamId::ContXTarget || id == contract::ParamId::ContYTarget)
    std::snprintf(value, vl, "%s", cont_target_name(r.value));
  else if (id == contract::ParamId::ContBendSnap) std::snprintf(value, vl, "%s", cont_bendsnap_name(r.value));
  else if (id == contract::ParamId::ContCurve) std::snprintf(value, vl, "%s", cont_curve_name(r.value));
  else if (id == contract::ParamId::ParamUpDownTarget || id == contract::ParamId::ParamLeftRightTarget ||
           id == contract::ParamId::ParamNwSeTarget || id == contract::ParamId::ParamNeSwTarget)
    std::snprintf(value, vl, "%s", axis_target_name(r.value));
  else if (id == contract::ParamId::ParamUpDownRole || id == contract::ParamId::ParamLeftRightRole ||
           id == contract::ParamId::ParamNwSeRole || id == contract::ParamId::ParamNeSwRole)
    std::snprintf(value, vl, "%s", axis_role_name(r.value));
  else if (id == contract::ParamId::ParamUpDownHold || id == contract::ParamId::ParamLeftRightHold ||
           id == contract::ParamId::ParamNwSeHold || id == contract::ParamId::ParamNeSwHold)
    std::snprintf(value, vl, "%s", axis_hold_name(r.value));
  else if (id == contract::ParamId::ParamUpDownAmount || id == contract::ParamId::ParamLeftRightAmount ||
           id == contract::ParamId::ParamNwSeAmount || id == contract::ParamId::ParamNeSwAmount)
    std::snprintf(value, vl, "%s", axis_amount_name(r.value));
  else std::snprintf(value, vl, "%ld%s", static_cast<long>(r.value), d->unit);
  return true;
}
}  // namespace app::parameters
