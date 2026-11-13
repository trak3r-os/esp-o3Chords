#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

struct ProgressionStep {
  uint8_t scale_degree = 1;   // 1 = tonic
  int8_t octave = 0;          // octave offset relative to root (C4 base)
  uint8_t velocity = 100;
  bool bend_into_next = false;
  std::string chord_quality = "maj";  // maj, min, maj7, min7, dom7, sus2, sus4, dim, aug, etc.
  uint8_t inversion = 0;               // 0 = root, 1 = first inversion, etc.
  int8_t transpose = 0;                // semitone transpose for this step
};

class Progression {
 public:
  void set_steps(const std::vector<ProgressionStep>& steps);
  const std::vector<ProgressionStep>& steps() const;
  std::vector<ProgressionStep>& steps_mutable() { return steps_; }
  void clear();

 private:
  std::vector<ProgressionStep> steps_;
};

}  // namespace engine
