#include "app/harmony_transform.h"

namespace app::harmony {
namespace {
constexpr const char* kDiatonic[4][3] = {
    {"add9", "maj7", "maj9"}, {"sus2", "sus4", "aug"},
    {"min7", "dim7", "dim"}, {"dom7", "dom9", "half-dim7"}};
constexpr const char* kAltered[4][3] = {
    {"dom7", "dom9", "dom13"}, {"min7", "min9", "min11"},
    {"dim", "dim7", "half-dim7"}, {"maj7", "aug", "maj9"}};
constexpr const char* kScales[8] = {
    "Ionian", "Dorian", "Phrygian", "Lydian",
    "Mixolydian", "Aeolian", "Locrian", "Harmonic Minor"};

uint8_t zone_for(float distance) { return distance < 0.5f ? 0 : distance < 0.8f ? 1 : 2; }
uint8_t quadrant_for(float x, float y) {
  return static_cast<uint8_t>((x >= 0.0f ? 0 : 2) + (y >= 0.0f ? 0 : 1));
}
}  // namespace

const char* quality_for_xy(TensionMap map, float x, float y, float distance) {
  if (distance < 0.2f) return nullptr;
  return (map == TensionMap::Diatonic ? kDiatonic : kAltered)
      [quadrant_for(x, y)][zone_for(distance)];
}

uint8_t tension_slot_for_xy(float x, float y, float distance) {
  if (distance < 0.2f) return 0;
  return static_cast<uint8_t>(1 + quadrant_for(x, y) * 3 + zone_for(distance));
}

uint8_t voicing_for_direction(int8_t direction) {
  // The one place the joystick's eight-way voicing direction map is stated:
  // each of the eight compass directions names a distinct engine::VoicingSpread
  // style (see engine/modifier.h). Direction indices follow this codebase's
  // usual 8-way convention: 0=E,1=NE,2=N,3=NW,4=W,5=SW,6=S,7=SE.
  //   N=Default  NE=Close  E=Drop2  SE=Drop3
  //   S=Open     SW=Shell  W=Rootless  NW=Guitar
  // Invalid input falls back to Default (index 1) — "no direction" means "no
  // change", not a dedicated reset slot.
  constexpr uint8_t kByClock[8] = {2, 0, 1, 7, 6, 5, 4, 3};
  if (direction < 0 || direction > 7) return 1;
  return kByClock[direction];
}

const char* scale_for_direction(uint8_t direction) {
  return kScales[direction < 8 ? direction : 0];
}
}  // namespace app::harmony
