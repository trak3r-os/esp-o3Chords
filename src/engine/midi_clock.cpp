#include "engine/midi_clock.h"

#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>

static const char* TAG = "MidiClock";

namespace engine {

void MidiClock::feed_byte(uint8_t status_byte, ClockSource source) {
  switch (status_byte) {
    case 0xF8:  // Timing Clock
      // The first source to reach the clock owns it until timeout or mode
      // change. This prevents USB and BLE from producing a blended tempo.
      {
        uint8_t owner = pending_source_.load(std::memory_order_relaxed);
        if (owner != 0 && owner != static_cast<uint8_t>(source)) return;
        if (owner == 0 && !pending_source_.compare_exchange_strong(
                owner, static_cast<uint8_t>(source), std::memory_order_relaxed)) return;
      }
      pending_ticks_.fetch_add(1, std::memory_order_relaxed);
      latest_tick_us_.store(static_cast<uint64_t>(esp_timer_get_time()),
                            std::memory_order_relaxed);
      pending_source_.store(static_cast<uint8_t>(source),
                            std::memory_order_relaxed);
      break;
    case 0xFA:  // Start
    case 0xFB:  // Continue
    case 0xFC:  // Stop
      {
        uint8_t owner = pending_source_.load(std::memory_order_relaxed);
        if (owner != 0 && owner != static_cast<uint8_t>(source)) return;
        if (owner == 0 && !pending_source_.compare_exchange_strong(
                owner, static_cast<uint8_t>(source), std::memory_order_relaxed)) return;
      }
      pending_transport_.store(status_byte, std::memory_order_relaxed);
      pending_transport_source_.store(static_cast<uint8_t>(source), std::memory_order_relaxed);
      break;
    default:
      break;
  }
}

void MidiClock::set_mode(ClockMode mode) {
  if (mode_ == mode) return;
  mode_ = mode;
  // Reset internal tick generator on mode change.
  last_update_us_ = 0;
  internal_tick_remainder_ = 0.0;
  pending_ticks_.store(0, std::memory_order_relaxed);
  pending_source_.store(0, std::memory_order_relaxed);
  pending_transport_.store(0, std::memory_order_relaxed);
  pending_transport_source_.store(0, std::memory_order_relaxed);
  transport_stop_seen_ = false;
  if (mode == ClockMode::kInternal) {
    synced_ = false;
    active_source_ = ClockSource::kNone;
    ema_primed_ = false;
    ESP_LOGI(TAG, "Clock mode: Internal");
  } else if (mode == ClockMode::kAuto) {
    ESP_LOGI(TAG, "Clock mode: Auto");
  } else {
    ESP_LOGI(TAG, "Clock mode: External");
  }
}

uint16_t MidiClock::update(uint64_t now_us) {
  // --- External clock processing (for Auto and External modes) ---
  if (mode_ != ClockMode::kInternal) {
    // Process transport commands
    uint8_t cmd = pending_transport_.exchange(0, std::memory_order_relaxed);
    const uint8_t cmd_source = pending_transport_source_.exchange(0, std::memory_order_relaxed);
    const uint8_t owner = pending_source_.load(std::memory_order_relaxed);
    if (cmd != 0 && cmd_source != 0 && owner != 0 && cmd_source != owner) cmd = 0;
    if (cmd != 0) {
      last_activity_us_ = now_us;
      switch (cmd) {
        case 0xFA:  // Start
          transport_state_ = TransportState::kPlaying;
          transport_stop_seen_ = false;
          ema_primed_ = false;
          ESP_LOGI(TAG, "Transport: Start");
          break;
        case 0xFB:  // Continue
          transport_state_ = TransportState::kPlaying;
          transport_stop_seen_ = false;
          ESP_LOGI(TAG, "Transport: Continue");
          break;
        case 0xFC:  // Stop
          transport_state_ = TransportState::kStopped;
          transport_stop_seen_ = true;
          ESP_LOGI(TAG, "Transport: Stop");
          break;
      }
    }

    // Process accumulated external ticks
    uint32_t ticks = pending_ticks_.exchange(0, std::memory_order_relaxed);
    uint64_t tick_us = latest_tick_us_.load(std::memory_order_relaxed);
    uint8_t src = pending_source_.load(std::memory_order_relaxed);

    if (ticks > 0 && tick_us > 0) {
      const auto incoming_source = static_cast<ClockSource>(src);
      if (active_source_ != ClockSource::kNone && active_source_ != incoming_source) {
        // A new transport must relock from its own timing; never combine the
        // accumulated interval or position of two physical sources.
        synced_ = false;
        ema_primed_ = false;
        avg_interval_us_ = 0.0f;
        position_ = 0;
        last_tick_processed_us_ = 0;
      }
      active_source_ = incoming_source;

      // Position counts accepted external clock ticks, including the ticks
      // spent acquiring the first lock. Tempo estimation must not reset it.
      position_ += ticks;

      if (last_tick_processed_us_ > 0 && tick_us > last_tick_processed_us_) {
        uint64_t total_interval = tick_us - last_tick_processed_us_;
        float per_tick_interval =
            static_cast<float>(total_interval) / static_cast<float>(ticks);

        if (per_tick_interval >= kMinIntervalUs &&
            per_tick_interval <= kMaxIntervalUs) {
          if (!ema_primed_) {
            avg_interval_us_ = per_tick_interval;
            ema_primed_ = true;
          } else {
            avg_interval_us_ =
                kAlpha * per_tick_interval + (1.0f - kAlpha) * avg_interval_us_;
          }

          float new_bpm = 60'000'000.0f / (avg_interval_us_ * 24.0f);
          if (!synced_) {
            ESP_LOGI(TAG, "Synced to external clock: %.1f BPM", new_bpm);
          }
          external_bpm_ = new_bpm;
          synced_ = true;
        }
      }
      last_tick_processed_us_ = tick_us;
    }

    // Timeout check
    const uint64_t last_realtime_us = std::max(last_tick_processed_us_, last_activity_us_);
    // A tick stamped by a transport task after this pass sampled `now_us` is
    // newer than now; unsigned subtraction would turn that into a huge age
    // and declare a live clock lost.
    if (last_realtime_us > 0 && now_us > last_realtime_us &&
        (now_us - last_realtime_us) > kSyncTimeoutUs) {
      ESP_LOGI(TAG, "External clock lost, reverting to internal");
      synced_ = false;
      active_source_ = ClockSource::kNone;
      ema_primed_ = false;
      position_ = 0;
      pending_source_.store(0, std::memory_order_relaxed);
      transport_state_ = TransportState::kStopped;
      // A dropout is not a Stop: a clock-only source that comes back must be
      // able to drive ticks again without ever sending Start.
      transport_stop_seen_ = false;
      // Both timestamps cleared, so this fires once per loss, not every pass.
      last_activity_us_ = 0;
      last_tick_processed_us_ = 0;
    }

    // If synced to external clock, return real tick count.
    if (synced_) {
      last_update_us_ = now_us;
      return transport_stop_seen_ ? 0 : static_cast<uint16_t>(ticks);
    }

    // External mode is a hard sync boundary: without a lock it must not
    // invent attacks from the internal clock. Auto mode may fall back.
    if (mode_ == ClockMode::kExternal) {
      last_update_us_ = now_us;
      internal_tick_remainder_ = 0.0;
      return 0;
    }
  }

  // --- Internal PPQ tick generation (kInternal, or Auto/External without sync) ---
  // Synthesize 24-PPQ ticks from elapsed time and internal BPM.
  if (last_update_us_ == 0 || now_us < last_update_us_) {
    last_update_us_ = now_us;
    return 0;
  }

  uint64_t elapsed_us = now_us - last_update_us_;
  last_update_us_ = now_us;

  double tick_interval_us = 60'000'000.0 / (static_cast<double>(internal_bpm_) * 24.0);
  double total = internal_tick_remainder_ + static_cast<double>(elapsed_us);
  uint16_t gen_ticks = static_cast<uint16_t>(total / tick_interval_us);
  internal_tick_remainder_ = total - (gen_ticks * tick_interval_us);

  position_ += gen_ticks;
  return gen_ticks;
}

float MidiClock::effective_bpm() const {
  if (mode_ == ClockMode::kInternal) {
    return internal_bpm_;
  }
  return synced_ ? external_bpm_ : internal_bpm_;
}

MidiClock::Snapshot MidiClock::snapshot() const {
  Snapshot result;
  result.mode = mode_;
  result.source = (mode_ == ClockMode::kInternal)
                      ? ClockSource::kNone : active_source_;
  if (mode_ != ClockMode::kInternal && !synced_ && mode_ == ClockMode::kAuto)
    result.source = ClockSource::kNone;
  result.running = transport_state_ == TransportState::kPlaying;
  result.tempo = effective_bpm();
  result.position = position_;
  result.locked = synced_;
  return result;
}

}  // namespace engine
