#pragma once

#include "engine/performer.h"
#include <cstdint>
#include <vector>

namespace engine {

enum class ArpPattern {
  kUp,          // C E G
  kDown,        // G E C
  kUpDown,      // C E G E
  kDownUp,      // G E C E
  kUpDownInc,   // C E G G E C (includes top note twice)
  kRandom,      // Random order
  kOrder        // As played (original order)
};

enum class ArpRate {
  kQuarter,     // 1/4 notes
  kEighth,      // 1/8 notes
  kSixteenth,   // 1/16 notes
  kTriplet      // 1/8 triplets
};

class Arpeggiator {
 public:
  void set_enabled(bool enabled) { enabled_ = enabled; }
  void set_pattern(ArpPattern pattern) { pattern_ = pattern; }
  void set_rate(ArpRate rate) { rate_ = rate; }
  void set_gate_percent(uint8_t percent);  // 1-100, note length
  void set_octaves(uint8_t octaves);       // 1-4, octave range

  bool enabled() const { return enabled_; }
  ArpPattern pattern() const { return pattern_; }
  ArpRate rate() const { return rate_; }
  uint8_t gate_percent() const { return gate_percent_; }
  uint8_t octaves() const { return octaves_; }

  // Given a chord, BPM, and current time, generate arp note events
  std::vector<NoteEvent> render_arp(
    const std::vector<uint8_t>& chord_notes,
    float bpm,
    uint64_t step_start_ms,
    uint32_t step_duration_ms,
    uint8_t velocity,
    uint8_t channel
  ) const;

 private:
  bool enabled_ = false;
  ArpPattern pattern_ = ArpPattern::kUp;
  ArpRate rate_ = ArpRate::kEighth;
  uint8_t gate_percent_ = 80;  // 80% gate
  uint8_t octaves_ = 1;         // 1-4 octaves

  // Generate arpeggio sequence based on pattern
  std::vector<uint8_t> generate_arp_sequence(
    const std::vector<uint8_t>& base_notes
  ) const;

  // Calculate note interval in milliseconds based on rate and BPM
  uint32_t get_note_interval_ms(float bpm) const;
};

}  // namespace engine
