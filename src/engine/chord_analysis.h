#pragma once

// Chord and scale analysis utilities: symmetry detection, interval-class
// vector, entropy, and spectrum analysis.  Header-only, no heap allocation.
//
// These metrics give richer harmonic insight than the existing binary
// chord_tension() and can feed Discover mode, OLED display, and
// voice-leading decisions.
//
// Inspired by the analytical approach in the music++ library
// (github.com/sivabenepoivediamo/musicplusplus, MIT license).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace engine {

// ---------------------------------------------------------------------------
// Interval-class vector (ICV)
// Counts occurrences of each interval class (1..6) in a pitch-class set.
// The ICV is a fingerprint: chords with identical ICVs sound related even
// if they share no pitch classes (Z-related sets).
//
// ic1 = minor 2nd / major 7th
// ic2 = major 2nd / minor 7th
// ic3 = minor 3rd / major 6th
// ic4 = major 3rd / minor 6th
// ic5 = perfect 4th / perfect 5th
// ic6 = tritone
// ---------------------------------------------------------------------------
struct IntervalClassVector {
    uint8_t ic[6] = {};  // ic[0] = ic1, ... ic[5] = ic6

    bool operator==(const IntervalClassVector& o) const {
        for (int i = 0; i < 6; ++i) {
            if (ic[i] != o.ic[i]) return false;
        }
        return true;
    }
};

inline IntervalClassVector interval_class_vector(const uint8_t* notes, size_t n) {
    IntervalClassVector icv;
    // Collect unique pitch classes.
    bool present[12] = {};
    uint8_t pcs[12];
    uint8_t npc = 0;
    for (size_t i = 0; i < n && i < 16; ++i) {
        uint8_t pc = notes[i] % 12;
        if (!present[pc]) {
            present[pc] = true;
            pcs[npc++] = pc;
        }
    }
    // Count interval classes between all pairs.
    for (uint8_t i = 0; i < npc; ++i) {
        for (uint8_t j = i + 1; j < npc; ++j) {
            int diff = std::abs(static_cast<int>(pcs[i]) - static_cast<int>(pcs[j]));
            if (diff > 6) diff = 12 - diff;  // Fold to interval class (1-6).
            if (diff >= 1 && diff <= 6) {
                icv.ic[diff - 1]++;
            }
        }
    }
    return icv;
}

// ---------------------------------------------------------------------------
// ICV similarity
// Manhattan distance between two interval-class vectors.
// 0 = identical harmonic profile, higher = more different.
// ---------------------------------------------------------------------------
inline uint8_t icv_distance(const IntervalClassVector& a, const IntervalClassVector& b) {
    uint8_t dist = 0;
    for (int i = 0; i < 6; ++i) {
        dist += static_cast<uint8_t>(std::abs(static_cast<int>(a.ic[i]) - static_cast<int>(b.ic[i])));
    }
    return dist;
}

// ---------------------------------------------------------------------------
// Interval entropy
// Shannon entropy of the interval-class distribution.  Low entropy means
// the chord is built from repetitive intervals (diminished, augmented,
// whole-tone), high entropy means diverse intervals (complex jazz chords).
// Returns value 0.0 .. ~2.58 (log2(6)).
// ---------------------------------------------------------------------------
inline float interval_entropy(const uint8_t* notes, size_t n) {
    IntervalClassVector icv = interval_class_vector(notes, n);
    int total = 0;
    for (int i = 0; i < 6; ++i) total += icv.ic[i];
    if (total == 0) return 0.0f;

    float entropy = 0.0f;
    for (int i = 0; i < 6; ++i) {
        if (icv.ic[i] == 0) continue;
        float p = static_cast<float>(icv.ic[i]) / static_cast<float>(total);
        entropy -= p * log2f(p);
    }
    return entropy;
}

// ---------------------------------------------------------------------------
// Symmetry score
// How symmetric is the interval pattern of this chord?  Compares the
// ascending inter-note intervals to their reverse.  Returns 0-100 where
// 100 = perfectly symmetric (e.g. dim7 {3,3,3,3}, aug {4,4,4}).
// ---------------------------------------------------------------------------
inline uint8_t symmetry_score(const uint8_t* notes, size_t n) {
    if (n < 2) return 100;

    // Sort and compute intervals.
    uint8_t sorted[16];
    size_t sn = (n < 16) ? n : 16;
    for (size_t i = 0; i < sn; ++i) sorted[i] = notes[i];
    std::sort(sorted, sorted + sn);

    // Compute pitch-class intervals (mod 12, wrapping around).
    uint8_t intervals[16];
    for (size_t i = 0; i < sn - 1; ++i) {
        intervals[i] = (sorted[i + 1] - sorted[i]) % 12;
    }
    // Wrap-around interval.
    intervals[sn - 1] = (12 - (sorted[sn - 1] - sorted[0]) % 12) % 12;
    const size_t ni = sn;

    // Compare forward and reverse.
    int match = 0;
    int total = 0;
    for (size_t i = 0; i < ni; ++i) {
        size_t rev = ni - 1 - i;
        if (intervals[i] == intervals[rev]) ++match;
        ++total;
    }
    if (total == 0) return 100;
    return static_cast<uint8_t>(match * 100 / total);
}

// ---------------------------------------------------------------------------
// Pitch-class density
// Number of distinct pitch classes in the chord (1-12).
// ---------------------------------------------------------------------------
inline uint8_t pitch_class_density(const uint8_t* notes, size_t n) {
    bool present[12] = {};
    for (size_t i = 0; i < n && i < 16; ++i) {
        present[notes[i] % 12] = true;
    }
    uint8_t count = 0;
    for (int i = 0; i < 12; ++i) {
        if (present[i]) ++count;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Consonance score  (simplified Hindemith-inspired)
// Rates overall consonance based on interval-class content.
// Perfect 5ths/4ths are consonant, tritones are dissonant.
// Returns 0 (maximally dissonant) to 100 (maximally consonant).
// ---------------------------------------------------------------------------
inline uint8_t consonance_score(const uint8_t* notes, size_t n) {
    if (n < 2) return 100;
    IntervalClassVector icv = interval_class_vector(notes, n);

    // Weights: ic5(P4/P5)=+5, ic3(m3/M6)=+3, ic4(M3/m6)=+3,
    //          ic2(M2/m7)=-1, ic1(m2/M7)=-3, ic6(tritone)=-4
    static constexpr int kWeights[6] = {-3, -1, 3, 3, 5, -4};
    int score = 0;
    int total_intervals = 0;
    for (int i = 0; i < 6; ++i) {
        score += icv.ic[i] * kWeights[i];
        total_intervals += icv.ic[i];
    }
    if (total_intervals == 0) return 100;

    // Normalize: theoretical range is [-4..+5] per interval.
    // Map to 0-100 with 50 as neutral.
    float normalized = static_cast<float>(score) / static_cast<float>(total_intervals);
    // normalized is roughly in [-4, +5].  Map to [0, 100].
    float pct = (normalized + 4.0f) / 9.0f * 100.0f;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return static_cast<uint8_t>(pct);
}

}  // namespace engine
