#include "app/joy_family.h"
#include "app/contracts/contracts.h"

namespace app {

const PresetInfo kPresets[static_cast<size_t>(PresetId::kCount)] = {
    {PresetId::kLead, JoyFamily::kNotes, "Lead"},
    {PresetId::kBass, JoyFamily::kNotes, "Bass"},
    {PresetId::kDrone, JoyFamily::kNotes, "Drone"},
    {PresetId::kColors, JoyFamily::kChord, "Colors"},
    {PresetId::kDiscover, JoyFamily::kChord, "Discover"},
    {PresetId::kVoicings, JoyFamily::kChord, "Voicings"},
    {PresetId::kBend, JoyFamily::kCont, "Bend"},
    {PresetId::kMacro, JoyFamily::kCont, "Macro"},
    // {PresetId::kFlip, JoyFamily::kParam, "Flip"},  // Disabled.
};

namespace {

NotesParams notes_lead() {
  NotesParams p;
  p.source = NoteSource::kScale;
  p.gesture = NoteGesture::kWalk;
  p.voices = NoteVoices::kNote;
  p.sustain = NoteSustain::kMomentary;
  p.velocity_mode = NoteVelocityMode::kByDistance;
  p.channel = NoteChannel::kLead;
  return p;
}

NotesParams notes_bass() {
  NotesParams p;
  p.source = NoteSource::kScale;
  p.gesture = NoteGesture::kClock;
  p.octave = -2;
  p.mono = true;
  return p;
}

NotesParams notes_drone() {
  NotesParams p;
  p.source = NoteSource::kDegrees;
  p.gesture = NoteGesture::kClock;
  p.voices = NoteVoices::kChord;
  p.sustain = NoteSustain::kLatch;
  return p;
}

ChordParams chord_colors() {
  ChordParams p;
  p.palette = 0;  // kQualityMaps[0] == "CLSC"
  p.order = ChordOrder::kFixed;
  return p;
}

ChordParams chord_discover() {
  ChordParams p;
  // Dispatch (input_joystick.cpp) reorders palette qualities by
  // consonance/tension/distance when order=kRanking, so any palette works.
  // "7THS" gives Discover a richer, more chromatic set to rank than the CLSC
  // default, which suits exploration better.
  p.palette = 1;  // kQualityMaps[1] == "7THS"
  p.order = ChordOrder::kRanking;
  return p;
}

ChordParams chord_voicings() {
  ChordParams p;
  // Direction selects the voicing for the next numbered-key chord.
  return p;
}

ContParams cont_bend() {
  ContParams p;
  p.x_target = ContTarget::kBend;
  p.y_target = ContTarget::kVibrato;
  p.bend_snap = ContBendSnap::kChordTones;
  return p;
}

ContParams cont_macro() {
  ContParams p;
  p.x_target = ContTarget::kCc;
  p.y_target = ContTarget::kCc;
  return p;
}

}  // namespace

bool joystick_push_param(PresetId preset, contract::ParamId& out) {
  using P = contract::ParamId;
  switch (preset) {
    // Source is the one parameter that means the same thing in all three
    // NOTES presets. Drone's own defining field, Voices, lives in the menu
    // rather than on the push slot.
    case PresetId::kLead: case PresetId::kBass: case PresetId::kDrone:
      out = P::NotesSource; return true;
    // Voicings has no field of its own to push (order/retrigger are
    // per-preset constants, hidden from the menu); Palette only matters to
    // Colors and Discover.
    case PresetId::kColors: case PresetId::kDiscover:
      out = P::ChordPalette; return true;
    default: return false;  // Voicings, CONT and PARAM: no single parameter beats the rest.
  }
}

JoyFamilyState preset_defaults(PresetId id) {
  JoyFamilyState s;
  switch (id) {
    case PresetId::kLead:     s.notes = notes_lead(); break;
    case PresetId::kBass:     s.notes = notes_bass(); break;
    case PresetId::kDrone:    s.notes = notes_drone(); break;
    case PresetId::kColors:   s.chord = chord_colors(); break;
    case PresetId::kDiscover: s.chord = chord_discover(); break;
    case PresetId::kVoicings: s.chord = chord_voicings(); break;
    case PresetId::kBend:     s.cont = cont_bend(); break;
    case PresetId::kMacro:    s.cont = cont_macro(); break;
    default: break;
  }
  return s;
}

void apply_legacy_harmony_override(NotesParams& notes) {
  notes.voices = NoteVoices::kNote;
  notes.mono = true;
  notes.channel = NoteChannel::kLead;
}

}  // namespace app
