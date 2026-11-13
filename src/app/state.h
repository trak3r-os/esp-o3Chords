#pragma once

#include <stddef.h>
#include <stdint.h>

#include "midi/router.h"
#include "app/contracts/contracts.h"

namespace app {

enum class PerformanceMode : uint8_t {
  kChord = 0,
  kStrum = 1,
  kArp = 2,
  kArp2Oct = 3,
  kHarp = 4,
};

enum class PlayTrigger : uint8_t {
  kShot = 0,   // one-shot fire-and-forget   (was bool false)
  kHold = 1,   // chord sustained until key release (was bool true)
  kRepeat = 2, // auto-retrigger at note_length_ms while held
  kCOUNT = 3,
};

inline bool play_trigger_holds(PlayTrigger t) { return t != PlayTrigger::kShot; }
inline bool play_trigger_sustains(PlayTrigger t) { return t == PlayTrigger::kHold; }

// Persisted settings (must remain POD for NVS blob storage).
struct Settings {
  // Header
  uint32_t magic = 0x4F334348;   // 'O3CH'
  uint16_t version = 1;
  uint16_t size = 0;

  // Musical params
  float bpm = 120.0f;
  char key[4] = "C";             // e.g. "C", "F#", "Bb"
  char scale[16] = "Major";      // e.g. "Major", "Minor"
  uint8_t midi_channel = 0;      // 0-15
  uint8_t velocity = 110;        // 1-127

  // Play behavior
  PerformanceMode mode = PerformanceMode::kChord;
  PlayTrigger play_trigger = PlayTrigger::kHold;  // HOLD / SHOT / REPE

  // Timing/feel
  uint16_t note_length_ms = 240;
  uint16_t strum_ms = 14;
  uint8_t humanize_ms = 6;

  // Arp params
  uint8_t arp_rate = 1;          // 0=quarter,1=eighth,2=sixteenth,3=triplet
  uint8_t arp_pattern = 0;       // 0=up,1=down,2=updown,3=downup,4=updowninc,5=random,6=order
  uint8_t arp_octaves = 1;       // 1-4
  uint8_t arp_gate_percent = 80; // 1-100

  // Strum rhythm
  uint8_t strum_rhythm = 0;     // 0=none, 1=salsa, 2=bossa, 3=reggae, 4=funk

  // Clock sync
  uint8_t clock_mode = 1;       // 0=Internal, 1=Auto, 2=External

  // MIDI transports
  bool ble_midi_enabled = true;
  bool usb_device_midi_enabled = true;
  char ble_device_name[32] = "o3Chords";

  // Input mapping
  uint8_t joystick_rotation = 0;    // 0-3: rotate 8-dir joystick by 0/90/180/270 degrees CW
  uint8_t button_select_role = 0;   // 0=btn0 is select, 1=btn8 is select, 2=btn9 is select

  // Per-scope MIDI channels
  // Opaque byte retained solely at the historical v1 offset for migration.
  uint8_t legacy_v1_removed_channel = 1;
  uint8_t midi_channel_joy = 0;     // Joystick direct-MIDI channel (0-15)

  // Joystick flip
  bool joystick_flip_h = false;     // Flip joystick horizontal axis
  bool joystick_flip_v = false;     // Flip joystick vertical axis

  // Opaque v1 bytes for the removed sequencer voice. They are never read by
  // runtime code; migrate_v1_result() only relies on the frozen struct size.
  uint8_t legacy_v1_removed_voice[10] = {};

  // Soloist voice (independent from play-mode settings)
  uint8_t solo_velocity = 100;        // 1-127
  uint16_t solo_note_length_ms = 240; // note sustain length in ms

  // Voice leading & voicing
  uint8_t voice_leading = 1;        // 0=Off, 1=Smooth, 2=Common tones
  uint8_t voicing_spread = 1;       // 0=Close, 1=Default, 2=Spread (drop-2)

  // Display
  uint8_t nashville_display = 0;    // 0=Roman numerals, 1=Nashville numbers

  // Enhanced humanize
  uint8_t swing_percent = 0;        // 0-100 swing amount
  uint8_t velocity_random = 0;      // 0-30 random velocity deviation range
  uint8_t accent_pattern = 0;       // 0=off, 1=downbeat, 2=backbeat, 3=every4th

  // Voice-leading complexity: 0 = smoothest voicing, 100 = most contrasting.
  // Controls rototranslation-based voicing selection in the engine.
  uint8_t voice_leading_complexity = 0;

  // USB Host MIDI: receive clock/notes from external USB MIDI devices.
  // Mutually exclusive with usb_device_midi at runtime (one USB OTG peripheral).
  // Default false: device mode (o3Chords appears as MIDI device to DAW).
  bool usb_host_midi_enabled = false;

  // Pitch bend range for kBend joystick mode and arp bend-into-next.
  // Valid values: 2, 5, 7, 12 semitones. 0 from old NVS → sanitize() → 12.
  uint8_t bend_range_semitones = 12;

  // Key navigation step: 0=Fifths, 1=Fourths, 2=Semitone (mirrors KeyNavMode).
  // 0 from old NVS is valid (Fifths), so no special handling needed.
  uint8_t key_nav_mode = 2;  // kSemitone

  // IMPORTANT: New fields MUST be appended at the END.
  // Zero-value must be handled by sanitize(). Never reorder or remove fields.
};

static_assert(sizeof(Settings) <= 512, "Settings must fit in NVS downgrade buffer");

class AppState {
 public:
  Settings& settings() { return settings_; }
  const Settings& settings() const { return settings_; }
  contract::Config& config() { return config_; }
  const contract::Config& config() const { return config_; }
  void set_config(const contract::Config& config) { config_ = config; config_initialized_ = true; }

  void sanitize();
  midi::UsbRoleState& usb_role_state() { return usb_role_state_; }
  const midi::UsbRoleState& usb_role_state() const { return usb_role_state_; }

 private:
  Settings settings_;
  contract::Config config_;
  bool config_initialized_ = false;
  midi::UsbRoleState usb_role_state_;
};

}  // namespace app
