#include "engine/modifier.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace engine {

void ChordModifier::reset_quality() {
  quality_name_[0] = '\0';
  quality_active_ = false;
}

void ChordModifier::set_quality(const char* quality) {
  if (quality && quality[0]) {
    std::strncpy(quality_name_, quality, sizeof(quality_name_) - 1);
    quality_name_[sizeof(quality_name_) - 1] = '\0';
    quality_active_ = true;
  } else {
    reset_quality();
  }
}

void ChordModifier::adjust_octave(int8_t delta) {
  octave_shift_ += delta;
  octave_shift_ = std::max<int8_t>(-3, std::min<int8_t>(3, octave_shift_));
}

void ChordModifier::reset_octave() {
  octave_shift_ = 0;
}

void ChordModifier::set_manual_inversion(uint8_t inversion) {
  inversion_override_ = std::min<uint8_t>(inversion, 5);
  inversion_active_ = true;
}

void ChordModifier::reset_inversion() {
  inversion_override_ = 0;
  inversion_active_ = false;
}

void ChordModifier::reset() {
  reset_quality();
  reset_octave();
  reset_inversion();
}

ProgressionStep ChordModifier::apply(const ProgressionStep& base) const {
  ProgressionStep modified = base;

  // Apply quality modification
  if (quality_active_ && quality_name_[0]) {
    modified.chord_quality = quality_name_;
  }

  // Apply octave shift
  modified.octave += octave_shift_;

  // A manual inversion replaces the step's own; without one the engine's
  // voice leading decides (Engine::voice_chord).
  if (inversion_active_) {
    modified.inversion = inversion_override_;
  }

  return modified;
}

std::string ChordModifier::get_quality_display() const {
  if (!quality_active_ || !quality_name_[0]) {
    return "---";
  }
  return quality_name_;
}

std::string ChordModifier::get_octave_display() const {
  if (octave_shift_ == 0) {
    return "0";
  }
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%+d", octave_shift_);
  return buf;
}

std::string ChordModifier::get_inversion_display() const {
  if (voice_leads()) {
    return "A";
  }
  if (!inversion_active_) {
    return "0";
  }
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%d", inversion_override_);
  return buf;
}

std::string ChordModifier::get_voice_leading_display() const {
  switch (voice_leading_) {
    case VoiceLeading::kOff:    return "Off";
    case VoiceLeading::kSmooth: return "Smooth";
    case VoiceLeading::kCommon: return "Common";
  }
  return "?";
}

std::string ChordModifier::get_voicing_spread_display() const {
  return voicing_spread_name(voicing_spread_);
}

const char* voicing_spread_name(VoicingSpread v) {
  switch (v) {
    case VoicingSpread::kClose:    return "Close";
    case VoicingSpread::kDefault:  return "Default";
    case VoicingSpread::kDrop2:    return "Drop2";
    case VoicingSpread::kDrop3:    return "Drop3";
    case VoicingSpread::kOpen:     return "Open";
    case VoicingSpread::kShell:    return "Shell";
    case VoicingSpread::kRootless: return "Rootless";
    case VoicingSpread::kGuitar:   return "Guitar";
  }
  return "?";
}

bool ChordModifier::has_modifications() const {
  return (quality_active_ && quality_name_[0]) || octave_shift_ != 0 ||
         (inversion_active_ && inversion_override_ != 0);
}

}  // namespace engine
