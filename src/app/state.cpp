#include "app/state.h"
#include "app/config_codec.h"

#include <cmath>
#include <cstring>

namespace app {

static void ensure_null_terminated(char* s, size_t n) {
  if (n == 0) return;
  s[n - 1] = '\0';
}

void AppState::sanitize() {
  // Defensive clamps for persisted blobs.
  if (settings_.magic != 0x4F334348) {
    settings_ = Settings{};
  }

  ensure_null_terminated(settings_.key, sizeof(settings_.key));
  ensure_null_terminated(settings_.scale, sizeof(settings_.scale));
  ensure_null_terminated(settings_.ble_device_name, sizeof(settings_.ble_device_name));

  if (!std::isfinite(settings_.bpm) || settings_.bpm < 20.0f) settings_.bpm = 20.0f;
  if (settings_.bpm > 300.0f) settings_.bpm = 300.0f;

  if (settings_.midi_channel > 15) settings_.midi_channel = 0;
  if (settings_.midi_channel_joy > 15) settings_.midi_channel_joy = 0;
  if (settings_.velocity < 1) settings_.velocity = 1;
  if (settings_.velocity > 127) settings_.velocity = 127;

  if (settings_.note_length_ms < 20) settings_.note_length_ms = 20;
  if (settings_.note_length_ms > 4000) settings_.note_length_ms = 4000;

  if (settings_.strum_ms > 240) settings_.strum_ms = 240;
  if (settings_.humanize_ms > 40) settings_.humanize_ms = 40;

  if (settings_.arp_rate > 3) settings_.arp_rate = 1;
  if (settings_.arp_pattern > 6) settings_.arp_pattern = 0;
  if (settings_.arp_octaves < 1) settings_.arp_octaves = 1;
  if (settings_.arp_octaves > 4) settings_.arp_octaves = 4;
  if (settings_.arp_gate_percent < 1) settings_.arp_gate_percent = 80;
  if (settings_.arp_gate_percent > 100) settings_.arp_gate_percent = 100;

  if (settings_.solo_velocity < 1 || settings_.solo_velocity > 127) settings_.solo_velocity = 100;
  if (settings_.solo_note_length_ms < 20 || settings_.solo_note_length_ms > 4000) settings_.solo_note_length_ms = 240;

  if (static_cast<uint8_t>(settings_.play_trigger) >= static_cast<uint8_t>(PlayTrigger::kCOUNT))
    settings_.play_trigger = PlayTrigger::kHold;

  if (settings_.clock_mode > 2) settings_.clock_mode = 0;

  // Performance mode bounds check
  if (static_cast<uint8_t>(settings_.mode) > 4)
    settings_.mode = PerformanceMode::kChord;

  // ---- Remaining fields ----

  // swing_percent: 0-100 (0 = no swing)
  if (settings_.swing_percent > 100) settings_.swing_percent = 0;

  // velocity_random: 0-30 random velocity deviation
  if (settings_.velocity_random > 30) settings_.velocity_random = 0;

  // voice_leading: 0=Off, 1=Smooth, 2=Common
  if (settings_.voice_leading > 2) settings_.voice_leading = 1;

  // voicing_spread: 0=Close,1=Default,2=Drop2,3=Drop3,4=Open,5=Shell,6=Rootless,7=Guitar
  if (settings_.voicing_spread > 7) settings_.voicing_spread = 1;

  // joystick_rotation: must be 0/1/2/3 (maps to 0°/90°/180°/270°)
  if (settings_.joystick_rotation > 3) settings_.joystick_rotation = 0;

  // accent_pattern: 0=off, 1=downbeat, 2=backbeat, 3=every4th
  if (settings_.accent_pattern > 3) settings_.accent_pattern = 0;

  // nashville_display: 0=Roman, 1=Nashville — clamp to bool range
  if (settings_.nashville_display > 1) settings_.nashville_display = 0;

  // voice_leading_complexity: 0=smoothest, 100=most contrasting
  if (settings_.voice_leading_complexity > 100) settings_.voice_leading_complexity = 0;

  // bend_range_semitones: valid values {2,5,7,12}. 0 from old NVS defaults to 12.
  {
    const uint8_t br = settings_.bend_range_semitones;
    if (br != 2 && br != 5 && br != 7 && br != 12)
      settings_.bend_range_semitones = 12;
  }

  // key_nav_mode: 0=Fifths, 1=Fourths, 2=Semitone. Values >= 3 are invalid.
  if (settings_.key_nav_mode >= 3) settings_.key_nav_mode = 2;

  // Keep size/version coherent (informational)
  settings_.version = 1;
  settings_.size = sizeof(Settings);
  if (!config_initialized_) {
    config_ = config::from_settings(settings_);
    config_initialized_ = true;
  }
}

}  // namespace app
