#include "midi/router.h"

namespace midi {

bool Router::begin() { return true; }

void Router::loop() {
  // TODO: Consume engine events and fan out to registered transports.
}

void Router::register_sink(TransportSink* sink) { sinks_.push_back(sink); }

void Router::note_on(uint8_t channel, uint8_t note, uint8_t velocity) {
  for (auto* sink : sinks_) {
    sink->send_note_on(channel, note, velocity);
  }
}

void Router::note_off(uint8_t channel, uint8_t note, uint8_t velocity) {
  for (auto* sink : sinks_) {
    sink->send_note_off(channel, note, velocity);
  }
}

void Router::pitch_bend(uint8_t channel, int16_t value) {
  for (auto* sink : sinks_) {
    sink->send_pitch_bend(channel, value);
  }
}

void Router::control_change(uint8_t channel, uint8_t controller, uint8_t value) {
  for (auto* sink : sinks_) {
    sink->send_control_change(channel, controller, value);
  }
}

void Router::channel_pressure(uint8_t channel, uint8_t value) {
  for (auto* sink : sinks_) {
    sink->send_channel_pressure(channel, value);
  }
}

void Router::forward_note_on(TransportSink* source, uint8_t channel, uint8_t note, uint8_t velocity) {
  for (auto* sink : sinks_) if (sink && sink != source) sink->send_note_on(channel, note, velocity);
}

void Router::forward_note_off(TransportSink* source, uint8_t channel, uint8_t note, uint8_t velocity) {
  for (auto* sink : sinks_) if (sink && sink != source) sink->send_note_off(channel, note, velocity);
}

void Router::forward_pitch_bend(TransportSink* source, uint8_t channel, int16_t value) {
  for (auto* sink : sinks_) if (sink && sink != source) sink->send_pitch_bend(channel, value);
}

void Router::forward_control_change(TransportSink* source, uint8_t channel,
                                    uint8_t controller, uint8_t value) {
  for (auto* sink : sinks_)
    if (sink && sink != source) sink->send_control_change(channel, controller, value);
}

uint8_t Router::active_route_mask() const {
  uint8_t mask = 0;
  for (size_t i = 0; i < sinks_.size() && i < 8; ++i)
    if (sinks_[i] && sinks_[i]->is_available()) mask |= static_cast<uint8_t>(1u << i);
  return mask;
}

app::contract::SendResult Router::route_note_on(uint8_t route_mask, uint8_t channel,
                                                 uint8_t note, uint8_t velocity) {
  bool accepted = false, backpressure = false;
  for (size_t i = 0; i < sinks_.size() && i < 8; ++i) {
    if (!(route_mask & (1u << i)) || !sinks_[i]) continue;
    auto* sink = sinks_[i];
    if (sink->transport_state() == app::contract::TransportState::Recovering) {
      backpressure = true;
      continue;
    }
    if (!sink->is_available()) continue;
    sink->send_note_on(channel, note, velocity);
    const auto result = sink->last_delivery_result();
    accepted |= result == app::contract::SendResult::Accepted;
    backpressure |= result == app::contract::SendResult::Backpressure;
  }
  return accepted ? app::contract::SendResult::Accepted :
         backpressure ? app::contract::SendResult::Backpressure :
                        app::contract::SendResult::Disconnected;
}

app::contract::SendResult Router::route_note_off(uint8_t route_mask, uint8_t channel,
                                                  uint8_t note, uint8_t velocity) {
  bool accepted = false, backpressure = false;
  for (size_t i = 0; i < sinks_.size() && i < 8; ++i) {
    if (!(route_mask & (1u << i)) || !sinks_[i]) continue;
    auto* sink = sinks_[i];
    if (!sink->is_available()) continue;
    sink->send_note_off(channel, note, velocity);
    const auto result = sink->last_delivery_result();
    accepted |= result == app::contract::SendResult::Accepted;
    backpressure |= result == app::contract::SendResult::Backpressure;
  }
  return accepted ? app::contract::SendResult::Accepted :
         backpressure ? app::contract::SendResult::Backpressure :
                        app::contract::SendResult::Disconnected;
}

}  // namespace midi
