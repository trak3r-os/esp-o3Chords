#include "ui/display_names.h"
#include "engine/modifier.h"
#include "engine/music_theory.h"

#include <cstdio>

namespace ui {

int key_nav_step(app::KeyNavMode mode) {
  switch (mode) {
    case app::KeyNavMode::kFifths:   return 7;
    case app::KeyNavMode::kFourths:  return 5;
    case app::KeyNavMode::kSemitone: return 1;
    default: return 7;
  }
}

const char* key_nav_mode_name(app::KeyNavMode mode) {
  switch (mode) {
    case app::KeyNavMode::kFifths:   return "5th";
    case app::KeyNavMode::kFourths:  return "4th";
    case app::KeyNavMode::kSemitone: return "Semi";
    default: return "?";
  }
}

void joystick_mode_label(app::JoystickPlayMode m, uint8_t map_idx, char* buf, size_t len) {
  using app::JoystickPlayMode;
  using app::kQualityMaps;
  switch (m) {
    case JoystickPlayMode::kQuality:
      std::snprintf(buf, len, "Q:%.4s", kQualityMaps[map_idx].name); break;
    case JoystickPlayMode::kChordVel:
      std::snprintf(buf, len, "CHVEL"); break;
    case JoystickPlayMode::kBend:
      std::snprintf(buf, len, "BEND"); break;
    case JoystickPlayMode::kSoloist:
      std::snprintf(buf, len, "SOLO"); break;
    case JoystickPlayMode::kBassClock:
      std::snprintf(buf, len, "BASS"); break;
    case JoystickPlayMode::kDiscover:
      std::snprintf(buf, len, "DISC"); break;
    case JoystickPlayMode::kArpControl:
      std::snprintf(buf, len, "ARPC"); break;
    case JoystickPlayMode::kInterval:
      std::snprintf(buf, len, "INTV"); break;
    case JoystickPlayMode::kDrone:
      std::snprintf(buf, len, "DRNE"); break;
    case JoystickPlayMode::kRhythm:
      std::snprintf(buf, len, "RHYT"); break;
    case JoystickPlayMode::kCCControl:
      std::snprintf(buf, len, "CC"); break;
    case JoystickPlayMode::kPentatonic:
      std::snprintf(buf, len, "PENT"); break;
    case JoystickPlayMode::kVoicing:
      std::snprintf(buf, len, "VOIC"); break;
    case JoystickPlayMode::kEuclidean:
      std::snprintf(buf, len, "EUCL"); break;
    case JoystickPlayMode::kStrumFeel:
      std::snprintf(buf, len, "SFEEL"); break;
    case JoystickPlayMode::kHarmony:
      std::snprintf(buf, len, "HARM"); break;
    case JoystickPlayMode::kScaleExplore:
      std::snprintf(buf, len, "SCLE"); break;
    default:
      std::snprintf(buf, len, "?"); break;
  }
}

const char* arp_rate_name(uint8_t v) {
  switch (v) {
    case 0: return "1/4";
    case 1: return "1/8";
    case 2: return "1/16";
    case 3: return "TRI";
    default: return "?";
  }
}

const char* arp_pattern_name(uint8_t p) {
  switch (p) {
    case 0: return "Up";
    case 1: return "Down";
    case 2: return "UpDn";
    case 3: return "DnUp";
    case 4: return "UDI";
    case 5: return "Rand";
    case 6: return "Ordr";
    default: return "?";
  }
}

void joystick_note_output_label(const std::array<uint8_t, 8>& active_notes,
                                uint8_t active_count, uint8_t last_note,
                                char* out, size_t out_len) {
  if (!out || out_len == 0) return;
  if (active_count == 0) {
    char n[8];
    engine::midi_note_label(last_note, n, sizeof(n));
    std::snprintf(out, out_len, "     LAST:%s", n);
    return;
  }

  char n0[8], n1[8], n2[8];
  engine::midi_note_label(active_notes[0], n0, sizeof(n0));
  if (active_count == 1) {
    std::snprintf(out, out_len, "     N:%s", n0);
    return;
  }

  engine::midi_note_label(active_notes[1], n1, sizeof(n1));
  if (active_count == 2) {
    std::snprintf(out, out_len, "     N:%s %s", n0, n1);
    return;
  }

  engine::midi_note_label(active_notes[2], n2, sizeof(n2));
  if (active_count > 3) {
    std::snprintf(out, out_len, "     N:%s %s %s+", n0, n1, n2);
  } else {
    std::snprintf(out, out_len, "     N:%s %s %s", n0, n1, n2);
  }
}

const char* joy_mode_name(app::JoystickPlayMode m) {
  using app::JoystickPlayMode;
  switch (m) {
    case JoystickPlayMode::kQuality:    return "Quality";
    case JoystickPlayMode::kChordVel:   return "ChordVel";
    case JoystickPlayMode::kBend:       return "Bend";
    case JoystickPlayMode::kSoloist:    return "Soloist";
    case JoystickPlayMode::kBassClock:  return "BassClock";
    case JoystickPlayMode::kDiscover:   return "Discover";
    case JoystickPlayMode::kArpControl: return "ArpCtrl";
    case JoystickPlayMode::kInterval:   return "Interval";
    case JoystickPlayMode::kDrone:      return "Drone";
    case JoystickPlayMode::kRhythm:     return "Rhythm";
    case JoystickPlayMode::kCCControl:  return "CC Ctrl";
    case JoystickPlayMode::kPentatonic: return "Penta";
    case JoystickPlayMode::kVoicing:    return "Voicing";
    case JoystickPlayMode::kEuclidean:  return "Euclid";
    case JoystickPlayMode::kStrumFeel:  return "StrFeel";
    case JoystickPlayMode::kHarmony:    return "Harmony";
    case JoystickPlayMode::kScaleExplore: return "ScaleXp";
    default: return "?";
  }
}

const char* interval_sub_name(uint8_t sub) {
  switch (sub) {
    case 0: return "Scale";
    case 1: return "Chord+";
    case 2: return "Power";
    default: return "?";
  }
}

const char* penta_sub_name(uint8_t sub) {
  switch (sub) {
    case 0: return "Major";
    case 1: return "Minor";
    case 2: return "Blues";
    default: return "?";
  }
}

const char* solo_scale_lock_name(uint8_t v) {
  switch (v) {
    case 0: return "Follow";
    case 1: return "PntMaj";
    case 2: return "PntMin";
    case 3: return "Blues";
    default: return "?";
  }
}

const char* vel_curve_name(uint8_t v) {
  switch (v) {
    case 0: return "Linear";
    case 1: return "Exp";
    case 2: return "Log";
    default: return "?";
  }
}

const char* voicing_name(uint8_t v) {
  return v <= 7 ? engine::voicing_spread_name(static_cast<engine::VoicingSpread>(v)) : "?";
}

// JoystickPlayMode compatibility label — production screens are driven by
// the family/preset model and never call this; kept only so
// JoystickPlayMode-based fixtures still have a label to ask for. It reports
// the handful of per-mode runtime fields JoystickState still holds.
const char* joy_mode_config_label(app::JoystickPlayMode m,
                                   const app::JoystickState& js) {
  using app::JoystickPlayMode;
  static char buf[24];
  switch (m) {
    case JoystickPlayMode::kQuality:
    case JoystickPlayMode::kDiscover:
      std::snprintf(buf, sizeof(buf), "Map:%.4s%s",
                    app::kQualityMaps[js.quality_map_index].name,
                    js.quality_retrigger ? " R" : "");
      return buf;
    case JoystickPlayMode::kChordVel:
      std::snprintf(buf, sizeof(buf), "Oct:%+d %s", js.note_octave, vel_curve_name(js.chordvel_curve));
      return buf;
    case JoystickPlayMode::kVoicing:
      std::snprintf(buf, sizeof(buf), "%s%s", voicing_name(js.voicing_selection),
                    js.voicing_auto_reset ? " A" : "");
      return buf;
    case JoystickPlayMode::kArpControl:
    case JoystickPlayMode::kRhythm:
    case JoystickPlayMode::kScaleExplore:
      return "Live";
    default:
      return "";
  }
}

}  // namespace ui
