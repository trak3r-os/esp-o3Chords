#pragma once

#include <cstdint>
#include <cstddef>

namespace engine {

struct RhythmHit {
  uint8_t position;       // 0-based 16th-note position within the pattern
  uint8_t velocity_pct;   // 1-100, percentage of base velocity
};

enum class StrumRhythm : uint8_t {
  kNone = 0,
  kSalsa,       // Montuno syncopation
  kBossaNova,   // Classic long-short feel
  kReggae,      // Offbeat skank
  kFunk,        // 16th-note ghost/open
  kCOUNT
};

struct RhythmPattern {
  const char* name;
  const char* short_name;   // 4-char for OLED
  uint8_t length_16ths;     // Pattern length in 16th notes
  const RhythmHit* hits;
  uint8_t num_hits;
};

// --- Pattern data ---

// Salsa montuno (1 bar = 16 16ths): . . X . . . X . X . . . X . X .
static constexpr RhythmHit kSalsaHits[] = {
  {2, 100}, {6, 90}, {8, 100}, {12, 90}, {14, 100}
};

// Bossa nova (2 bars = 32 16ths):
// Bar1: X . . X . . X . . . X . . . . .
// Bar2: . . X . . . X . . X . . . . . .
static constexpr RhythmHit kBossaNovaHits[] = {
  {0, 100}, {3, 80}, {6, 100}, {10, 80}, {18, 100}, {22, 80}, {25, 100}
};

// Reggae skank (1 bar = 16 16ths): . . X . . . X . . . X . . . X .
static constexpr RhythmHit kReggaeHits[] = {
  {2, 100}, {6, 100}, {10, 100}, {14, 100}
};

// Funk (1 bar = 16 16ths): X g . X . g X . X g . X . g X .
static constexpr RhythmHit kFunkHits[] = {
  {0, 100}, {1, 40}, {3, 100}, {5, 40}, {6, 100},
  {8, 100}, {9, 40}, {11, 100}, {13, 40}, {14, 100}
};

static constexpr RhythmPattern kRhythmPatterns[] = {
  {"Salsa",      "SALS", 16, kSalsaHits,     sizeof(kSalsaHits)     / sizeof(RhythmHit)},
  {"Bossa Nova", "BOSS", 32, kBossaNovaHits,  sizeof(kBossaNovaHits) / sizeof(RhythmHit)},
  {"Reggae",     "REGG", 16, kReggaeHits,     sizeof(kReggaeHits)    / sizeof(RhythmHit)},
  {"Funk",       "FUNK", 16, kFunkHits,       sizeof(kFunkHits)      / sizeof(RhythmHit)},
};

inline const RhythmPattern* get_rhythm_pattern(StrumRhythm rhythm) {
  if (rhythm == StrumRhythm::kNone || rhythm >= StrumRhythm::kCOUNT) return nullptr;
  return &kRhythmPatterns[static_cast<uint8_t>(rhythm) - 1];
}

inline const char* strum_rhythm_short_name(StrumRhythm rhythm) {
  const auto* p = get_rhythm_pattern(rhythm);
  return p ? p->short_name : "NONE";
}

}  // namespace engine
