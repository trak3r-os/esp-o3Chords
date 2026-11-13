#include "ui/production_render.h"
#include "app/parameters.h"
#include "app/input_joystick.h"
#include "app/version.h"
#include "engine/music_theory.h"
#include "drivers/oled_framebuffer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ui {
namespace {
using app::contract::NavigationMode;
using app::contract::ParamId;
using drivers::OledSsd13xx;

const char* filter_name(app::contract::ParameterFilter filter) {
  switch (filter) {
    case app::contract::ParameterFilter::Chord: return "PLAY EDIT";
    case app::contract::ParameterFilter::Joystick: return "JOYSTICK EDIT";
    case app::contract::ParameterFilter::System: return "SYSTEM";
  }
  return "EDIT";
}

// Every row has a fixed 21-character budget. Reserve the right-hand value
// first; labels yield space rather than pushing values off the OLED.
void pair(OledSsd13xx& oled, uint8_t page, const char* left,
          const char* right, bool inverse = false) {
  char line[22];
  std::memset(line, ' ', 21);
  line[21] = '\0';
  const size_t rw = std::min<size_t>(std::strlen(right), 20);
  const size_t lw = std::min<size_t>(std::strlen(left), 21 - rw - (rw ? 1 : 0));
  std::memcpy(line, left, lw);
  std::memcpy(line + 21 - rw, right, rw);
  if (inverse) oled.fill_line(page);
  oled.draw_text(0, page, line, inverse);
}

void text(OledSsd13xx& oled, uint8_t page, const char* value, bool inverse = false) {
  char line[22];
  std::snprintf(line, sizeof(line), "%.21s", value);
  if (inverse) oled.fill_line(page);
  oled.draw_text(0, page, line, inverse);
}

void center(OledSsd13xx& oled, uint8_t page, const char* value) {
  char line[22];
  std::snprintf(line, sizeof(line), "%.21s", value);
  oled.draw_text((128 - std::strlen(line) * 6) / 2, page, line);
}

const char* short_label(ParamId id, const char* fallback) {
  switch (id) {
    case ParamId::JoystickRecipe: return "Recipe";
    case ParamId::JoystickXTarget: return "X target";
    case ParamId::JoystickYTarget: return "Y target";
    case ParamId::JoystickCurve: return "Curve";
    case ParamId::ChordChannel: return "Chord ch";
    case ParamId::LeadChannel: return "Joy ch";
    case ParamId::Articulation: return "Style";
    case ParamId::Voicing: return "Voicing";
    case ParamId::KeypadAsJoy: return "Keypad joy";
    case ParamId::JoystickInvertX: return "Invert X";
    case ParamId::JoystickInvertY: return "Invert Y";
    default: return fallback;
  }
}

const char* transport(const app::contract::TransportStatus& t) {
  return !t.enabled ? "OFF" : t.recovering ? "RETRY" : t.connected ? "LINK" : "WAIT";
}

// Transport health belongs to System, never to a performance screen.
void connection_bar(OledSsd13xx& oled, const app::contract::UiModel& m) {
  char usb[12], ble[12];
  std::snprintf(usb, sizeof(usb), "USB %s", transport(m.usb));
  std::snprintf(ble, sizeof(ble), "BLE %s", transport(m.ble));
  pair(oled, 7, usb, ble);
}

// All performance cells own their pixel budget. Long values can never
// overwrite a neighboring control or wrap an unsigned drawing coordinate.
void cell_text(OledSsd13xx& oled, uint8_t x, uint8_t page, uint8_t width,
               const char* value, bool inverse = false, bool centered = false,
               bool large = false, bool right_aligned = false) {
  char label[22];
  const size_t capacity = std::min<size_t>(width / 6, sizeof(label) - 1);
  std::snprintf(label, capacity + 1, "%s", value);
  if (std::strlen(value) > capacity && capacity) label[capacity - 1] = '~';
  const bool twice = large && std::strlen(value) * 12 <= width;
  const int used = std::strlen(label) * (twice ? 12 : 6);
  const uint8_t at = x + (right_aligned ? width - used : centered ? (width - used) / 2 : 0);
  if (inverse) oled.fill_rect(x, page * 8, width, twice ? 16 : 8);
  if (twice) oled.draw_text_2x(at, page, label, inverse);
  else oled.draw_text(at, page, label, inverse);
}

void joystick_register_line(OledSsd13xx& oled, uint8_t page,
                            const app::contract::UiModel& m, const app::InputContext& input) {
  char harmony[24], reg[12];
  std::snprintf(harmony, sizeof(harmony), "%s %s", m.config.key_name, m.config.scale_name);
  std::snprintf(reg, sizeof(reg), "O%+d I:%s", input.engine.modifier().octave_shift(), m.inversion_code);
  pair(oled, page, harmony, reg);
}

// Match the continuous controller's transform, then convert screen Y to
// up-positive. Keypad directions do not generate continuous MIDI output.
void joystick_xy(const app::InputContext& input, int& x, int& y) {
  const auto& s = input.app_state.settings();
  x = input.js.raw_x; y = input.js.raw_y;
  for (uint8_t i = 0; i < s.joystick_rotation; ++i) { const int old = x; x = -y; y = old; }
  if (s.joystick_flip_h) x = -x;
  if (s.joystick_flip_v) y = -y;
  y = -y;
}

// Frame primitives shared by the per-family bodies.
void box(OledSsd13xx& oled, uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) {
  oled.draw_hline(x0, x1, y0); oled.draw_hline(x0, x1, y1);
  oled.draw_vline(x0, y0, y1); oled.draw_vline(x1, y0, y1);
}
void meter(OledSsd13xx& oled, uint8_t x, uint8_t y, uint8_t w, uint8_t h, float frac) {
  box(oled, x, y, static_cast<uint8_t>(x + w), static_cast<uint8_t>(y + h));
  const int inner = static_cast<int>(std::lround((w - 3) * std::clamp(frac, 0.0f, 1.0f)));
  if (inner > 0) oled.fill_rect(static_cast<uint8_t>(x + 2), static_cast<uint8_t>(y + 2), static_cast<uint8_t>(inner), static_cast<uint8_t>(h - 3));
}

// Stable family/status header, with separate bounded recipe and parameter
// cells. The recipe stays visible while the encoder edits its second control.
void draw_joystick_header(OledSsd13xx& oled, const app::contract::UiModel& m,
                          const app::InputContext& input, const char* family_name,
                          const char* row1_right = nullptr) {
  const auto& j = input.js;
  const bool recenter = j.recipe_neutral_required ||
      (m.config.active_preset == app::PresetId::kBend && j.bend_neutral_required);
  char sounding[20];
  if (recenter) {
    std::snprintf(sounding, sizeof(sounding), "%s", "RECENTER");
  } else if (j.note_active_count == 1) {
    engine::midi_note_label(j.note_active[0], sounding, sizeof(sounding));
  } else if (j.note_active_count > 1) {
    std::snprintf(sounding, sizeof(sounding), "%u notes", j.note_active_count);
  } else if (j.drone_active_mask != 0) {
    uint8_t held = 0;
    for (uint8_t mask = j.drone_active_mask; mask; mask >>= 1) held += (mask & 1);
    std::snprintf(sounding, sizeof(sounding), "%u held", held);
  } else if (input.nav.active_degree >= 1) {
    std::snprintf(sounding, sizeof(sounding), "%s", m.active_chord_symbol);
  } else {
    std::snprintf(sounding, sizeof(sounding), "%s", "--");
  }
  pair(oled, 0, family_name, sounding, true);

  char recipe_label[24], recipe_value[24];
  app::parameters::format(m.config, ParamId::JoystickRecipe, recipe_label, sizeof(recipe_label),
                          recipe_value, sizeof(recipe_value));
  ParamId one_id = ParamId::Invalid;
  char one_value[24] = {};
  if (row1_right) {
    std::snprintf(one_value, sizeof(one_value), "%s", row1_right);
  } else if (app::joystick_push_param(m.config.active_preset, one_id)) {
    char one_label[24];
    app::parameters::format(m.config, one_id, one_label, sizeof(one_label), one_value, sizeof(one_value));
  }
  // row1_right (CONT's channel, PARAM's tempo) is informational, never the
  // cursor target - only a real one_id can be knocked out on the right.
  const bool cursor_on_one = !row1_right && one_id != ParamId::Invalid && m.navigation.parameter == one_id;
  const uint8_t right_width = one_value[0]
      ? static_cast<uint8_t>(std::min<size_t>(std::strlen(one_value) * 6, 66)) : 0;
  cell_text(oled, 0, 1, right_width ? 126 - right_width - 6 : 128,
            recipe_value, !cursor_on_one);
  if (right_width)
    cell_text(oled, 126 - right_width, 1, right_width, one_value, cursor_on_one);
}

// A small pad with a moving thumb marker, shared by directional recipes.
void draw_joypad(OledSsd13xx& oled, int8_t direction) {
  static constexpr int8_t dx[] = {1, 1, 0, -1, -1, -1, 0, 1};
  static constexpr int8_t dy[] = {0, -1, -1, -1, 0, 1, 1, 1};
  box(oled, 54, 25, 74, 39);
  oled.draw_hline(58, 70, 32);
  oled.draw_vline(64, 28, 36);
  const bool active = direction >= 0 && direction < 8;
  const int x = 64 + (active ? dx[direction] * 7 : 0);
  const int y = 32 + (active ? dy[direction] * 4 : 0);
  oled.fill_rect(x - 1, y - 1, 3, 3);
  if (!active) oled.fill_rect(x, y, 1, 1, false);
}

const char* compass_label(const char* value) {
  if (std::strcmp(value, "half-dim7") == 0) return "m7b5";
  if (std::strcmp(value, "Rootless") == 0) return "Rootls";
  if (std::strcmp(value, "min7") == 0) return "m7";
  if (std::strcmp(value, "min9") == 0) return "m9";
  if (std::strcmp(value, "min11") == 0) return "m11";
  if (std::strcmp(value, "dom7") == 0) return "7";
  if (std::strcmp(value, "dom9") == 0) return "9";
  if (std::strcmp(value, "dom13") == 0) return "13";
  return value[0] ? value : "--";
}

// Spatial compass: labels on the west/east edges face outwards, while
// north/south are centered. Corner cells leave a clear middle for the pad.
// `armed_dir` marks the cell whose outcome is currently in force (the armed
// colour, the selected voicing) with an underline, so it stays readable
// after the stick has sprung back to centre.
void draw_compass_body(OledSsd13xx& oled, const char cells[][12], int8_t active_dir,
                       int8_t armed_dir = -1) {
  static constexpr uint8_t xs[] = {80, 86, 43, 0, 0, 0, 43, 86};
  static constexpr uint8_t pages[] = {3, 2, 2, 2, 3, 5, 5, 5};
  for (uint8_t d = 0; d < 8; ++d) {
    if (!cells[d][0]) continue;
    const uint8_t width = d == 0 || d == 4 ? 48 : 42;
    const char* label = compass_label(cells[d]);
    const bool centered = d == 2 || d == 6, right = d == 0 || d == 1 || d == 7;
    cell_text(oled, xs[d], pages[d], width, label, d == active_dir, centered, false, right);
    if (d == armed_dir && d != active_dir) {
      const int used = static_cast<int>(std::min<size_t>(std::strlen(label), width / 6)) * 6;
      const int x0 = xs[d] + (right ? width - used : centered ? (width - used) / 2 : 0);
      oled.draw_hline(static_cast<uint8_t>(x0), static_cast<uint8_t>(x0 + used - 2), pages[d] * 8 + 7);
    }
  }
  draw_joypad(oled, active_dir);
}

void draw_notes_clock_body(OledSsd13xx& oled, const app::contract::UiModel& m,
                           const app::InputContext& input, const app::JoystickPreview& preview) {
  const int8_t active_dir = input.js.physical_dir >= 0 ? input.js.physical_dir : input.js.keypad_dir;
  draw_compass_body(oled, preview.compass, active_dir);
  const auto& p = m.config.joy_families.notes;
  const uint8_t channel = (p.channel == app::NoteChannel::kLead
      ? m.config.lead_channel : m.config.chord_channel) + 1;
  char detail[24], velocity[8];
  std::snprintf(detail, sizeof(detail), "%s O%+d ch%u", p.mono ? "Mono" : "Poly", p.octave, channel);
  // With Vel mode = Distance the stick sets each note's velocity itself.
  if (p.velocity_mode == app::NoteVelocityMode::kByDistance) std::snprintf(velocity, sizeof(velocity), "vDist");
  else std::snprintf(velocity, sizeof(velocity), "v%u", preview.velocity);
  pair(oled, 6, detail, velocity);
  joystick_register_line(oled, 7, m, input);
}

void draw_notes_walk_body(OledSsd13xx& oled, const app::contract::UiModel& m,
                          const app::InputContext& input, const app::JoystickPreview& preview) {
  if (m.config.active_preset == app::PresetId::kLead) {
    // Current note and centered next/previous hints above the full source.
    cell_text(oled, 0, 2, 64, preview.current, false, true, true);
    char next[16], previous[16];
    std::snprintf(next, sizeof(next), "^ %s", preview.up);
    std::snprintf(previous, sizeof(previous), "v %s", preview.down);
    cell_text(oled, 64, 2, 64, next, false, true);
    cell_text(oled, 64, 3, 64, previous, false, true);
    const uint8_t index = input.js.walk_index % 8;
    for (uint8_t i = 0; i < 8; ++i)
      cell_text(oled, (i % 4) * 32, 4 + i / 4, 30, preview.walk[i], i == index, true);
    char status[16];
    std::snprintf(status, sizeof(status), "%u/8 v%u", index + 1, preview.velocity);
    pair(oled, 6, "< Vel >", status);
    joystick_register_line(oled, 7, m, input);
    return;
  }
  // Walk has four actions, rather than eight fixed note destinations.
  // Diagonal gestures use the vertical action, matching the input handler.
  char cells[8][12] = {};
  std::snprintf(cells[2], sizeof(cells[2]), "%s", preview.up);
  std::snprintf(cells[6], sizeof(cells[6]), "%s", preview.down);
  std::snprintf(cells[4], sizeof(cells[4]), "%s", "Vel -");
  std::snprintf(cells[0], sizeof(cells[0]), "%s", "Vel +");
  const int8_t dir = input.js.physical_dir >= 0 ? input.js.physical_dir : input.js.keypad_dir;
  const int8_t action = dir == 1 || dir == 3 ? 2 : dir == 5 || dir == 7 ? 6 : dir;
  draw_compass_body(oled, cells, action);
  // Keep the thumb marker faithful to the actual diagonal position.
  if (action != dir) {
    oled.fill_rect(54, 25, 21, 15, false);
    draw_joypad(oled, dir);
  }
  char position[20], velocity[12];
  std::snprintf(position, sizeof(position), "%u/8 %s", input.js.walk_index % 8 + 1, preview.current);
  std::snprintf(velocity, sizeof(velocity), "Vel %u", preview.velocity);
  pair(oled, 6, position, velocity);
  joystick_register_line(oled, 7, m, input);
}

const char* cont_target_short(app::ContTarget t) {
  switch (t) {
    case app::ContTarget::kBend: return "Bend";
    case app::ContTarget::kVibrato: return "Vib";
    case app::ContTarget::kCc: return "CC";
    case app::ContTarget::kAftertouch: return "AT";
  }
  return "?";
}
const char* cont_curve_short(uint8_t v) { return v == 0 ? "lin" : v == 1 ? "exp" : v == 2 ? "log" : "?"; }

// Position and output values occupy separate areas. Numeric readings get
// their own row, including the sign and all four pitch-bend digits.
void draw_cont_body(OledSsd13xx& oled, const app::contract::UiModel& m,
                    const app::InputContext& input, const app::JoystickPreview& preview) {
  const auto& p = m.config.joy_families.cont;
  int x, y;
  joystick_xy(input, x, y);
  box(oled, 51, 18, 77, 46);
  for (uint8_t px = 54; px < 77; px += 3) oled.fill_rect(px, 32, 1, 1);
  for (uint8_t py = 20; py < 46; py += 3) oled.fill_rect(64, py, 1, 1);
  const int cx = 64 + std::clamp(x, -512, 512) * 9 / 512;
  const int cy = 32 - std::clamp(y, -512, 512) * 10 / 512;
  oled.fill_rect(cx - 2, cy - 2, 5, 5);
  oled.fill_rect(cx, cy, 1, 1, false);

  char x_label[12], y_label[12];
  if (p.x_target == app::ContTarget::kCc) std::snprintf(x_label, sizeof(x_label), "X CC%u", p.x_cc);
  else std::snprintf(x_label, sizeof(x_label), "X %s", cont_target_short(p.x_target));
  if (p.y_target == app::ContTarget::kCc) std::snprintf(y_label, sizeof(y_label), "Y CC%u", p.y_cc);
  else std::snprintf(y_label, sizeof(y_label), "Y %s", cont_target_short(p.y_target));
  cell_text(oled, 0, 2, 48, x_label);
  cell_text(oled, 80, 2, 48, y_label, false, false, false, true);
  cell_text(oled, 0, 3, 48, preview.x_value);
  cell_text(oled, 80, 3, 48, preview.y_value, false, false, false, true);
  // Both meters fill as the value rises: right on X, up on Y.
  meter(oled, 0, 37, 46, 8, (std::clamp(x, -512, 512) + 512) / 1024.0f);
  meter(oled, 81, 37, 46, 8, (std::clamp(y, -512, 512) + 512) / 1024.0f);
  char curve[16], deadzone[12];
  std::snprintf(curve, sizeof(curve), "Curve %s", cont_curve_short(p.curve));
  std::snprintf(deadzone, sizeof(deadzone), "DZ %u%%", p.deadzone);
  pair(oled, 6, curve, deadzone);
  joystick_register_line(oled, 7, m, input);
}

void draw_param_compass_body(OledSsd13xx& oled, const app::contract::UiModel& m,
                             const app::InputContext& input, const app::JoystickPreview& preview) {
  const int8_t active_dir = input.js.physical_dir >= 0 ? input.js.physical_dir : input.js.keypad_dir;
  draw_compass_body(oled, preview.compass, active_dir);
  pair(oled, 6, preview.current, m.config.joy_families.param.retrigger_on_change ? "Retrig on" : "Retrig off");
  joystick_register_line(oled, 7, m, input);
}

// CHORD arms a change for the chord keys rather than sounding anything
// itself, so the screen's job is to show what is armed: the cell is
// underlined in the compass and named on row 6.
void draw_chord_compass_body(OledSsd13xx& oled, const app::contract::UiModel& m,
                             const app::InputContext& input, const app::JoystickPreview& preview) {
  const int8_t active_dir = input.js.physical_dir >= 0 ? input.js.physical_dir : input.js.keypad_dir;
  const bool voicings = m.config.active_preset == app::PresetId::kVoicings;
  const char* armed = voicings
      ? engine::voicing_spread_name(static_cast<engine::VoicingSpread>(std::min<uint8_t>(m.config.voicing_spread, 7)))
      : input.engine.modifier().quality_name();
  int8_t armed_dir = -1;
  for (int8_t d = 0; d < 8 && armed[0]; ++d)
    if (std::strcmp(preview.compass[d], armed) == 0) { armed_dir = d; break; }
  draw_compass_body(oled, preview.compass, active_dir, armed_dir);
  if (voicings) {
    pair(oled, 6, "Voicing", armed);
  } else {
    const auto& p = m.config.joy_families.chord;
    const char* hint = p.order == app::ChordOrder::kRanking ? input.js.modal_hint : nullptr;
    pair(oled, 6, hint ? hint : "Next chord", armed[0] ? compass_label(armed) : "as scale");
  }
  joystick_register_line(oled, 7, m, input);
}

// Every joystick family has its own body: NOTES' compass (or walk strip),
// CONT's 2D pad, and CHORD and PARAM share NOTES' compass.
void draw_joystick(OledSsd13xx& oled, const app::contract::UiModel& m,
                   const app::InputContext& input) {
  const auto family = app::kPresets[static_cast<size_t>(m.config.active_preset)].family;
  switch (family) {
    case app::JoyFamily::kNotes: {
      draw_joystick_header(oled, m, input, "NOTES");
      const auto preview = app::joystick_preview(input);
      if (m.config.joy_families.notes.gesture == app::NoteGesture::kClock)
        draw_notes_clock_body(oled, m, input, preview);
      else
        draw_notes_walk_body(oled, m, input, preview);
      return;
    }
    case app::JoyFamily::kChord: {
      draw_joystick_header(oled, m, input, "CHORD");
      draw_chord_compass_body(oled, m, input, app::joystick_preview(input));
      return;
    }
    case app::JoyFamily::kCont: {
      // Pitch bend follows the chords; controllers go out on the joy channel.
      const auto& cont = m.config.joy_families.cont;
      const auto bends = [](app::ContTarget t) { return t == app::ContTarget::kBend || t == app::ContTarget::kVibrato; };
      const bool bend_only = bends(cont.x_target) && bends(cont.y_target);
      char channel[8];
      std::snprintf(channel, sizeof(channel), "ch%u", (bend_only ? m.config.chord_channel : m.config.lead_channel) + 1);
      draw_joystick_header(oled, m, input, "CONT", channel);
      draw_cont_body(oled, m, input, app::joystick_preview(input));
      return;
    }
    case app::JoyFamily::kParam: {
      char tempo[10];
      std::snprintf(tempo, sizeof(tempo), "%ldbpm", std::lround(m.config.bpm));
      draw_joystick_header(oled, m, input, "PARAM", tempo);
      draw_param_compass_body(oled, m, input, app::joystick_preview(input));
      return;
    }
    default:
      return;
  }
}

// Keypad-shaped chord map, sounding harmony, then one explicit encoder
// assignment. Settings retain their existing cursor order and edit behavior.
void draw_play(OledSsd13xx& oled, const app::contract::UiModel& m,
               const app::InputContext& input) {
  char harmony[24];
  std::snprintf(harmony, sizeof(harmony), "%s %s", m.config.key_name, m.config.scale_name);
  // A colour armed from the joystick's CHORD presets recolours every chord
  // key until it is disarmed; say so where the key and scale are read.
  char armed[12] = {};
  if (input.engine.modifier().quality_name()[0])
    std::snprintf(armed, sizeof(armed), "+%s", compass_label(input.engine.modifier().quality_name()));
  pair(oled, 0, harmony, armed, true);
  cell_text(oled, 0, 1, 72, m.active_chord_symbol, false, true, true);
  cell_text(oled, 76, 1, 52, m.active_chord_degree);
  char reg[12];
  std::snprintf(reg, sizeof(reg), "O%+d I:%s", input.engine.modifier().octave_shift(), m.inversion_code);
  cell_text(oled, 76, 2, 52, reg);
  center(oled, 3, input.nav.active_degree < 1 ? "Press a chord key" : m.active_chord_notes);

  for (uint8_t i = 0; i < 7; ++i) {
    if (!m.pad_symbols[i][0]) continue;
    const uint8_t x = (i % 3) * 43;
    const uint8_t page = 4 + i / 3;
    const bool selected = input.nav.active_degree == i + 1;
    const bool sounding = selected && m.performance_active;
    char label[12];
    std::snprintf(label, sizeof(label), "%u%s", i + 1, m.pad_symbols[i]);
    cell_text(oled, x, page, 42, label, sounding);
    // Reverse only the key number in idle cells, making number and chord
    // visually distinct even when a six-character extension uses all space.
    if (!sounding) {
      char key[2] = {static_cast<char>('1' + i), '\0'};
      cell_text(oled, x, page, 6, key, true);
    }
    if (selected && !sounding) oled.draw_hline(x, x + 40, page * 8 + 7);
  }
  // 8/9 are inversion shortcuts, never extra or missing chord slots.
  cell_text(oled, 43, 6, 42, "8 Inv-");
  cell_text(oled, 86, 6, 42, "9 Inv+");

  char label[24] = {}, value[24] = {};
  app::parameters::format(m.config, m.navigation.parameter, label, sizeof(label), value, sizeof(value));
  pair(oled, 7, short_label(m.navigation.parameter, label), value, true);
}

// Mirrors the boot emblem at a glance, then a details page. Reused from the
// System / Info row so players can find build and credit info without a host.
void draw_info(OledSsd13xx& oled, uint8_t page) {
  if (page == 0) {
    oled.draw_hline(8, 37, 17); oled.draw_hline(90, 119, 17);
    oled.draw_vline(37, 13, 17); oled.draw_vline(90, 17, 21);
    oled.fill_rect(5, 16, 3, 3); oled.fill_rect(120, 16, 3, 3);
    for (uint8_t i = 0; i < 3; ++i) {
      const uint8_t x = 48 + i * 12; const uint8_t y = 21 - i * 4;
      oled.fill_rect(x, y, 7, 4); oled.fill_rect(x + 2, y - 1, 5, 1); oled.fill_rect(x + 5, y - 12, 2, 13);
    }
    oled.draw_text_2x(16, 4, "o3CHORDS");
    oled.draw_hline(16, 111, 49);
    center(oled, 7, "click: more info");
    return;
  }
  text(oled, 0, "SYSTEM / INFO", true);
  char version[24];
  std::snprintf(version, sizeof(version), "Firmware %s", O3CHORDS_VERSION);
  center(oled, 2, version);
  center(oled, 3, "Created by no3z");
  center(oled, 5, "ESP-IDF + TinyUSB");
  center(oled, 6, "NimBLE (BLE MIDI)");
  center(oled, 7, "click: close");
}

// The wheel: ParamId::Key's own editor instead of the generic bar-graph value
// screen. Circle-of-fifths layout, current key knocked out; up to 3
// suggest_modulation() destinations (the nearest keys where the active
// chord's out-of-scale notes become diatonic) get an outline box, and the
// footer names the best one's pivot chord (pivot_chord_mask()'s lowest shared
// diatonic degree) - the chord that makes the jump land somewhere, not just a
// cut. The framebuffer only addresses text at 8px page rows, so the ellipse
// is approximated with 5 rows (pages 2-6) and continuous per-position X - a
// real circle, compressed to what a 128x64 mono OLED can actually draw text
// on.
//
// Key edits stay live on every encoder tick here, same as every other
// ParamId (mutate() already applied before this renders). Applying the
// change only on release would need a deferred-commit mechanism unique to
// this one parameter, so it is left as a known simplification.
void draw_key_wheel(OledSsd13xx& oled, const app::contract::UiModel& m,
                    const app::InputContext& input) {
  pair(oled, 0, "KEY", m.config.key_name, true);
  center(oled, 1, "circle of fifths");

  struct FifthsSlot { const char* name; uint8_t semitone; };
  static constexpr FifthsSlot kFifths[12] = {
      {"C", 0}, {"G", 7}, {"D", 2}, {"A", 9}, {"E", 4}, {"B", 11},
      {"F#", 6}, {"C#", 1}, {"G#", 8}, {"D#", 3}, {"A#", 10}, {"F", 5},
  };

  const auto& s = input.app_state.settings();
  const int current_key = engine::key_offset_from_str(s.key);

  const int8_t* sc_steps = nullptr;
  size_t sc_len = 0;
  engine::scale_steps_for_name(s.scale, &sc_steps, &sc_len);
  bool in_scale[12] = {};
  if (sc_steps)
    for (size_t i = 0; i < sc_len; ++i)
      in_scale[static_cast<size_t>((current_key + sc_steps[i]) % 12)] = true;

  // Foreign pitch classes come from the active degree's sounding chord -
  // with nothing held there is nothing foreign, so no destination is marked.
  uint8_t foreign[16] = {};
  size_t n_foreign = 0;
  const auto& steps = input.engine.progression().steps();
  if (input.nav.active_degree >= 1 && static_cast<size_t>(input.nav.active_degree) <= steps.size()) {
    const auto step = input.engine.modifier().apply(steps[static_cast<size_t>(input.nav.active_degree - 1)]);
    const auto notes = engine::progression_step_chord_notes(s, input.engine, step);
    for (uint8_t note : notes) {
      const uint8_t pc = static_cast<uint8_t>(note % 12);
      if (in_scale[pc]) continue;
      bool dup = false;
      for (size_t i = 0; i < n_foreign; ++i) if (foreign[i] == pc) dup = true;
      if (!dup && n_foreign < 16) foreign[n_foreign++] = pc;
    }
  }

  engine::ModulationSuggestion suggestions[3] = {};
  const size_t n_sugg = n_foreign ? engine::suggest_modulation(s, foreign, n_foreign, suggestions, 3) : 0;

  constexpr int kCx = 64;
  constexpr float kRx = 50.0f;
  for (uint8_t i = 0; i < 12; ++i) {
    const float rad = (static_cast<float>(i) * 30.0f - 90.0f) * 3.14159265f / 180.0f;
    const uint8_t page = static_cast<uint8_t>(2 + std::lround((std::sin(rad) + 1.0f) / 2.0f * 4.0f));
    const int label_w = static_cast<int>(std::strlen(kFifths[i].name)) * 6;
    const int x = kCx + static_cast<int>(kRx * std::cos(rad)) - label_w / 2;
    const uint8_t px = static_cast<uint8_t>(std::clamp(x, 0, 128 - label_w));

    const bool is_current = kFifths[i].semitone == static_cast<uint8_t>(current_key);
    int dest_idx = -1;
    for (size_t j = 0; j < n_sugg; ++j)
      if (std::strcmp(suggestions[j].target_key, kFifths[i].name) == 0) dest_idx = static_cast<int>(j);

    if (is_current) {
      oled.fill_rect(px > 0 ? static_cast<uint8_t>(px - 1) : 0, static_cast<uint8_t>(page * 8), static_cast<uint8_t>(label_w + 1), 8);
      oled.draw_text(px, page, kFifths[i].name, true);
    } else {
      oled.draw_text(px, page, kFifths[i].name);
      if (dest_idx >= 0) {
        box(oled, px > 1 ? static_cast<uint8_t>(px - 2) : 0, static_cast<uint8_t>(page * 8 - 1),
            static_cast<uint8_t>(px + label_w + 1), static_cast<uint8_t>(page * 8 + 8));
      }
    }
  }

  if (n_sugg) {
    const uint8_t mask = engine::pivot_chord_mask(s, suggestions[0].target_key);
    uint8_t degree = 1;
    for (uint8_t d = 1; d <= 7; ++d) if (mask & (1u << (d - 1))) { degree = d; break; }
    char footer[22];
    std::snprintf(footer, sizeof(footer), "-> %s via %s", suggestions[0].target_key,
                  mask ? engine::roman_degree(degree) : "?");
    center(oled, 7, footer);
  } else {
    center(oled, 7, "Hold chord for hints");
  }
}

void draw_model(OledSsd13xx& oled, const app::contract::UiModel& m,
                const app::InputContext& input) {
  if (m.info_open) { draw_info(oled, m.info_page); return; }
  char right[32];
  const bool system = m.navigation.filter == app::contract::ParameterFilter::System;
  if (m.confirmation.action != app::contract::ConfirmationAction::None) {
    text(oled, 0, "SYSTEM / CONFIRM", true);
    const auto action = m.confirmation.action;
    center(oled, 2, action == app::contract::ConfirmationAction::Dfu ? "Restart for firmware?" :
        action == app::contract::ConfirmationAction::Reset ? "Restore defaults?" :
        action == app::contract::ConfirmationAction::Reboot ? "Restart device?" : "Stop all MIDI notes?");
    text(oled, 4, "Cancel", !m.confirmation.selected);
    text(oled, 5, "Confirm", m.confirmation.selected);
    center(oled, 7, action == app::contract::ConfirmationAction::Reset ? "All settings replaced" :
        action == app::contract::ConfirmationAction::Dfu ? "Firmware update mode" :
        action == app::contract::ConfirmationAction::Reboot ? "Settings kept" : "All channels silent");
    return;
  }
  if (m.navigation.mode == NavigationMode::Edit && !system && m.navigation.parameter == ParamId::Key) {
    draw_key_wheel(oled, m, input);
    return;
  }
  if (m.navigation.mode == NavigationMode::Edit && !system) {
    text(oled, 0, filter_name(m.navigation.filter), true);
    const auto* d = app::parameters::find(m.navigation.parameter);
    center(oled, 1, d ? d->label : "Parameter");
    char label[24] = {}, value[24] = {};
    app::parameters::format(m.config, m.navigation.parameter, label, sizeof(label), value, sizeof(value));
    const size_t len = std::strlen(value);
    if (len && len <= 10) oled.draw_text_2x((128 - len * 12) / 2, 3, value);
    else center(oled, 3, value);
    if (d && d->max > d->min) {
      oled.draw_hline(6, 121, 46);
      const auto r = app::parameters::read(m.config, m.navigation.parameter);
      const int pos = 6 + (std::clamp(r.value, d->min, d->max) - d->min) * 115 / (d->max - d->min);
      oled.fill_rect(pos - 2, 44, 5, 4);
    }
    pair(oled, 6, m.config.key_name, m.config.scale_name);
    std::snprintf(right, sizeof(right), "%d BPM", static_cast<int>(m.config.bpm));
    pair(oled, 7, m.articulation_name, right);
    return;
  }
  if (m.navigation.mode == NavigationMode::Select || m.navigation.mode == NavigationMode::Edit) {
    const bool editing = m.navigation.mode == NavigationMode::Edit;
    uint8_t selected = 0;
    for (uint8_t i = 0; i < m.row_count; ++i) if (m.rows[i].selected) selected = i;
    std::snprintf(right, sizeof(right), "%u/%u", m.row_count ? selected + 1 : 0, m.row_count);
    // System's header doubles as the status line: settings that are not
    // being saved matter more than a pending reboot, which matters more
    // than the row counter.
    const char* status = system && m.storage_blocked ? "NO SAVE" : system && m.restart_required ? "REBOOT" : right;
    pair(oled, 0, filter_name(m.navigation.filter), status, true);
    const int visible_rows = system ? 6 : 7;
    const int start = std::clamp(static_cast<int>(selected) - 2, 0, std::max(0, static_cast<int>(m.row_count) - visible_rows));
    for (int i = start; i < m.row_count && i < start + visible_rows; ++i) {
      const auto& row = m.rows[i];
      const char* value = row.value;
      if (editing && row.selected) {
        std::snprintf(right, sizeof(right), "<%.17s>", row.value);
        value = right;
      }
      pair(oled, i - start + 1, short_label(row.parameter, row.label), value, row.selected);
    }
    if (system) connection_bar(oled, m);
    return;
  }
  if (m.joystick_mode) draw_joystick(oled, m, input);
  else draw_play(oled, m, input);
}

}  // namespace

