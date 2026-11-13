#pragma once

#include "midi/router.h"
#include <stdint.h>

namespace engine { class MidiClock; }

struct ble_gap_event;

namespace midi {

class BleMidi : public TransportSink {
 public:
  explicit BleMidi(Router& router);
  ~BleMidi();
  bool begin();
  void loop();

  void enable(bool enabled);
  void set_device_name(const char* name);
  void set_midi_clock(engine::MidiClock* clock) { midi_clock_ = clock; }
  engine::MidiClock* midi_clock() const { return midi_clock_; }
  bool is_enabled() const { return enabled_; }
  bool is_connected() const { return connected_; }
  bool is_available() const override { return enabled_ && connected_; }
  app::contract::TransportState transport_state() const override;
  app::contract::SendResult last_delivery_result() const override { return last_result_; }

  void set_own_addr_type(uint8_t type) { own_addr_type_ = type; }

  void send_note_on(uint8_t channel, uint8_t note, uint8_t velocity) override;
  void send_note_off(uint8_t channel, uint8_t note, uint8_t velocity) override;
  void send_pitch_bend(uint8_t channel, int16_t value) override;
  void send_control_change(uint8_t channel, uint8_t controller, uint8_t value) override;
  void send_channel_pressure(uint8_t channel, uint8_t value) override;

 private:
  bool send_midi_packet(const uint8_t* data, size_t len);

 public:
  void start_advertising();  // Exposed for callbacks

 private:
  static int gap_event_callback(struct ble_gap_event* event, void* arg);
  static void nimble_host_task(void* param);

  void send_panic();

  Router& router_;
  engine::MidiClock* midi_clock_ = nullptr;
  bool enabled_ = false;
  bool connected_ = false;
  bool initialized_ = false;
  bool subscribed_ = false;
  bool needs_panic_ = false;
  uint8_t panic_step_ = 0;
  uint8_t own_addr_type_ = 0;
  uint16_t conn_handle_ = 0;
  uint16_t midi_char_val_handle_ = 0;
  app::contract::SendResult last_result_ = app::contract::SendResult::Disconnected;
  char device_name_[32] = "ChordESP32";
};

}  // namespace midi
