#include "engine/performer.h"

#include <algorithm>
#include <cstdlib>

#include "esp_system.h"
#include "esp_random.h"

namespace engine {
namespace {

// Per-voice roll offset. `pivot` is the voice that fires first for kOut and
// last for kIn: index n/2 (integer division) — the exact middle for an odd
// voice count, the upper of the two middle voices for an even one.
int32_t roll_offset_ms(size_t i, size_t n, RollDirection dir, uint8_t roll_ms) {
  if (n == 0 || roll_ms == 0) return 0;
  switch (dir) {
    case RollDirection::kUp:   return static_cast<int32_t>(i) * roll_ms;
    case RollDirection::kDown: return static_cast<int32_t>(n - 1 - i) * roll_ms;
    case RollDirection::kOut: {
      const int pivot = static_cast<int>(n / 2);
      return std::abs(static_cast<int>(i) - pivot) * static_cast<int32_t>(roll_ms);
    }
    case RollDirection::kIn: {
      const int pivot = static_cast<int>(n / 2);
      const int max_dist = std::max(pivot, static_cast<int>(n - 1) - pivot);
      const int dist = std::abs(static_cast<int>(i) - pivot);
      return (max_dist - dist) * static_cast<int32_t>(roll_ms);
    }
  }
  return 0;
}

}  // namespace

void Performer::set_strum_speed(float speed) {
  // Clamp to reasonable range (0.5x to 2.0x)
  strum_speed_ = std::max(0.5f, std::min(2.0f, speed));
}

std::vector<NoteEvent> Performer::render_step(const std::vector<uint8_t>& notes, uint8_t velocity, uint8_t channel) const {
  std::vector<NoteEvent> events;

  if (notes.empty()) {
    return events;
  }

  events.reserve(notes.size());

  // Create a copy to potentially reorder
  std::vector<uint8_t> ordered_notes = notes;

  // Roll ("stagger") and strum are mutually exclusive: roll owns the timing
  // for every voice, so strum's own spread/direction/alternation is skipped
  // entirely rather than compounding with it on the same voice.
  const bool rolling = roll_ms_ > 0;

  // Apply strum direction
  StrumDirection current_direction = strum_direction_;

  if (!rolling) {
    // Handle alternating strum
    if (strum_direction_ == StrumDirection::kBoth) {
      current_direction = strum_alternate_state_ ? StrumDirection::kDown : StrumDirection::kUp;
      strum_alternate_state_ = !strum_alternate_state_;
    }

    // Reverse for down strum
    if (current_direction == StrumDirection::kDown) {
      std::reverse(ordered_notes.begin(), ordered_notes.end());
    }
  }

  // Calculate effective strum timing with speed multiplier
  uint16_t effective_strum_ms = static_cast<uint16_t>(strum_ms_ / strum_speed_);

  // Generate note events
  for (size_t i = 0; i < ordered_notes.size(); ++i) {
    int32_t offset = rolling
        ? roll_offset_ms(i, ordered_notes.size(), roll_direction_, roll_ms_)
        : static_cast<int32_t>(i * effective_strum_ms);

    // Apply swing: delay even-indexed notes. Scaled off whichever spread is
    // actually active, so swing survives a switch from strum to roll.
    const uint16_t swing_unit = rolling ? roll_ms_ : effective_strum_ms;
    if (swing_percent_ > 0 && (i % 2) == 1) {
      offset += static_cast<int32_t>(swing_unit * swing_percent_ / 100);
    }

    // Apply humanization (timing jitter)
    if (humanize_ms_ > 0) {
      int32_t jitter = static_cast<int32_t>(esp_random() % (humanize_ms_ + 1));
      jitter -= humanize_ms_ / 2;
      offset += jitter;
    }

    offset = std::max<int32_t>(0, offset);

    // Compute velocity with randomization and accents
    int32_t vel = static_cast<int32_t>(velocity);

    if (velocity_random_ > 0) {
      int32_t dev = static_cast<int32_t>(esp_random() % (velocity_random_ * 2 + 1));
      dev -= velocity_random_;
      vel += dev;
    }

    // Accent patterns: boost accented notes by +15
    if (accent_pattern_ > 0) {
      bool accented = false;
      switch (accent_pattern_) {
        case 1: accented = (i == 0); break;                    // Downbeat
        case 2: accented = (i == 1 || i == 3); break;         // Backbeat
        case 3: accented = (i % 4 == 0); break;               // Every 4th
      }
      if (accented) vel += 15;
    }

    vel = std::clamp(vel, static_cast<int32_t>(1), static_cast<int32_t>(127));

    NoteEvent ev;
    ev.note = ordered_notes[i];
    ev.velocity = static_cast<uint8_t>(vel);
    ev.channel = channel;
    ev.offset_ms = static_cast<uint32_t>(offset);
    events.push_back(ev);
  }

  return events;
}

}  // namespace engine
