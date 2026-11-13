#pragma once

// Stable application boundaries.  These types deliberately contain no UI,
// engine, transport, or platform headers.

#include <array>
#include <cstdint>

#include "app/joy_family.h"

namespace app::contract {

enum class InputKind : uint8_t { Press, Release, Axis, EncoderStep, EncoderPress };
enum class Control : uint8_t { Keypad, Panic, JoystickX, JoystickY, Encoder, Confirm, Back };
struct InputEvent { InputKind kind{}; Control control{}; int16_t value{}; uint32_t timestamp{}; };

// Joystick recipe identities, still carried in the wire format's recipe_id
// byte.  The first six values are retained for v2 wire compatibility;
// preset_for_recipe_v2() maps every recipe onto the preset that covers it.
enum class RecipeId : uint8_t {
  Harmony, Expression, Melody, Drone, Euclidean, BassClock,
  Quality, ChordVel, Bend, Discover, ArpControl, Interval,
  Rhythm, Pentatonic, Voicing, StrumFeel, ScaleExplore,
  COUNT
};
enum class Articulation : uint8_t { Chord, Strum, Arp };
enum class UsbRole : uint8_t { Off, Device, Host };
enum class ClockSource : uint8_t { Internal, Auto, External };
enum class ParamId : uint16_t {
  Velocity, Duration, Key, Scale, Articulation, ArpRate, ArpPattern, ArpOctaves,
  StrumFeel, Swing, Humanize, ChordChannel, LeadChannel, JoystickRecipe,
  JoystickXTarget, JoystickYTarget, JoystickCurve, KeypadAsJoy, ClockSource, UsbRole,
  BleEnabled, Advanced, Bpm, JoystickInvertX, JoystickInvertY, Voicing,
  // Rows for Config fields shown on the play screen and in the edit menu.
  VoiceLeading, Roll, RollDir, PlayTrigger, SoloVelocity,
  // Edit menu: one ParamId per JoyFamilyState field, all Joystick-filtered.
  // parameters::visible() additionally gates each on whether the Config's
  // active preset belongs to that field's family, so only the active family's
  // rows ever show in the edit menu at once.
  NotesSource, NotesGesture, NotesVoices, NotesSustain, NotesMono, NotesOctave,
  NotesVelMode, NotesChannel,
  // CHORD: direction changes are applied by the next numbered-key chord, and
  // which behavior a direction produces is fixed by preset identity rather
  // than a field. Order and Retrigger stay readable/mutable but are hidden
  // from the menu — see parameters.cpp.
  ChordPalette, ChordOrder, ChordRetrigger,
  ContXTarget, ContYTarget, ContXCc, ContYCc, ContBendSnap, ContCurve, ContDeadzone,
  ParamUpDownTarget, ParamUpDownRole, ParamUpDownHold, ParamUpDownAmount,
  ParamLeftRightTarget, ParamLeftRightRole, ParamLeftRightHold, ParamLeftRightAmount,
  ParamNwSeTarget, ParamNwSeRole, ParamNwSeHold, ParamNwSeAmount,
  ParamNeSwTarget, ParamNeSwRole, ParamNeSwHold, ParamNeSwAmount,
  ParamRetrigger,
  Invalid
};

struct JoystickAssignment {
  RecipeId recipe_id = RecipeId::Expression;
  uint8_t x_target = 1;       // recipe-defined typed destination
  uint8_t y_target = 11;
  uint8_t curve = 0;
  int16_t x_min = -127;
  int16_t x_max = 127;
  int16_t y_min = -127;
  int16_t y_max = 127;
  bool keypad_as_joy = false;
};

struct Config {
  float bpm = 120.0f;
  char key_name[4] = "C";
  char scale_name[16] = "Major";
  Articulation articulation = Articulation::Chord;
  uint8_t play_trigger = 1; // Shot/Hold/Repeat, preserved without engine enum
  uint8_t velocity = 100;
  uint16_t duration_ms = 500;
  uint8_t key = 0;
  uint8_t scale = 0;
  uint8_t arp_rate = 0;
  uint8_t arp_pattern = 0;
  uint8_t arp_octaves = 1;
  uint8_t arp_gate_percent = 80;
  uint8_t strum_rhythm = 0;
  uint8_t strum_feel = 0;
  uint8_t swing = 0;
  uint8_t humanize = 0;
  ClockSource clock_source = ClockSource::Internal;
  uint8_t chord_channel = 0;
  uint8_t lead_channel = 0;
  UsbRole usb_role = UsbRole::Off;
  bool ble_enabled = true;
  uint8_t voice_leading = 1;
  uint8_t voicing_spread = 1;
  uint8_t bend_range_semitones = 12;
  uint8_t solo_velocity = 100;
  uint16_t solo_note_length_ms = 240;
  uint8_t velocity_random = 0;
  uint8_t accent_pattern = 0;
  uint8_t voice_leading_complexity = 0;
  // Per-voice roll/stagger offset (engine::Performer::roll_ms/roll_direction).
  // Mutually exclusive with strum; 0 = off.
  uint8_t roll_ms = 0;
  uint8_t roll_dir = 0;  // engine::RollDirection: 0=Up 1=Down 2=Out 3=In
  uint8_t joystick_rotation = 0;
  bool joystick_flip_h = false;
  bool joystick_flip_v = false;
  JoystickAssignment joystick{};
  // The joystick family model: all four families' parameters are always
  // persisted, so switching preset never wipes another family's tuning.
  // `active_preset` implies the active family — there is no separate family
  // selector, since this hardware has no joystick button.
  app::JoyFamilyState joy_families{};
  app::PresetId active_preset = app::PresetId::kLead;
  uint32_t revision = 0;
};

enum class ValidationError : uint8_t { None, Range, NonFinite, InvalidEnum, InvalidRecipe };
struct ValidationResult { ValidationError error = ValidationError::None; uint16_t field = 0; };
ValidationResult validate(const Config& config);

enum class CommandKind : uint8_t { SetParameter, SetJoystick, SetArticulation, SetUsbRole,
                                    RestoreRecipe, Panic, Reset, Dfu };
struct Command { CommandKind kind{}; ParamId parameter{}; int32_t value{}; JoystickAssignment joystick{}; };
enum class ApplyError : uint8_t { None, Invalid, Busy, Unsupported };
struct ApplyResult { bool changed = false; ApplyError error = ApplyError::None; };

using SourceId = uint16_t;
using Generation = uint32_t;
struct VoiceToken { uint16_t value = 0; Generation generation = 0; };
struct NoteIntent { SourceId source{}; Generation generation{}; uint8_t channel{}; uint8_t note{}; uint8_t velocity{}; uint8_t route_mask{}; };

enum class ScheduledKind : uint8_t { Acquire, Release, Control };
struct ScheduledEvent { uint32_t deadline{}; uint32_t sequence{}; SourceId source{}; Generation generation{}; ScheduledKind kind{}; NoteIntent note{}; };

enum class SendResult : uint8_t { Accepted, Backpressure, Disconnected };
enum class TransportState : uint8_t { Disabled, Disconnected, Connected, Recovering };
struct TransportStatus { bool enabled = false; bool connected = false; bool recovering = false; };

struct ClockSnapshot { ClockSource source = ClockSource::Internal; bool running = false; uint16_t tempo = 120; uint32_t position = 0; bool locked = false; };

enum class NavigationMode : uint8_t { Play, Select, Edit };
enum class ParameterFilter : uint8_t { Chord, Joystick, System };
struct NavigationState { NavigationMode mode = NavigationMode::Play; ParameterFilter filter = ParameterFilter::Chord; ParamId parameter = ParamId::Velocity; };
enum class ConfirmationAction : uint8_t { None, Panic, Reset, Reboot, Dfu, Info, Calibrate };
struct Confirmation { ConfirmationAction action = ConfirmationAction::None; bool selected = false; };
struct UiFeedback { bool active = false; ParamId parameter = ParamId::Velocity; int32_t value = 0; uint32_t until = 0; };
struct UiRow {
  ParamId parameter = ParamId::Invalid;
  ConfirmationAction action = ConfirmationAction::None;
  bool selected = false;
  bool editable = false;
  char label[24] = {};
  char value[24] = {};
};
struct UiModel {
  Config config{}; NavigationState navigation{}; Confirmation confirmation{}; UiFeedback feedback{};
  bool settings_dirty = false;
  uint32_t settings_revision = 0;
  TransportStatus usb{}; TransportStatus ble{}; bool restart_required = false;
  bool storage_blocked = false;
  bool performance_active = false;
  bool joystick_mode = false;
  bool info_open = false;
  uint8_t info_page = 0;
  char articulation_name[8] = {};
  char active_chord_name[24] = {};
  char active_chord_symbol[16] = {};
  // 16 bytes: the play screen appends " Tn" tension to the roman numeral
  // ("I  T1"), which 8 bytes couldn't always hold.
  char active_chord_degree[16] = {};
  char active_chord_notes[32] = {};
  char chord_intervals[32] = {};
  // One compact chord symbol per mapped keypad slot, including extensions.
  // Empty slots are not drawn; live joystick overrides leave the map intact.
  char pad_symbols[7][8] = {};
  char octave_name[16] = {};
  char inversion_name[16] = {};
  char inversion_code[4] = {};
  char joystick_legend[32] = {};
  char joystick_status[16] = {};
  std::array<UiRow, 24> rows{};
  uint8_t row_count = 0;
};

enum class LoadStatus : uint8_t { Missing, Valid, Migrated, Corrupt, Unsupported, IOError };
struct LoadResult { Config config{}; uint16_t source_version = 0; LoadStatus status = LoadStatus::Missing; };
enum class SaveStatus : uint8_t { Saved, Rejected, IOError, Unsupported };
struct SaveResult { SaveStatus status = SaveStatus::Rejected; uint32_t revision = 0; };

}  // namespace app::contract
