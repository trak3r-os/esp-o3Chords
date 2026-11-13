#pragma once

#include "engine/arpeggiator.h"
#include "engine/chord_distance.h"
#include "engine/chords.h"
#include "engine/midi_clock.h"
#include "engine/midi_scheduler.h"
#include "engine/modifier.h"
#include "engine/performer.h"
#include "engine/progression.h"
#include "engine/timing.h"
#include "util/fixed_vec.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "midi/router.h"

namespace engine {

enum class PerformanceMode {
  kChord,   // Immediate chord playback
  kArp,     // Arpeggiated playback
  kStrum    // Strummed playback
};

enum class TriggerMode {
  kOneShot,
  kHeld
};

class Engine {
 public:
  static constexpr app::contract::SourceId kChordSource = 1;
  static constexpr app::contract::SourceId kLeadSource = 2;
  bool begin();
  void loop();

  void set_router(midi::Router* router);
  void set_note_ledger(NoteLedger* ledger) { scheduler_.set_ledger(ledger); }
  void set_route_mask(uint8_t routes) { scheduler_.set_route_mask(routes); }

  void set_playing(bool playing);
  bool playing() const { return playing_; }

  void set_key(const std::string& key);
  void set_scale(const std::string& scale);
  void set_bpm(float bpm);
  void set_bend_range(int semitones);
  void set_bend_glide_ms(int ms);
  void set_channel(uint8_t channel);
  void set_lead_channel(uint8_t channel);
  int bend_range() const { return bend_range_semitones_; }
  int bend_glide_ms() const { return bend_glide_ms_; }
  uint8_t channel() const { return channel_; }
  uint8_t lead_channel() const { return lead_channel_; }

  ChordLibrary& chord_library() { return chords_; }
  Progression& progression() { return progression_; }
  Timing& timing() { return timing_; }
  Performer& performer() { return performer_; }
  ChordModifier& modifier() { return modifier_; }
  Arpeggiator& arpeggiator() { return arpeggiator_; }
  MidiClock& midi_clock() { return midi_clock_; }
  uint16_t consume_clock_ticks();

  void set_performance_mode(PerformanceMode mode);
  PerformanceMode performance_mode() const { return performance_mode_; }

  // Clear previous chord memory (resets auto-inversion voice leading)
  void clear_prev_chord();
  void shift_prev_chord(int8_t semitones);
  // The inversion the last chord actually sounded in: the manual one, or the
  // one voice leading picked.
  uint8_t sounding_inversion() const { return sounding_inversion_; }

  // Play a specific chord immediately (for manual triggering)
  void play_chord(size_t step_index);

  // Trigger a step in one-shot or held mode (held supports continuous arpeggio).
  void trigger_step(size_t step_index, TriggerMode mode);
  void release_held();
  bool held_active() const { return held_active_; }
  bool held_is_arp() const { return held_is_arp_; }
  size_t held_arp_index() const { return held_arp_index_; }
  size_t held_arp_length() const { return held_arp_sequence_.size(); }

  // Play arbitrary notes through the arp/performer/scheduler pipeline.
  // Unlike trigger_step(), accepts caller-provided MIDI notes instead of
  // looking them up from the progression.
  void play_notes(const std::vector<uint8_t>& notes, uint8_t velocity,
                  TriggerMode mode = TriggerMode::kOneShot);

  // Silence all notes immediately
  void all_notes_off();
  void invalidate_source(app::contract::SourceId source);

 private:
  void update_step_interval();
  void play_step(uint64_t now_ms);
  void play_step_held(uint64_t now_ms);
  uint8_t resolve_root(const ProgressionStep& step) const;
  static constexpr size_t kMaxChordNotes = 16;
  static constexpr size_t kMaxArpSeq = 64;  // 8 notes * 4 oct * 2 (up-down)

  util::FixedVec<uint8_t, kMaxChordNotes> build_chord(const ProgressionStep& step) const;
  util::FixedVec<uint8_t, kMaxChordNotes> voice_chord(ProgressionStep step);
  void apply_voicing_spread(util::FixedVec<uint8_t, kMaxChordNotes>& notes, uint8_t root_note) const;
  util::FixedVec<uint8_t, kMaxArpSeq> generate_arp_sequence(
      const util::FixedVec<uint8_t, kMaxChordNotes>& base_notes) const;
  uint32_t get_arp_interval_ms(float bpm) const;
  static uint16_t get_arp_ticks_per_step(ArpRate rate);
  const Chord* find_chord(const std::string& quality) const;
  const Scale* find_scale(const std::string& name) const;
  void service_held(uint64_t now_ms, uint16_t ticks);
  void reset_held_state();
  static void admission_failed(void* owner, app::contract::SourceId source);

  midi::Router* router_ = nullptr;
  MidiScheduler scheduler_;
  bool playing_ = false;
  size_t step_index_ = 0;
  uint64_t last_step_ms_ = 0;
  uint32_t step_interval_ms_ = 500;
  int bend_range_semitones_ = 12;
  int bend_glide_ms_ = 120;
  std::string key_ = "C";
  std::string scale_name_ = "Major";
  std::unordered_map<std::string, int> key_offsets_;
  uint8_t channel_ = 0;
  uint8_t lead_channel_ = 0;
  bool lead_channel_explicit_ = false;

  ChordLibrary chords_;
  Progression progression_;
  Timing timing_;
  Performer performer_;
  ChordModifier modifier_;
  Arpeggiator arpeggiator_;
  MidiClock midi_clock_;
  PerformanceMode performance_mode_ = PerformanceMode::kChord;

  // Held note state
  bool held_active_ = false;
  size_t held_step_index_ = 0;
  bool held_is_arp_ = false;
  util::FixedVec<uint8_t, kMaxArpSeq> held_arp_sequence_;
  size_t held_arp_index_ = 0;
  double held_arp_phase_ms_ = 0.0;    // Precise schedule time (no integer truncation drift)
  uint16_t held_arp_tick_acc_ = 0;    // Accumulated PPQ ticks toward next arp step
  uint8_t held_velocity_ = 100;

  // Held strum rhythm state
  bool held_is_strum_rhythm_ = false;
  util::FixedVec<uint8_t, kMaxChordNotes> held_rhythm_notes_;
  uint8_t held_rhythm_hit_index_ = 0;   // Next hit to play in pattern
  uint64_t held_rhythm_bar_start_ms_ = 0; // When current pattern cycle began
  uint32_t held_rhythm_16th_ms_ = 0;    // Duration of one 16th note in ms
  uint16_t held_rhythm_tick_acc_ = 0;   // Accumulated MIDI clock ticks for rhythm (6 = one 16th)

  // Previous chord notes for auto-inversion voice leading (sorted ascending)
  util::FixedVec<uint8_t, kMaxChordNotes> prev_chord_notes_;
  uint8_t sounding_inversion_ = 0;

  // Lookahead: how far ahead (ms) to pre-schedule held events.
  static constexpr uint64_t kLookaheadMs = 100;
  uint16_t pending_clock_ticks_ = 0;
  bool last_clock_locked_ = false;
  bool last_clock_running_ = false;
  ClockSource last_clock_source_ = ClockSource::kNone;
};

}  // namespace engine
