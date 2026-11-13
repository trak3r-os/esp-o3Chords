// Stub implementations of BLE MIDI and USB Device MIDI for native host tests.
// These classes have NimBLE / TinyUSB hardware dependencies that can't link
// on the host.  The stubs satisfy the linker without any real functionality.
#include "midi/ble_midi.h"
#include "midi/usb_device_midi.h"

// --- BleMidi stubs ---
namespace midi {

BleMidi::BleMidi(Router& r) : router_(r) { r.register_sink(this); }
BleMidi::~BleMidi() = default;
bool BleMidi::begin()                        { return true; }
void BleMidi::loop()                         {}
void BleMidi::enable(bool e)                 { enabled_ = e; }
void BleMidi::set_device_name(const char*)   {}
void BleMidi::start_advertising()            {}
bool BleMidi::send_midi_packet(const uint8_t*, size_t) { return true; }
int  BleMidi::gap_event_callback(struct ble_gap_event*, void*) { return 0; }
void BleMidi::nimble_host_task(void*)        {}
void BleMidi::send_panic()                   {}

void BleMidi::send_note_on(uint8_t, uint8_t, uint8_t)         {}
void BleMidi::send_note_off(uint8_t, uint8_t, uint8_t)        {}
void BleMidi::send_pitch_bend(uint8_t, int16_t)               {}
void BleMidi::send_control_change(uint8_t, uint8_t, uint8_t)  {}
void BleMidi::send_channel_pressure(uint8_t, uint8_t)         {}
app::contract::TransportState BleMidi::transport_state() const {
  return enabled_ && connected_ ? app::contract::TransportState::Connected
                                : app::contract::TransportState::Disconnected;
}

// --- UsbDeviceMidi stubs ---

UsbDeviceMidi::UsbDeviceMidi(Router& r) : router_(r) { r.register_sink(this); }
bool UsbDeviceMidi::begin()                   { return true; }
void UsbDeviceMidi::loop()                    {}
void UsbDeviceMidi::enable(bool e)            { enabled_ = e; }
void UsbDeviceMidi::send_panic()              {}
bool UsbDeviceMidi::send_usb_midi_packet(uint8_t, uint8_t, uint8_t, uint8_t) { return true; }

void UsbDeviceMidi::send_note_on(uint8_t, uint8_t, uint8_t)         {}
void UsbDeviceMidi::send_note_off(uint8_t, uint8_t, uint8_t)        {}
void UsbDeviceMidi::send_pitch_bend(uint8_t, int16_t)               {}
void UsbDeviceMidi::send_control_change(uint8_t, uint8_t, uint8_t)  {}
void UsbDeviceMidi::send_channel_pressure(uint8_t, uint8_t)         {}
app::contract::TransportState UsbDeviceMidi::transport_state() const {
  return enabled_ && connected_ ? app::contract::TransportState::Connected
                                : app::contract::TransportState::Disconnected;
}

}  // namespace midi
