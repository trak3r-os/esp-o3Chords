#pragma once

#include "midi/router.h"
#include "engine/note_ledger.h"
#include "util/fixed_vec.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace engine {

enum class EventType { kNoteOn, kNoteOff, kPitchBend };

struct ScheduledEvent {
  uint64_t due_ms = 0;
  uint32_t sequence = 0;  // assigned on admission; equal deadlines stay FIFO
  EventType type = EventType::kNoteOn;
  uint8_t note = 0;
  uint8_t velocity = 0;
  int16_t bend = 0;
  // Zero means "the scheduler's configured source/channel" and is resolved
  // when the event is admitted.  Once queued these values are immutable.
  app::contract::SourceId source = 0;
  uint8_t channel = 0;
  app::contract::Generation generation = 0;
  uint8_t route_mask = 0;  // captured at admission, never read from live config
};

/// High-precision MIDI event scheduler using esp_timer.
///
/// Events are dispatched from the esp_timer task (priority 22) with
/// microsecond-level accuracy, independent of the FreeRTOS tick rate.
/// All public methods are thread-safe (protected by a FreeRTOS mutex).
class MidiScheduler : private NoteLedgerSink {
 public:
  using AdmissionFailureCallback = void (*)(void*, app::contract::SourceId);
  ~MidiScheduler() { stop(); }

  bool start(midi::Router* router, uint8_t channel);
  void set_ledger(NoteLedger* ledger) { if (ledger) ledger_ = ledger; }
  void stop();

  void set_channel(uint8_t ch);
  void set_source(app::contract::SourceId source) { source_id_ = source; }
  void set_route_mask(uint8_t routes) { route_mask_ = routes; }
  void set_admission_failure_callback(AdmissionFailureCallback cb, void* arg) {
    admission_failure_callback_ = cb; admission_failure_arg_ = arg;
  }

  /// Schedule a single event (thread-safe, called from main task).
  bool schedule(const ScheduledEvent& event);

  /// Atomically admit a batch. Overflow returns false and silences this scheduler.
  bool schedule_batch(const ScheduledEvent* events, size_t count);

  /// Cancel all pending events without sending note-offs.
  void clear();

  /// Release this scheduler's source and cancel its queued events.
  void all_notes_off();
  void all_notes_off(app::contract::SourceId source);
  void invalidate_source(app::contract::SourceId source);

  /// Return a copy of currently sounding notes.
  util::FixedVec<uint8_t, 32> active_notes();  // ledger projection; compatibility API

 private:
  static void timer_callback(void* arg);
  void note_on(uint8_t route, uint8_t channel, uint8_t note, uint8_t velocity) override;
  void note_off(uint8_t route, uint8_t channel, uint8_t note) override;
  void dispatch_due();
  void silence_locked();
  void silence_source_locked(app::contract::SourceId source);
  void arm_next();

  midi::Router* router_ = nullptr;
  uint8_t channel_ = 0;
  esp_timer_handle_t timer_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;
  bool timer_running_ = false;

  static constexpr size_t kMaxQueue = 128;
  static constexpr size_t kMaxActive = 32;
  util::FixedVec<ScheduledEvent, kMaxQueue> queue_;   // sorted ascending by due_ms
  NoteLedger owned_ledger_;
  NoteLedger* ledger_ = &owned_ledger_;
  struct ActiveToken {
    uint8_t note = 0;
    app::contract::SourceId source = 0;
    app::contract::VoiceToken token{};
  };
  util::FixedVec<ActiveToken, kMaxActive> tokens_;
  uint32_t next_sequence_ = 1;
  app::contract::SourceId source_id_ = 1;
  uint8_t route_mask_ = 1;
  AdmissionFailureCallback admission_failure_callback_ = nullptr;
  void* admission_failure_arg_ = nullptr;
};

}  // namespace engine
