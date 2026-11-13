#include "engine/arpeggiator.h"
#include <algorithm>
#include <cstdlib>

namespace engine {

void Arpeggiator::set_gate_percent(uint8_t percent) {
  gate_percent_ = std::max<uint8_t>(1, std::min<uint8_t>(100, percent));
}

void Arpeggiator::set_octaves(uint8_t octaves) {
  octaves_ = std::max<uint8_t>(1, std::min<uint8_t>(4, octaves));
}

uint32_t Arpeggiator::get_note_interval_ms(float bpm) const {
  // Quarter note duration in ms
  float quarter_note_ms = (60.0f / bpm) * 1000.0f;

  switch (rate_) {
    case ArpRate::kQuarter:
      return static_cast<uint32_t>(quarter_note_ms);
    case ArpRate::kEighth:
      return static_cast<uint32_t>(quarter_note_ms / 2.0f);
    case ArpRate::kSixteenth:
      return static_cast<uint32_t>(quarter_note_ms / 4.0f);
    case ArpRate::kTriplet:
      return static_cast<uint32_t>(quarter_note_ms / 3.0f);
    default:
      return static_cast<uint32_t>(quarter_note_ms / 2.0f);
  }
}

std::vector<uint8_t> Arpeggiator::generate_arp_sequence(
  const std::vector<uint8_t>& base_notes
) const {
  if (base_notes.empty()) {
    return {};
  }

  std::vector<uint8_t> sequence;

  // First, expand to multiple octaves if needed
  std::vector<uint8_t> expanded_notes;
  for (uint8_t oct = 0; oct < octaves_; ++oct) {
    for (uint8_t note : base_notes) {
      const int transposed = static_cast<int>(note) + static_cast<int>(oct) * 12;
      if (transposed <= 127) {  // MIDI note range check
        expanded_notes.push_back(static_cast<uint8_t>(transposed));
      }
    }
  }

  // Apply pattern
  switch (pattern_) {
    case ArpPattern::kUp:
      // Already in ascending order
      sequence = expanded_notes;
      break;

    case ArpPattern::kDown:
      // Reverse order
      sequence = expanded_notes;
      std::reverse(sequence.begin(), sequence.end());
      break;

    case ArpPattern::kUpDown:
      // Up then down, excluding duplicates at both endpoints.
      // [C,E,G] → [C,E,G,E] — neither C nor G repeated at the seam.
      sequence = expanded_notes;
      for (int i = static_cast<int>(expanded_notes.size()) - 2; i > 0; --i) {
        sequence.push_back(expanded_notes[i]);
      }
      break;

    case ArpPattern::kDownUp:
      // Down then up, excluding duplicates at both endpoints.
      // [C,E,G] → [G,E,C,E] — neither G nor C repeated at the seam.
      sequence = expanded_notes;
      std::reverse(sequence.begin(), sequence.end());
      for (size_t i = 1; i < expanded_notes.size() - 1; ++i) {
        sequence.push_back(expanded_notes[i]);
      }
      break;

    case ArpPattern::kUpDownInc:
      // Up then down (including duplicates at endpoints)
      sequence = expanded_notes;
      for (int i = static_cast<int>(expanded_notes.size()) - 1; i >= 0; --i) {
        sequence.push_back(expanded_notes[i]);
      }
      break;

    case ArpPattern::kRandom:
      // Random order
      sequence = expanded_notes;
      // Simple shuffle using std::random_shuffle equivalent
      for (size_t i = sequence.size() - 1; i > 0; --i) {
        size_t j = rand() % (i + 1);
        std::swap(sequence[i], sequence[j]);
      }
      break;

    case ArpPattern::kOrder:
      // Original order
      sequence = expanded_notes;
      break;
  }

  return sequence;
}

std::vector<NoteEvent> Arpeggiator::render_arp(
  const std::vector<uint8_t>& chord_notes,
  float bpm,
  uint64_t step_start_ms,
  uint32_t step_duration_ms,
  uint8_t velocity,
  uint8_t channel
) const {
  std::vector<NoteEvent> events;

  if (!enabled_ || chord_notes.empty()) {
    return events;
  }

  // Generate arpeggio sequence
  std::vector<uint8_t> arp_sequence = generate_arp_sequence(chord_notes);

  if (arp_sequence.empty()) {
    return events;
  }

  // Calculate timing
  uint32_t note_interval_ms = get_note_interval_ms(bpm);
  uint32_t note_duration_ms = (note_interval_ms * gate_percent_) / 100;

  // Generate note events for the step duration
  uint32_t current_offset_ms = 0;
  size_t arp_index = 0;

  // Only notes that fit whole: the step length and the note interval are
  // rounded separately, so at most tempos (130 BPM sixteenths: 461 vs 4x115)
  // a `< step` test let a fifth, one-millisecond note in at the very end.
  while (current_offset_ms == 0 || current_offset_ms + note_interval_ms <= step_duration_ms) {
    // Get next note in sequence (loop around)
    uint8_t note = arp_sequence[arp_index % arp_sequence.size()];

    // Create note on event
    NoteEvent note_on;
    note_on.note = note;
    note_on.velocity = velocity;
    note_on.channel = channel;
    note_on.offset_ms = current_offset_ms;
    events.push_back(note_on);

    // Create note off event — always emit it, capped at the step boundary so
    // it never fires after the next chord starts. Dropping it when gate%
    // pushes it past step_duration_ms would leave the note stuck for the rest
    // of the phrase.
    {
      uint32_t note_off_offset = std::min(current_offset_ms + note_duration_ms,
                                          step_duration_ms);
      NoteEvent note_off;
      note_off.note = note;
      note_off.velocity = 0;
      note_off.channel = channel;
      note_off.offset_ms = note_off_offset;
      events.push_back(note_off);
    }

    // Advance to next note
    current_offset_ms += note_interval_ms;
    arp_index++;
  }

  return events;
}

}  // namespace engine
