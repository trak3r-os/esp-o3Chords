#include "engine/chords.h"

namespace engine {

void ChordLibrary::load_defaults() {
  chords_.clear();
  scales_.clear();

  chords_ = {
      {"maj", {0, 4, 7}},
      {"min", {0, 3, 7}},
      {"sus2", {0, 2, 7}},
      {"sus4", {0, 5, 7}},
      {"dim", {0, 3, 6}},
      {"aug", {0, 4, 8}},
      {"maj6", {0, 4, 7, 9}},
      {"min6", {0, 3, 7, 9}},
      {"maj7", {0, 4, 7, 11}},
      {"min7", {0, 3, 7, 10}},
      {"dom7", {0, 4, 7, 10}},
      {"half-dim7", {0, 3, 6, 10}},
      {"dim7", {0, 3, 6, 9}},
      {"add9", {0, 4, 7, 14}},
      {"min9", {0, 3, 7, 10, 14}},
      {"maj9", {0, 4, 7, 11, 14}},
      {"dom9", {0, 4, 7, 10, 14}},
      {"min11", {0, 3, 7, 10, 14, 17}},
      {"dom13", {0, 4, 7, 10, 14, 21}},
      {"maj13", {0, 4, 7, 11, 14, 21}},
  };

  scales_ = {
      {"Major", {0, 2, 4, 5, 7, 9, 11}},
      {"Natural Minor", {0, 2, 3, 5, 7, 8, 10}},
      {"Harmonic Minor", {0, 2, 3, 5, 7, 8, 11}},
      {"Melodic Minor", {0, 2, 3, 5, 7, 9, 11}},
      {"Dorian", {0, 2, 3, 5, 7, 9, 10}},
      {"Mixolydian", {0, 2, 4, 5, 7, 9, 10}},
      {"Lydian", {0, 2, 4, 6, 7, 9, 11}},
      {"Phrygian", {0, 1, 3, 5, 7, 8, 10}},
      {"Locrian", {0, 1, 3, 5, 6, 8, 10}},
  };
}

}  // namespace engine
