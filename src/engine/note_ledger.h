#pragma once

#include "app/contracts/contracts.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include "util/fixed_vec.h"

namespace engine {

class NoteLedgerSink {
 public:
  virtual ~NoteLedgerSink() = default;
  virtual void note_on(uint8_t route, uint8_t channel, uint8_t note, uint8_t velocity) = 0;
  virtual void note_off(uint8_t route, uint8_t channel, uint8_t note) = 0;
};

// The ledger owns logical note lifetime.  It intentionally has fixed storage:
// failure to reserve is visible to the producer and cannot silently drop Off.
//
// One ledger is shared by the scheduler (esp_timer task) and by the joystick's
// direct notes (main loop), so every public method takes the ledger's own
// lock. It is recursive because the batch/source operations are built from
// the single-note ones.
class NoteLedger {
 public:
  static constexpr uint8_t kMaxVoices = 32;
  static constexpr uint8_t kMaxTokens = 64;

  explicit NoteLedger(NoteLedgerSink* sink = nullptr) : sink_(sink) {}
  void set_sink(NoteLedgerSink* sink) { sink_ = sink; }

  bool acquire(const app::contract::NoteIntent& intent, app::contract::VoiceToken& token);
  bool acquire_batch(const app::contract::NoteIntent* intents, size_t count,
                     app::contract::VoiceToken* tokens);
  bool release(app::contract::VoiceToken token);
  uint8_t release_source(app::contract::SourceId source);
  void invalidate_source(app::contract::SourceId source);
  void panic();
  uint8_t active_voices() const { std::lock_guard<std::recursive_mutex> lock(mutex_); return active_voices_; }
  uint8_t active_tokens() const { std::lock_guard<std::recursive_mutex> lock(mutex_); return active_tokens_; }
  util::FixedVec<uint8_t, kMaxVoices> active_notes() const;
  app::contract::Generation generation() const { std::lock_guard<std::recursive_mutex> lock(mutex_); return generation_; }
  app::contract::Generation generation_for(app::contract::SourceId source) const;

 private:
  struct TokenSlot { bool used = false; app::contract::VoiceToken token{}; app::contract::NoteIntent intent{}; };
  struct Voice { bool used = false; uint8_t route = 0; uint8_t channel = 0; uint8_t note = 0; uint8_t owners = 0; };
  struct SourceGeneration { bool used = false; app::contract::SourceId source = 0; app::contract::Generation generation = 1; };
  int find_token(app::contract::VoiceToken token) const;
  int find_voice(uint8_t route, uint8_t channel, uint8_t note) const;
  void emit_off(const Voice& voice);

  mutable std::recursive_mutex mutex_;
  NoteLedgerSink* sink_ = nullptr;
  std::array<TokenSlot, kMaxTokens> tokens_{};
  std::array<Voice, kMaxVoices> voices_{};
  std::array<SourceGeneration, 32> source_generations_{};
  app::contract::Generation generation_ = 1;
  uint16_t next_token_ = 1;
  uint8_t active_tokens_ = 0;
  uint8_t active_voices_ = 0;
};

}  // namespace engine
