#pragma once

#include <cstdint>

namespace engine {

class Timing {
 public:
  void set_bpm(float bpm) { bpm_ = bpm; }
  float bpm() const { return bpm_; }
  void set_swing(float swing) { swing_ = swing; }
  float swing() const { return swing_; }

 private:
  float bpm_ = 120.0f;
  float swing_ = 0.0f;
};

}  // namespace engine