bool render_boot_screen(OledSsd13xx& oled) {
  oled.clear();
  // Three rising note stems form a chord emblem, flanked by signal traces.
  // Pixel geometry stays sharp on the native monochrome display.
  oled.draw_hline(8, 37, 17);
  oled.draw_hline(90, 119, 17);
  oled.draw_vline(37, 13, 17);
  oled.draw_vline(90, 17, 21);
  oled.fill_rect(5, 16, 3, 3);
  oled.fill_rect(120, 16, 3, 3);
  for (uint8_t i = 0; i < 3; ++i) {
    const uint8_t x = 48 + i * 12;
    const uint8_t y = 21 - i * 4;
    oled.fill_rect(x, y, 7, 4);
    oled.fill_rect(x + 2, y - 1, 5, 1);
    oled.fill_rect(x + 5, y - 12, 2, 13);
  }
  oled.draw_text_2x(16, 4, "o3CHORDS");
  oled.draw_hline(16, 111, 49);
  center(oled, 7, "HARMONY IN YOUR HANDS");
  return oled.flush();
}

void draw_calibration(OledSsd13xx& oled, const ProductionRenderCtx& context) {
  const auto& u = context.ui;
  using R = drivers::CalibrationResult;
  text(oled, 0, "JOYSTICK CALIBRATION", true);
  center(oled, 1, u.calibration_result == R::Sampling ? "Keep joystick still" :
      u.calibration_result == R::Saved ? "Calibration saved" :
      u.calibration_result == R::Unstable ? "Movement detected" :
      u.calibration_result == R::Error ? "Read / save failed" : "Release the joystick");
  center(oled, 2, u.calibration_result == R::Ready ? "Confirm to measure" :
      u.calibration_result == R::Sampling ? "Measuring center..." :
      u.calibration_result == R::Saved ? "Center and noise set" : "Confirm to retry");
  oled.draw_hline(4, 123, 30);
  if (u.calibration_progress) oled.fill_rect(4, 27, u.calibration_progress * 120 / 100, 4);
  char left[24], right[24];
  std::snprintf(left, sizeof(left), "Center X %u", u.calibration_x);
  std::snprintf(right, sizeof(right), "Y %u", u.calibration_y);
  pair(oled, 4, left, right);
  std::snprintf(right, sizeof(right), "%u ADC", u.calibration_zone);
  pair(oled, 5, "Neutral zone", right);
  pair(oled, 6, "Filter", "4x + smooth");
  std::snprintf(left, sizeof(left), "X %+d", context.input.js.raw_x);
  std::snprintf(right, sizeof(right), "Y %+d", context.input.js.raw_y);
  pair(oled, 7, left, right);
}

bool render_production_screen(OledSsd13xx& oled, const ProductionRenderCtx& context) {
  oled.clear();
  if (context.ui.calibration_open) draw_calibration(oled, context);
  else draw_model(oled, app::prod::model_snapshot(context.input, context.ui), context.input);
  return oled.flush();
}
}  // namespace ui
