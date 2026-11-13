#include "app/input_joystick.h"
#include "app/config_codec.h"
#include "app/harmony_transform.h"
#include "app/modulation_recipe.h"
#include "app/voice_recipe.h"
#include "app/settings_bridge.h"
#include "app/parameters.h"
#include "engine/chord_analysis.h"
#include "engine/chord_distance.h"
#include "engine/chord_substitutions.h"
#include "engine/music_theory.h"
#include "engine/pitch_bend.h"

#include <algorithm>
#include <cstdio>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

// The joystick runs on a 4-family model: NOTES fires its own notes, CHORD
// transforms the sounding chord via ChordModifier, CONT streams a continuous
// position, PARAM edits a live Settings/engine parameter per axis. Behavior
// is resolved from Config::active_preset (-> family) and Config::joy_families
// (that family's parameters), not from a per-recipe function table.
namespace app {

// -- Local helpers (joystick-only) ------------------------------------------------

static bool joy_get_step(InputContext& ctx, size_t hint_idx,
                         engine::ProgressionStep* out_step, size_t* out_idx) {
  const auto& steps = ctx.engine.progression().steps();
  if (steps.empty()) return false;
  size_t idx = hint_idx;
  if (idx >= steps.size()) idx = 0;
  if (out_step) *out_step = steps[idx];
  if (out_idx) *out_idx = idx;
  return true;
}

static bool joy_current_step(InputContext& ctx,
                             engine::ProgressionStep* out_step, size_t* out_idx) {
  const auto& steps = ctx.engine.progression().steps();
  if (steps.empty()) return false;
  size_t idx = 0;
  if (ctx.nav.active_degree >= 1 &&
      static_cast<size_t>(ctx.nav.active_degree) <= steps.size()) {
    idx = static_cast<size_t>(ctx.nav.active_degree - 1);
  } else if (ctx.js.browse_index < steps.size()) {
    idx = ctx.js.browse_index;
  }
  return joy_get_step(ctx, idx, out_step, out_idx);
}

// The 8 progression-degree root notes (I..VII plus VIII = octave of I). For a
// diatonic progression these are also the 8 running scale notes, which is why
// NoteSource::kDegrees and NoteSource::kScale share this one builder — this
// engine has no separate "global scale independent of chord roots" function,
// and for a diatonic progression the two concepts coincide.
static std::array<uint8_t, 8> keymap_root_notes(InputContext& ctx, int base_octave) {
  std::array<uint8_t, 8> notes{};
  notes.fill(60);
  const auto& steps = ctx.engine.progression().steps();
  if (steps.empty()) return notes;

  const auto& s = ctx.app_state.settings();
  const size_t filled = std::min<size_t>(7, steps.size());
  for (size_t i = 0; i < filled; ++i) {
    engine::ProgressionStep st = ctx.engine.modifier().apply(steps[i]);
    notes[i] = engine::clamp_midi_note(
        engine::progression_step_root_midi(s, st, base_octave));
  }
  for (size_t i = filled; i < 7; ++i) {
    notes[i] = notes[(i == 0) ? 0 : (i - 1)];
  }
  notes[7] = engine::clamp_midi_note(static_cast<int>(notes[0]) + 12);
  return notes;
}

static std::vector<uint8_t> current_chord_notes(InputContext& ctx, int base_octave) {
  engine::ProgressionStep st{};
  size_t idx = 0;
  if (!joy_current_step(ctx, &st, &idx)) return {};
  st = ctx.engine.modifier().apply(st);
  const auto& s = ctx.app_state.settings();
  return engine::progression_step_chord_notes(s, ctx.engine, st, base_octave);
}

// Build 8 chord tones for clock mapping, extending across octaves.
static std::array<uint8_t, 8> chord_tone_clock(InputContext& ctx, int base_octave) {
  std::array<uint8_t, 8> result{};
  result.fill(60);
  auto notes = current_chord_notes(ctx, base_octave);
  if (notes.empty()) return result;

  size_t out = 0;
  int oct = 0;
  while (out < 8) {
    for (size_t i = 0; i < notes.size() && out < 8; ++i) {
      int note = static_cast<int>(notes[i]) + oct * 12;
      result[out++] = engine::clamp_midi_note(note);
    }
    ++oct;
  }
  return result;
}

// Apply joystick rotation + flip settings to raw analog X/Y.
static void apply_joy_transform(const Settings& s, int16_t& x, int16_t& y) {
  for (uint8_t i = 0; i < s.joystick_rotation; ++i) {
    int16_t tmp = x;
    x = -y;
    y = tmp;
  }
  if (s.joystick_flip_h) {
    x = -x;
  }
  if (s.joystick_flip_v) {
    y = -y;
  }
}

// Stick deflection as 0..1 of full scale (js.raw_x/raw_y are normalised by
// process_joystick()).
static float joy_deflection(const InputContext& ctx) {
  const float x = ctx.js.raw_x, y = ctx.js.raw_y;
  return std::min(1.0f, std::sqrt(x * x + y * y) / static_cast<float>(modulation::kAxisFullScale));
}

// Compute velocity (20-127) from analog joystick distance to center.
static uint8_t velocity_from_distance(const InputContext& ctx, uint8_t curve = 0) {
  float norm = joy_deflection(ctx);
  // Apply curve: 0=linear, 1=exp (more dynamic range), 2=log (more even)
  if (curve == 1) norm = norm * norm;
  else if (curve == 2) norm = std::sqrt(norm);
  uint8_t vel = static_cast<uint8_t>(20 + norm * 107);
  if (vel < 1) vel = 1;
  if (vel > 127) vel = 127;
  return vel;
}

// Harmony interval offsets in semitones — the fixed stack (octave, fifth,
// third, sixth, and their inversions) that gives NoteSource::kIntervals its
// character.
static constexpr int8_t kHarmonyIntervals[8] = {12, 7, 4, 9, -12, -5, -3, -15};

static uint8_t bend_channel(const InputContext& ctx) {
  const auto& s = ctx.app_state.settings();
  return is_chord_screen(ctx) ? s.midi_channel : s.midi_channel_joy;
}

static int wrap_index(int idx, int size) {
  if (size <= 0) return 0;
  idx %= size;
  if (idx < 0) idx += size;
  return idx;
}

// Vertical step from joystick direction (up = +1, down = -1).
static int joystick_vertical_step(int8_t dir) {
  switch (dir) {
    case 2: case 1: case 3: return 1;   // N, NE, NW = up
    case 6: case 5: case 7: return -1;  // S, SE, SW = down
    default: return 0;
  }
}

// Left/Right step from joystick direction.
static int joystick_lr_step(int8_t dir) {
  switch (dir) {
    case 0: return 1;   // E = right
    case 4: return -1;  // W = left
    default: return 0;
  }
}

// Get current chord intervals for bend calculations.
static std::vector<int8_t> current_chord_intervals(InputContext& ctx) {
  std::vector<int8_t> intervals = {0};
  if (ctx.nav.active_degree >= 1 && ctx.nav.active_degree <= 7) {
    const auto& steps = ctx.engine.progression().steps();
    if (static_cast<size_t>(ctx.nav.active_degree - 1) < steps.size()) {
      engine::ProgressionStep mod = ctx.engine.modifier().apply(steps[ctx.nav.active_degree - 1]);
      for (const auto& c : ctx.engine.chord_library().chords()) {
        if (c.name == mod.chord_quality) {
          intervals = c.intervals;
          break;
        }
      }
    }
  }
  return intervals;
}

// Get the MIDI root note of the active chord (or degree I).
static uint8_t active_chord_root_midi(InputContext& ctx, int base_octave) {
  const auto& steps = ctx.engine.progression().steps();
  if (steps.empty()) return 60;
  size_t idx = 0;
  if (ctx.nav.active_degree >= 1 &&
      static_cast<size_t>(ctx.nav.active_degree - 1) < steps.size())
    idx = static_cast<size_t>(ctx.nav.active_degree - 1);
  engine::ProgressionStep st = ctx.engine.modifier().apply(steps[idx]);
  return engine::clamp_midi_note(
      engine::progression_step_root_midi(ctx.app_state.settings(), st, base_octave));
}

// Build pentatonic scale notes from current key. NOTES' Source=pentatonic is
// a single value on the wire; major vs. minor is inferred from the current
// scale rather than chosen by a separate sub-mode, as a deliberate
// simplification.
static constexpr int8_t kPentaMajor[] = {0, 2, 4, 7, 9};        // 5 notes
static constexpr int8_t kPentaMinor[] = {0, 3, 5, 7, 10};       // 5 notes

static std::array<uint8_t, 8> pentatonic_notes(InputContext& ctx, int base_octave) {
  std::array<uint8_t, 8> result{};
  result.fill(60);
  const auto& s = ctx.app_state.settings();
  // Octave 4 is middle C's octave (MIDI 60), as in progression_step_root_midi().
  int root = engine::key_offset_from_str(s.key) + (base_octave + 1) * 12;
  const bool minor = std::strstr(s.scale, "Minor") != nullptr;
  const int8_t* intervals = minor ? kPentaMinor : kPentaMajor;

  size_t out = 0;
  int oct = 0;
  while (out < 8) {
    for (size_t i = 0; i < 5 && out < 8; ++i) {
      result[out++] = engine::clamp_midi_note(root + intervals[i] + oct * 12);
    }
    ++oct;
  }
  return result;
}

// -- NOTES family -------------------------------------------------------------

static uint8_t notes_channel_for(const InputContext& ctx, NoteChannel ch) {
  const auto& s = ctx.app_state.settings();
  return ch == NoteChannel::kChord ? s.midi_channel : s.midi_channel_joy;
}

// Source=scale: the plain scale tones from the current key (7 scale degrees
// plus the octave of the root), independent of the progression's borrowed-
// chord transpositions — the concept Source=degrees (keymap_root_notes(),
// below) intentionally keeps, since "the roots of the diatonic chords" is a
// distinct, meaningful reading from "the plain scale" once a genre mode
// borrows chords outside the scale.
static std::array<uint8_t, 8> scale_walk_notes(InputContext& ctx, int base_octave) {
  std::array<uint8_t, 8> notes{};
  const auto& s = ctx.app_state.settings();
  // Same octave convention as Source=degrees (octave 4 = MIDI 60), so
  // switching Source never jumps the register by an octave.
  const int root = engine::key_offset_from_str(s.key) + (base_octave + 1) * 12;
  const int8_t* steps = nullptr;
  size_t nsteps = 0;
  engine::scale_steps_for_name(s.scale, &steps, &nsteps);
  for (size_t i = 0; i < 7; ++i)
    notes[i] = engine::clamp_midi_note(root + (steps && nsteps ? steps[i % nsteps] : 0));
  notes[7] = engine::clamp_midi_note(root + 12);
  return notes;
}

// The 8 candidate notes for a NOTES source, at the given absolute octave.
static std::array<uint8_t, 8> notes_for_source(InputContext& ctx, NoteSource src, int base_octave) {
  switch (src) {
    case NoteSource::kChordTones:
      return chord_tone_clock(ctx, base_octave);
    case NoteSource::kIntervals: {
      std::array<uint8_t, 8> result{};
      const uint8_t root = active_chord_root_midi(ctx, base_octave);
      for (int i = 0; i < 8; ++i)
        result[i] = engine::clamp_midi_note(static_cast<int>(root) + kHarmonyIntervals[i]);
      return result;
    }
    case NoteSource::kPentatonic:
      return pentatonic_notes(ctx, base_octave);
    case NoteSource::kScale:
      return scale_walk_notes(ctx, base_octave);
    case NoteSource::kDegrees:
    default:
      return keymap_root_notes(ctx, base_octave);
  }
}

// The full diatonic chord at progression step `step_idx`, for Voices=chord.
static std::vector<uint8_t> notes_chord_at_step(InputContext& ctx, size_t step_idx, int base_octave) {
  const auto& steps = ctx.engine.progression().steps();
  if (steps.empty()) return {};
  if (step_idx >= steps.size()) step_idx = 0;
  engine::ProgressionStep st = ctx.engine.modifier().apply(steps[step_idx]);
  return engine::progression_step_chord_notes(ctx.app_state.settings(), ctx.engine, st, base_octave);
}

// Sustain=momentary playback. Mono=on cuts whatever was sounding first (the
// previous note); Mono=off adds to the currently active note set (up to the
// 8-voice cap already enforced by joy_add_note), so overlapping gestures can
// ring together.
static void notes_trigger_momentary(InputContext& ctx, const NotesParams& p,
                                    const std::vector<uint8_t>& notes, uint8_t velocity) {
  if (notes.empty()) return;
  const uint8_t ch = notes_channel_for(ctx, p.channel);
  const auto& s = ctx.app_state.settings();
  const bool held = play_trigger_sustains(s.play_trigger);
  const uint16_t len = s.solo_note_length_ms;
  if (p.mono) {
    joy_play_notes(ctx, notes, ch, velocity, !held, len);
    return;
  }
  for (uint8_t n : notes) joy_add_note(ctx, n, ch, velocity);
  ctx.js.note_one_shot = !held;
  if (!held) ctx.js.note_off_due_ms = ctx.t + len;
}

// Sustain=latch: one sustained note (or chord, if Voices=chord) per slot.
// `slot` is the compass direction for Gesture=clock, or the walk index for
// Gesture=walk — either way, one of 8 independent sustain slots that starts
// and stops with the same slot pressed again (the same direction again
// un-latches). Mono=on restricts this to one active slot at a time, same
// rule as momentary playback.
//
// When the keypad chord changes underneath a latched Source=degrees/
// chord-tones note, this holds the old pitches rather than re-voicing them —
// notes are computed once at the acquire below and never revisited, so an
// already-latched direction never re-reads the progression.
static void notes_latch_toggle(InputContext& ctx, const NotesParams& p, uint8_t slot,
                               const std::vector<uint8_t>& notes) {
  const uint8_t mask = static_cast<uint8_t>(1u << slot);
  if (ctx.js.drone_active_mask & mask) {
    joy_stop_drone(ctx, slot);
    ctx.nav.dirty = true;
    return;
  }
  if (notes.empty()) return;
  if (p.mono) {
    for (uint8_t d = 0; d < 8; ++d) joy_stop_drone(ctx, d);
  }
  const uint8_t ch = notes_channel_for(ctx, p.channel);
  const auto& s = ctx.app_state.settings();
  const auto source = static_cast<contract::SourceId>(0x0200 + slot);
  ctx.js.drone_channels[slot] = ch;
  ctx.js.drone_chords[slot].clear();
  ctx.js.drone_tokens[slot].clear();
  if (notes.size() == 1) {
    contract::VoiceToken token{};
    if (!joy_note_acquire(ctx, source, ch, notes[0], s.solo_velocity, token)) return;
    ctx.js.drone_chords[slot].push_back(notes[0]);
    ctx.js.drone_tokens[slot].push_back(token);
  } else {
    contract::NoteIntent intents[8]{};
    contract::VoiceToken tokens[8]{};
    const size_t count = std::min<size_t>(notes.size(), 8);
    const uint8_t routes = ctx.router.active_route_mask();
    for (size_t i = 0; i < count; ++i)
      intents[i] = {source, 0, ch, notes[i], s.solo_velocity, routes};
    if (!ctx.note_ledger || !ctx.note_ledger->acquire_batch(intents, count, tokens)) {
      if (ctx.note_ledger) ctx.note_ledger->invalidate_source(source);
      return;
    }
    for (size_t i = 0; i < count; ++i) {
      ctx.js.drone_chords[slot].push_back(notes[i]);
      ctx.js.drone_tokens[slot].push_back(tokens[i]);
    }
  }
  ctx.js.drone_notes[slot] = notes[0];
  ctx.js.drone_active_mask |= mask;
  ctx.nav.dirty = true;
}

static void handle_notes(InputContext& ctx, const NotesParams& p, int8_t dir, bool dc,
                        drivers::JoystickAnalog* joystick) {
  const int base_octave = 4 + p.octave + ctx.js.note_octave;
  const auto velocity_for = [&]() -> uint8_t {
    return (p.velocity_mode == NoteVelocityMode::kByDistance && joystick)
        ? velocity_from_distance(ctx, p.velocity_curve)
        : ctx.app_state.settings().solo_velocity;
  };

  if (p.gesture == NoteGesture::kClock) {
    if (!dc || dir < 0) return;
    const uint8_t d = static_cast<uint8_t>(dir);
    const uint8_t ni = kClockMap[d];
    std::vector<uint8_t> notes;
    uint8_t single_note = 0;
    if (p.voices == NoteVoices::kChord) {
      const int chord_octave = 3 + p.octave + ctx.js.note_octave;
      if (p.source == NoteSource::kDegrees || p.source == NoteSource::kScale) {
        const auto& steps = ctx.engine.progression().steps();
        const size_t step_idx = (!steps.empty() && ni < steps.size()) ? ni : 0;
        ctx.js.browse_index = static_cast<uint8_t>(step_idx);
        ctx.nav.active_degree = static_cast<int>(step_idx + 1);
        notes = notes_chord_at_step(ctx, step_idx, chord_octave);
      } else {
        // ChordTones/Intervals/Pentatonic have no progression-quality table
        // to build a chord from, so stack every other note of the source's
        // own 8-note array (root, +2, +4 - the same "skip a step" logic a
        // diatonic triad uses over the scale) - what makes a latched chord
        // over intervals possible.
        auto arr = notes_for_source(ctx, p.source, chord_octave);
        notes = {arr[ni], arr[(ni + 2) % 8], arr[(ni + 4) % 8]};
        ctx.js.browse_index = ni;
      }
    } else {
      auto arr = notes_for_source(ctx, p.source, base_octave);
      single_note = arr[ni];
      notes = {single_note};
      ctx.js.browse_index = ni;
    }
    if (p.sustain == NoteSustain::kLatch) {
      notes_latch_toggle(ctx, p, d, notes);
    } else {
      notes_trigger_momentary(ctx, p, notes, velocity_for());
      if (single_note) ctx.js.last_note = single_note;
    }
    ctx.nav.dirty = true;
    return;
  }

  // Gesture=walk: up/down step through the source, left/right adjusts velocity.
  if (!dc || dir < 0) return;
  const int vstep = joystick_vertical_step(dir);
  const int hstep = joystick_lr_step(dir);
  if (vstep != 0) {
    auto arr = notes_for_source(ctx, p.source, base_octave);
    const int next = static_cast<int>(ctx.js.walk_index) + vstep;
    ctx.js.walk_index = static_cast<uint8_t>(wrap_index(next, 8));
    const uint8_t note = arr[ctx.js.walk_index];
    if (p.sustain == NoteSustain::kLatch) {
      notes_latch_toggle(ctx, p, ctx.js.walk_index, {note});
    } else {
      notes_trigger_momentary(ctx, p, {note}, velocity_for());
    }
    ctx.js.last_note = note;
  } else if (hstep != 0) {
    // Goes through Config like an encoder edit, so the menu's Vel row, the
    // screen and the stored settings all agree. min=1: a note-on with
    // velocity 0 is a note-off by spec.
    auto next = ctx.app_state.config();
    next.solo_velocity = static_cast<uint8_t>(std::clamp(static_cast<int>(next.solo_velocity) + hstep * 10, 1, 127));
    commit_config(ctx, next);
  }
  ctx.nav.dirty = true;
}

// -- CHORD family -------------------------------------------------------------

// Rebuild discover mode's tension-sorted quality slots for the active degree.
// Uses consonance scoring and hamming distance for richer ranking than
// simple tension count alone. Feeds ChordParams::order == kRanking.
void discover_rebuild_slots(InputContext& ctx) {
  const auto& s = ctx.app_state.settings();
  const auto& cfg = ctx.app_state.config();
  const auto& map = kQualityMaps[std::min<uint8_t>(cfg.joy_families.chord.palette, kQualityMapCount - 1)];
  engine::ProgressionStep current_step{};
  const bool has_step = joy_current_step(ctx, &current_step, nullptr);
  const uint8_t degree = has_step ? current_step.scale_degree : 1;

  // Build the current chord's MIDI notes for distance comparison.
  auto current_notes = engine::progression_step_chord_notes(s, ctx.engine, current_step);

  // Compute tension + consonance + hamming distance for each quality.
  struct Entry {
    const char* q;
    uint8_t tension;
    uint8_t consonance;    // 0-100, higher = more consonant
    uint8_t hamming;       // 0-12, pitch-class distance from current chord
    uint8_t orig;
  };
  Entry entries[8];
  for (uint8_t i = 0; i < 8; ++i) {
    entries[i].q = map.qualities[i];
    entries[i].tension = engine::chord_tension(s, ctx.engine, degree, map.qualities[i]);
    entries[i].orig = i;

    // Build candidate chord to compute analysis metrics.
    engine::ProgressionStep cand_step = current_step;
    cand_step.scale_degree = degree;
    cand_step.chord_quality = map.qualities[i];
    cand_step.velocity = 100;
    auto cand_notes = engine::progression_step_chord_notes(s, ctx.engine, cand_step);

    entries[i].consonance = cand_notes.empty() ? 50
        : engine::consonance_score(cand_notes.data(), cand_notes.size());
    entries[i].hamming = (current_notes.empty() || cand_notes.empty()) ? 0
        : engine::hamming_distance_pc(current_notes.data(), current_notes.size(),
                                      cand_notes.data(), cand_notes.size());
  }

  // Sort by composite score: consonance descending (most consonant first),
  // then tension ascending, then hamming ascending, stable by original index.
  for (int i = 0; i < 7; ++i)
    for (int j = i + 1; j < 8; ++j) {
      bool swap = false;
      if (entries[j].consonance > entries[i].consonance) swap = true;
      else if (entries[j].consonance == entries[i].consonance) {
        if (entries[j].tension < entries[i].tension) swap = true;
        else if (entries[j].tension == entries[i].tension) {
          if (entries[j].hamming < entries[i].hamming) swap = true;
          else if (entries[j].hamming == entries[i].hamming &&
                   entries[j].orig < entries[i].orig) swap = true;
        }
      }
      if (swap) std::swap(entries[i], entries[j]);
    }

  // Fill all 8 slots: kDiscoverMap ranks every direction, so there is no
  // 8th "least consonant duplicate" to skip and no suggestion slot to make
  // room for.
  for (uint8_t i = 0; i < 8; ++i) {
    ctx.js.discover_slots[i] = entries[i].q;
    ctx.js.discover_tension[i] = entries[i].tension;
    ctx.js.discover_consonance[i] = entries[i].consonance;
  }

  // Modal interchange hint: if current chord has tension >= 1, find which
  // mode would make it diatonic and set the hint for OLED display.
  ctx.js.modal_hint = nullptr;
  if (has_step) {
    const auto& step = current_step;
    uint8_t t = engine::chord_tension(s, ctx.engine, degree, step.chord_quality);
    if (t >= 1) {
      // Get chord pitch classes.
      auto notes = engine::progression_step_chord_notes(s, ctx.engine, step);
      uint8_t pcs[16];
      size_t npc = 0;
      for (size_t n = 0; n < notes.size() && npc < 16; ++n) {
        pcs[npc++] = notes[n] % 12;
      }
      engine::ModalMatch match;
      if (engine::find_modal_interchange(s, pcs, npc, &match, 1) > 0 &&
          match.foreign_tones == 0) {
        ctx.js.modal_hint = match.mode_name;
      }
    }
  }
}

// Direction (0=E..7=SE) -> quality name candidate: qualities from the
// current palette, or from the ranked slots when Order=ranking.
struct ChordCandidate {
  const char* quality = nullptr;
};

static ChordCandidate chord_candidate_for_direction(InputContext& ctx, const ChordParams& p, uint8_t dir) {
  ChordCandidate out;
  if (p.order == ChordOrder::kRanking) {
    out.quality = ctx.js.discover_slots[kDiscoverMap[dir]];
  } else {
    const auto& map = kQualityMaps[std::min<uint8_t>(p.palette, kQualityMapCount - 1)];
    out.quality = map.qualities[kClockMap[dir]];
  }
  return out;
}

// Which behavior a direction produces is fixed by preset identity, not by a
// per-preset field: Voicings changes voicing, Colors/Discover change quality.
// Direction changes only arm the modifier; the next numbered-key chord
// applies it.
static void handle_chord(InputContext& ctx, PresetId preset, const ChordParams& p, int8_t dir, bool dc) {
  if (!dc || dir < 0) return;  // center release is handled by release_on_center()
  const uint8_t d = static_cast<uint8_t>(dir);

  if (preset == PresetId::kVoicings) {
    // The stick is a second way to set the Voicing parameter, not a parallel
    // value: written to Config, so the play screen shows it, the encoder
    // edits the same thing, and no later settings pass silently reverts it.
    ctx.js.voicing_selection = harmony::voicing_for_direction(dir);
    ctx.engine.modifier().reset_inversion();
    auto next = ctx.app_state.config();
    next.voicing_spread = ctx.js.voicing_selection;
    commit_config(ctx, next);
  } else {
    auto cand = chord_candidate_for_direction(ctx, p, d);
    auto& modifier = ctx.engine.modifier();
    // The colour stays armed after the stick recentres; flicking the same
    // colour again disarms it and the chords return to the scale's own.
    if (cand.quality && modifier.quality_name()[0] && std::strcmp(modifier.quality_name(), cand.quality) == 0)
      modifier.reset_quality();
    else if (cand.quality)
      modifier.set_quality(cand.quality);
  }
  ctx.js.browse_index = d;
  ctx.nav.dirty = true;
}

// -- CONT family --------------------------------------------------------------

// One axis streaming a controller value (CC number or channel pressure).
// Change-only: nothing is sent while the quantised value stays the same.
static void cont_send_axis(InputContext& ctx, ContTarget target, int16_t value, uint8_t cc,
                           int16_t deadzone, modulation::Curve curve, int16_t& last, uint8_t ch) {
  const int16_t sent = modulation::axis_to_cc(value, deadzone, curve);
  if (sent == last) return;
  if (target == ContTarget::kAftertouch) ctx.router.channel_pressure(ch, static_cast<uint8_t>(sent));
  else ctx.router.control_change(ch, cc, static_cast<uint8_t>(sent));
  last = sent;
  ctx.nav.dirty = true;
}

// Each axis follows its own target. Bend and Vibrato both land on the one
// pitch-bend stream (vibrato is an LFO added to whatever the bend axis
// holds), CC and Aftertouch stream a controller. Up and right increase.
static void handle_cont(InputContext& ctx, const ContParams& p) {
  int16_t x = ctx.js.raw_x, y = ctx.js.raw_y;
  apply_joy_transform(ctx.app_state.settings(), x, y);
  y = static_cast<int16_t>(-y);  // raw Y grows downward; up means "more"

  const int16_t dz = modulation::deadzone_units(p.deadzone);
  const auto curve = static_cast<modulation::Curve>(std::min<uint8_t>(p.curve, 2));
  const uint8_t ch = ctx.app_state.settings().midi_channel_joy;

  bool has_bend = false, has_vibrato = false;
  int16_t bend_axis = 0, vibrato_axis = 0;
  const auto route = [&](ContTarget target, int16_t value, uint8_t cc, int16_t& last) {
    switch (target) {
      case ContTarget::kBend:    has_bend = true; bend_axis = value; break;
      case ContTarget::kVibrato: has_vibrato = true; vibrato_axis = value; break;
      default: cont_send_axis(ctx, target, value, cc, dz, curve, last, ch); break;
    }
  };
  route(p.x_target, x, p.x_cc, ctx.js.cc_last_x);
  route(p.y_target, y, p.y_cc, ctx.js.cc_last_y);
  if (!has_bend && !has_vibrato) return;

  // After a panic or a recipe change the stick must return to neutral before
  // it bends again, so a held stick cannot re-bend the notes it just reset.
  if (ctx.js.bend_neutral_required) {
    if (std::abs(bend_axis) <= dz && std::abs(vibrato_axis) <= dz) ctx.js.bend_neutral_required = false;
    return;
  }

  const int bend_range = std::max(1, ctx.engine.bend_range());
  int16_t bend = has_bend ? modulation::axis_to_bend(bend_axis, dz) : 0;
  float snap = 0.0f;
  if (has_bend && p.bend_snap == ContBendSnap::kChordTones) {
    auto intervals = current_chord_intervals(ctx);
    bend = engine::quantize_bend_to_chord(bend, intervals, bend_range, &snap);
  } else if (has_bend && p.bend_snap == ContBendSnap::kChromatic) {
    const float per_semitone = 8192.0f / static_cast<float>(bend_range);
    snap = std::round(static_cast<float>(bend) / per_semitone);
    bend = static_cast<int16_t>(std::clamp<long>(std::lround(snap * per_semitone), -8192, 8191));
  }

  const int16_t vibrato = has_vibrato ? modulation::apply_deadzone(vibrato_axis, dz) : 0;
  if (vibrato != 0) {
    // Depth follows deflection up to +/-50 cents whatever the bend range is;
    // the rate is tied to the clock, not to how often this loop happens to run.
    constexpr float kVibratoHz = 5.5f, kMaxCents = 50.0f;
    const float depth = std::abs(static_cast<float>(vibrato)) / modulation::kAxisFullScale;
    ctx.js.bend_vibrato_depth = static_cast<uint8_t>(depth * 127.0f);
    const float phase = static_cast<float>(ctx.t % 100000u) * (kVibratoHz * 2.0f * 3.14159265f / 1000.0f);
    const float cents = std::sin(phase) * depth * kMaxCents;
    const int offset = static_cast<int>(std::lround(cents * 8192.0f / (100.0f * bend_range)));
    bend = static_cast<int16_t>(std::clamp(static_cast<int>(bend) + offset, -8192, 8191));
  } else {
    ctx.js.bend_vibrato_depth = 0;
  }

  if (bend != ctx.js.last_bend) {
    ctx.router.pitch_bend(bend_channel(ctx), bend);
    ctx.js.last_bend = bend;
    ctx.js.bend_snap_semitones = snap;
    ctx.nav.dirty = true;
  }
}

// -- PARAM family --------------------------------------------------------------

namespace {
struct AxisMeta { int32_t min; int32_t max; int32_t step; bool wrap; };
}  // namespace

static AxisMeta axis_meta(AxisTarget t) {
  using AT = AxisTarget;
  switch (t) {
    case AT::kArticulation:            return {0, 2, 1, true};
    case AT::kPlayTrigger:             return {0, 2, 1, true};
    case AT::kVoicing:                 return {0, 7, 1, true};
    case AT::kArpPattern:              return {0, 6, 1, true};
    case AT::kArpRate:                 return {0, 3, 1, false};
    case AT::kArpOctaves:              return {1, 4, 1, false};
    case AT::kArpGate:                 return {20, 100, 10, false};
    case AT::kStrumFeel:               return {0, 60, 5, false};
    case AT::kSwing:                   return {0, 100, 10, false};
    case AT::kHumanize:                return {0, 40, 5, false};
    case AT::kVelocity:                return {1, 127, 10, false};
    case AT::kDuration:                return {20, 4000, 50, false};
    case AT::kAccentPattern:           return {0, 3, 1, true};
    case AT::kVelocityRandom:          return {0, 30, 5, false};
    case AT::kVoiceLeading:            return {0, 2, 1, true};
    case AT::kVoiceLeadingComplexity:  return {0, 100, 10, false};
    case AT::kSoloVelocity:            return {1, 127, 10, false};
    case AT::kSoloNoteLength:          return {20, 4000, 50, false};
    default:                          return {0, 0, 1, false};
  }
}

int32_t axis_get(const Settings& s, AxisTarget t) {
  using AT = AxisTarget;
  switch (t) {
    case AT::kArticulation:            return static_cast<int32_t>(s.mode);
    case AT::kPlayTrigger:             return static_cast<int32_t>(s.play_trigger);
    case AT::kVoicing:                 return s.voicing_spread;
    case AT::kArpPattern:              return s.arp_pattern;
    case AT::kArpRate:                 return s.arp_rate;
    case AT::kArpOctaves:              return s.arp_octaves;
    case AT::kArpGate:                 return s.arp_gate_percent;
    case AT::kStrumFeel:               return s.strum_ms;
    case AT::kSwing:                   return s.swing_percent;
    case AT::kHumanize:                return s.humanize_ms;
    case AT::kVelocity:                return s.velocity;
    case AT::kDuration:                return s.note_length_ms;
    case AT::kAccentPattern:           return s.accent_pattern;
    case AT::kVelocityRandom:          return s.velocity_random;
    case AT::kVoiceLeading:            return s.voice_leading;
    case AT::kVoiceLeadingComplexity:  return s.voice_leading_complexity;
    case AT::kSoloVelocity:            return s.solo_velocity;
    case AT::kSoloNoteLength:          return s.solo_note_length_ms;
    default:                          return 0;
  }
}

static void axis_set(InputContext& ctx, AxisTarget t, int32_t v) {
  using AT = AxisTarget;
  auto& s = ctx.app_state.settings();
  switch (t) {
    case AT::kArticulation:  s.mode = static_cast<PerformanceMode>(v); break;
    case AT::kPlayTrigger:   s.play_trigger = static_cast<PlayTrigger>(v); break;
    case AT::kVoicing:
      s.voicing_spread = static_cast<uint8_t>(v);
      ctx.engine.modifier().set_voicing_spread(static_cast<engine::VoicingSpread>(v));
      break;
    case AT::kArpPattern:              s.arp_pattern = static_cast<uint8_t>(v); break;
    case AT::kArpRate:                 s.arp_rate = static_cast<uint8_t>(v); break;
    case AT::kArpOctaves:              s.arp_octaves = static_cast<uint8_t>(v); break;
    case AT::kArpGate:                 s.arp_gate_percent = static_cast<uint8_t>(v); break;
    case AT::kStrumFeel:               s.strum_ms = static_cast<uint8_t>(v); break;
    case AT::kSwing:                   s.swing_percent = static_cast<uint8_t>(v); break;
    case AT::kHumanize:                s.humanize_ms = static_cast<uint8_t>(v); break;
    case AT::kVelocity:                s.velocity = static_cast<uint8_t>(v); break;
    case AT::kDuration:                s.note_length_ms = static_cast<uint16_t>(v); break;
    case AT::kAccentPattern:           s.accent_pattern = static_cast<uint8_t>(v); break;
    case AT::kVelocityRandom:          s.velocity_random = static_cast<uint8_t>(v); break;
    case AT::kVoiceLeading:            s.voice_leading = static_cast<uint8_t>(v); break;
    case AT::kVoiceLeadingComplexity:  s.voice_leading_complexity = static_cast<uint8_t>(v); break;
    case AT::kSoloVelocity:            s.solo_velocity = static_cast<uint8_t>(v); break;
    case AT::kSoloNoteLength:          s.solo_note_length_ms = static_cast<uint16_t>(v); break;
    default: return;
  }
  apply_engine_settings(s, ctx.engine, s.midi_channel);
}

namespace {
enum class AxisSlot : uint8_t { kUpDown = 0, kLeftRight = 1, kNwSe = 2, kNeSw = 3 };
}  // namespace

static const ParamAxis& axis_for_slot(const ParamParams& p, AxisSlot slot) {
  switch (slot) {
    case AxisSlot::kUpDown:    return p.up_down;
    case AxisSlot::kLeftRight: return p.left_right;
    case AxisSlot::kNwSe:      return p.nw_se;
    default:                  return p.ne_sw;
  }
}

struct SlotHit { AxisSlot slot; int sign; };

// Diagonals are illegal when either cardinal axis is continuous — a diagonal
// push necessarily moves both axes, which would stream a continuous CC and
// fire a discrete change at once. Resolve to the nearest discrete cardinal
// instead; N/S wins the tie, matching this codebase's existing
// clock-direction convention.
static int8_t resolve_param_direction(const ParamParams& p, int8_t dir) {
  const bool diagonal = (dir == 1 || dir == 3 || dir == 5 || dir == 7);
  if (!diagonal) return dir;
  if (p.up_down.role != AxisRole::kContinuous && p.left_right.role != AxisRole::kContinuous)
    return dir;
  return (dir == 1 || dir == 3) ? 2 : 6;  // NE,NW -> N ; SW,SE -> S
}

static bool slot_hit_for_direction(int8_t dir, SlotHit& out) {
  switch (dir) {
    case 2: out = {AxisSlot::kUpDown, 1}; return true;     // N
    case 6: out = {AxisSlot::kUpDown, -1}; return true;    // S
    case 0: out = {AxisSlot::kLeftRight, 1}; return true;  // E
    case 4: out = {AxisSlot::kLeftRight, -1}; return true; // W
    case 3: out = {AxisSlot::kNwSe, 1}; return true;       // NW
    case 7: out = {AxisSlot::kNwSe, -1}; return true;      // SE
    case 1: out = {AxisSlot::kNeSw, 1}; return true;       // NE
    case 5: out = {AxisSlot::kNeSw, -1}; return true;      // SW
    default: return false;
  }
}

static void handle_param(InputContext& ctx, const ParamParams& p, int8_t dir, bool dc,
                        drivers::JoystickAnalog* joystick) {
  // Continuous axes read position every tick; only meaningful from the
  // analog stick (keypad-as-joy has no position to read).
  if (joystick) {
    const int16_t x = ctx.js.raw_x, y = ctx.js.raw_y;
    auto continuous_update = [&](const ParamAxis& axis, AxisSlot slot, int16_t value) {
      if (axis.role != AxisRole::kContinuous || axis.target == AxisTarget::kNone) return;
      if (axis.target == AxisTarget::kExpressionCc) {
        // Not a Settings field — stream the position straight out as a CC
        // (an up/down continuous axis -> CC 11).
        const uint8_t ch = ctx.app_state.settings().midi_channel_joy;
        const int16_t sent = modulation::axis_to_cc(value, modulation::deadzone_units(ctx.js.cc_dead_zone));
        const size_t idx = static_cast<size_t>(slot);
        if (ctx.js.param_saved_value[idx] == sent) return;
        ctx.js.param_saved_value[idx] = sent;
        ctx.router.control_change(ch, 11, static_cast<uint8_t>(sent));
        ctx.nav.dirty = true;
        return;
      }
      const auto meta = axis_meta(axis.target);
      const float norm = std::clamp(static_cast<float>(value) / modulation::kAxisFullScale, -1.0f, 1.0f);
      const int32_t span = meta.max - meta.min;
      const int32_t v = meta.min + static_cast<int32_t>(std::lround((norm * 0.5f + 0.5f) * span));
      const size_t idx = static_cast<size_t>(slot);
      if (ctx.js.param_saved_value[idx] == v) return;
      ctx.js.param_saved_value[idx] = v;
      axis_set(ctx, axis.target, std::clamp(v, meta.min, meta.max));
      if (p.retrigger_on_change && ctx.nav.active_degree >= 1)
        ctx.engine.trigger_step(static_cast<size_t>(ctx.nav.active_degree - 1), engine::TriggerMode::kOneShot);
      ctx.nav.dirty = true;
    };
    continuous_update(p.up_down, AxisSlot::kUpDown, y);
    continuous_update(p.left_right, AxisSlot::kLeftRight, x);
    continuous_update(p.nw_se, AxisSlot::kNwSe, static_cast<int16_t>((y - x) / 2));
    continuous_update(p.ne_sw, AxisSlot::kNeSw, static_cast<int16_t>((y + x) / 2));
  }

  if (!dc) return;

  if (dir < 0) {
    // Center release: momentary axes snap back to what they were before.
    for (size_t i = 0; i < 4; ++i) {
      if (!ctx.js.param_axis_active[i]) continue;
      const ParamAxis& axis = axis_for_slot(p, static_cast<AxisSlot>(i));
      if (axis.hold == AxisHold::kMomentary && axis.target != AxisTarget::kNone)
        axis_set(ctx, axis.target, ctx.js.param_saved_value[i]);
      ctx.js.param_axis_active[i] = false;
    }
    return;
  }

  const int8_t resolved = resolve_param_direction(p, dir);
  SlotHit hit{};
  if (!slot_hit_for_direction(resolved, hit)) return;
  const ParamAxis& axis = axis_for_slot(p, hit.slot);
  if (axis.role != AxisRole::kDiscrete || axis.target == AxisTarget::kNone) return;

  const auto meta = axis_meta(axis.target);
  const size_t idx = static_cast<size_t>(hit.slot);
  const int32_t current = axis_get(ctx.app_state.settings(), axis.target);
  if (axis.hold == AxisHold::kMomentary && !ctx.js.param_axis_active[idx]) {
    ctx.js.param_saved_value[idx] = current;
    ctx.js.param_axis_active[idx] = true;
  }

  int32_t delta = meta.step;
  if (axis.amount == AxisAmount::kByDistance) {
    // A keypad-as-joy press is inherently a full-distance gesture.
    const float dist = joystick ? joy_deflection(ctx) : 1.0f;
    delta = std::max<int32_t>(meta.step, static_cast<int32_t>(std::lround((meta.max - meta.min) * dist)));
  }

  int32_t next = current + hit.sign * delta;
  if (meta.wrap) {
    const int32_t span = meta.max - meta.min + 1;
    next = meta.min + ((next - meta.min) % span + span) % span;
  } else {
    next = std::clamp(next, meta.min, meta.max);
  }
  axis_set(ctx, axis.target, next);
  if (p.retrigger_on_change && ctx.nav.active_degree >= 1)
    ctx.engine.trigger_step(static_cast<size_t>(ctx.nav.active_degree - 1), engine::TriggerMode::kOneShot);
  ctx.nav.dirty = true;
}

// The value a PARAM direction would produce if pushed - what the compass
// (draw_compass_body, shared with NOTES/CHORD) shows in each cell for
// PARAM/Flip's momentary axes: cells show outcomes, never parameter names.
// Tier-1 targets step through parameters::mutate()'s own clamp/wrap on a
// scratch Config, so the preview always matches what pushing that direction
// for real would do; tier-2 targets (reachable only if a user retunes Flip's
// axis target away from its default) fall back to axis_meta's step/wrap.
static void param_direction_outcome(InputContext& ctx, const ParamParams& p, int8_t dir,
                                    char* out, size_t out_len) {
  SlotHit hit{};
  if (!slot_hit_for_direction(dir, hit)) { std::snprintf(out, out_len, "--"); return; }
  const ParamAxis& axis = axis_for_slot(p, hit.slot);
  if (axis.target == AxisTarget::kNone || axis.role != AxisRole::kDiscrete) {
    std::snprintf(out, out_len, "--");
    return;
  }
  contract::ParamId id;
  if (parameters::axis_target_param_id(axis.target, id)) {
    contract::Config scratch = ctx.app_state.config();
    parameters::mutate(scratch, id, hit.sign);
    char label[20];
    parameters::format(scratch, id, label, sizeof(label), out, out_len);
    return;
  }
  const auto meta = axis_meta(axis.target);
  const int32_t current = axis_get(ctx.app_state.settings(), axis.target);
  int32_t next = current + hit.sign * meta.step;
  if (meta.wrap) {
    const int32_t span = meta.max - meta.min + 1;
    next = meta.min + ((next - meta.min) % span + span) % span;
  } else {
    next = std::clamp(next, meta.min, meta.max);
  }
  parameters::format_axis_tier2_value(axis.target, next, out, out_len);
}

// -- Family dispatch and shared release logic --------------------------------

static JoyFamily active_family(const contract::Config& cfg) {
  return kPresets[static_cast<size_t>(cfg.active_preset)].family;
}

static void dispatch_family_gesture(InputContext& ctx, int8_t dir, bool dc,
                                    drivers::JoystickAnalog* joystick) {
  const auto& cfg = ctx.app_state.config();
  switch (active_family(cfg)) {
    case JoyFamily::kNotes: handle_notes(ctx, cfg.joy_families.notes, dir, dc, joystick); break;
    case JoyFamily::kChord: handle_chord(ctx, cfg.active_preset, cfg.joy_families.chord, dir, dc); break;
    case JoyFamily::kCont:
      // Position-driven: only the analog stick has a position to read.
      if (joystick) handle_cont(ctx, cfg.joy_families.cont);
      break;
    case JoyFamily::kParam: handle_param(ctx, cfg.joy_families.param, dir, dc, joystick); break;
    default: break;
  }
}

// Center release: every family's own note/modifier state cleans up here,
// in one place per family rather than through per-recipe lookup tables.
// The chord held on the keypad belongs to the keypad: no joystick family plays
// through the engine's held slot, so recentring must not release it.
static void release_on_center(InputContext& ctx) {
  const auto& cfg = ctx.app_state.config();
  switch (active_family(cfg)) {
    case JoyFamily::kNotes:
      if (cfg.joy_families.notes.sustain == NoteSustain::kMomentary) joy_notes_off(ctx);
      break;
    // CHORD: center release does not undo a quality/voicing change.
    default:
      break;
  }
}

JoystickPreview joystick_preview(const InputContext& input) {
  // InputContext contains references; this copy lets the existing read-only
  // map builders be reused without simulating or dispatching a gesture.
  InputContext ctx = input;
  JoystickPreview out;
  const auto& cfg = ctx.app_state.config();
  auto put = [](char* dst, size_t n, const char* v) { std::snprintf(dst, n, "%s", v ? v : "--"); };

  switch (active_family(cfg)) {
    case JoyFamily::kNotes: {
      const auto& p = cfg.joy_families.notes;
      out.available = true;
      const int base_octave = 4 + p.octave + ctx.js.note_octave;
      if (p.gesture == NoteGesture::kClock) {
        auto notes = (p.voices == NoteVoices::kChord) ? keymap_root_notes(ctx, base_octave)
                                                        : notes_for_source(ctx, p.source, base_octave);
        put(out.x_name, sizeof(out.x_name), "Note"); put(out.y_name, sizeof(out.y_name), "Note");
        const int dir = ctx.js.physical_dir >= 0 ? ctx.js.physical_dir : ctx.js.keypad_dir;
        if (dir >= 0 && dir < 8) engine::midi_note_label(notes[kClockMap[dir]], out.current, sizeof(out.current));
        else put(out.current, sizeof(out.current), "--");
        engine::midi_note_label(notes[kClockMap[2]], out.up, sizeof(out.up));
        engine::midi_note_label(notes[kClockMap[6]], out.down, sizeof(out.down));
        engine::midi_note_label(notes[kClockMap[4]], out.left, sizeof(out.left));
        engine::midi_note_label(notes[kClockMap[0]], out.right, sizeof(out.right));
        for (uint8_t i = 0; i < 8; ++i)
          engine::midi_note_label(notes[kClockMap[i]], out.compass[i], sizeof(out.compass[i]));
      } else {
        auto notes = notes_for_source(ctx, p.source, base_octave);
        out.velocity_x = true;
        engine::midi_note_label(notes[ctx.js.walk_index % 8], out.current, sizeof(out.current));
        engine::midi_note_label(notes[(ctx.js.walk_index + 1) % 8], out.up, sizeof(out.up));
        engine::midi_note_label(notes[(ctx.js.walk_index + 7) % 8], out.down, sizeof(out.down));
        for (uint8_t i = 0; i < 8; ++i)
          engine::midi_note_label(notes[i], out.walk[i], sizeof(out.walk[i]));
        put(out.x_name, sizeof(out.x_name), "Vel");
        std::snprintf(out.x_value, sizeof(out.x_value), "%u", ctx.app_state.settings().solo_velocity);
      }
      out.velocity = ctx.app_state.settings().solo_velocity;
      return out;
    }
    case JoyFamily::kChord: {
      const auto& p = cfg.joy_families.chord;
      out.available = true;
      const bool voicings = cfg.active_preset == PresetId::kVoicings;
      put(out.x_name, sizeof(out.x_name), voicings ? "Voice" : "Qual");
      put(out.y_name, sizeof(out.y_name), voicings ? "Voice" : "Qual");
      // One pass over all 8 directions fills compass[] — the outcome that
      // direction produces (cells show outcomes), quality name or voicing
      // name.
      for (uint8_t i = 0; i < 8; ++i) {
        const char* label = voicings
            ? engine::voicing_spread_name(static_cast<engine::VoicingSpread>(harmony::voicing_for_direction(i)))
            : chord_candidate_for_direction(ctx, p, i).quality;
        put(out.compass[i], sizeof(out.compass[i]), label);
      }
      const int dir = ctx.js.physical_dir >= 0 ? ctx.js.physical_dir : ctx.js.keypad_dir;
      put(out.current, sizeof(out.current), (dir >= 0 && dir < 8) ? out.compass[dir] : nullptr);
      put(out.up, sizeof(out.up), out.compass[2]);
      put(out.down, sizeof(out.down), out.compass[6]);
      put(out.left, sizeof(out.left), out.compass[4]);
      put(out.right, sizeof(out.right), out.compass[0]);
      put(out.x_value, sizeof(out.x_value), out.current);
      put(out.y_value, sizeof(out.y_value), out.current);
      return out;
    }
    case JoyFamily::kCont: {
      const auto& p = cfg.joy_families.cont;
      out.available = true;
      // Each axis reads back what its own target last sent.
      const auto readout = [&](ContTarget target, int16_t cc_last, char* name, size_t name_len,
                               char* value, size_t value_len) {
        if (target == ContTarget::kBend) {
          put(name, name_len, "Bend");
          std::snprintf(value, value_len, "%+d", ctx.js.last_bend);
        } else if (target == ContTarget::kVibrato) {
          put(name, name_len, "Vib");
          std::snprintf(value, value_len, "%u", ctx.js.bend_vibrato_depth);
        } else {
          put(name, name_len, target == ContTarget::kAftertouch ? "AT" : "CC");
          if (cc_last >= 0) std::snprintf(value, value_len, "%d", cc_last);
          else put(value, value_len, "--");
        }
      };
      readout(p.x_target, ctx.js.cc_last_x, out.x_name, sizeof(out.x_name), out.x_value, sizeof(out.x_value));
      readout(p.y_target, ctx.js.cc_last_y, out.y_name, sizeof(out.y_name), out.y_value, sizeof(out.y_value));
      put(out.current, sizeof(out.current), "Continuous");
      return out;
    }
    case JoyFamily::kParam: {
      const auto& p = cfg.joy_families.param;
      out.available = true;
      put(out.x_name, sizeof(out.x_name), "Axis"); put(out.y_name, sizeof(out.y_name), "Axis");
      for (uint8_t i = 0; i < 8; ++i) {
        char cell[12];
        param_direction_outcome(ctx, p, static_cast<int8_t>(i), cell, sizeof(cell));
        put(out.compass[i], sizeof(out.compass[i]), cell);
      }
      const int dir = ctx.js.physical_dir >= 0 ? ctx.js.physical_dir : ctx.js.keypad_dir;
      put(out.current, sizeof(out.current), (dir >= 0 && dir < 8) ? out.compass[dir] : nullptr);
      put(out.x_value, sizeof(out.x_value), out.current);
      put(out.y_value, sizeof(out.y_value), out.current);
      return out;
    }
    default:
      return out;
  }
}

// -- Main entry points ------------------------------------------------------------

void process_joystick(InputContext& ctx, drivers::JoystickAnalog& joystick,
                      const drivers::JoystickAnalogConfig& /*joy_cfg*/) {
  // There is no physical joystick button on this hardware, so no switch is
  // read here.
  int8_t dir = joystick.read_direction();
  joystick.get_xy_normalized(ctx.js.raw_x, ctx.js.raw_y);

  // Apply joystick rotation setting (0-3 = 0/90/180/270 degrees CW)
  if (dir >= 0) {
    const uint8_t rot = ctx.app_state.settings().joystick_rotation;
    if (rot > 0) dir = static_cast<int8_t>((dir + rot * 2) % 8);
    if (ctx.app_state.settings().joystick_flip_h) {
      static const int8_t kFlip[] = {4, 3, 2, 1, 0, 7, 6, 5};
      dir = kFlip[dir];
    }
    // Invert Y has to turn the directions over as well as the X/Y position.
    if (ctx.app_state.settings().joystick_flip_v) dir = static_cast<int8_t>((8 - dir) % 8);
  }

  const bool dc = (dir != ctx.js.physical_dir);
  if (dc) ctx.nav.dirty = true;

  if (ctx.js.recipe_neutral_required) {
    if (dir < 0 && dc) ctx.js.recipe_neutral_required = false;
    ctx.js.physical_dir = dir;
    ctx.js.last_dir = dir;
    return;
  }

  // Timer note-off for direct-MIDI Sustain=momentary playback.
  if (ctx.js.note_one_shot && static_cast<int32_t>(ctx.t - ctx.js.note_off_due_ms) >= 0) {
    joy_notes_off(ctx);
  }

  // Centre release: stop notes cleanly.
  if (dir < 0 && dc && !ctx.js.keypad_gesture_active) {
    release_on_center(ctx);
  }

  dispatch_family_gesture(ctx, dir, dc, &joystick);

  ctx.js.physical_dir = dir;
  ctx.js.last_dir = dir;
}

void dispatch_keypad_joy_direction(InputContext& ctx, int8_t dir) {
  ctx.js.keypad_gesture_active = dir >= 0;
  const bool dc = (dir != ctx.js.keypad_dir);
  if (dc) ctx.nav.dirty = true;

  if (ctx.js.recipe_neutral_required) {
    if (dir < 0 && dc) ctx.js.recipe_neutral_required = false;
    ctx.js.keypad_dir = dir;
    ctx.js.last_dir = dir;  // telemetry compatibility; keypad_dir is authority
    return;
  }

  // Timer note-off for direct-MIDI Sustain=momentary playback.
  if (ctx.js.note_one_shot && static_cast<int32_t>(ctx.t - ctx.js.note_off_due_ms) >= 0) {
    joy_notes_off(ctx);
  }

  // Centre release: stop notes cleanly.
  if (dir < 0 && dc) {
    release_on_center(ctx);
  }

  dispatch_family_gesture(ctx, dir, dc, nullptr);

  ctx.js.keypad_dir = dir;
  ctx.js.last_dir = dir;  // telemetry compatibility; keypad_dir is authority
  ctx.nav.dirty = true;
}

void service_joystick_ticks(InputContext& /*ctx*/, uint16_t /*clock_ticks*/) {
  // No joystick producer is clock-driven: the MIDI clock drives the
  // arpeggiator directly. Kept as the main loop's hook for one.
}

}  // namespace app
