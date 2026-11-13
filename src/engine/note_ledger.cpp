#include "engine/note_ledger.h"

namespace engine {

int NoteLedger::find_token(app::contract::VoiceToken token) const {
  for (uint8_t i = 0; i < kMaxTokens; ++i)
    if (tokens_[i].used && tokens_[i].token.value == token.value && tokens_[i].token.generation == token.generation) return i;
  return -1;
}

int NoteLedger::find_voice(uint8_t route, uint8_t channel, uint8_t note) const {
  for (uint8_t i = 0; i < kMaxVoices; ++i)
    if (voices_[i].used && voices_[i].route == route && voices_[i].channel == channel && voices_[i].note == note) return i;
  return -1;
}

app::contract::Generation NoteLedger::generation_for(app::contract::SourceId source) const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  for (const auto& entry : source_generations_)
    if (entry.used && entry.source == source) return entry.generation;
  return generation_;
}

void NoteLedger::invalidate_source(app::contract::SourceId source) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  release_source(source);
  for (auto& entry : source_generations_) {
    if (entry.used && entry.source == source) {
      ++entry.generation;
      if (entry.generation == 0) entry.generation = 1;
      return;
    }
  }
  for (auto& entry : source_generations_) {
    if (!entry.used) {
      entry = {true, source, generation_ + 1};
      if (entry.generation == 0) entry.generation = 1;
      return;
    }
  }
}

bool NoteLedger::acquire(const app::contract::NoteIntent& intent, app::contract::VoiceToken& token) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  int token_slot = -1;
  for (uint8_t i = 0; i < kMaxTokens; ++i) if (!tokens_[i].used) { token_slot = i; break; }
  if (token_slot < 0 || intent.route_mask == 0) return false;
  uint8_t new_voices = 0;
  for (uint8_t route = 0; route < 8; ++route) if (intent.route_mask & (1u << route))
    if (find_voice(route, intent.channel, intent.note) < 0) ++new_voices;
  if (active_voices_ + new_voices > kMaxVoices) return false;

  // Ids wrap after 65535 acquisitions; skip zero (the "no token" value) and
  // any id a long-held note still owns, or its release would hit the wrong one.
  const auto id_in_use = [this](uint16_t value) {
    for (const auto& slot : tokens_) if (slot.used && slot.token.value == value) return true;
    return false;
  };
  do { token.value = next_token_++; } while (token.value == 0 || id_in_use(token.value));
  // A producer may only submit the current generation.  Zero means “current”
  // for callers that do not need explicit cancellation generations.
  const auto source_generation = generation_for(intent.source);
  if (intent.generation != 0 && intent.generation != source_generation) return false;
  token.generation = source_generation;
  tokens_[token_slot] = {true, token, intent};
  ++active_tokens_;
  for (uint8_t route = 0; route < 8; ++route) if (intent.route_mask & (1u << route)) {
    int v = find_voice(route, intent.channel, intent.note);
    if (v >= 0) { ++voices_[v].owners; continue; }
    for (uint8_t i = 0; i < kMaxVoices; ++i) if (!voices_[i].used) {
      voices_[i] = {true, route, intent.channel, intent.note, 1}; ++active_voices_;
      if (sink_) sink_->note_on(route, intent.channel, intent.note, intent.velocity);
      break;
    }
  }
  return true;
}

bool NoteLedger::acquire_batch(const app::contract::NoteIntent* intents, size_t count,
                               app::contract::VoiceToken* tokens) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (count == 0) return true;
  if (!intents || !tokens || count > static_cast<size_t>(kMaxTokens - active_tokens_)) return false;

  struct Key { uint8_t route, channel, note; } new_keys[kMaxVoices]{};
  size_t new_count = 0;
  for (size_t i = 0; i < count; ++i) {
    const auto& intent = intents[i];
    if (intent.route_mask == 0 ||
        (intent.generation != 0 && intent.generation != generation_for(intent.source))) return false;
    for (uint8_t route = 0; route < 8; ++route) if (intent.route_mask & (1u << route)) {
      if (find_voice(route, intent.channel, intent.note) >= 0) continue;
      bool known = false;
      for (size_t j = 0; j < new_count; ++j)
        if (new_keys[j].route == route && new_keys[j].channel == intent.channel &&
            new_keys[j].note == intent.note) { known = true; break; }
      if (!known) {
        if (new_count == kMaxVoices) return false;
        new_keys[new_count++] = {route, intent.channel, intent.note};
      }
    }
  }
  if (active_voices_ + new_count > kMaxVoices) return false;

  size_t acquired = 0;
  for (; acquired < count; ++acquired) {
    if (!acquire(intents[acquired], tokens[acquired])) {
      while (acquired > 0) release(tokens[--acquired]);
      return false;
    }
  }
  return true;
}

util::FixedVec<uint8_t, NoteLedger::kMaxVoices> NoteLedger::active_notes() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  util::FixedVec<uint8_t, kMaxVoices> result;
  for (const auto& voice : voices_) {
    if (voice.used) result.push_back(voice.note);
  }
  return result;
}

void NoteLedger::emit_off(const Voice& voice) {
  if (sink_) sink_->note_off(voice.route, voice.channel, voice.note);
}

bool NoteLedger::release(app::contract::VoiceToken token) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  int t = find_token(token); if (t < 0) return false;
  const auto intent = tokens_[t].intent;
  tokens_[t].used = false; --active_tokens_;
  for (uint8_t route = 0; route < 8; ++route) if (intent.route_mask & (1u << route)) {
    int v = find_voice(route, intent.channel, intent.note); if (v < 0) continue;
    if (--voices_[v].owners == 0) { emit_off(voices_[v]); voices_[v].used = false; --active_voices_; }
  }
  return true;
}

uint8_t NoteLedger::release_source(app::contract::SourceId source) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  app::contract::VoiceToken doomed[kMaxTokens]{}; uint8_t count = 0;
  for (const auto& slot : tokens_) if (slot.used && slot.intent.source == source) doomed[count++] = slot.token;
  for (uint8_t i = 0; i < count; ++i) release(doomed[i]);
  return count;
}

void NoteLedger::panic() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  for (const auto& voice : voices_) if (voice.used) emit_off(voice);
  for (auto& token : tokens_) token.used = false;
  for (auto& voice : voices_) voice.used = false;
  active_tokens_ = active_voices_ = 0;
  ++generation_; if (generation_ == 0) generation_ = 1;
  for (auto& entry : source_generations_)
    if (entry.used) {
      ++entry.generation;
      if (entry.generation == 0) entry.generation = 1;
    }
}

}  // namespace engine
