#include "engine/engine.h"
#include "engine/music_theory.h"
#include "engine/rhythm_pattern.h"

#include <algorithm>
#include <climits>
#include <initializer_list>
#include <cstdlib>

#include "esp_timer.h"
#include "esp_log.h"

namespace engine {

bool Engine::begin() {
  chords_.load_defaults();

  key_offsets_ = {{"C", 0},  {"C#", 1}, {"Db", 1}, {"D", 2},  {"D#", 3}, {"Eb", 3},
                  {"E", 4},  {"F", 5},  {"F#", 6}, {"Gb", 6}, {"G", 7},  {"G#", 8},
                  {"Ab", 8}, {"A", 9},  {"A#", 10}, {"Bb", 10}, {"B", 11}};

  performer_.set_strum_ms(8);
  performer_.set_humanize_ms(6);
  performer_.set_note_length_ms(240);

  update_step_interval();
  return true;
}

void Engine::set_router(midi::Router* router) {
  router_ = router;
  scheduler_.start(router_, channel_);
  scheduler_.set_admission_failure_callback(&Engine::admission_failed, this);
}

void Engine::admission_failed(void* owner, app::contract::SourceId source) {
  auto* engine = static_cast<Engine*>(owner);
  if (source == kChordSource || source == kLeadSource) engine->reset_held_state();
}

void Engine::set_playing(bool playing) {
  playing_ = playing;
  if (playing_) {
    last_step_ms_ = esp_timer_get_time() / 1000;
  } else {
    all_notes_off();
  }
}

void Engine::all_notes_off() {
  scheduler_.all_notes_off();
  scheduler_.all_notes_off(kLeadSource);
  ESP_LOGI("Engine", "All notes silenced");
}

void Engine::invalidate_source(app::contract::SourceId source) {
  scheduler_.invalidate_source(source);
}

uint16_t Engine::consume_clock_ticks() {
  const uint16_t ticks = pending_clock_ticks_;
  pending_clock_ticks_ = 0;
  return ticks;
}

// Settings are re-applied wholesale on every edit, so these setters are
// called constantly with the value they already hold. Only a real change may
// have side effects: clearing the remembered chord on every call made voice
// leading start over (root position) after any encoder tick.
void Engine::set_key(const std::string& key) {
  if (key == key_) return;
  key_ = key;
  prev_chord_notes_.clear();
}

void Engine::set_scale(const std::string& scale) {
  if (scale == scale_name_) return;
  scale_name_ = scale;
  prev_chord_notes_.clear();
}

void Engine::clear_prev_chord() { prev_chord_notes_.clear(); }

void Engine::shift_prev_chord(int8_t semitones) {
  for (auto& n : prev_chord_notes_) {
    int v = static_cast<int>(n) + semitones;
    n = static_cast<uint8_t>(std::clamp(v, 0, 127));
  }
}

void Engine::set_bpm(float bpm) {
  timing_.set_bpm(bpm);
  midi_clock_.set_internal_bpm(bpm);
  update_step_interval();
}

void Engine::set_bend_range(int semitones) { bend_range_semitones_ = semitones; }

void Engine::set_bend_glide_ms(int ms) { bend_glide_ms_ = ms; }

void Engine::set_channel(uint8_t channel) {
  channel_ = channel;
  scheduler_.set_channel(channel);
  if (!lead_channel_explicit_) lead_channel_ = channel;
}

void Engine::set_lead_channel(uint8_t channel) {
  lead_channel_explicit_ = true;
  if (channel == lead_channel_) return;
  scheduler_.all_notes_off(kLeadSource);
  lead_channel_ = channel;
}

void Engine::update_step_interval() {
  const float bpm = timing_.bpm();
  if (bpm <= 0.1f) {
    step_interval_ms_ = 500;
    return;
  }
  step_interval_ms_ = static_cast<uint32_t>((60.0f / bpm) * 1000.0f);
}

const Chord* Engine::find_chord(const std::string& quality) const {
  auto& list = chords_.chords();
  auto it = std::find_if(list.begin(), list.end(), [&](const Chord& c) { return c.name == quality; });
  if (it != list.end()) return &(*it);
  return nullptr;
}

const Scale* Engine::find_scale(const std::string& name) const {
  auto& list = chords_.scales();
  auto it = std::find_if(list.begin(), list.end(), [&](const Scale& s) { return s.name == name; });
  if (it != list.end()) return &(*it);
  return nullptr;
}

uint8_t Engine::resolve_root(const ProgressionStep& step) const {
  const int base = 60;  // C4
  int key_offset = 0;
  auto it = key_offsets_.find(key_);
  if (it != key_offsets_.end()) key_offset = it->second;

  const Scale* scale = find_scale(scale_name_);
  int degree_offset = 0;
  if (scale && !scale->steps.empty()) {
    const size_t idx = (step.scale_degree - 1) % scale->steps.size();
    degree_offset = scale->steps[idx];
  }

  int note = base + key_offset + degree_offset + (step.octave * 12) + step.transpose;
  note = std::clamp(note, 0, 127);
  return static_cast<uint8_t>(note);
}

util::FixedVec<uint8_t, Engine::kMaxChordNotes> Engine::build_chord(const ProgressionStep& step) const {
  util::FixedVec<uint8_t, kMaxChordNotes> notes;
  const Chord* chord = find_chord(step.chord_quality);
  const int root = resolve_root(step);
  if (!chord) {
    notes.push_back(root);
    return notes;
  }

  for (auto interval : chord->intervals) {
    int note = root + interval;
    notes.push_back(static_cast<uint8_t>(std::clamp(note, 0, 127)));
  }

  apply_chord_inversion(notes, step.inversion);
  return notes;
}

namespace {
// How far a chord moves from the previous one: every note's distance to its
// nearest neighbour in the other chord, counted both ways so chords of
// different sizes compare fairly.
int chord_movement(const uint8_t* a, size_t na, const uint8_t* b, size_t nb) {
  const auto one_way = [](const uint8_t* from, size_t nf, const uint8_t* to, size_t nt) {
    int total = 0;
    for (size_t i = 0; i < nf; ++i) {
      int best = 127;
      for (size_t j = 0; j < nt; ++j) best = std::min(best, std::abs(static_cast<int>(from[i]) - static_cast<int>(to[j])));
      total += best;
    }
    return total;
  };
  return one_way(a, na, b, nb) + one_way(b, nb, a, na);
}

int common_tones(const uint8_t* a, size_t na, const uint8_t* b, size_t nb) {
  int count = 0;
  for (size_t i = 0; i < na; ++i)
    for (size_t j = 0; j < nb; ++j)
      if (a[i] == b[j]) { ++count; break; }
  return count;
}
}  // namespace

// The one place a step becomes sounding notes. Three independent choices,
// applied in this order:
//
//   register   the Octave setting. It moves everything and is never
//              overridden by history.
//   inversion  which chord tone is lowest. Either the player's (keys 8/9) or,
//              when voice leading is on and no manual inversion is set, the
//              engine's: the inversion and octave placement that moves least
//              from the previous chord.
//   voicing    how the notes are spaced (Close, Drop2, ...).
//
// Voice leading only ever considers placements whose bass lies within half
// an octave of the key's tonic at the selected register, so a long
// progression cannot creep up or down the keyboard: after any number of
// chords, the tonic still sits where the Octave setting put it.
util::FixedVec<uint8_t, Engine::kMaxChordNotes> Engine::voice_chord(ProgressionStep step) {
  const Chord* chord = find_chord(step.chord_quality);
  const uint8_t size = chord ? static_cast<uint8_t>(std::min<size_t>(chord->intervals.size(), kMaxChordNotes)) : 1;
  const bool led = modifier_.voice_leads() && size > 1;

  util::FixedVec<uint8_t, kMaxChordNotes> notes;
  if (!led || prev_chord_notes_.empty()) {
    // As written: the root where the register puts it, the chosen inversion
    // above it. The first chord of a voice-led passage is the reference the
    // rest is led from, so it starts in root position.
    if (led) step.inversion = 0;
    notes = build_chord(step);
    apply_voicing_spread(notes, resolve_root(step));
    sounding_inversion_ = std::min<uint8_t>(step.inversion, static_cast<uint8_t>(size - 1));
  } else {
    int key_offset = 0;
    if (auto it = key_offsets_.find(key_); it != key_offsets_.end()) key_offset = it->second;
    const int anchor = 60 + key_offset + step.octave * 12;
    const bool hold_common = modifier_.voice_leading() == VoiceLeading::kCommon;
    const int root = resolve_root(step);
    int best_score = INT_MAX;
    for (uint8_t inversion = 0; inversion < size; ++inversion) {
      ProgressionStep candidate_step = step;
      candidate_step.inversion = inversion;
      const auto close = build_chord(candidate_step);
      if (close.empty()) continue;
      for (int shift : {0, -12, 12}) {
        const int bass = static_cast<int>(*std::min_element(close.begin(), close.end())) + shift;
        const int top = static_cast<int>(*std::max_element(close.begin(), close.end())) + shift;
        if (bass < anchor - 6 || bass > anchor + 6 || bass < 0 || top > 127) continue;
        util::FixedVec<uint8_t, kMaxChordNotes> candidate;
        for (uint8_t note : close) candidate.push_back(static_cast<uint8_t>(note + shift));
        apply_voicing_spread(candidate, static_cast<uint8_t>(std::clamp(root + shift, 0, 127)));
        std::sort(candidate.begin(), candidate.end());
        int score = chord_movement(candidate.data(), candidate.size(),
                                   prev_chord_notes_.data(), prev_chord_notes_.size());
        // Common: a note that can stay exactly where it was outweighs any
        // saving in how far the others travel.
        if (hold_common)
          score -= 48 * common_tones(candidate.data(), candidate.size(),
                                     prev_chord_notes_.data(), prev_chord_notes_.size());
        if (score < best_score) {
          best_score = score;
          notes = candidate;
          sounding_inversion_ = inversion;
        }
      }
    }
    if (notes.empty()) {  // nothing fitted the window (extreme register)
      notes = build_chord(step);
      apply_voicing_spread(notes, resolve_root(step));
      sounding_inversion_ = 0;
    }
  }

  prev_chord_notes_ = notes;
  std::sort(prev_chord_notes_.begin(), prev_chord_notes_.end());
  return notes;
}

namespace {
uint8_t drop_octave(uint8_t note) { return note >= 12 ? static_cast<uint8_t>(note - 12) : note; }
uint8_t raise_octave(uint8_t note) { return note <= 115 ? static_cast<uint8_t>(note + 12) : note; }
}  // namespace

// Voicing styles operate on the lowest four voices of the chord (the "window");
// a 5+-note chord leaves everything above that window untouched. Like the
// Close/Drop2 logic, these transforms are purely positional (bottom-to-top),
// not harmony-aware: they don't track which voice is truly "the root" once an
// inversion has rotated the chord. Root/9th derivation is the one exception,
// since it must come from the chord root and scale (`root_note`), not from a
// voice's position.
void Engine::apply_voicing_spread(util::FixedVec<uint8_t, kMaxChordNotes>& notes,
                                   uint8_t root_note) const {
  if (notes.size() < 2) return;
  const auto vs = modifier_.voicing_spread();
  if (vs == VoicingSpread::kDefault) return;

  std::sort(notes.begin(), notes.end());

  if (vs == VoicingSpread::kClose) {
    // All notes within 12 semitones of bass.
    const uint8_t bass = notes[0];
    for (size_t i = 1; i < notes.size(); ++i) {
      while (notes[i] > bass + 12 && notes[i] >= 12) notes[i] -= 12;
    }
    std::sort(notes.begin(), notes.end());
    return;
  }

  // Styles below apply only to the lowest four voices (or fewer, for a triad
  // or dyad); any extensions above stay exactly as built.
  const size_t k = std::min<size_t>(4, notes.size());
  if (k < 2) return;
  util::FixedVec<uint8_t, 4> w;
  for (size_t i = 0; i < k; ++i) w.push_back(notes[i]);

  switch (vs) {
    case VoicingSpread::kDrop2: {
      w[k - 2] = drop_octave(w[k - 2]);
      break;
    }
    case VoicingSpread::kDrop3: {
      // Triads have no seventh to drop below the third — fall back to Drop2.
      const size_t idx = (k >= 4) ? k - 3 : k - 2;
      w[idx] = drop_octave(w[idx]);
      break;
    }
    case VoicingSpread::kOpen: {
      for (size_t i = 0; i < k; i += 2) w[i] = drop_octave(w[i]);
      break;
    }
    case VoicingSpread::kShell: {
      // Needs a seventh to omit the fifth meaningfully; a triad is already
      // just root+third+fifth, so it's left unchanged.
      if (k >= 4) {
        w[1] = raise_octave(w[1]);  // third up an octave, above the seventh
        w.erase(w.begin() + 2);     // drop the fifth
      }
      break;
    }
    case VoicingSpread::kRootless: {
      w.erase(w.begin());  // drop the root
      if (k >= 4) {
        // Add the ninth derived from the chord root + scale — not by
        // transposing an existing voice — and only if it stays in range.
        int ninth_offset = 2;  // whole-step fallback if no scale is loaded
        if (const Scale* scale = find_scale(scale_name_); scale && !scale->steps.empty()) {
          ninth_offset = scale->steps[1 % scale->steps.size()];
        }
        const int ninth = static_cast<int>(root_note) + 12 + ninth_offset;
        // A chord that already carries its ninth (add9, 9, 13...) keeps the one it has.
        const bool present = std::find(notes.begin(), notes.end(), static_cast<uint8_t>(ninth)) != notes.end();
        if (ninth >= 0 && ninth <= 127 && !present) w.push_back(static_cast<uint8_t>(ninth));
      }
      // Triads: root omitted, third + fifth kept, no ninth added.
      break;
    }
    case VoicingSpread::kGuitar: {
      // Third (index 1) stays put; everything else drops an octave.
      for (size_t i = 0; i < k; ++i) {
        if (i != 1) w[i] = drop_octave(w[i]);
      }
      break;
    }
    default: break;
  }

  notes.erase(notes.begin(), notes.begin() + k);
  for (auto n : w) notes.push_back(n);
  std::sort(notes.begin(), notes.end());
}

void Engine::play_step(uint64_t now_ms) {
  if (!router_ || progression_.steps().empty()) return;

  // Stop currently active notes before sounding the next chord.
  scheduler_.all_notes_off();

  const auto& base_step = progression_.steps()[step_index_];

  // Apply real-time modifiers (non-destructive)
  ProgressionStep modified_step = modifier_.apply(base_step);
  auto notes = voice_chord(modified_step);

  // Collect all events for this step, then schedule as a batch.
  util::FixedVec<ScheduledEvent, 32> batch;

  // Emit pitch bend toward next chord if requested.
  // Pre-bend is always the full-range minimum (-8192): the semitone range is
  // programmed into the synth via RPN and does not scale the raw 14-bit value.
  // The old formula `(bend_range_semitones_ * 8192) / 2` overflowed int16_t
  // for any range > 4 and produced wrong or UB values for range 5, 7, 12.
  if (base_step.bend_into_next) {
    constexpr int16_t kFullBendDown = -8192;
    batch.push_back(
        ScheduledEvent{now_ms, 0, EventType::kPitchBend, 0, 0, kFullBendDown});
    batch.push_back(
        ScheduledEvent{now_ms + static_cast<uint64_t>(bend_glide_ms_), 0, EventType::kPitchBend, 0, 0, 0});
  }

  // Generate note events based on performance mode
  std::vector<NoteEvent> note_events;
  uint16_t note_len = performer_.note_length_ms();

  if (performance_mode_ == PerformanceMode::kArp && arpeggiator_.enabled()) {
    // render_arp embeds gate-timed note-off events (velocity == 0) in the same
    // vector.  Schedule them directly rather than wrapping everything as kNoteOn
    // and adding a second redundant note-off — that would corrupt active_notes_
    // and send doubled note-offs.
    note_events = arpeggiator_.render_arp(notes, timing_.bpm(), now_ms,
                                          step_interval_ms_, modified_step.velocity, channel_);
    for (const auto& ev : note_events) {
      if (ev.velocity > 0) {
        batch.push_back(ScheduledEvent{now_ms + ev.offset_ms, 0, EventType::kNoteOn, ev.note, ev.velocity, 0,
                                       kChordSource, channel_});
      } else {
        batch.push_back(ScheduledEvent{now_ms + ev.offset_ms, 0, EventType::kNoteOff, ev.note, 0, 0,
                                       kChordSource, channel_});
      }
    }
  } else {
    // render_step returns only note-on events; we add the note-off at note_len.
    note_events = performer_.render_step(notes, modified_step.velocity, channel_);
    for (const auto& ev : note_events) {
      batch.push_back(ScheduledEvent{now_ms + ev.offset_ms, 0, EventType::kNoteOn, ev.note, ev.velocity, 0,
                                     kChordSource, channel_});
      batch.push_back(
          ScheduledEvent{now_ms + ev.offset_ms + note_len, 0, EventType::kNoteOff, ev.note, 0, 0,
                         kChordSource, channel_});
    }
  }

  if (!scheduler_.schedule_batch(batch.data(), batch.size()))
    scheduler_.invalidate_source(kChordSource);
}

void Engine::loop() {
  uint64_t now_us = esp_timer_get_time();
  uint64_t now_ms = now_us / 1000;

  const auto before_clock = midi_clock_.snapshot();

  // Process incoming MIDI clock ticks and compute external BPM.
  // Returns number of ticks processed (for tick-driven arp scheduling).
  uint16_t ticks = midi_clock_.update(now_us);
  const auto clock = midi_clock_.snapshot();
  pending_clock_ticks_ = static_cast<uint16_t>(pending_clock_ticks_ + ticks);

  if (before_clock.running && !clock.running) {
    // Stop is a barrier for scheduled attacks, but releases are emitted by
    // the ledger while clearing both held sources.
    scheduler_.all_notes_off();
    scheduler_.all_notes_off(kLeadSource);
  }
  if ((clock.locked && (!before_clock.locked || before_clock.source != clock.source)) ||
      (clock.running && !before_clock.running)) {
    // Never replay the elapsed wall-clock gap as a burst after relock, or
    // after the transport resumes from a Stop with a chord still held.
    held_arp_tick_acc_ = 0;
    held_arp_phase_ms_ = static_cast<double>(now_ms);
    held_rhythm_tick_acc_ = 0;
    held_rhythm_hit_index_ = 0;
    held_rhythm_bar_start_ms_ = now_ms;
  }
  last_clock_locked_ = clock.locked;
  last_clock_running_ = clock.running;
  last_clock_source_ = clock.source;

  // Sync effective BPM from external clock when active.
  float eff_bpm = midi_clock_.effective_bpm();
  if (eff_bpm != timing_.bpm()) {
    timing_.set_bpm(eff_bpm);
    update_step_interval();
  }

  // Event dispatch is handled by the MidiScheduler's esp_timer callback.
  // We only need to feed the scheduler with lookahead events for held notes.
  service_held(now_ms, ticks);
}

uint32_t Engine::get_arp_interval_ms(float bpm) const {
  float quarter_note_ms = (60.0f / bpm) * 1000.0f;
  using R = ArpRate;
  switch (arpeggiator_.rate()) {
    case R::kQuarter:
      return static_cast<uint32_t>(quarter_note_ms);
    case R::kEighth:
      return static_cast<uint32_t>(quarter_note_ms / 2.0f);
    case R::kSixteenth:
      return static_cast<uint32_t>(quarter_note_ms / 4.0f);
    case R::kTriplet:
      return static_cast<uint32_t>(quarter_note_ms / 3.0f);
    default:
      return static_cast<uint32_t>(quarter_note_ms / 2.0f);
  }
}

uint16_t Engine::get_arp_ticks_per_step(ArpRate rate) {
  // MIDI clock sends 24 PPQ (pulses per quarter note).
  switch (rate) {
    case ArpRate::kQuarter:   return 24;
    case ArpRate::kEighth:    return 12;
    case ArpRate::kSixteenth: return 6;
    case ArpRate::kTriplet:   return 8;   // 24/3 = 8 ticks per triplet
    default:                  return 12;
  }
}

util::FixedVec<uint8_t, Engine::kMaxArpSeq> Engine::generate_arp_sequence(
    const util::FixedVec<uint8_t, kMaxChordNotes>& base_notes) const {
  util::FixedVec<uint8_t, kMaxArpSeq> seq;
  if (base_notes.empty()) return seq;

  util::FixedVec<uint8_t, kMaxArpSeq> expanded;
  for (uint8_t oct = 0; oct < arpeggiator_.octaves(); ++oct) {
    for (size_t i = 0; i < base_notes.size(); ++i) {
      // Use int to avoid uint8_t wrap-around before the range check.
      const int t = static_cast<int>(base_notes[i]) + static_cast<int>(oct) * 12;
      if (t >= 0 && t <= 127) expanded.push_back(static_cast<uint8_t>(t));
    }
  }

  using P = ArpPattern;
  switch (arpeggiator_.pattern()) {
    case P::kUp:
      seq = expanded;
      break;
    case P::kDown:
      seq = expanded;
      std::reverse(seq.begin(), seq.end());
      break;
    case P::kUpDown:
      // [C,E,G] → [C,E,G,E] — no endpoint duplicates.
      seq = expanded;
      for (int i = static_cast<int>(expanded.size()) - 2; i > 0; --i) seq.push_back(expanded[i]);
      break;
    case P::kDownUp:
      // [C,E,G] → [G,E,C,E] — no endpoint duplicates.
      seq = expanded;
      std::reverse(seq.begin(), seq.end());
      for (size_t i = 1; i + 1 < expanded.size(); ++i) seq.push_back(expanded[i]);
      break;
    case P::kUpDownInc:
      seq = expanded;
      for (int i = static_cast<int>(expanded.size()) - 1; i >= 0; --i) seq.push_back(expanded[i]);
      break;
    case P::kRandom:
      seq = expanded;
      for (size_t i = seq.size() - 1; i > 0; --i) {
        size_t j = static_cast<size_t>(rand() % (i + 1));
        std::swap(seq[i], seq[j]);
      }
      break;
    case P::kOrder:
      seq = expanded;
      break;
  }

  return seq;
}

void Engine::service_held(uint64_t now_ms, uint16_t ticks) {
  if (!held_active_ || !router_) return;

  const auto clock = midi_clock_.snapshot();
  if (clock.mode == ClockMode::kExternal && (!clock.locked || !clock.running)) return;
  // External wall-clock time can place deadlines, but cannot authorize new
  // rhythmic attacks. Producers advance only on fresh external ticks.
  if (clock.mode == ClockMode::kExternal && ticks == 0) return;

  const uint64_t lookahead = now_ms + kLookaheadMs;

  // --- Held arpeggio: unified tick-driven scheduling ---
  // Both internal and external clocks generate PPQ ticks. We count ticks
  // to gate advancement (phase-locked) and use a double-precision phase
  // accumulator for scheduling (eliminates integer truncation drift).
  if (held_is_arp_) {
    if (held_arp_sequence_.empty()) return;

    held_arp_tick_acc_ += ticks;
    const uint16_t ticks_per_step = get_arp_ticks_per_step(arpeggiator_.rate());

    // Compute precise interval and gate using double (no truncation).
    const double bpm = static_cast<double>(timing_.bpm());
    double interval_ms = 60000.0 / bpm;
    switch (arpeggiator_.rate()) {
      case ArpRate::kEighth:    interval_ms /= 2.0; break;
      case ArpRate::kSixteenth: interval_ms /= 4.0; break;
      case ArpRate::kTriplet:   interval_ms /= 3.0; break;
      default: break;
    }
    const uint32_t gate_ms = static_cast<uint32_t>(
        interval_ms * arpeggiator_.gate_percent() / 100.0);

    while (held_arp_tick_acc_ >= ticks_per_step) {
      held_arp_tick_acc_ -= ticks_per_step;

      const uint8_t note = held_arp_sequence_[held_arp_index_ % held_arp_sequence_.size()];
      held_arp_index_++;

      // A phase that has fallen behind the clock (ticks stalled, then came
      // back) would schedule notes whose off time is already past: they
      // would sound as zero-length clicks. Resume from now instead.
      if (held_arp_phase_ms_ + interval_ms < static_cast<double>(now_ms))
        held_arp_phase_ms_ = static_cast<double>(now_ms);
      const uint64_t sched_ms = static_cast<uint64_t>(held_arp_phase_ms_);
      scheduler_.schedule(ScheduledEvent{
          sched_ms, 0, EventType::kNoteOn, note, held_velocity_, 0, kLeadSource, lead_channel_});
      scheduler_.schedule(ScheduledEvent{
          sched_ms + static_cast<uint64_t>(gate_ms),
          0, EventType::kNoteOff, note, 0, 0, kLeadSource, lead_channel_});

      // Advance phase with full double precision — no drift.
      held_arp_phase_ms_ += interval_ms;
    }
    return;
  }

  // --- Held strum rhythm: pre-schedule hits into the lookahead window ---
  if (!held_is_strum_rhythm_) return;
  if (held_rhythm_notes_.empty() || held_rhythm_16th_ms_ == 0) return;

  // Tick-driven: accumulate ticks and advance rhythm position.
  // 6 MIDI clock ticks = one 16th note (24 PPQ / 4).
  if (clock.locked && ticks > 0) {
    held_rhythm_tick_acc_ += ticks;
    // Recalculate 16th-note duration for note-off timing.
    float quarter_ms = (60.0f / timing_.bpm()) * 1000.0f;
    held_rhythm_16th_ms_ = static_cast<uint32_t>(quarter_ms / 4.0f);
  } else if (clock.locked) {
    // Synced but no new ticks — nothing to advance.
    float quarter_ms = (60.0f / timing_.bpm()) * 1000.0f;
    held_rhythm_16th_ms_ = static_cast<uint32_t>(quarter_ms / 4.0f);
  }

  const auto* pattern = get_rhythm_pattern(performer_.strum_rhythm());
  if (!pattern || pattern->num_hits == 0) return;

  const uint16_t note_len = performer_.note_length_ms();

  // Likewise for the rhythm: a bar that started more than its own length ago
  // must not be replayed hit by hit to catch up.
  const uint64_t bar_ms = static_cast<uint64_t>(pattern->length_16ths) * held_rhythm_16th_ms_;
  if (held_rhythm_bar_start_ms_ + 2 * bar_ms < now_ms) {
    held_rhythm_bar_start_ms_ = now_ms;
    held_rhythm_hit_index_ = 0;
  }

  while (true) {
    if (held_rhythm_hit_index_ >= pattern->num_hits) {
      // Pattern finished — loop back to start.
      held_rhythm_hit_index_ = 0;
      held_rhythm_bar_start_ms_ += static_cast<uint64_t>(pattern->length_16ths) * held_rhythm_16th_ms_;
    }

    const auto& hit = pattern->hits[held_rhythm_hit_index_];
    const uint64_t hit_time = held_rhythm_bar_start_ms_ +
        static_cast<uint64_t>(hit.position) * held_rhythm_16th_ms_;

    if (hit_time > lookahead) break;  // Beyond lookahead window

    // Calculate when these notes must stop: before the next hit starts.
    uint64_t next_hit_time;
    if (held_rhythm_hit_index_ + 1 < pattern->num_hits) {
      next_hit_time = held_rhythm_bar_start_ms_ +
          static_cast<uint64_t>(pattern->hits[held_rhythm_hit_index_ + 1].position) * held_rhythm_16th_ms_;
    } else {
      // Last hit in pattern — notes stop at pattern loop point.
      next_hit_time = held_rhythm_bar_start_ms_ +
          static_cast<uint64_t>(pattern->length_16ths) * held_rhythm_16th_ms_;
    }

    // Scale velocity
    const uint8_t vel = static_cast<uint8_t>(
        std::max(1, static_cast<int>(held_velocity_) * hit.velocity_pct / 100));

    // Render the strum and schedule events
    auto note_events = performer_.render_step(held_rhythm_notes_, vel, channel_);

    for (const auto& ev : note_events) {
      const uint64_t on_time = hit_time + ev.offset_ms;
      // Cap note-off at next hit time to avoid overlapping note-on/off conflicts.
      // ...but never before the note's own start: a rolled or strummed voice
      // can begin after the next hit, and an off that precedes its on is
      // dropped, leaving that voice sounding until release.
      const uint64_t off_time = std::max(
          on_time + 1, std::min(on_time + static_cast<uint64_t>(note_len), next_hit_time));

      scheduler_.schedule(ScheduledEvent{on_time, 0, EventType::kNoteOn, ev.note, ev.velocity, 0, kLeadSource, lead_channel_});
      scheduler_.schedule(ScheduledEvent{off_time, 0, EventType::kNoteOff, ev.note, 0, 0, kLeadSource, lead_channel_});
    }

    held_rhythm_hit_index_++;
  }
}

void Engine::reset_held_state() {
  held_active_ = false;
  held_is_arp_ = false;
  held_arp_sequence_.clear();
  held_arp_tick_acc_ = 0;
  held_is_strum_rhythm_ = false;
  held_rhythm_notes_.clear();
  held_rhythm_tick_acc_ = 0;
}

void Engine::set_performance_mode(PerformanceMode mode) {
  // Same rule: re-applying the current mode must not cut a sounding chord.
  if (mode == performance_mode_) return;
  scheduler_.all_notes_off();
  reset_held_state();
  performance_mode_ = mode;
}

void Engine::play_chord(size_t step_index) {
  if (step_index >= progression_.steps().size()) return;

  step_index_ = step_index;

  uint64_t now_ms = esp_timer_get_time() / 1000;
  play_step(now_ms);
}

void Engine::play_step_held(uint64_t now_ms) {
  if (!router_ || progression_.steps().empty()) return;

  // Replace only the chord source. Lead/joystick owners have independent lifetimes.
  scheduler_.all_notes_off(kChordSource);

  const auto& base_step = progression_.steps()[step_index_];
  ProgressionStep modified_step = modifier_.apply(base_step);
  auto notes = voice_chord(modified_step);

  held_velocity_ = modified_step.velocity;
  held_active_ = true;

  // Held arpeggio: continuous until release.
  if (performance_mode_ == PerformanceMode::kArp && arpeggiator_.enabled()) {
    held_is_arp_ = true;
    held_arp_sequence_ = generate_arp_sequence(notes);
    held_arp_index_ = 0;
    held_arp_phase_ms_ = static_cast<double>(now_ms);
    // Pre-load accumulator so first note fires immediately in service_held().
    held_arp_tick_acc_ = get_arp_ticks_per_step(arpeggiator_.rate());
    // First batch of notes will be pre-scheduled in the next service_held() call.
    return;
  }

  held_is_arp_ = false;
  held_arp_sequence_.clear();

  // Held strum with rhythm pattern: auto-comping loop.
  if (performance_mode_ == PerformanceMode::kStrum &&
      performer_.strum_rhythm() != StrumRhythm::kNone) {
    const auto* pattern = get_rhythm_pattern(performer_.strum_rhythm());
    if (pattern && pattern->num_hits > 0) {
      held_is_strum_rhythm_ = true;
      held_rhythm_notes_ = notes;
      held_rhythm_hit_index_ = 0;
      held_rhythm_tick_acc_ = 0;
      held_rhythm_bar_start_ms_ = now_ms;
      float quarter_ms = (60.0f / timing_.bpm()) * 1000.0f;
      held_rhythm_16th_ms_ = static_cast<uint32_t>(quarter_ms / 4.0f);
      // First batch of hits will be pre-scheduled in the next service_held() call.
      return;
    }
  }

  held_is_strum_rhythm_ = false;
  held_rhythm_notes_.clear();

  // Held chord/strum (no rhythm): schedule note-ons only; note-offs come on release.
  std::vector<NoteEvent> note_events = performer_.render_step(notes, modified_step.velocity, channel_);
  util::FixedVec<ScheduledEvent, 32> batch;
  for (const auto& ev : note_events) {
    batch.push_back(ScheduledEvent{now_ms + ev.offset_ms, 0, EventType::kNoteOn, ev.note, ev.velocity, 0,
                                   kChordSource, channel_});
  }
  if (!scheduler_.schedule_batch(batch.data(), batch.size())) {
    scheduler_.invalidate_source(kChordSource);
    reset_held_state();
  }
}

void Engine::trigger_step(size_t step_index, TriggerMode mode) {
  if (step_index >= progression_.steps().size()) return;

  reset_held_state();

  step_index_ = step_index;
  held_step_index_ = step_index;

  const uint64_t now_ms = esp_timer_get_time() / 1000;
  if (mode == TriggerMode::kHeld) {
    play_step_held(now_ms);
  } else {
    play_step(now_ms);
  }
}

void Engine::play_notes(const std::vector<uint8_t>& notes, uint8_t velocity,
                        TriggerMode mode) {
  if (!router_ || notes.empty()) return;

  reset_held_state();

  const uint64_t now_ms = esp_timer_get_time() / 1000;

  if (mode == TriggerMode::kHeld) {
    scheduler_.all_notes_off(kLeadSource);
    held_velocity_ = velocity;
    held_active_ = true;

    if (performance_mode_ == PerformanceMode::kArp && arpeggiator_.enabled()) {
      held_is_arp_ = true;
      held_arp_sequence_ = generate_arp_sequence(notes);
      held_arp_index_ = 0;
      held_arp_phase_ms_ = static_cast<double>(now_ms);
      held_arp_tick_acc_ = get_arp_ticks_per_step(arpeggiator_.rate());
      return;
    }

    held_is_arp_ = false;
    held_arp_sequence_.clear();

    if (performance_mode_ == PerformanceMode::kStrum &&
        performer_.strum_rhythm() != StrumRhythm::kNone) {
      const auto* pattern = get_rhythm_pattern(performer_.strum_rhythm());
      if (pattern && pattern->num_hits > 0) {
        held_is_strum_rhythm_ = true;
        held_rhythm_notes_ = notes;
        held_rhythm_hit_index_ = 0;
        held_rhythm_tick_acc_ = 0;
        held_rhythm_bar_start_ms_ = now_ms;
        float quarter_ms = (60.0f / timing_.bpm()) * 1000.0f;
        held_rhythm_16th_ms_ = static_cast<uint32_t>(quarter_ms / 4.0f);
        return;
      }
    }

    held_is_strum_rhythm_ = false;
    held_rhythm_notes_.clear();

    std::vector<NoteEvent> note_events = performer_.render_step(notes, velocity, channel_);
    util::FixedVec<ScheduledEvent, 32> batch;
    for (const auto& ev : note_events) {
      batch.push_back(ScheduledEvent{now_ms + ev.offset_ms, 0, EventType::kNoteOn, ev.note, ev.velocity, 0, kLeadSource, lead_channel_});
    }
    if (!scheduler_.schedule_batch(batch.data(), batch.size())) {
      scheduler_.invalidate_source(kLeadSource);
      reset_held_state();
    }
    return;
  }

  // OneShot mode.
  scheduler_.all_notes_off(kLeadSource);

  std::vector<NoteEvent> note_events;
  uint16_t note_len = performer_.note_length_ms();

  if (performance_mode_ == PerformanceMode::kArp && arpeggiator_.enabled()) {
    note_events = arpeggiator_.render_arp(notes, timing_.bpm(), now_ms,
                                          step_interval_ms_, velocity, channel_);
  } else {
    note_events = performer_.render_step(notes, velocity, channel_);
  }

  util::FixedVec<ScheduledEvent, 32> batch;
  if (performance_mode_ == PerformanceMode::kArp && arpeggiator_.enabled()) {
    // render_arp embeds gate-timed note-offs (velocity==0); dispatch directly.
    for (const auto& ev : note_events) {
      if (ev.velocity > 0) {
        batch.push_back(ScheduledEvent{now_ms + ev.offset_ms, 0, EventType::kNoteOn, ev.note, ev.velocity, 0, kLeadSource, lead_channel_});
      } else {
        batch.push_back(ScheduledEvent{now_ms + ev.offset_ms, 0, EventType::kNoteOff, ev.note, 0, 0, kLeadSource, lead_channel_});
      }
    }
  } else {
    for (const auto& ev : note_events) {
      batch.push_back(ScheduledEvent{now_ms + ev.offset_ms, 0, EventType::kNoteOn, ev.note, ev.velocity, 0, kLeadSource, lead_channel_});
      batch.push_back(ScheduledEvent{now_ms + ev.offset_ms + note_len, 0, EventType::kNoteOff, ev.note, 0, 0, kLeadSource, lead_channel_});
    }
  }
  if (!scheduler_.schedule_batch(batch.data(), batch.size()))
    scheduler_.invalidate_source(kLeadSource);
}

void Engine::release_held() {
  if (!held_active_) return;
  reset_held_state();
  all_notes_off();
}

}  // namespace engine
