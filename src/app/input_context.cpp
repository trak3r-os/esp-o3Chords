#include "app/input_context.h"
#include "app/config_codec.h"
#include "app/input_joystick.h"
#include "app/modulation_recipe.h"
#include "app/progression_modes.h"
#include "app/settings_bridge.h"
#include "app/voice_recipe.h"
#include "engine/music_theory.h"
#include "ui/display_names.h"

namespace app {

bool joy_note_acquire(InputContext& ctx, contract::SourceId source, uint8_t channel,
                      uint8_t note, uint8_t velocity, contract::VoiceToken& token) {
  token = {};
  if (ctx.note_ledger) {
    const uint8_t routes = ctx.router.active_route_mask();
    return ctx.note_ledger->acquire({source, 0, channel, note, velocity, routes}, token);
  }
  // Production composition must provide the shared ledger.  A direct router
  // fallback would create a second ownership system and cannot be safe.
  return false;
}

bool joy_note_release(InputContext& ctx, contract::VoiceToken token,
                      uint8_t channel, uint8_t note) {
  if (ctx.note_ledger && token.value != 0) return ctx.note_ledger->release(token);
  return false;
}

bool commit_config(InputContext& ctx, contract::Config next) {
  const contract::Config prev = ctx.app_state.config();
  config::sync_names(next);
  if (contract::validate(next).error != contract::ValidationError::None) return false;

  const bool preset_changed = next.active_preset != prev.active_preset;
  // Release joystick voices and centre the bend while Settings still names
  // the channels they went out on.
  if (preset_changed || next.lead_channel != prev.lead_channel) {
    joy_all_notes_off(ctx);
    ctx.engine.invalidate_source(engine::Engine::kLeadSource);
  }
  if (preset_changed) {
    // The stick may still be held: do not reset physical_dir/keypad_dir, or
    // the real centre release that follows would not register as a change.
    ctx.js.recipe_neutral_required = ctx.js.physical_dir >= 0 || ctx.js.keypad_dir >= 0;
    ctx.engine.release_held();
    ctx.engine.all_notes_off();
    // A colour armed by a CHORD preset must not outlive the preset.
    ctx.engine.modifier().reset_quality();
  }

  if (!config::apply_to_settings(next, ctx.app_state.settings())) return false;
  next.revision = ctx.nav.settings_revision + 1;
  ctx.app_state.set_config(next);
  if (next.scale != prev.scale) {
    ctx.nav.prog_mode = static_cast<ProgressionMode>(next.scale);
    apply_progression_mode(ctx.engine, ctx.nav.prog_mode, ctx.app_state.settings());
  }
  ctx.js.keypad_as_joy = next.joystick.keypad_as_joy;
  apply_settings(ctx.app_state, ctx.engine, ctx.ble, ctx.usb);

  const auto family = kPresets[static_cast<size_t>(next.active_preset)].family;
  if (family == JoyFamily::kChord && next.joy_families.chord.order == ChordOrder::kRanking &&
      (preset_changed || next.scale != prev.scale || next.key != prev.key ||
       next.joy_families.chord.palette != prev.joy_families.chord.palette))
    discover_rebuild_slots(ctx);

  ctx.nav.settings_revision = next.revision;
  ctx.nav.settings_dirty = true;
  ctx.nav.last_settings_change = ctx.t;
  ctx.nav.dirty = true;
  return true;
}

namespace {

static constexpr JoystickPlayMode kChordJoyModes[] = {
    JoystickPlayMode::kQuality,    // Tension map A (diatonic extensions)
    JoystickPlayMode::kDiscover,   // Tension map B (jazz/altered)
    JoystickPlayMode::kBend,       // Pitch bend + vibrato
    JoystickPlayMode::kStrumFeel,  // Dynamics: CC11 (expression) + CC7 (volume)
    JoystickPlayMode::kRhythm,     // Filter: CC74 (cutoff) + CC71 (resonance)
    JoystickPlayMode::kVoicing,    // Strum: direction flick triggers chord strum
};
static constexpr uint8_t kChordJoyModeCount =
    static_cast<uint8_t>(sizeof(kChordJoyModes) / sizeof(kChordJoyModes[0]));

static constexpr JoystickPlayMode kLeadJoyModes[] = {
    JoystickPlayMode::kSoloist,
    JoystickPlayMode::kBassClock,
    JoystickPlayMode::kInterval,
    JoystickPlayMode::kDrone,
    JoystickPlayMode::kCCControl,
    JoystickPlayMode::kPentatonic,
    JoystickPlayMode::kHarmony,
};
static constexpr uint8_t kLeadJoyModeCount =
    static_cast<uint8_t>(sizeof(kLeadJoyModes) / sizeof(kLeadJoyModes[0]));

}  // namespace

void joy_notes_off(InputContext& ctx) {
  // Note-ons are always sent on midi_channel_joy (see joy_add_note / joy_play_single_note_vel).
  // Note-offs MUST use the same channel, regardless of which screen is currently active.
  const uint8_t ch = ctx.js.note_channel;
  for (uint8_t i = 0; i < ctx.js.note_active_count; ++i) {
    joy_note_release(ctx, ctx.js.note_tokens[i], ch, ctx.js.note_active[i]);
    ctx.js.note_tokens[i] = {};
  }
  ctx.js.note_active_count = 0;
  ctx.js.note_one_shot = false;
}

void joy_stop_drone(InputContext& ctx, uint8_t d) {
  if (d >= 8 || !(ctx.js.drone_active_mask & (1u << d))) return;
  ctx.js.drone_active_mask &= ~(1u << d);
  uint8_t i = 0;
  for (uint8_t n : ctx.js.drone_chords[d]) {
    joy_note_release(ctx, ctx.js.drone_tokens[d][i], ctx.js.drone_channels[d], n);
    ctx.js.drone_tokens[d][i++] = {};
  }
  ctx.js.drone_chords[d].clear();
  ctx.js.drone_tokens[d].clear();
}

void joy_all_notes_off(InputContext& ctx) {
  joy_notes_off(ctx);
  for (uint8_t d = 0; d < 8; ++d) joy_stop_drone(ctx, d);
  ctx.js.latch_active = false;
  ctx.js.latched_dir = -1;
  ctx.js.last_bend = 0;
  ctx.js.bend_snap_semitones = 0;
  ctx.js.bend_neutral_required = true;
  if (ctx.note_ledger) ctx.note_ledger->invalidate_source(0x0100);
  for (uint8_t ch : {ctx.app_state.settings().midi_channel,
                     ctx.app_state.settings().midi_channel_joy})
    ctx.router.pitch_bend(ch, 0);
}

void joy_add_note(InputContext& ctx, uint8_t note, uint8_t channel, uint8_t velocity) {
  if (ctx.js.note_active_count >= ctx.js.note_active.size()) return;
  for (uint8_t i = 0; i < ctx.js.note_active_count; ++i) {
    if (ctx.js.note_active[i] == note) return;
  }
  ctx.js.note_channel = channel;
  if (!joy_note_acquire(ctx, 0x0100, channel, note, velocity,
                        ctx.js.note_tokens[ctx.js.note_active_count])) return;
  ctx.js.note_active[ctx.js.note_active_count++] = note;
}

void joy_play_notes(InputContext& ctx, const std::vector<uint8_t>& notes,
                    uint8_t channel, uint8_t velocity, bool one_shot, uint16_t note_length_ms) {
  joy_notes_off(ctx);
  for (uint8_t n : notes) joy_add_note(ctx, n, channel, velocity);
  if (ctx.js.note_active_count == 0) return;
  ctx.js.note_one_shot = one_shot;
  if (one_shot) ctx.js.note_off_due_ms = ctx.t + note_length_ms;
}

void joy_play_single_note_vel(InputContext& ctx, int note, uint8_t channel,
                              uint8_t velocity, bool one_shot, uint16_t note_length_ms) {
  const uint8_t n = engine::clamp_midi_note(note);
  joy_notes_off(ctx);
  ctx.js.note_channel = channel;
  if (!joy_note_acquire(ctx, 0x0100, channel, n, velocity, ctx.js.note_tokens[0])) return;
  ctx.js.note_active[0] = n;
  ctx.js.note_active_count = 1;
  ctx.js.note_one_shot = one_shot;
  if (one_shot) ctx.js.note_off_due_ms = ctx.t + note_length_ms;
  ctx.js.last_note = n;
}

void apply_joystick_mode(InputContext& ctx, JoystickPlayMode next_mode) {
  // Compatibility entry point for JoystickPlayMode fixtures. Production
  // input does not call this function; it selects Config.active_preset
  // directly. The mapping below reuses the v2-wire RecipeId migration table
  // so a legacy fixture keeps landing on the preset that recipe was folded
  // into.
  static constexpr contract::RecipeId kModeRecipe[] = {
      contract::RecipeId::Quality, contract::RecipeId::ChordVel,
      contract::RecipeId::Bend, contract::RecipeId::Melody,
      contract::RecipeId::BassClock, contract::RecipeId::Discover,
      contract::RecipeId::ArpControl, contract::RecipeId::Interval,
      contract::RecipeId::Drone, contract::RecipeId::Rhythm,
      contract::RecipeId::Expression, contract::RecipeId::Pentatonic,
      contract::RecipeId::Voicing, contract::RecipeId::Euclidean,
      contract::RecipeId::StrumFeel, contract::RecipeId::Harmony,
      contract::RecipeId::ScaleExplore};
  const auto mode_index = static_cast<size_t>(next_mode);
  if (mode_index < sizeof(kModeRecipe) / sizeof(kModeRecipe[0])) {
    auto config = ctx.app_state.config();
    config.active_preset = config::preset_for_recipe_v2(kModeRecipe[mode_index]);
    config.joy_families = {};
    config::recall_preset(config.joy_families, config.active_preset);
    ctx.app_state.set_config(config);
  }
  ctx.js.mode = next_mode;
  reset_joystick_gesture_state(ctx);
}

void reset_joystick_gesture_state(InputContext& ctx) {
  const bool retained_gesture = ctx.js.last_dir >= 0 || ctx.engine.held_active() ||
                                ctx.js.note_active_count != 0;
  const bool retained_bend = ctx.js.last_bend != 0;
  const auto& cfg = ctx.app_state.config();
  const auto family = kPresets[static_cast<size_t>(cfg.active_preset)].family;

  // Reset the quality latch when leaving CHORD.
  if (family != JoyFamily::kChord) {
    ctx.js.latch_active = false;
    ctx.js.latched_dir = -1;
    ctx.engine.modifier().set_latch_mode(false);
  }

  ctx.js.last_dir = -1;
  ctx.js.physical_dir = -1;
  ctx.js.keypad_dir = -1;
  ctx.js.recipe_neutral_required = retained_gesture;

  // Clear quality modifier so tension-map settings don't bleed across mode switches.
  ctx.engine.modifier().reset_quality();

  // Stop direct-MIDI notes (both NOTES momentary and NOTES/latch — every
  // NOTES source manages its own notes directly).
  joy_notes_off(ctx);
  for (uint8_t d = 0; d < 8; ++d) joy_stop_drone(ctx, d);

  ctx.engine.release_held();
  ctx.engine.all_notes_off();
  ctx.engine.invalidate_source(engine::Engine::kLeadSource);
  if (ctx.note_ledger) ctx.note_ledger->invalidate_source(0x0100);

  // Reset pitch bend on both channels so nothing is left hanging.
  {
    const auto& s = ctx.app_state.settings();
    ctx.router.pitch_bend(s.midi_channel_joy, 0);
    if (s.midi_channel != s.midi_channel_joy)
      ctx.router.pitch_bend(s.midi_channel, 0);
  }
  ctx.js.last_bend = 0;
  ctx.js.bend_snap_semitones = 0.0f;
  ctx.js.bend_neutral_required = retained_bend;

  // Reset CC and tension-map tracking on every mode switch.
  const auto modulation_restore = modulation::restore_state();
  ctx.js.cc_last_x = modulation_restore.cc_x;
  ctx.js.browse_index = 0xFF;
  ctx.js.cc_last_y = modulation_restore.cc_y;

  // Rebuild tension slots when entering a ranked CHORD preset (Discover).
  if (family == JoyFamily::kChord && cfg.joy_families.chord.order == ChordOrder::kRanking) {
    discover_rebuild_slots(ctx);
  }

  // Program the synth's pitch bend range to match engine.bend_range() on every
  // active channel via RPN 0x0000 (Pitch Bend Sensitivity).
  // A fixed value would leave a synth whose range differs from the engine's
  // bending several times too far (or not far enough).
  if (family == JoyFamily::kCont && cfg.joy_families.cont.x_target == ContTarget::kBend) {
    const auto& s = ctx.app_state.settings();
    const uint8_t semitones = static_cast<uint8_t>(ctx.engine.bend_range());
    for (uint8_t ch : {s.midi_channel, s.midi_channel_joy}) {
      ctx.router.control_change(ch, 101, 0);  // RPN MSB
      ctx.router.control_change(ch, 100, 0);  // RPN LSB  (selects Pitch Bend Sensitivity)
      ctx.router.control_change(ch, 6,   semitones);  // Data Entry: semitones
      ctx.router.control_change(ch, 38,  0);   // Data Entry LSB: cents (0)
      // RPN null-select: prevents accidental edits from subsequent CC6/CC38
      ctx.router.control_change(ch, 101, 127);
      ctx.router.control_change(ch, 100, 127);
    }
  }

  // Reset voicing when entering the Voicings preset.
  if (cfg.active_preset == PresetId::kVoicings) {
    ctx.js.voicing_selection = 0;
  }

  ctx.nav.dirty = true;
}

void cycle_joystick_mode_in_filter(InputContext& ctx) {
  const bool chord_ctx = is_chord_screen(ctx);
  const JoystickPlayMode* modes = chord_ctx ? kChordJoyModes : kLeadJoyModes;
  const uint8_t count = chord_ctx ? kChordJoyModeCount : kLeadJoyModeCount;

  uint8_t cur = count;  // sentinel: not found
  for (uint8_t i = 0; i < count; ++i) {
    if (modes[i] == ctx.js.mode) { cur = i; break; }
  }
  const uint8_t next = (cur < count) ? static_cast<uint8_t>((cur + 1) % count) : 0;
  apply_joystick_mode(ctx, modes[next]);

  std::snprintf(ctx.nav.toast_text, sizeof(ctx.nav.toast_text),
                "Joy: %s", ui::joy_mode_name(ctx.js.mode));
  ctx.nav.toast_until = ctx.t + 1500;
}

uint8_t joy_mode_filter_count(bool chord_context) {
  return chord_context ? kChordJoyModeCount : kLeadJoyModeCount;
}

JoystickPlayMode joy_mode_filter_item(bool chord_context, uint8_t index) {
  const JoystickPlayMode* modes = chord_context ? kChordJoyModes : kLeadJoyModes;
  const uint8_t count = chord_context ? kChordJoyModeCount : kLeadJoyModeCount;
  if (index < count) return modes[index];
  return JoystickPlayMode::kQuality;
}

void joy_cycle_mode_config(InputContext& ctx) {
  switch (ctx.js.mode) {
    case JoystickPlayMode::kQuality:
      ctx.js.quality_map_index = (ctx.js.quality_map_index + 1) % kQualityMapCount;
      ctx.engine.modifier().reset_quality();
      break;
    case JoystickPlayMode::kChordVel: {
      // Cycle octave offset: -3 -> ... -> 3 -> -3
      if (ctx.js.note_octave >= 3) ctx.js.note_octave = -3;
      else ++ctx.js.note_octave;
    } break;
    case JoystickPlayMode::kBend:
      break;
    case JoystickPlayMode::kSoloist:
      // Sub-mode selection is NotesParams::source (Config-level), not a
      // runtime toggle this compatibility shim can cycle.
      ctx.js.walk_index = 0;
      ctx.engine.release_held();
      break;
    case JoystickPlayMode::kBassClock:
      break;
    case JoystickPlayMode::kDiscover:
      // Cycle quality map (same as Quality mode) and rebuild tension slots.
      ctx.js.quality_map_index = (ctx.js.quality_map_index + 1) % kQualityMapCount;
      ctx.engine.modifier().reset_quality();
      discover_rebuild_slots(ctx);
      break;
    case JoystickPlayMode::kArpControl:
      // No quick-cycle config (params are controlled by directions).
      break;
    case JoystickPlayMode::kInterval:
      ctx.engine.release_held();
      break;
    case JoystickPlayMode::kDrone:
      break;
    case JoystickPlayMode::kRhythm:
      // No quick-cycle config (directions ARE the config).
      break;
    case JoystickPlayMode::kCCControl:
      // No quick-cycle; params are CC numbers (edit via menu).
      break;
    case JoystickPlayMode::kPentatonic:
      ctx.js.walk_index = 0;
      ctx.engine.release_held();
      break;
    case JoystickPlayMode::kVoicing:
      // Cycle auto-reset toggle.
      ctx.js.voicing_auto_reset = !ctx.js.voicing_auto_reset;
      break;
    case JoystickPlayMode::kEuclidean:
      // Euclidean has no joystick behavior; nothing to cycle.
      break;
    case JoystickPlayMode::kStrumFeel:
      break;
    case JoystickPlayMode::kHarmony:
      break;
    case JoystickPlayMode::kScaleExplore:
      // No quick-cycle
      break;
    default:
      break;
  }
  ctx.nav.dirty = true;
}

}  // namespace app
