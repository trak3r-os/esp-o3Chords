#pragma once

#include "app/state.h"
#include "engine/engine.h"
#include "midi/ble_midi.h"
#include "midi/usb_device_midi.h"

namespace app {

engine::StrumRhythm strum_rhythm_from_u8(uint8_t v);
engine::ArpRate arp_rate_from_u8(uint8_t v);
engine::ArpPattern arp_pattern_from_u8(uint8_t v);

void apply_engine_settings(const Settings& s, engine::Engine& engine, uint8_t midi_channel);
void apply_settings(AppState& app_state, engine::Engine& engine,
                    midi::BleMidi& ble, midi::UsbDeviceMidi& usb);

}  // namespace app
