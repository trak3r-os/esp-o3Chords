#include "engine/pitch_bend.h"

namespace engine {

int16_t bend_from_x(int16_t x, uint16_t deadzone) {
  const int32_t ax = (x < 0) ? -x : x;
  if (ax <= (int32_t)deadzone) return 0;
  int32_t v = (int32_t)x * 4;
  if (v < -8192) v = -8192;
  if (v > 8191) v = 8191;
  return (int16_t)v;
}

int16_t quantize_bend_to_chord(int16_t raw_bend,
                                const std::vector<int8_t>& intervals,
                                int bend_range,
                                float* out_snap_semitones) {
  if (out_snap_semitones) *out_snap_semitones = 0.0f;
  if (intervals.empty() || bend_range <= 0) return 0;
  if (raw_bend == 0) return 0;

  const float target = static_cast<float>(raw_bend) * bend_range / 8192.0f;

  float best = 0.0f;
  float best_dist = 1000.0f;
  for (int oct = -3; oct <= 3; ++oct) {
    for (int8_t iv : intervals) {
      const float tone = static_cast<float>(iv + oct * 12);
      if (tone < -static_cast<float>(bend_range) ||
          tone >  static_cast<float>(bend_range))
        continue;
      const float d = (target > tone) ? (target - tone) : (tone - target);
      if (d < best_dist) {
        best_dist = d;
        best = tone;
      }
    }
  }

  if (out_snap_semitones) *out_snap_semitones = best;

  int32_t bend = static_cast<int32_t>(best * 8192.0f / bend_range);
  if (bend < -8192) bend = -8192;
  if (bend > 8191) bend = 8191;
  return static_cast<int16_t>(bend);
}


}  // namespace engine
