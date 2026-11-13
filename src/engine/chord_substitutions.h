#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace app { struct Settings; }

namespace engine {

class Engine;

struct SubstitutionResult {
  std::string root_name;      // e.g. "Db"
  std::string quality;        // e.g. "dom7"
  std::vector<uint8_t> notes; // MIDI notes to play
};

// Labels for the 8 joystick directions in substitute mode.
// Order: E(0), NE(1), N(2), NW(3), W(4), SW(5), S(6), SE(7)
inline constexpr const char* kSubLabels[8] = {
    "Tri",   // E:  Tritone substitution
    "V/V",   // NE: Secondary dominant
    "Rel",   // N:  Relative minor/major
    "Brw",   // NW: Borrowed chord
    "Dim",   // W:  Diminished passing
    "Aug6",  // SW: Augmented 6th
    "Ped",   // S:  Pedal (same root, quality cycle)
    "ChrM",  // SE: Chromatic mediant
};

// Compute a chord substitution for the given degree and joystick direction.
// Returns MIDI notes ready to play, plus display info.
//
// Dead code: nothing in the firmware calls it. Left in place deliberately
// rather than removed.
SubstitutionResult compute_substitution(const app::Settings& s,
                                         Engine& engine,
                                         uint8_t degree,
                                         uint8_t direction);

}  // namespace engine
