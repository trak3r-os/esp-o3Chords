#include "app/settings_bridge.h"

#include <algorithm>

namespace app {

engine::StrumRhythm strum_rhythm_from_u8(uint8_t v) {
  if (v >= static_cast<uint8_t>(engine::StrumRhythm::kCOUNT)) return engine::StrumRhythm::kNone;
  return static_cast<engine::StrumRhythm>(v);
}

engine::ArpRate arp_rate_from_u8(uint8_t v) {
  switch (v) {
    case 0: return engine::ArpRate::kQuarter;
    case 1: return engine::ArpRate::kEighth;
    case 2: return engine::ArpRate::kSixteenth;
    case 3: return engine::ArpRate::kTriplet;
    default: return engine::ArpRate::kEighth;
  }
}

engine::ArpPattern arp_pattern_from_u8(uint8_t v) {
  switch (v) {
    case 0: return engine::ArpPattern::kUp;
    case 1: return engine::ArpPattern::kDown;
    case 2: return engine::ArpPattern::kUpDown;
    case 3: return engine::ArpPattern::kDownUp;
    case 4: return engine::ArpPattern::kUpDownInc;
    case 5: return engine::ArpPattern::kRandom;
    case 6: return engine::ArpPattern::kOrder;
    default: return engine::ArpPattern::kUp;
  }
}

void apply_engine_settings(const Settings& s, engine::Engine& engine, uint8_t midi_channel) {
  engine.set_bpm(s.bpm);
  engine.set_key(s.key);
  engine.set_scale(s.scale);
  engine.set_channel(midi_channel);
  engine.set_lead_channel(s.midi_channel_joy);

  // Chord velocity lives on each progression step; without this the Velocity
  // setting would only take effect the next time the progression is rebuilt.
  for (auto& step : engine.progression().steps_mutable()) step.velocity = s.velocity;

  engine.performer().set_note_length_ms(s.note_length_ms);
  engine.performer().set_humanize_ms(s.humanize_ms);
  engine.performer().set_swing_percent(s.swing_percent);
  engine.performer().set_velocity_random(s.velocity_random);
  engine.performer().set_accent_pattern(s.accent_pattern);

  // Voice leading & voicing spread
  engine.modifier().set_voice_leading(
      static_cast<engine::VoiceLeading>(std::min<uint8_t>(s.voice_leading, 2)));
  engine.modifier().set_voicing_spread(
      static_cast<engine::VoicingSpread>(std::min<uint8_t>(s.voicing_spread, 7)));
  engine.modifier().set_voice_leading_complexity(s.voice_leading_complexity);

  switch (s.mode) {
    case PerformanceMode::kChord:
      engine.set_performance_mode(engine::PerformanceMode::kChord);
      engine.performer().set_strum_ms(0);
      engine.arpeggiator().set_enabled(false);
      break;
    case PerformanceMode::kStrum:
      engine.set_performance_mode(engine::PerformanceMode::kStrum);
      engine.performer().set_strum_ms(s.strum_ms);
      engine.performer().set_strum_rhythm(strum_rhythm_from_u8(s.strum_rhythm));
      engine.arpeggiator().set_enabled(false);
      break;
    case PerformanceMode::kArp:
    case PerformanceMode::kArp2Oct:
    case PerformanceMode::kHarp: {
      engine.set_performance_mode(engine::PerformanceMode::kArp);
      engine.arpeggiator().set_enabled(true);
      engine.arpeggiator().set_octaves(std::max<uint8_t>(1, std::min<uint8_t>(s.arp_octaves, 4)));
      engine.arpeggiator().set_rate(arp_rate_from_u8(s.arp_rate));
      engine.arpeggiator().set_pattern(
          arp_pattern_from_u8(s.arp_pattern));
      engine.arpeggiator().set_gate_percent(s.arp_gate_percent);
    } break;
  }

  engine.set_bend_range(s.bend_range_semitones);
  engine.midi_clock().set_mode(static_cast<engine::ClockMode>(s.clock_mode));
}

void apply_settings(AppState& app_state, engine::Engine& engine,
                    midi::BleMidi& ble, midi::UsbDeviceMidi& usb) {
  auto& s = app_state.settings();
  app_state.sanitize();
  apply_engine_settings(s, engine, s.midi_channel);

  // Roll lives only in Config, not the flat Settings blob — adding it there
  // would grow the frozen 112-byte v1 NVS layout the migration path pins
  // (state.cpp's static_assert). Config already has proper versioned wire
  // handling (config_codec v3), so roll is pushed straight from there.
  const auto& cfg = app_state.config();
  engine.performer().set_roll_ms(cfg.roll_ms);
  engine.performer().set_roll_direction(static_cast<engine::RollDirection>(std::min<uint8_t>(cfg.roll_dir, 3)));

  const auto desired = s.usb_host_midi_enabled
      ? midi::UsbRole::Host
      : (s.usb_device_midi_enabled ? midi::UsbRole::Device : midi::UsbRole::Off);
  auto& usb_role = app_state.usb_role_state();
  usb_role.request(desired);

  ble.set_device_name(s.ble_device_name);
  ble.enable(s.ble_midi_enabled);
  // A role transition is a reboot-boundary change.  Do not mutate a live OTG
  // peripheral while desired != applied; main applies the selected role once
  // during boot and only the device driver is touched for a stable device role.
  if (!usb_role.restart_required()) usb.enable(usb_role.applied() == midi::UsbRole::Device);
}

}  // namespace app
