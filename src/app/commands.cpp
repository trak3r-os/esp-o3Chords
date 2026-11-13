#include "app/commands.h"
#include "app/config_codec.h"
#include "app/parameters.h"
#include <algorithm>

namespace app {

contract::ApplyResult CommandController::apply_parameter(const contract::Command& command) {
  contract::Config next = config_;
  const auto* d = parameters::find(command.parameter);
  if (!d) return {false, contract::ApplyError::Invalid};
  const int32_t value = parameters::clamp(*d, command.value);
  switch (command.parameter) {
    case contract::ParamId::Velocity: next.velocity = static_cast<uint8_t>(value); break;
    case contract::ParamId::Duration: next.duration_ms = static_cast<uint16_t>(value); break;
    case contract::ParamId::Key: next.key = static_cast<uint8_t>(value); break;
    case contract::ParamId::Scale: next.scale = static_cast<uint8_t>(value); break;
    case contract::ParamId::Articulation: next.articulation = static_cast<contract::Articulation>(value); break;
    case contract::ParamId::ArpRate: next.arp_rate = static_cast<uint8_t>(value); break;
    case contract::ParamId::ArpPattern: next.arp_pattern = static_cast<uint8_t>(value); break;
    case contract::ParamId::ArpOctaves: next.arp_octaves = static_cast<uint8_t>(value); break;
    case contract::ParamId::Swing: next.swing = static_cast<uint8_t>(value); break;
    case contract::ParamId::Humanize: next.humanize = static_cast<uint8_t>(value); break;
    case contract::ParamId::Voicing: next.voicing_spread = static_cast<uint8_t>(value); break;
    case contract::ParamId::StrumFeel: next.strum_feel = static_cast<uint8_t>(value); break;
    case contract::ParamId::ChordChannel: next.chord_channel = static_cast<uint8_t>(value); break;
    case contract::ParamId::LeadChannel: next.lead_channel = static_cast<uint8_t>(value); break;
    case contract::ParamId::JoystickRecipe:
      next.active_preset = static_cast<app::PresetId>(value);
      app::config::recall_preset(next.joy_families, next.active_preset);
      break;
    case contract::ParamId::JoystickXTarget: next.joystick.x_target = static_cast<uint8_t>(value); break;
    case contract::ParamId::JoystickYTarget: next.joystick.y_target = static_cast<uint8_t>(value); break;
    case contract::ParamId::JoystickCurve: next.joystick.curve = static_cast<uint8_t>(value); break;
    case contract::ParamId::ClockSource: next.clock_source = static_cast<contract::ClockSource>(value); break;
    case contract::ParamId::UsbRole: next.usb_role = static_cast<contract::UsbRole>(value); break;
    case contract::ParamId::BleEnabled: next.ble_enabled = value != 0; break;
    case contract::ParamId::Bpm: next.bpm = static_cast<float>(value); break;
    default: return {false, contract::ApplyError::Unsupported};
  }
  if (contract::validate(next).error != contract::ValidationError::None) return {false, contract::ApplyError::Invalid};
  const bool channel_change = next.chord_channel != config_.chord_channel || next.lead_channel != config_.lead_channel;
  if (channel_change) engine_.all_notes_off();
  next.revision = config_.revision + 1;
  config_ = next;
  if (command.parameter == contract::ParamId::Velocity) {
    // Engine's existing progression remains the source of chord structure;
    // this command still updates the canonical snapshot atomically.
  } else if (command.parameter == contract::ParamId::ArpOctaves) {
    engine_.arpeggiator().set_octaves(next.arp_octaves);
  }
  return {true, contract::ApplyError::None};
}

contract::ApplyResult CommandController::apply(const contract::Command& command) {
  switch (command.kind) {
    case contract::CommandKind::SetParameter: return apply_parameter(command);
    case contract::CommandKind::SetJoystick:
      if (command.joystick.x_min >= command.joystick.x_max || command.joystick.y_min >= command.joystick.y_max)
        return {false, contract::ApplyError::Invalid};
      config_.joystick = command.joystick; ++config_.revision; return {true, contract::ApplyError::None};
    case contract::CommandKind::SetArticulation:
      return apply_parameter({contract::CommandKind::SetParameter, contract::ParamId::Articulation, command.value, {}});
    case contract::CommandKind::Panic:
      engine_.all_notes_off(); ++config_.revision; return {true, contract::ApplyError::None};
    case contract::CommandKind::Reset: return {false, contract::ApplyError::Busy};
    default: return {false, contract::ApplyError::Unsupported};
  }
}

}  // namespace app
