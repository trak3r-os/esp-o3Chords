#include "engine/midi_scheduler.h"

#include <algorithm>

#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "MidiSched";

namespace engine {

void MidiScheduler::note_on(uint8_t, uint8_t channel, uint8_t note, uint8_t velocity) {
  if (router_) router_->note_on(channel, note, velocity);
}

void MidiScheduler::note_off(uint8_t, uint8_t channel, uint8_t note) {
  if (router_) router_->note_off(channel, note, 0);
}

bool MidiScheduler::start(midi::Router* router, uint8_t channel) {
  router_ = router;
  channel_ = channel;
  if (ledger_ == &owned_ledger_) owned_ledger_.set_sink(this);

  mutex_ = xSemaphoreCreateMutex();
  if (!mutex_) {
    ESP_LOGE(TAG, "Failed to create mutex");
    return false;
  }

  esp_timer_create_args_t args{};
  args.callback = timer_callback;
  args.arg = this;
  args.name = "midi_sched";
  args.dispatch_method = ESP_TIMER_TASK;  // Run in esp_timer task (priority 22)

  esp_err_t err = esp_timer_create(&args, &timer_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(err));
    return false;
  }

  ESP_LOGI(TAG, "Started (esp_timer, microsecond precision)");
  return true;
}

void MidiScheduler::stop() {
  if (timer_) {
    esp_timer_stop(timer_);
    esp_timer_delete(timer_);
    timer_ = nullptr;
    timer_running_ = false;
  }
  if (mutex_) {
    vSemaphoreDelete(mutex_);
    mutex_ = nullptr;
  }
}

void MidiScheduler::set_channel(uint8_t ch) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (ch != channel_) {
    silence_locked();  // Cancel this scheduler's source, preserving other owners.
  }
  channel_ = ch;
  xSemaphoreGive(mutex_);
}

bool MidiScheduler::schedule(const ScheduledEvent& event) {
  return schedule_batch(&event, 1);
}

bool MidiScheduler::schedule_batch(const ScheduledEvent* events, size_t count) {
  if (count == 0) return true;
  if (!events || !mutex_) return false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (count > kMaxQueue - queue_.size()) {
    // A rejected off may belong to an already sounding note. Fail closed instead
    // of partially accepting a phrase or silently losing its release.
    silence_locked();
    ESP_LOGW(TAG, "Event queue overflow: silenced scheduler");
    xSemaphoreGive(mutex_);
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    ScheduledEvent admitted = events[i];
    const bool source_default = admitted.source == 0;
    if (admitted.source == 0) admitted.source = source_id_;
    if (admitted.channel == 0 && source_default) admitted.channel = channel_;
    if (admitted.generation == 0) admitted.generation = ledger_->generation_for(admitted.source);
    admitted.route_mask = route_mask_;
    admitted.sequence = next_sequence_++;
    if (next_sequence_ == 0) next_sequence_ = 1;
    auto pos = std::upper_bound(queue_.begin(), queue_.end(), admitted,
        [](const ScheduledEvent& a, const ScheduledEvent& b) {
          return a.due_ms < b.due_ms ||
                 (a.due_ms == b.due_ms && a.sequence < b.sequence);
        });
    queue_.insert(pos, admitted);
  }
  arm_next();
  xSemaphoreGive(mutex_);
  return true;
}

void MidiScheduler::clear() {
  xSemaphoreTake(mutex_, portMAX_DELAY);

  if (timer_running_) {
    esp_timer_stop(timer_);
    timer_running_ = false;
  }
  // Do not discard events admitted by another producer sharing this
  // scheduler.  Their source and captured channel remain valid.
  for (auto it = queue_.begin(); it != queue_.end();) {
    if (it->source == source_id_) it = queue_.erase(it);
    else ++it;
  }
  if (!queue_.empty()) arm_next();

  xSemaphoreGive(mutex_);
}

void MidiScheduler::all_notes_off() {
  xSemaphoreTake(mutex_, portMAX_DELAY);

  silence_locked();
  xSemaphoreGive(mutex_);
}

void MidiScheduler::all_notes_off(app::contract::SourceId source) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  silence_source_locked(source);
  xSemaphoreGive(mutex_);
}

void MidiScheduler::invalidate_source(app::contract::SourceId source) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  silence_source_locked(source);
  ledger_->invalidate_source(source);
  xSemaphoreGive(mutex_);
}

void MidiScheduler::silence_locked() {
  silence_source_locked(source_id_);
}

void MidiScheduler::silence_source_locked(app::contract::SourceId source) {
  // Stop timer and remove only this source's queued work.
  if (timer_running_) {
    esp_timer_stop(timer_);
    timer_running_ = false;
  }
  for (auto it = queue_.begin(); it != queue_.end();) {
    if (it->source == source) it = queue_.erase(it);
    else ++it;
  }
  if (!queue_.empty()) arm_next();

  // Release only this scheduler's owner.  A ledger release emits a physical
  // Note Off only when no other source still owns the same voice.
  ledger_->release_source(source);
  for (auto it = tokens_.begin(); it != tokens_.end();) {
    if (it->source == source) it = tokens_.erase(it);
    else ++it;
  }
}

