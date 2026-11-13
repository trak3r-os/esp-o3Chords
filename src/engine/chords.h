#pragma once

#include <string>
#include <vector>

namespace engine {

struct Chord {
  std::string name;
  std::vector<int8_t> intervals;  // semitone offsets from root
};

struct Scale {
  std::string name;
  std::vector<int8_t> steps;
};

class ChordLibrary {
 public:
  void load_defaults();
  const std::vector<Chord>& chords() const { return chords_; }
  const std::vector<Scale>& scales() const { return scales_; }

 private:
  std::vector<Chord> chords_;
  std::vector<Scale> scales_;
};

}  // namespace engine
