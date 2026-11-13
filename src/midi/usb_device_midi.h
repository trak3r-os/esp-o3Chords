#pragma once

#include "midi/router.h"
#include <stdint.h>

namespace engine { class MidiClock; }

namespace midi {

class UsbDeviceMidi : public TransportSink {
 public:
  explicit UsbDeviceMidi(Router& router);
  bool begin();
  void loop();

  void enable(bool enabled);
  void set_midi_clock(engine::MidiClock* clock) { midi_clock_ = clock; }
  bool is_enabled() const { return enabled_; }
  bool is_connected() const { return connected_; }
  bool is_available() const override { return enabled_ && connected_; }
  app::contract::TransportState transport_state() const override;
  app::contract::SendResult last_delivery_result() const override { return last_result_; }

  void send_note_on(uint8_t channel, uint8_t note, uint8_t velocity) override;
  void send_note_off(uint8_t channel, uint8_t note, uint8_t velocity) override;
  void send_pitch_bend(uint8_t channel, int16_t value) override;
  void send_control_change(uint8_t channel, uint8_t controller, uint8_t value) override;
  void send_channel_pressure(uint8_t channel, uint8_t value) override;
  void send_panic();

 private:
  bool send_usb_midi_packet(uint8_t cable_num, uint8_t status, uint8_t data1, uint8_t data2);

  Router& router_;
  engine::MidiClock* midi_clock_ = nullptr;
  bool enabled_ = false;
  bool connected_ = false;
  bool initialized_ = false;
  bool needs_panic_ = false;
  uint8_t panic_step_ = 0;
  app::contract::SendResult last_result_ = app::contract::SendResult::Disconnected;
};

}  // namespace midi
