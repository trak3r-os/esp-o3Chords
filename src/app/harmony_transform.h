#pragma once

#include <cstdint>

namespace app::harmony {

enum class TensionMap : uint8_t { Diatonic, Altered };
const char* quality_for_xy(TensionMap map, float x, float y, float distance);
uint8_t tension_slot_for_xy(float x, float y, float distance);
uint8_t voicing_for_direction(int8_t direction);
const char* scale_for_direction(uint8_t direction);

}  // namespace app::harmony