util::FixedVec<uint8_t, 32> MidiScheduler::active_notes() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  auto copy = ledger_->active_notes();
  xSemaphoreGive(mutex_);
  return copy;
}

// --- Private ---

void MidiScheduler::timer_callback(void* arg) {
  auto* self = static_cast<MidiScheduler*>(arg);
  xSemaphoreTake(self->mutex_, portMAX_DELAY);

  self->timer_running_ = false;
  self->dispatch_due();
  self->arm_next();

  xSemaphoreGive(self->mutex_);
}

void MidiScheduler::dispatch_due() {
  if (!router_ || queue_.empty()) return;

  const uint64_t now_ms = esp_timer_get_time() / 1000;

  // Dispatch all events whose time has come (queue is sorted ascending).
  while (!queue_.empty() && queue_.front().due_ms <= now_ms) {
    const auto& ev = queue_.front();

    // Generation is captured at admission.  A Panic or other barrier may
    // advance the ledger before this deadline; such work must be discarded,
    // never reinterpreted using the current generation.
    if (ev.generation != ledger_->generation_for(ev.source)) {
      queue_.erase(queue_.begin());
      continue;
    }

    // Queue admission is not enough: reserve the complete due phrase in the
    // shared ledger before any note-on is published.
    if (ev.type == EventType::kNoteOn && ev.velocity > 0) {
      ScheduledEvent phrase[32]{};
      app::contract::NoteIntent intents[32]{};
      app::contract::VoiceToken admitted[32]{};
      size_t count = 0;
      while (count < 32 && count < queue_.size() &&
             queue_[count].due_ms <= now_ms &&
             queue_[count].type == EventType::kNoteOn && queue_[count].velocity > 0) {
        phrase[count] = queue_[count];
        intents[count] = {phrase[count].source, phrase[count].generation,
                          phrase[count].channel, phrase[count].note,
                          phrase[count].velocity, phrase[count].route_mask};
        ++count;
      }
      const bool accepted = tokens_.size() + count <= kMaxActive &&
                            ledger_->acquire_batch(intents, count, admitted);
      if (!accepted) {
        const auto source = phrase[0].source;
        for (size_t i = 0; i < count; ++i) queue_.erase(queue_.begin());
        silence_source_locked(source);
        if (admission_failure_callback_) admission_failure_callback_(admission_failure_arg_, source);
        continue;
      }
      for (size_t i = 0; i < count; ++i) {
        tokens_.push_back({phrase[i].note, phrase[i].source, admitted[i]});
        queue_.erase(queue_.begin());
      }
      continue;
    }

    switch (ev.type) {
      case EventType::kNoteOn: {
        if (ev.velocity == 0) {
          for (auto it = tokens_.begin(); it != tokens_.end(); ++it) {
            if (it->note == ev.note && it->source == ev.source) {
              ledger_->release(it->token); tokens_.erase(it); break;
            }
          }
          break;
        }
        if (tokens_.full()) {
          silence_locked();
          ESP_LOGW(TAG, "Active note capacity exceeded: silenced scheduler");
          return;
        }
        app::contract::VoiceToken token{};
          if (ledger_->acquire({ev.source, ev.generation, ev.channel,
                              ev.note, ev.velocity, ev.route_mask}, token))
          tokens_.push_back({ev.note, ev.source, token});
        break;
      }

      case EventType::kNoteOff:
        for (auto it = tokens_.begin(); it != tokens_.end(); ++it) {
          if (it->note == ev.note && it->source == ev.source) {
            ledger_->release(it->token); tokens_.erase(it); break;
          }
        }
        break;

      case EventType::kPitchBend:
        router_->pitch_bend(channel_, ev.bend);
        break;
    }

    queue_.erase(queue_.begin());
  }
}

void MidiScheduler::arm_next() {
  if (queue_.empty()) return;

  const uint64_t now_us = esp_timer_get_time();
  const uint64_t now_ms = now_us / 1000;
  const uint64_t next_ms = queue_.front().due_ms;

  if (next_ms <= now_ms) {
    // Event already due — dispatch immediately then re-arm.
    dispatch_due();
    if (queue_.empty()) return;
    // Recalculate for the next remaining event.
    arm_next();
    return;
  }

  // Convert delta to microseconds for esp_timer.
  const uint64_t delta_us = (next_ms - now_ms) * 1000;

  // Always stop before re-arming. The callback clears timer_running_ only
  // once it gets the mutex, which can be after this task has already re-armed
  // the timer; trusting the flag then would leave the old deadline in place.
  // Stopping a timer that is not armed is harmless.
  esp_timer_stop(timer_);
  timer_running_ = false;

  esp_err_t err = esp_timer_start_once(timer_, delta_us);
  if (err == ESP_OK) {
    timer_running_ = true;
  } else {
    ESP_LOGE(TAG, "arm_next failed: %s", esp_err_to_name(err));
  }
}

}  // namespace engine
