#pragma once

#include "engine/rhythm_pattern.h"

#include <cstdint>
#include <vector>

namespace engine {

struct NoteEvent {
  uint8_t note = 60;
  uint8_t velocity = 100;
  uint8_t channel = 0;
  uint32_t offset_ms = 0;  // relative to step start
};

enum class StrumDirection {
  kUp,      // Low to high
  kDown,    // High to low
  kBoth     // Alternate (up then down)
};

// Roll ("stagger"): a per-voice timing offset, distinct from strum. Roll and
// strum are mutually exclusive — when roll_ms() > 0 it replaces strum's
// spread entirely rather than compounding with it (see render_step()).
enum class RollDirection : uint8_t {
  kUp,    // Lowest voice first, ascending pitch order.
  kDown,  // Highest voice first, descending pitch order.
  kOut,   // The pivot voice first, spreading outward to both edges.
  kIn,    // The two edge voices first, converging inward on the pivot.
};

class Performer {
 public:
  void set_humanize_ms(uint16_t value) { humanize_ms_ = value; }
  void set_swing_percent(uint8_t value) { swing_percent_ = value; }
  void set_strum_ms(uint16_t value) { strum_ms_ = value; }
  void set_note_length_ms(uint16_t value) { note_length_ms_ = value; }
  void set_strum_direction(StrumDirection dir) { strum_direction_ = dir; }
  void set_strum_speed(float speed);  // 0.5 = slower, 2.0 = faster
  void set_strum_rhythm(StrumRhythm rhythm) { strum_rhythm_ = rhythm; }
  void set_velocity_random(uint8_t range) { velocity_random_ = range; }
  void set_accent_pattern(uint8_t pattern) { accent_pattern_ = pattern; }
  void set_roll_ms(uint8_t value) { roll_ms_ = std::min<uint8_t>(value, 60); }
  void set_roll_direction(RollDirection dir) { roll_direction_ = dir; }

  uint16_t humanize_ms() const { return humanize_ms_; }
  uint8_t swing_percent() const { return swing_percent_; }
  uint16_t strum_ms() const { return strum_ms_; }
  uint16_t note_length_ms() const { return note_length_ms_; }
  StrumDirection strum_direction() const { return strum_direction_; }
  float strum_speed() const { return strum_speed_; }
  StrumRhythm strum_rhythm() const { return strum_rhythm_; }
  uint8_t velocity_random() const { return velocity_random_; }
  uint8_t accent_pattern() const { return accent_pattern_; }
  uint8_t roll_ms() const { return roll_ms_; }
  RollDirection roll_direction() const { return roll_direction_; }

  std::vector<NoteEvent> render_step(const std::vector<uint8_t>& notes, uint8_t velocity, uint8_t channel) const;

 private:
  uint16_t humanize_ms_ = 0;
  uint8_t swing_percent_ = 0;
  uint16_t strum_ms_ = 0;
  uint16_t note_length_ms_ = 240;
  StrumDirection strum_direction_ = StrumDirection::kUp;
  float strum_speed_ = 1.0f;
  StrumRhythm strum_rhythm_ = StrumRhythm::kNone;
  uint8_t velocity_random_ = 0;    // 0-30: random velocity deviation range
  uint8_t accent_pattern_ = 0;     // 0=off, 1=downbeat, 2=backbeat, 3=every4th
  uint8_t roll_ms_ = 0;            // 0-60ms per-voice offset; 0 = off
  RollDirection roll_direction_ = RollDirection::kUp;
  mutable bool strum_alternate_state_ = false;  // For kBoth mode
};

}  // namespace engine
