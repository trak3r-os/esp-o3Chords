#pragma once

#include "midi/router.h"
#include <stdint.h>

namespace engine { class MidiClock; }

namespace midi {

// Internal state (heap-allocated; opaque to callers).
struct UsbHostCtx;

// USB Host MIDI transport.
//
// When enabled, the ESP32-S3 USB OTG peripheral is placed in host mode.
// This is mutually exclusive with UsbDeviceMidi (device mode).
// Use case: receive MIDI clock and notes from an external USB MIDI device
// (drum machine, external player, etc.) and optionally forward output back to it.
//
// Received clock bytes (0xF8-0xFC) are fed to MidiClock.
// Received notes/CC/bend are forwarded to the Router.
// send_*() methods forward MIDI out to the connected USB device.
class UsbHostMidi : public TransportSink {
 public:
  explicit UsbHostMidi(Router& router);
  ~UsbHostMidi();

  bool begin();
  void loop();
  void enable(bool enabled);

  void set_midi_clock(engine::MidiClock* c) { midi_clock_ = c; }
  engine::MidiClock* midi_clock() const { return midi_clock_; }
  Router& router_ref() { return router_; }

  bool is_enabled()   const { return enabled_; }
  bool is_connected() const { return connected_; }
  bool is_available() const override { return enabled_ && connected_; }

  void send_note_on(uint8_t ch, uint8_t note, uint8_t vel)      override;
  void send_note_off(uint8_t ch, uint8_t note, uint8_t vel)     override;
  void send_pitch_bend(uint8_t ch, int16_t value)               override;
  void send_control_change(uint8_t ch, uint8_t cc, uint8_t val) override;
  void send_channel_pressure(uint8_t ch, uint8_t val) override;

  // Called from USB task callbacks — do not call from application code.
  void on_device_ready();
  void on_device_gone();

 private:
  void send_usb_midi(uint8_t cin, uint8_t status, uint8_t d1, uint8_t d2);

  static void host_lib_task(void* arg);
  static void client_task(void* arg);

  Router&            router_;
  engine::MidiClock* midi_clock_  = nullptr;
  bool               enabled_     = false;
  volatile bool      connected_   = false;
  bool               initialized_ = false;
  UsbHostCtx*        ctx_         = nullptr;
};

}  // namespace midi
