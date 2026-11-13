#pragma once

// Chord distance metrics for voice-leading optimization, chord comparison,
// and harmonic analysis.  All functions are header-only, operate on fixed-size
// data, and avoid heap allocation — safe for real-time ESP32 paths.
//
// Inspired by the vectorial distance approach in the music++ library
// (github.com/sivabenepoivediamo/musicplusplus, MIT license).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace engine {

// ---------------------------------------------------------------------------
// Manhattan distance  (L1 norm)
// Sum of absolute per-voice differences.  Voices are paired by index (both
// arrays assumed sorted ascending).  This is the metric already used
// implicitly by pick_best_inversion — made explicit here.
// ---------------------------------------------------------------------------
inline int manhattan_distance(const uint8_t* a, size_t na,
                              const uint8_t* b, size_t nb) {
    int dist = 0;
    const size_t n = (na < nb) ? na : nb;
    for (size_t i = 0; i < n; ++i) {
        dist += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
    }
    // Penalty for size mismatch (12 semitones per missing voice).
    const size_t diff = (na > nb) ? (na - nb) : (nb - na);
    dist += static_cast<int>(diff) * 12;
    return dist;
}

// ---------------------------------------------------------------------------
// Hamming distance  (pitch-class set)
// Counts how many of the 12 pitch classes differ between two chords.
// Useful for comparing harmonic colour independently of voicing/register.
// ---------------------------------------------------------------------------
inline uint8_t hamming_distance_pc(const uint8_t* a, size_t na,
                                   const uint8_t* b, size_t nb) {
    bool set_a[12] = {};
    bool set_b[12] = {};
    for (size_t i = 0; i < na; ++i) set_a[a[i] % 12] = true;
    for (size_t i = 0; i < nb; ++i) set_b[b[i] % 12] = true;
    uint8_t diff = 0;
    for (int pc = 0; pc < 12; ++pc) {
        if (set_a[pc] != set_b[pc]) ++diff;
    }
    return diff;
}

// ---------------------------------------------------------------------------
// Common-tone count  (pitch-class set)
// Number of pitch classes shared between two chords.
// ---------------------------------------------------------------------------
inline uint8_t common_tone_count(const uint8_t* a, size_t na,
                                 const uint8_t* b, size_t nb) {
    bool set_a[12] = {};
    bool set_b[12] = {};
    for (size_t i = 0; i < na; ++i) set_a[a[i] % 12] = true;
    for (size_t i = 0; i < nb; ++i) set_b[b[i] % 12] = true;
    uint8_t count = 0;
    for (int pc = 0; pc < 12; ++pc) {
        if (set_a[pc] && set_b[pc]) ++count;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Variation distance
// Handles chords of different sizes gracefully.  For common voices, sums
// absolute differences.  For extra voices in the longer chord, adds the
// average distance to the shorter chord's nearest and furthest notes.
// ---------------------------------------------------------------------------
inline float variation_distance(const uint8_t* a, size_t na,
                                const uint8_t* b, size_t nb) {
    if (na == 0 && nb == 0) return 0.0f;
    if (na == 0 || nb == 0) return 12.0f * static_cast<float>((na > nb) ? na : nb);

    // Work on sorted copies (caller likely already sorted, but be safe).
    uint8_t sa[16], sb[16];
    const size_t cap = 16;
    na = (na < cap) ? na : cap;
    nb = (nb < cap) ? nb : cap;
    for (size_t i = 0; i < na; ++i) sa[i] = a[i];
    for (size_t i = 0; i < nb; ++i) sb[i] = b[i];
    std::sort(sa, sa + na);
    std::sort(sb, sb + nb);

    const size_t common = (na < nb) ? na : nb;
    float dist = 0.0f;
    for (size_t i = 0; i < common; ++i) {
        dist += static_cast<float>(std::abs(static_cast<int>(sa[i]) - static_cast<int>(sb[i])));
    }

    // Extra voices — average of min and max distance to the other chord.
    const uint8_t* longer = (na > nb) ? sa : sb;
    const uint8_t* shorter = (na > nb) ? sb : sa;
    const size_t ns = (na < nb) ? na : nb;
    const size_t nl = (na > nb) ? na : nb;
    for (size_t i = common; i < nl; ++i) {
        int min_d = 127, max_d = 0;
        for (size_t j = 0; j < ns; ++j) {
            int d = std::abs(static_cast<int>(longer[i]) - static_cast<int>(shorter[j]));
            if (d < min_d) min_d = d;
            if (d > max_d) max_d = d;
        }
        dist += static_cast<float>(min_d + max_d) / 2.0f;
    }
    return dist;
}

// ---------------------------------------------------------------------------
// Weighted transformation distance
// Minimum total semitone movement to transform chord A into chord B,
// considering octave transpositions per voice.  This approximates
// music++'s transformationSteps metric but uses a greedy O(n*m) approach
// suitable for real-time use with small chords (≤8 notes).
// ---------------------------------------------------------------------------
inline int weighted_transform_distance(const uint8_t* a, size_t na,
                                       const uint8_t* b, size_t nb) {
    if (na == 0 || nb == 0) return static_cast<int>((na > nb ? na : nb)) * 12;

    // For each note in A, find the minimum semitone shift to reach any note
    // in B (considering ±2 octave transpositions).  Sum the best matches.
    const size_t n = (na < nb) ? na : nb;
    int total = 0;

    // Simple greedy: pair by sorted position.
    uint8_t sa[16], sb[16];
    const size_t cap = 16;
    size_t sna = (na < cap) ? na : cap;
    size_t snb = (nb < cap) ? nb : cap;
    for (size_t i = 0; i < sna; ++i) sa[i] = a[i];
    for (size_t i = 0; i < snb; ++i) sb[i] = b[i];
    std::sort(sa, sa + sna);
    std::sort(sb, sb + snb);

    for (size_t i = 0; i < n; ++i) {
        int best = std::abs(static_cast<int>(sa[i]) - static_cast<int>(sb[i]));
        // Check octave transpositions.
        for (int oct = -2; oct <= 2; ++oct) {
            if (oct == 0) continue;
            int shifted = static_cast<int>(sa[i]) + oct * 12;
            if (shifted < 0 || shifted > 127) continue;
            int d = std::abs(shifted - static_cast<int>(sb[i]));
            if (d < best) best = d;
        }
        total += best;
    }

    // Penalty for unmatched voices.
    const size_t diff = (sna > snb) ? (sna - snb) : (snb - sna);
    total += static_cast<int>(diff) * 12;
    return total;
}

}  // namespace engine
