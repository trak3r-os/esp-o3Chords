#include "app/progression_modes.h"
#include "engine/music_theory.h"

#include <cstdio>
#include <vector>

namespace app {

namespace {
// A performance keymap needs distinct destinations even when a genre's
// progression repeats a root.  ChordModifier can replace every step's quality
// with one selected CHORD/Color, so changing only the quality of a repeated
// root is not enough: two differently-labelled pads then produce the exact
// same chord. Preserve the first occurrence and move later occurrences to an
// unused scale root while retaining the genre's chord quality.
void make_chords_distinct(std::vector<engine::ProgressionStep>& steps, const Settings& s) {
  const auto root = [&](const auto& step) {
    return engine::progression_step_root_offset(s, step);
  };
  for (size_t i = 0; i < steps.size(); ++i) {
    bool repeated = false;
    for (size_t j = 0; j < i; ++j) repeated |= root(steps[i]) == root(steps[j]);
    if (!repeated) continue;
    for (uint8_t degree = 1; degree <= 7; ++degree) {
      auto candidate = steps[i];
      candidate.scale_degree = degree;
      bool occupied = false;
      for (size_t j = 0; j < i; ++j) occupied |= root(candidate) == root(steps[j]);
      if (occupied) continue;
      steps[i] = candidate;
      break;
    }
  }
}
}  // namespace

engine::ProgressionStep gstep(uint8_t deg, const char* qual, uint8_t vel, int8_t tp) {
  return engine::ProgressionStep{deg, 0, vel, false, qual, 0, tp};
}

void apply_progression_mode(engine::Engine& engine, ProgressionMode mode, Settings& s) {
  std::snprintf(s.scale, sizeof(s.scale), "%s", progression_mode_engine_scale(mode));

  const uint8_t vel = s.velocity;
  std::vector<engine::ProgressionStep> steps;
  steps.reserve(7);

  switch (mode) {
    case ProgressionMode::kMajor:
    case ProgressionMode::kNaturalMinor:
    case ProgressionMode::kHarmonicMinor:
    case ProgressionMode::kDorian:
    case ProgressionMode::kMixolydian:
    case ProgressionMode::kSus2:
    case ProgressionMode::kSevenths: {
      auto q = [&](int degree) -> const char* {
        switch (mode) {
          case ProgressionMode::kMajor:
          default: {
            if (degree == 1 || degree == 4 || degree == 5) return "maj";
            if (degree == 2 || degree == 3 || degree == 6) return "min";
            if (degree == 7) return "dim";
            return "maj";
          }
          case ProgressionMode::kNaturalMinor: {
            if (degree == 1) return "min";
            if (degree == 2) return "dim";
            if (degree == 3) return "maj";
            if (degree == 4) return "min";
            if (degree == 5) return "min";
            if (degree == 6) return "maj";
            if (degree == 7) return "maj";
            return "min";
          }
          case ProgressionMode::kHarmonicMinor: {
            if (degree == 1) return "min";
            if (degree == 2) return "dim";
            if (degree == 3) return "aug";
            if (degree == 4) return "min";
            if (degree == 5) return "maj";
            if (degree == 6) return "maj";
            if (degree == 7) return "dim";
            return "min";
          }
          case ProgressionMode::kDorian: {
            if (degree == 1) return "min";
            if (degree == 2) return "min";
            if (degree == 3) return "maj";
            if (degree == 4) return "maj";
            if (degree == 5) return "min";
            if (degree == 6) return "dim";
            if (degree == 7) return "maj";
            return "min";
          }
          case ProgressionMode::kMixolydian: {
            if (degree == 1) return "maj";
            if (degree == 2) return "min";
            if (degree == 3) return "dim";
            if (degree == 4) return "maj";
            if (degree == 5) return "min";
            if (degree == 6) return "min";
            if (degree == 7) return "maj";
            return "maj";
          }
          case ProgressionMode::kSus2: {
            (void)degree; return "sus2";
          }
          case ProgressionMode::kSevenths: {
            if (degree == 1) return "maj7";
            if (degree == 2) return "min7";
            if (degree == 3) return "min7";
            if (degree == 4) return "maj7";
            if (degree == 5) return "dom7";
            if (degree == 6) return "min7";
            if (degree == 7) return "half-dim7";
            return "maj7";
          }
        }
      };
      for (int d = 1; d <= 7; ++d) {
        steps.push_back(gstep(static_cast<uint8_t>(d), q(d), vel));
      }
      break;
    }

    case ProgressionMode::kSalsa:
      steps = {gstep(1,"maj7",vel), gstep(4,"dom7",vel), gstep(5,"dom7",vel),
               gstep(6,"min7",vel), gstep(2,"min7",vel), gstep(5,"dom7",vel),
               gstep(1,"maj7",vel)};
      break;
    case ProgressionMode::kBossaNova:
      steps = {gstep(1,"maj7",vel), gstep(2,"min7",vel), gstep(5,"dom7",vel),
               gstep(1,"maj7",vel), gstep(4,"maj7",vel), gstep(4,"min7",vel),
               gstep(1,"maj7",vel)};
      break;
    case ProgressionMode::kBachata:
      steps = {gstep(1,"maj",vel), gstep(6,"min",vel), gstep(2,"min",vel),
               gstep(5,"dom7",vel), gstep(1,"maj",vel), gstep(4,"maj",vel),
               gstep(5,"dom7",vel)};
      break;
    case ProgressionMode::kReggaeton:
      steps = {gstep(6,"min",vel), gstep(4,"maj",vel), gstep(1,"maj",vel),
               gstep(5,"maj",vel), gstep(6,"min",vel), gstep(4,"maj",vel),
               gstep(5,"dom7",vel)};
      break;
    case ProgressionMode::kJazz:
      steps = {gstep(2,"min7",vel), gstep(5,"dom7",vel), gstep(1,"maj7",vel),
               gstep(6,"min7",vel), gstep(2,"min7",vel), gstep(5,"dom7",vel),
               gstep(1,"maj9",vel)};
      break;
    case ProgressionMode::kNeoSoul:
      steps = {gstep(1,"maj9",vel), gstep(4,"maj7",vel), gstep(2,"min9",vel),
               gstep(5,"dom7",vel), gstep(3,"min7",vel), gstep(6,"min7",vel),
               gstep(2,"min7",vel)};
      break;
    case ProgressionMode::kGospel:
      steps = {gstep(1,"maj7",vel), gstep(1,"dom7",vel), gstep(4,"maj7",vel),
               gstep(4,"min7",vel), gstep(1,"maj7",vel), gstep(2,"min7",vel),
               gstep(5,"dom7",vel)};
      break;
    case ProgressionMode::kLofi:
      steps = {gstep(2,"min9",vel), gstep(5,"dom7",vel), gstep(1,"maj7",vel),
               gstep(6,"min7",vel), gstep(2,"min9",vel), gstep(3,"min7",vel),
               gstep(4,"maj7",vel)};
      break;
    case ProgressionMode::kBlues:
      steps = {gstep(1,"dom7",vel), gstep(1,"dom7",vel), gstep(4,"dom7",vel),
               gstep(1,"dom7",vel), gstep(5,"dom7",vel), gstep(4,"dom7",vel),
               gstep(1,"dom7",vel)};
      break;
    case ProgressionMode::kFunk:
      steps = {gstep(1,"dom7",vel), gstep(4,"dom7",vel), gstep(1,"dom7",vel),
               gstep(7,"dom7",vel,-1), gstep(4,"dom7",vel), gstep(5,"dom7",vel),
               gstep(1,"dom7",vel)};
      break;
    case ProgressionMode::kRnB:
      steps = {gstep(1,"maj7",vel), gstep(3,"min7",vel), gstep(6,"min7",vel),
               gstep(4,"maj7",vel), gstep(2,"min9",vel), gstep(5,"dom7",vel),
               gstep(1,"maj7",vel)};
      break;
    case ProgressionMode::kPop:
      steps = {gstep(1,"maj",vel), gstep(5,"maj",vel), gstep(6,"min",vel),
               gstep(4,"maj",vel), gstep(1,"maj",vel), gstep(4,"maj",vel),
               gstep(5,"maj",vel)};
      break;
    case ProgressionMode::kTrap:
      steps = {gstep(1,"min",vel),   gstep(6,"maj",vel,-1), gstep(7,"maj",vel,-1),
               gstep(1,"min",vel),   gstep(4,"min",vel),    gstep(6,"maj",vel,-1),
               gstep(7,"maj",vel,-1)};
      break;
    case ProgressionMode::kMotown:
      steps = {gstep(1,"maj",vel), gstep(4,"maj",vel), gstep(6,"min",vel),
               gstep(5,"maj",vel), gstep(1,"maj",vel), gstep(2,"min7",vel),
               gstep(5,"dom7",vel)};
      break;
    case ProgressionMode::kFlamenco:
      steps = {gstep(1,"min",vel),    gstep(7,"maj",vel,-1), gstep(6,"maj",vel,-1),
               gstep(5,"dom7",vel),   gstep(1,"min",vel),    gstep(7,"maj",vel,-1),
               gstep(5,"dom7",vel)};
      break;
    default:
      for (int d = 1; d <= 7; ++d) {
        steps.push_back(gstep(static_cast<uint8_t>(d), "maj", vel));
      }
      break;
  }

  make_chords_distinct(steps, s);
  engine.progression().set_steps(steps);
}

}  // namespace app
