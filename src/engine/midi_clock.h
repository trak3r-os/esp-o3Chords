#pragma once

#include <atomic>
#include <cstdint>

namespace engine {

enum class ClockMode : uint8_t {
  kInternal = 0,  // Ignore external clock
  kAuto = 1,      // Use external when present, fallback to internal
  kExternal = 2,  // External only (falls back gracefully if no clock)
};

enum class ClockSource : uint8_t {
  kNone = 0,
  kUsb = 1,
  kBle = 2,
};

enum class TransportState : uint8_t {
  kStopped = 0,
  kPlaying = 1,
};

class MidiClock {
 public:
  // Called from any task (USB loop or BLE NimBLE task).
  // Thread-safe: uses atomics only.
  void feed_byte(uint8_t status_byte, ClockSource source);

  // Called from main loop only (~every 10ms).
  // Returns number of MIDI clock ticks processed this call (for tick-driven scheduling).
  uint16_t update(uint64_t now_us);

  void set_mode(ClockMode mode);
  ClockMode mode() const { return mode_; }

  void set_internal_bpm(float bpm) { internal_bpm_ = bpm; }

  // Returns external BPM when synced, internal BPM otherwise.
  float effective_bpm() const;

  bool is_synced() const { return synced_; }
  ClockSource active_source() const { return active_source_; }
  TransportState transport_state() const { return transport_state_; }

  // A stable projection consumed by musical producers.  Producers must use
  // this state rather than deriving tempo or source ownership themselves.
  struct Snapshot {
    ClockMode mode = ClockMode::kInternal;
    ClockSource source = ClockSource::kNone;
    bool running = false;
    float tempo = 120.0f;
    uint32_t position = 0;
    bool locked = false;
  };
  Snapshot snapshot() const;

 private:
  static constexpr float kAlpha = 2.0f / 25.0f;  // EMA ~24-sample
  static constexpr uint64_t kSyncTimeoutUs = 2'000'000;
  static constexpr uint32_t kMinIntervalUs = 5'000;   // ~500 BPM max
  static constexpr uint32_t kMaxIntervalUs = 125'000;  // ~20 BPM min

  ClockMode mode_ = ClockMode::kInternal;
  float internal_bpm_ = 120.0f;

  // Atomic exchange with transport tasks
  std::atomic<uint32_t> pending_ticks_{0};
  std::atomic<uint64_t> latest_tick_us_{0};
  std::atomic<uint8_t> pending_transport_{0};  // 0 or 0xFA/0xFB/0xFC
  std::atomic<uint8_t> pending_transport_source_{0};
  std::atomic<uint8_t> pending_source_{0};

  // Main-task-only state
  bool synced_ = false;
  ClockSource active_source_ = ClockSource::kNone;
  TransportState transport_state_ = TransportState::kStopped;
  bool transport_stop_seen_ = false;
  float external_bpm_ = 0.0f;
  float avg_interval_us_ = 0.0f;
  uint64_t last_tick_processed_us_ = 0;
  uint64_t last_activity_us_ = 0;
  uint32_t position_ = 0;
  bool ema_primed_ = false;

  // Internal PPQ tick generation (used when no external clock is active)
  uint64_t last_update_us_ = 0;
  double internal_tick_remainder_ = 0.0;
};

}  // namespace engine
