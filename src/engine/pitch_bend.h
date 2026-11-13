#pragma once

#include <cstdint>
#include <vector>

namespace engine {

int16_t bend_from_x(int16_t x, uint16_t deadzone);

// Quantize a raw pitch bend so sounding notes land on the nearest chord tone.
// |intervals|: semitone offsets from root (e.g. {0,4,7} for major).
// Extends across octaves within +/-bend_range and snaps to the closest one.
int16_t quantize_bend_to_chord(int16_t raw_bend,
                                const std::vector<int8_t>& intervals,
                                int bend_range,
                                float* out_snap_semitones = nullptr);


}  // namespace engine
