#include "app/production_ui.h"
#include "app/config_codec.h"
#include "app/input_joystick.h"
#include "app/parameters.h"
#include "app/progression_modes.h"
#include "app/settings_bridge.h"
#include "engine/music_theory.h"
#include "ui/display_names.h"
#include <algorithm>
#include <cstdio>

namespace app::prod {
namespace {
using P = contract::ParamId;

contract::Config cfg(const InputContext& c) {
  return c.app_state.config();
}

uint8_t visible_count(const contract::Config& c, const UiState& u) {
  size_t n = 0; const auto* d = parameters::all(n); uint8_t result = 0;
  for (size_t i = 0; i < n; ++i) if (parameters::visible(c, d[i].id, u.parameter_filter)) ++result;
  return result;
}

uint8_t action_count(const UiState& u) {
  return u.parameter_filter == contract::ParameterFilter::System ? 6 : 0;
}

contract::ConfirmationAction action_at(const UiState& u, uint8_t index) {
  if (u.parameter_filter != contract::ParameterFilter::System) return contract::ConfirmationAction::None;
  if (index == 0) return contract::ConfirmationAction::Panic;
  if (index == 1) return contract::ConfirmationAction::Reset;
  if (index == 2) return contract::ConfirmationAction::Reboot;
  if (index == 3) return contract::ConfirmationAction::Dfu;
  if (index == 4) return contract::ConfirmationAction::Info;
  return contract::ConfirmationAction::Calibrate;
}

bool visible_at(const contract::Config& c, const UiState& u, uint8_t wanted, P& result) {
  size_t n = 0; const auto* d = parameters::all(n); uint8_t pos = 0;
  for (size_t i = 0; i < n; ++i) {
    if (!parameters::visible(c, d[i].id, u.parameter_filter)) continue;
    if (pos++ == wanted) { result = d[i].id; return true; }
  }
  return false;
}

void first(const InputContext& c, UiState& u) {
  P id = P::Invalid; u.selected_action = contract::ConfirmationAction::None;
  u.selected_parameter = visible_at(cfg(c), u, 0, id) ? id : P::Velocity;
}

void mutate(InputContext& c, UiState& u, int8_t delta) {
  auto value = cfg(c);
  const auto preset = value.active_preset;
  if (!parameters::mutate(value, u.selected_parameter, delta) || !commit_config(c, value)) return;
  // The encoder always starts on Recipe: a new preset's one-param slot (if
  // any) is a different ParamId than the previous preset's, so land back on
  // Recipe rather than keep pointing at a now-unrelated parameter.
  if (value.active_preset != preset) u.joystick_cursor = 0;
}

// -- PLAY-screen cursor ------------------------------------------------------
//
// Rotary push and CON tap step the cursor forward through the values printed
// on the screen; BAK steps back; rotating edits the selected value live.
// Two independent, sticky cursors - switching the CHORD/JOY lane with
// `0` never disturbs the other one's position (UiState::play_cursor,
// UiState::joystick_cursor).

// Stable encoder order for the play screen's focused-setting footer.
// Octave and inversion remain dedicated performance shortcuts.
uint8_t play_cursor_ids(P* out) {
  uint8_t n = 0;
  out[n++] = P::Voicing;
  out[n++] = P::VoiceLeading;
  out[n++] = P::Roll;
  out[n++] = P::Velocity;
  out[n++] = P::PlayTrigger;
  out[n++] = P::Duration;
  out[n++] = P::ChordChannel;
  out[n++] = P::Articulation;
  out[n++] = P::Scale;
  out[n++] = P::Key;
  return n;
}

// The ParamId the PLAY-mode cursor currently points to - what rotating
// edits and what the renderer knocks out. On a joystick screen this is
// Recipe or, once joystick_cursor has stepped past it, the active preset's
// one push-reachable parameter (joystick_push_param()); reaching that
// second slot at all requires the preset to actually have one, so
// joystick_cursor otherwise stays pinned at Recipe.
P current_cursor_param(const InputContext& c, const UiState& u) {
  if (u.joystick_mode) {
    P one;
    if (u.joystick_cursor != 0 && joystick_push_param(cfg(c).active_preset, one)) return one;
    return P::JoystickRecipe;
  }
  P ids[10];
  const uint8_t n = play_cursor_ids(ids);
  return ids[std::min<uint8_t>(u.play_cursor, static_cast<uint8_t>(n - 1))];
}

// Steps the active cursor by +1/-1, wrapping at both ends (BAK on the first
// token wraps to the last, matching the forward wrap). No long-press
// semantics: CON hold reaches this the same as CON tap (encoder_long_click
// already aliases encoder_click).
void cursor_step(InputContext& c, UiState& u, int8_t dir) {
  if (u.joystick_mode) {
    P one;
    const uint8_t n = joystick_push_param(cfg(c).active_preset, one) ? 2 : 1;
    u.joystick_cursor = static_cast<uint8_t>((static_cast<int>(u.joystick_cursor) + dir + n) % n);
    return;
  }
  P ids[10];
  const uint8_t n = play_cursor_ids(ids);
  if (!n) return;
  u.play_cursor = static_cast<uint8_t>((static_cast<int>(u.play_cursor) + dir + n) % n);
}

// Play -> Select entry. button_menu() (`#`) is the only door, since a push
// in PLAY steps the cursor instead (see encoder_click() below).
void enter_select_from_play(InputContext& c, UiState& u) {
  u.parameter_filter = u.joystick_mode ? contract::ParameterFilter::Joystick : contract::ParameterFilter::Chord;
  u.navigation_mode = contract::NavigationMode::Select; u.select_cursor = 0; first(c, u);
}

int degree(uint8_t i) { static constexpr int k[16] = {1,2,3,-1,4,5,6,-1,7,-1,-1,-1,-1,-1,-1,-1}; return i < 16 ? k[i] : -1; }

const char* chord_quality_name(const std::string& quality) {
  if (quality == "maj") return "Major";
  if (quality == "min") return "Minor";
  if (quality == "maj7") return "Major 7";
  if (quality == "min7") return "Minor 7";
  if (quality == "dom7") return "Dominant 7";
  if (quality == "dim") return "Diminished";
  if (quality == "half-dim7") return "Half-dim 7";
  if (quality == "aug") return "Augmented";
  if (quality == "sus2") return "Sus 2";
  if (quality == "sus4") return "Sus 4";
  return quality.c_str();
}

const char* chord_quality_symbol(const std::string& quality) {
  if (quality == "maj") return "";
  if (quality == "min") return "m";
  if (quality == "min7") return "m7";
  if (quality == "min9") return "m9";
  if (quality == "min11") return "m11";
  if (quality == "min6") return "m6";
  if (quality == "maj6") return "6";
  if (quality == "dom7") return "7";
  if (quality == "dom9") return "9";
  if (quality == "dom13") return "13";
  if (quality == "half-dim7") return "m7b5";
  return quality.c_str();
}

// Compact, unambiguous chord symbols for the numbered keypad cells.
// Uppercase M retains the distinction between major and dominant extensions.
const char* pad_quality_symbol(const std::string& quality) {
  if (quality == "maj7") return "M7";
  if (quality == "maj9") return "M9";
  if (quality == "maj13") return "M13";
  if (quality == "dim") return "o";
  if (quality == "dim7") return "o7";
  if (quality == "aug") return "+";
  return chord_quality_symbol(quality);
}

void roman_chord_symbol(uint8_t degree, const std::string& quality,
                        char* out, size_t out_size) {
  engine::roman_symbol(degree, quality, out, out_size);
  const char* suffix = "";
  if (quality == "maj7" || quality == "min7") suffix = "7";
  else if (quality == "dom7") suffix = "7";
  else if (quality == "half-dim7" || quality == "dim7") suffix = "7";
  else if (quality == "maj9" || quality == "min9" || quality == "dom9") suffix = "9";
  else if (quality == "min11") suffix = "11";
  else if (quality == "maj13" || quality == "dom13") suffix = "13";
  else if (quality == "maj6" || quality == "min6") suffix = "6";
  const size_t used = std::strlen(out);
  if (used < out_size) std::snprintf(out + used, out_size - used, "%s", suffix);
}

const char* joystick_direction_name(int8_t direction) {
  static constexpr const char* kDirections[] = {
      "RIGHT", "UP-R", "UP", "UP-L",
      "LEFT", "DOWN-L", "DOWN", "DOWN-R"};
  return direction >= 0 && direction < 8 ? kDirections[direction] : "CENTER";
}

const char* inversion_name(const engine::ChordModifier& modifier) {
  if (modifier.voice_leads()) return "AUTO";
  static constexpr const char* kNames[] = {"ROOT", "1ST", "2ND", "3RD", "4TH", "5TH"};
  return kNames[modifier.manual_inversion() ? std::min<uint8_t>(modifier.inversion_override(), 5) : 0];
}

// Octave and inversion act on the chord being held, not only on the next
// one: a sustained chord is re-voiced on the spot so the change is heard.
void revoice_held(InputContext& c) {
  if (c.nav.holding && c.nav.active_degree >= 1 &&
      c.app_state.settings().play_trigger == PlayTrigger::kHold && c.engine.held_active())
    c.engine.trigger_step(static_cast<size_t>(c.nav.active_degree - 1), engine::TriggerMode::kHeld);
}

// Keys 8/9 step the inversion by hand. A chord has as many inversions as it
// has notes, so the steps wrap at the sounding chord's size (3 for a triad,
// up to 6 for a 13th). With voice leading on there is one more stop, AUTO,
// which hands the choice back to the engine:
//   AUTO -> ROOT -> 1ST -> 2ND -> AUTO     (voice leading Smooth/Common)
//           ROOT -> 1ST -> 2ND -> ROOT     (voice leading Off)
void change_inversion(InputContext& c, int8_t delta) {
  auto& modifier = c.engine.modifier();
  int count = 3;
  const auto& steps = c.engine.progression().steps();
  if (c.nav.active_degree >= 1 && static_cast<size_t>(c.nav.active_degree) <= steps.size()) {
    const auto step = modifier.apply(steps[static_cast<size_t>(c.nav.active_degree - 1)]);
    const auto notes = engine::progression_step_chord_notes(c.app_state.settings(), c.engine, step);
    count = std::clamp(static_cast<int>(notes.size()), 3, 6);
  }
  const bool has_auto = modifier.voice_leading() != engine::VoiceLeading::kOff;
  // Position in the cycle: 0 is AUTO when it exists, then the inversions.
  const int offset = has_auto ? 1 : 0;
  const int total = count + offset;
  const int manual = modifier.manual_inversion() ? std::min<int>(modifier.inversion_override(), count - 1) : 0;
  const int current = modifier.voice_leads() ? 0 : manual + offset;
  const int next = ((current + delta) % total + total) % total;
  if (has_auto && next == 0) modifier.reset_inversion();
  else modifier.set_manual_inversion(static_cast<uint8_t>(next - offset));
  revoice_held(c);
  c.nav.dirty = true;
}

void format_chord_notes(const app::Settings& settings, engine::Engine& engine_instance,
                        const engine::ProgressionStep& step, char* out, size_t out_len,
                        char* intervals, size_t intervals_len) {
  static constexpr const char* kPitchNames[] = {
      "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  if (!out || out_len == 0 || !intervals || intervals_len == 0) return;
  out[0] = '\0';
  const auto notes = engine::progression_step_chord_notes(settings, engine_instance, step);
  static constexpr const char* kIntervals[] = {"1", "b2", "2", "b3", "3", "4", "b5", "5", "b6", "6", "b7", "7"};
  bool present[12] = {};
  const int root = (engine::progression_step_root_offset(settings, step) % 12 + 12) % 12;
  for (uint8_t note : notes) present[(note % 12 - root + 12) % 12] = true;
  intervals[0] = '\0';
  for (uint8_t i = 0; i < 12; ++i) {
    if (!present[i]) continue;
    const size_t used = std::strlen(intervals);
    if (used + std::strlen(kIntervals[i]) + (used ? 1 : 0) + 1 > intervals_len) break;
    std::snprintf(intervals + used, intervals_len - used, "%s%s", used ? " " : "", kIntervals[i]);
  }
  bool seen[12] = {};
  for (uint8_t note : notes) {
    const uint8_t pitch = note % 12;
    if (seen[pitch]) continue;
    seen[pitch] = true;
    const size_t used = std::strlen(out);
    if (used + std::strlen(kPitchNames[pitch]) + (used ? 1 : 0) + 1 > out_len) break;
    std::snprintf(out + used, out_len - used, "%s%s", used ? " " : "", kPitchNames[pitch]);
  }
}

void change_key(InputContext& c, int semitones) {
  auto value = cfg(c);
  value.key = static_cast<uint8_t>(((value.key + semitones) % 12 + 12) % 12);
  commit_config(c, value);
}

void trigger(InputContext& c, int d) {
  auto& s = c.app_state.settings(); c.nav.active_degree = d; c.nav.holding = play_trigger_holds(s.play_trigger);
  if (s.play_trigger == PlayTrigger::kShot) c.nav.chord_flash_until = c.t + 600;
  if (s.play_trigger == PlayTrigger::kRepeat) c.nav.repeat_next_ms = c.t + s.note_length_ms;
  c.engine.trigger_step(static_cast<size_t>(d - 1), play_trigger_sustains(s.play_trigger) ? engine::TriggerMode::kHeld : engine::TriggerMode::kOneShot);
  {
    const auto& jf = c.app_state.config().joy_families;
    const auto family = kPresets[static_cast<size_t>(c.app_state.config().active_preset)].family;
    if (family == JoyFamily::kChord && jf.chord.order == ChordOrder::kRanking)
      discover_rebuild_slots(c);
  }
  c.nav.dirty = true;
}
}

void panic(InputContext& c) {
  joy_all_notes_off(c); c.engine.all_notes_off(); c.engine.release_held(); c.nav.holding = false; c.nav.active_degree = -1; c.nav.repeat_next_ms = 0;
  c.engine.clear_prev_chord();  // voice leading starts afresh from the next chord
  const auto& s = c.app_state.settings(); for (uint8_t ch : {s.midi_channel, s.midi_channel_joy}) { c.router.control_change(ch, 123, 0); c.router.control_change(ch, 120, 0); } c.nav.dirty = true;
}

static void reset_to_defaults(InputContext& c, UiState& u) {
  panic(c);
  // Octave, inversion and any armed colour are performance state the stored
  // Config does not carry; a factory reset clears them too.
  c.engine.modifier().reset();
  u.storage_overwrite_requested = true;
  if (!commit_config(c, config::factory_defaults())) return;
  u.play_cursor = UiState{}.play_cursor;
  u.joystick_cursor = 0;
}

void begin(UiState& u, InputContext& c) { u = UiState{}; first(c, u); c.nav.dirty = true; }

void encoder_turn(InputContext& c, UiState& u, int8_t delta) {
  if (u.calibration_open) return;
  if (u.info_open) { c.nav.dirty = true; return; }
  if (u.confirmation_action != contract::ConfirmationAction::None) {
    if (delta) u.confirmation_selected = delta > 0;
    c.nav.dirty = true;
    return;
  }
  if (u.navigation_mode == contract::NavigationMode::Select) {
    const uint8_t params = visible_count(cfg(c), u);
    const uint8_t n = static_cast<uint8_t>(params + action_count(u)); if (!n) return;
    u.select_cursor = static_cast<uint8_t>(std::clamp(static_cast<int>(u.select_cursor) + delta, 0, static_cast<int>(n) - 1));
    if (u.select_cursor < params) {
      visible_at(cfg(c), u, u.select_cursor, u.selected_parameter);
      u.selected_action = contract::ConfirmationAction::None;
    } else {
      u.selected_parameter = P::Invalid;
      u.selected_action = action_at(u, static_cast<uint8_t>(u.select_cursor - params));
    }
    c.nav.dirty = true; return;
  }
  if (u.navigation_mode == contract::NavigationMode::Play) {
    // Rotating edits whatever the PLAY cursor currently points to. The
    // cursor may have explored another row in SELECT without entering EDIT.
    const auto explored = u.selected_parameter;
    u.selected_parameter = current_cursor_param(c, u);
    mutate(c, u, delta);
    u.selected_parameter = explored;
    return;
  }
  mutate(c, u, delta);
}

void encoder_click(InputContext& c, UiState& u) {
  if (u.calibration_open) {
    if (u.calibration_result == drivers::CalibrationResult::Saved) u.calibration_open = false;
    else if (u.calibration_result != drivers::CalibrationResult::Sampling) u.calibration_requested = true;
    c.nav.dirty = true;
    return;
  }
  if (u.info_open) {
    if (u.info_page == 0) u.info_page = 1;
    else { u.info_open = false; u.info_page = 0; }
    c.nav.dirty = true;
    return;
  }
  if (u.navigation_mode == contract::NavigationMode::Play) {
    // CON tap / rotary push steps the PLAY cursor forward; the full menu is
    // entered with `#` (button_menu()) only (see enter_select_from_play()).
    cursor_step(c, u, 1);
  }
  else if (u.navigation_mode == contract::NavigationMode::Select) {
    if (u.confirmation_action != contract::ConfirmationAction::None) {
      if (!u.confirmation_selected) { u.confirmation_action = contract::ConfirmationAction::None; c.nav.dirty = true; return; }
      if (u.confirmation_action == contract::ConfirmationAction::Panic) panic(c);
      else if (u.confirmation_action == contract::ConfirmationAction::Reset) reset_to_defaults(c, u);
      else if (u.confirmation_action == contract::ConfirmationAction::Reboot) u.reboot_pending = true;
      else if (u.confirmation_action == contract::ConfirmationAction::Dfu) u.dfu_reboot_pending = true;
      u.confirmation_action = contract::ConfirmationAction::None;
      u.confirmation_selected = false;
      c.nav.dirty = true;
      return;
    }
    if (u.selected_action == contract::ConfirmationAction::Calibrate) {
      panic(c);
      u.calibration_open = true;
      u.calibration_requested = false;
      u.calibration_result = drivers::CalibrationResult::Ready;
      u.calibration_progress = 0;
      c.nav.dirty = true;
      return;
    }
    if (u.selected_action == contract::ConfirmationAction::Info) {
      u.info_open = true;
      u.info_page = 0;
      c.nav.dirty = true;
      return;
    }
    if (u.selected_action != contract::ConfirmationAction::None) {
      u.confirmation_action = u.selected_action;
      u.confirmation_selected = false;
      c.nav.dirty = true;
      return;
    }
    const auto* d = parameters::find(u.selected_parameter);
    if (u.selected_parameter == P::BleEnabled) {
      mutate(c, u, cfg(c).ble_enabled ? -1 : 1);
    } else if (d && d->editable && !d->action) { u.navigation_mode = contract::NavigationMode::Edit; }
  } else u.navigation_mode = contract::NavigationMode::Select;
  c.nav.dirty = true;
}

// Confirm and rotary push share both short- and long-press behavior.
void encoder_long_click(InputContext& c, UiState& u) { encoder_click(c, u); }

// Shared back-navigation logic. `bak` distinguishes the physical BAK button
// (steps the PLAY cursor backward in Play) from the keypad `*` tap, which
// also calls into this path for its own, unrelated reason (the panic/back
// shortcut on the 4x4 grid) and must stay a true no-op in Play, so the two
// callers cannot share one behavior here.
void back_impl(InputContext& c, UiState& u, bool bak) {
  if (u.calibration_open) {
    u.calibration_open = false; u.calibration_requested = false;
    c.nav.dirty = true;
    return;
  }
  if (u.info_open) {
    u.info_open = false;
    u.info_page = 0;
    c.nav.dirty = true;
    return;
  }
  if (u.confirmation_action != contract::ConfirmationAction::None) {
    u.confirmation_action = contract::ConfirmationAction::None;
    u.confirmation_selected = false;
  } else if (u.navigation_mode == contract::NavigationMode::Edit) {
    u.navigation_mode = contract::NavigationMode::Select;
  } else if (bak && u.navigation_mode == contract::NavigationMode::Play) {
    // BAK steps the PLAY cursor backward - the mirror of CON tap/rotary
    // push in encoder_click(). Wraps at the first token to the last,
    // matching the forward wrap.
    cursor_step(c, u, -1);
  } else {
    u.navigation_mode = contract::NavigationMode::Play;
  }
  c.nav.dirty = true;
}

void button_back(InputContext& c, UiState& u) { back_impl(c, u, false); }
void button_bak(InputContext& c, UiState& u) { back_impl(c, u, true); }

void button_menu(InputContext& c, UiState& u) {
  u.calibration_open = false; u.calibration_requested = false;
  if (u.navigation_mode == contract::NavigationMode::Play) enter_select_from_play(c, u);
  else {
    u.navigation_mode = contract::NavigationMode::Play;
    u.confirmation_action = contract::ConfirmationAction::None;
    u.confirmation_selected = false;
    u.info_open = false;
    u.info_page = 0;
  }
  c.nav.dirty = true;
}

void button_system(InputContext& c, UiState& u) {
  u.calibration_open = false; u.calibration_requested = false;
  u.navigation_mode = contract::NavigationMode::Select;
  u.parameter_filter = contract::ParameterFilter::System;
  u.confirmation_action = contract::ConfirmationAction::None;
  u.confirmation_selected = false;
  u.info_open = false;
  u.info_page = 0;
  u.select_cursor = 0;
  first(c, u);
  c.nav.dirty = true;
}

void keypad_zero(InputContext& c, UiState& u) {
  if (u.navigation_mode != contract::NavigationMode::Play) return;
  u.joystick_mode = !u.joystick_mode;
  c.nav.dirty = true;
}

void process_keypad_events(InputContext& c, UiState& u, const std::vector<drivers::KeypadEvent>& events) {
  auto& s = c.app_state.settings(); static constexpr int8_t joy[16] = {5,6,7,-2,4,-1,0,-2,3,2,1,-2,-2,-2,-2,-2};
  for (const auto& e : events) {
    const uint8_t i = e.index; const bool down = e.pressed;
    if (i >= 16) continue;
    if (i == 12) {
      u.star_down = down;
      if (down) { u.star_pressed_ms = c.t; u.star_long_sent = false; }
      else if (!u.star_long_sent) {
        if (c.t - u.star_pressed_ms >= 650) {
          panic(c);
          u.calibration_open = false; u.calibration_requested = false;
          u.navigation_mode = contract::NavigationMode::Play;
          u.confirmation_action = contract::ConfirmationAction::None;
          u.confirmation_selected = false;
          u.info_open = false;
          u.info_page = 0;
        } else button_back(c, u);
      }
      continue;
    }
    if (i == 14) {
      u.hash_down = down;
      if (down) { u.hash_pressed_ms = c.t; u.hash_long_sent = false; }
      else if (!u.hash_long_sent) {
        if (c.t - u.hash_pressed_ms >= 650) button_system(c, u);
        else button_menu(c, u);
      }
      continue;
    }
    if (i == 13) { if (down) { u.zero_down = true; u.zero_used = false; } else { u.zero_down = false; if (!u.zero_used) keypad_zero(c, u); } continue; }
    if (u.calibration_open) continue;
    // Dedicated performance shortcuts: 8/9 select the previous/next manual
    // inversion.  They stay reserved even when keypad-as-joystick is enabled.
    if ((i == 9 || i == 10) && u.navigation_mode == contract::NavigationMode::Play) {
      if (down) change_inversion(c, i == 9 ? -1 : 1);
      continue;
    }
    if (c.app_state.config().joystick.keypad_as_joy && joy[i] != -2 &&
        (u.navigation_mode == contract::NavigationMode::Play || !down)) {
      dispatch_keypad_joy_direction(c, down ? joy[i] : -1);
      continue;
    }
    if (!down) { const int d = degree(i); if (d != -1 && play_trigger_holds(s.play_trigger) && c.nav.active_degree == d) { c.engine.release_held(); c.nav.holding = false; c.nav.repeat_next_ms = 0; } continue; }
    if (u.navigation_mode != contract::NavigationMode::Play) continue;
    if (i == 3 || i == 7) { change_key(c, i == 3 ? ui::key_nav_step(c.nav.key_nav_mode) : -ui::key_nav_step(c.nav.key_nav_mode)); continue; }
    // C / D: octave up / down. The remembered previous chord moves with it,
    // so voice leading continues in the new register instead of pulling the
    // next chord back toward the old one.
    if (i == 11 || i == 15) {
      auto& modifier = c.engine.modifier();
      const int8_t before = modifier.octave_shift();
      modifier.adjust_octave(i == 11 ? 1 : -1);
      c.engine.shift_prev_chord(static_cast<int8_t>((modifier.octave_shift() - before) * 12));
      revoice_held(c);
      c.nav.dirty = true;
      continue;
    }
    const int d = degree(i); if (d != -1) trigger(c, d);
  }
  // Poll even when there are no edges, so holds act before release.
  if (u.star_down && !u.star_long_sent && c.t - u.star_pressed_ms >= 650) {
    u.star_long_sent = true;
    panic(c);
    u.calibration_open = false; u.calibration_requested = false;
    u.navigation_mode = contract::NavigationMode::Play;
    u.confirmation_action = contract::ConfirmationAction::None;
    u.confirmation_selected = false;
    u.info_open = false;
    u.info_page = 0;
  }
  if (u.hash_down && !u.hash_long_sent && c.t - u.hash_pressed_ms >= 650) {
    u.hash_long_sent = true;
    button_system(c, u);
  }
}

contract::UiModel model_snapshot(const InputContext& c, const UiState& u) {
  const auto value = cfg(c); contract::NavigationState n{}; n.mode = u.navigation_mode; n.filter = u.parameter_filter; n.parameter = u.navigation_mode == contract::NavigationMode::Play ? current_cursor_param(c, u) : u.selected_parameter;
  auto m = ui_model::project(value, n, c.app_state.usb_role_state().restart_required()); m.performance_active = c.nav.holding || c.engine.held_active() || c.js.note_active_count != 0;
  m.performance_active = m.performance_active || c.js.drone_active_mask != 0 ||
      (c.nav.chord_flash_until != 0 && static_cast<int32_t>(c.nav.chord_flash_until - c.t) > 0);
  m.joystick_mode = u.joystick_mode;
  m.storage_blocked = u.storage_blocked;
  m.info_open = u.info_open;
  m.info_page = u.info_page;
  m.settings_dirty = c.nav.settings_dirty;
  m.settings_revision = c.nav.settings_revision;
  std::snprintf(m.articulation_name, sizeof(m.articulation_name), "%s", value.articulation == contract::Articulation::Chord ? "Chord" : value.articulation == contract::Articulation::Strum ? "Strum" : "Arp");
  std::snprintf(m.active_chord_name, sizeof(m.active_chord_name), "%s", "--");
  std::snprintf(m.active_chord_symbol, sizeof(m.active_chord_symbol), "%s", "READY");
  m.active_chord_degree[0] = '\0';
  std::snprintf(m.active_chord_notes, sizeof(m.active_chord_notes), "%s", "READY");
  const auto& steps = c.engine.progression().steps();
  if (c.nav.active_degree >= 1 &&
      static_cast<size_t>(c.nav.active_degree) <= steps.size()) {
    const auto step = c.engine.modifier().apply(
        steps[static_cast<size_t>(c.nav.active_degree - 1)]);
    std::snprintf(m.active_chord_name, sizeof(m.active_chord_name), "%s %s",
                  engine::key_str_from_offset(
                      engine::progression_step_root_offset(c.app_state.settings(), step)),
                  chord_quality_name(step.chord_quality));
    std::snprintf(m.active_chord_symbol, sizeof(m.active_chord_symbol), "%s%s",
                  engine::key_str_from_offset(engine::progression_step_root_offset(c.app_state.settings(), step)),
                  chord_quality_symbol(step.chord_quality));
    char degree_symbol[12];
    roman_chord_symbol(step.scale_degree, step.chord_quality, degree_symbol, sizeof(degree_symbol));
    std::snprintf(m.active_chord_degree, sizeof(m.active_chord_degree), "%s%s",
                  step.transpose == -1 ? "b" : step.transpose == 1 ? "#" : "", degree_symbol);
    // Genre presets can put any harmonic degree on a numbered keypad slot.
    const uint8_t tension = engine::chord_tension(c.app_state.settings(), c.engine,
        step.scale_degree, step.chord_quality);
    const size_t degree_len = std::strlen(m.active_chord_degree);
    std::snprintf(m.active_chord_degree + degree_len, sizeof(m.active_chord_degree) - degree_len,
                  " T%u", tension);
    format_chord_notes(c.app_state.settings(), c.engine, step,
                       m.active_chord_notes, sizeof(m.active_chord_notes),
                       m.chord_intervals, sizeof(m.chord_intervals));
  }
  // Show only mapped chord slots, including the actual genre extensions.
  // Live joystick overrides affect the sounding chord, not these destinations.
  for (size_t i = 0; i < steps.size() && i < 7; ++i) {
    std::snprintf(m.pad_symbols[i], sizeof(m.pad_symbols[i]), "%s%s",
                  engine::key_str_from_offset(
                      engine::progression_step_root_offset(c.app_state.settings(), steps[i])),
                  pad_quality_symbol(steps[i].chord_quality));
  }
  const int octave = c.engine.modifier().octave_shift();
  if (octave == 0) std::snprintf(m.octave_name, sizeof(m.octave_name), "%s", "NORMAL");
  else std::snprintf(m.octave_name, sizeof(m.octave_name), "%s %d",
                     octave > 0 ? "UP" : "DOWN", octave > 0 ? octave : -octave);
  std::snprintf(m.inversion_name, sizeof(m.inversion_name), "%s",
                inversion_name(c.engine.modifier()));
  // Compact form for the register readout: R/1/2... by hand, or "A" plus the
  // inversion voice leading chose for the chord that is sounding.
  static constexpr char kInversionCodes[] = {'R', '1', '2', '3', '4', '5'};
  const auto& modifier = c.engine.modifier();
  if (!modifier.voice_leads())
    std::snprintf(m.inversion_code, sizeof(m.inversion_code), "%c",
                  kInversionCodes[modifier.manual_inversion() ? std::min<uint8_t>(modifier.inversion_override(), 5) : 0]);
  else if (c.nav.active_degree >= 1)
    std::snprintf(m.inversion_code, sizeof(m.inversion_code), "A%c",
                  kInversionCodes[std::min<uint8_t>(c.engine.sounding_inversion(), 5)]);
  else
    std::snprintf(m.inversion_code, sizeof(m.inversion_code), "A");
  const auto pi = static_cast<size_t>(value.active_preset);
  std::snprintf(m.joystick_legend, sizeof(m.joystick_legend), "JOY %s",
                pi < static_cast<size_t>(PresetId::kCount) ? kPresets[pi].name : "Unknown");
  const int8_t joy_direction = c.js.physical_dir >= 0 ? c.js.physical_dir : c.js.keypad_dir;
  std::snprintf(m.joystick_status, sizeof(m.joystick_status), "%s",
                joystick_direction_name(joy_direction));
  if (u.navigation_mode == contract::NavigationMode::Edit) {
    const auto read = parameters::read(value, u.selected_parameter);
    m.feedback.active = read.applicable;
    m.feedback.parameter = u.selected_parameter;
    m.feedback.value = read.value;
    m.feedback.until = c.t + 750;
  }
  size_t count = 0; const auto* d = parameters::all(count); if (u.navigation_mode == contract::NavigationMode::Select ||
      (u.navigation_mode == contract::NavigationMode::Edit && u.parameter_filter == contract::ParameterFilter::System)) {
    for (size_t i = 0; i < count && m.row_count < m.rows.size(); ++i) if (parameters::visible(value, d[i].id, u.parameter_filter)) { auto& r = m.rows[m.row_count++]; r.parameter = d[i].id; r.selected = r.parameter == u.selected_parameter; r.editable = d[i].editable; parameters::format(value, r.parameter, r.label, sizeof(r.label), r.value, sizeof(r.value)); }
    if (u.parameter_filter == contract::ParameterFilter::System) {
      if (m.row_count < m.rows.size()) {
        auto& r = m.rows[m.row_count++]; r.action = contract::ConfirmationAction::Panic; r.selected = u.selected_action == r.action; r.editable = false;
        std::snprintf(r.label, sizeof(r.label), "%s", "Panic"); std::snprintf(r.value, sizeof(r.value), "%s", "all notes off");
      }
      if (m.row_count < m.rows.size()) {
        auto& r = m.rows[m.row_count++]; r.action = contract::ConfirmationAction::Reset; r.selected = u.selected_action == r.action; r.editable = false;
        std::snprintf(r.label, sizeof(r.label), "%s", "Reset"); std::snprintf(r.value, sizeof(r.value), "%s", "defaults");
      }
      if (m.row_count < m.rows.size()) {
        auto& r = m.rows[m.row_count++]; r.action = contract::ConfirmationAction::Reboot; r.selected = u.selected_action == r.action; r.editable = false;
        std::snprintf(r.label, sizeof(r.label), "%s", "Reboot"); std::snprintf(r.value, sizeof(r.value), "%s", "restart");
      }
      if (m.row_count < m.rows.size()) {
        auto& r = m.rows[m.row_count++]; r.action = contract::ConfirmationAction::Dfu; r.selected = u.selected_action == r.action; r.editable = false;
        std::snprintf(r.label, sizeof(r.label), "%s", "DFU"); std::snprintf(r.value, sizeof(r.value), "%s", "reboot");
      }
      if (m.row_count < m.rows.size()) {
        auto& r = m.rows[m.row_count++]; r.action = contract::ConfirmationAction::Info; r.selected = u.selected_action == r.action; r.editable = false;
        std::snprintf(r.label, sizeof(r.label), "%s", "Info"); std::snprintf(r.value, sizeof(r.value), "%s", "about");
      }
      if (m.row_count < m.rows.size()) {
        auto& r = m.rows[m.row_count++]; r.action = contract::ConfirmationAction::Calibrate;
        r.selected = u.selected_action == r.action; r.editable = false;
        std::snprintf(r.label, sizeof(r.label), "%s", "Calibrate joy");
        std::snprintf(r.value, sizeof(r.value), "%s", "center");
      }
    }
  }
  m.confirmation.action = u.confirmation_action;
  m.confirmation.selected = u.confirmation_selected;
  auto transport = [](const auto& sink, contract::TransportStatus& out) { out.enabled = sink.is_enabled(); out.connected = sink.is_connected(); out.recovering = sink.transport_state() == contract::TransportState::Recovering; }; transport(c.usb, m.usb); transport(c.ble, m.ble); return m;
}
}  // namespace app::prod
