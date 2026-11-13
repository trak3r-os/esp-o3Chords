#pragma once

#include "app/state.h"
#include "app/types.h"
#include "app/joystick_state.h"
#include "app/ui_state.h"
#include "engine/engine.h"
#include "engine/note_ledger.h"
#include "midi/router.h"
#include "midi/ble_midi.h"
#include "midi/usb_device_midi.h"

#include <cstdint>
#include <vector>

namespace app {

struct InputContext {
  AppState& app_state;
  engine::Engine& engine;
  midi::Router& router;
  midi::BleMidi& ble;
  midi::UsbDeviceMidi& usb;
  JoystickState& js;
  UiNavState& nav;
  std::vector<engine::ProgressionStep>& play_mode_progression_steps;
  uint32_t t;
  // The application's shared ledger, wired in main.cpp. nullptr (a bare
  // context, as some tests build) makes joystick note acquisition fail safe.
  engine::NoteLedger* note_ledger = nullptr;
};

// Shared helpers used by both keypad and joystick input processors.
// NOTES family manages every note it plays directly through these — no
// source routes through Engine::play_notes(), so the channel it sounds on is
// always the one the caller asks for, never the engine's fixed lead channel.
void joy_notes_off(InputContext& ctx);
void joy_stop_drone(InputContext& ctx, uint8_t direction);
void joy_all_notes_off(InputContext& ctx);
void joy_add_note(InputContext& ctx, uint8_t note, uint8_t channel, uint8_t velocity);
void joy_play_notes(InputContext& ctx, const std::vector<uint8_t>& notes,
                    uint8_t channel, uint8_t velocity, bool one_shot, uint16_t note_length_ms);
void joy_play_single_note_vel(InputContext& ctx, int note, uint8_t channel,
                              uint8_t velocity, bool one_shot, uint16_t note_length_ms);
bool joy_note_acquire(InputContext& ctx, contract::SourceId source, uint8_t channel,
                      uint8_t note, uint8_t velocity, contract::VoiceToken& token);
bool joy_note_release(InputContext& ctx, contract::VoiceToken token,
                      uint8_t channel, uint8_t note);
void apply_joystick_mode(InputContext& ctx, JoystickPlayMode next_mode);
void joy_cycle_mode_config(InputContext& ctx);

// The single way an edited Config becomes live: validates it, silences what
// the change would otherwise strand (a moved lead channel, a different
// joystick preset), projects it onto Settings/engine/transports, rebuilds
// the progression when the scale changed, and marks it for autosave.
// Returns false, changing nothing, when `next` does not validate.
bool commit_config(InputContext& ctx, contract::Config next);

// The note/bend/drone reset apply_joystick_mode() performs on every family or
// preset change, without touching Config itself. Exposed so a caller that
// already knows exactly which family parameters it wants (e.g. a test
// building one migration-table row) doesn't have to go through a
// JoystickPlayMode/RecipeId proxy to get the same clean-slate guarantee.
void reset_joystick_gesture_state(InputContext& ctx);

// Joy-mode filter helpers — canonical source of the chord/lead filter lists.
// chord_context=true → chord filter, false → lead filter.
uint8_t joy_mode_filter_count(bool chord_context);
JoystickPlayMode joy_mode_filter_item(bool chord_context, uint8_t index);

inline bool is_chord_screen(const InputContext& ctx) {
  return ctx.nav.chord_context;
}

}  // namespace app
