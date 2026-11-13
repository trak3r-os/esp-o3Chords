#pragma once

#include <cstdint>
#include <vector>
#include "app/contracts/contracts.h"

namespace midi {

enum class UsbRole : uint8_t { Off, Device, Host };

// Configuration boundary for the single USB OTG peripheral.  Changing the
// desired role never mutates the active role; composition applies it at boot.
class UsbRoleState {
 public:
  UsbRoleState() = default;
  explicit UsbRoleState(UsbRole active) : configured_(active), active_(active) {}
  UsbRole configured() const { return configured_; }
  UsbRole active() const { return active_; }
  UsbRole desired() const { return configured_; }
  UsbRole applied() const { return active_; }
  bool restart_required() const { return configured_ != active_; }
  void request(UsbRole role) { configured_ = role; }
  void apply_at_boot() { active_ = configured_; }

 private:
  UsbRole configured_ = UsbRole::Off;
  UsbRole active_ = UsbRole::Off;
};

class TransportSink {
 public:
  virtual ~TransportSink() = default;
  virtual void send_note_on(uint8_t channel, uint8_t note, uint8_t velocity) = 0;
  virtual void send_note_off(uint8_t channel, uint8_t note, uint8_t velocity) = 0;
  virtual void send_pitch_bend(uint8_t channel, int16_t value) = 0;
  virtual void send_control_change(uint8_t channel, uint8_t controller, uint8_t value) = 0;
  // Channel aftertouch. Optional: a sink that does not carry it ignores it.
  virtual void send_channel_pressure(uint8_t /*channel*/, uint8_t /*value*/) {}
  virtual bool is_available() const { return true; }
  virtual app::contract::TransportState transport_state() const {
    return is_available() ? app::contract::TransportState::Connected
                          : app::contract::TransportState::Disconnected;
  }
  virtual app::contract::SendResult last_delivery_result() const {
    return is_available() ? app::contract::SendResult::Accepted
                          : app::contract::SendResult::Disconnected;
  }
};

class Router {
 public:
  bool begin();
  void loop();

  void register_sink(TransportSink* sink);

  void note_on(uint8_t channel, uint8_t note, uint8_t velocity);
  void note_off(uint8_t channel, uint8_t note, uint8_t velocity);
  void pitch_bend(uint8_t channel, int16_t value);
  void control_change(uint8_t channel, uint8_t controller, uint8_t value);
  void channel_pressure(uint8_t channel, uint8_t value);
  // Forward an event received from one transport without echoing it back to
  // that same transport. This prevents MIDI feedback loops.
  void forward_note_on(TransportSink* source, uint8_t channel, uint8_t note, uint8_t velocity);
  void forward_note_off(TransportSink* source, uint8_t channel, uint8_t note, uint8_t velocity);
  void forward_pitch_bend(TransportSink* source, uint8_t channel, int16_t value);
  void forward_control_change(TransportSink* source, uint8_t channel, uint8_t controller, uint8_t value);
  uint8_t active_route_mask() const;
  app::contract::SendResult route_note_on(uint8_t route_mask, uint8_t channel,
                                           uint8_t note, uint8_t velocity);
  app::contract::SendResult route_note_off(uint8_t route_mask, uint8_t channel,
                                            uint8_t note, uint8_t velocity);

 private:
  std::vector<TransportSink*> sinks_;
};

}  // namespace midi
