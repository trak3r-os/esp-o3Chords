#include "../framework.h"
#include "app/config_codec.h"
#include "app/types.h"
#include <cstring>
#include <cstdio>
#include <limits>

using app::contract::Config;

static void test_v3_roundtrip() {
  Config in{}; in.articulation = app::contract::Articulation::Arp; in.arp_octaves = 4; in.joystick.x_min = -100; in.revision = 42;
  uint8_t blob[128]{}; size_t written = 0;
  ASSERT_TRUE(app::config::encode(in, blob, sizeof(blob), written)); ASSERT_EQ(written, app::config::encoded_size());
  Config out{}; auto result = app::config::decode(blob, written, out);
  ASSERT_EQ(result.status, app::config::DecodeStatus::Valid); ASSERT_EQ(out.arp_octaves, 4); ASSERT_EQ(out.joystick.x_min, -100); ASSERT_EQ(out.revision, 42u);
}
static void test_corruption_and_unknown_are_protected() {
  Config c{}; uint8_t blob[128]{}; size_t n = 0; ASSERT_TRUE(app::config::encode(c, blob, sizeof(blob), n));
  blob[12] ^= 1; Config out{}; ASSERT_EQ(app::config::decode(blob, n, out).status, app::config::DecodeStatus::Corrupt);
  blob[0] = 0; ASSERT_EQ(app::config::decode(blob, n, out).status, app::config::DecodeStatus::Unsupported);
}
static void test_v1_migration_preserves_arp_and_usb_policy() {
  static_assert(sizeof(app::Settings) == 112, "legacy ESP32 v1 fixture layout changed");
  app::Settings old{}; old.size = 112; old.mode = app::PerformanceMode::kArp2Oct; old.arp_octaves = 1; old.usb_host_midi_enabled = true; old.usb_device_midi_enabled = true;
  Config out{}; ASSERT_TRUE(app::config::migrate_v1(old, out)); ASSERT_EQ(out.articulation, app::contract::Articulation::Arp); ASSERT_EQ(out.arp_octaves, 2); ASSERT_EQ(out.usb_role, app::contract::UsbRole::Host);
}
static void test_v3_roundtrip_preserves_complete_persistent_preferences() {
  Config in{}; in.bpm = 142.5f; std::snprintf(in.key_name, sizeof(in.key_name), "F#");
  std::snprintf(in.scale_name, sizeof(in.scale_name), "Dorian"); in.play_trigger = 2;
  in.arp_gate_percent = 63; in.strum_rhythm = 4; in.voice_leading = 2; in.voicing_spread = 2;
  in.bend_range_semitones = 7; in.joystick_rotation = 3; in.joystick_flip_h = true;
  in.roll_ms = 42; in.roll_dir = 2;
  uint8_t blob[128]{}; size_t n = 0; ASSERT_TRUE(app::config::encode(in, blob, sizeof(blob), n));
  Config out{}; ASSERT_EQ(app::config::decode(blob, n, out).status, app::config::DecodeStatus::Valid);
  ASSERT_NEAR(out.bpm, 142.5, 0.001); ASSERT_STREQ(out.key_name, "F#"); ASSERT_STREQ(out.scale_name, "Dorian");
  ASSERT_EQ(out.play_trigger, 2); ASSERT_EQ(out.arp_gate_percent, 63); ASSERT_EQ(out.strum_rhythm, 4);
  ASSERT_EQ(out.voice_leading, 2); ASSERT_EQ(out.voicing_spread, 2); ASSERT_EQ(out.bend_range_semitones, 7);
  ASSERT_EQ(out.joystick_rotation, 3); ASSERT_TRUE(out.joystick_flip_h);
  ASSERT_EQ(out.roll_ms, 42); ASSERT_EQ(out.roll_dir, 2);
}
static void test_harp_legacy_is_explicitly_unsupported() {
  app::Settings old{}; old.size = 112; old.mode = app::PerformanceMode::kHarp;
  Config out{}; ASSERT_EQ(app::config::migrate_v1_result(old, out), app::config::MigrationStatus::Unsupported);
}
static void test_runtime_projection_roundtrip_preserves_persisted_fields() {
  app::Settings in{}; in.size = 112; in.bpm = 137.0f; std::snprintf(in.key, sizeof(in.key), "Bb");
  std::snprintf(in.scale, sizeof(in.scale), "Mixolydian"); in.play_trigger = app::PlayTrigger::kRepeat;
  in.arp_gate_percent = 72; in.strum_rhythm = 3; in.voice_leading = 2; in.voicing_spread = 2;
  in.bend_range_semitones = 5; in.joystick_rotation = 2; in.joystick_flip_h = true;
  auto config = app::config::from_settings(in);
  // The key index is the authority: a legacy flat spelling maps onto it, and
  // the names written back are derived from the index (sharps).
  ASSERT_EQ(config.key, 10u); ASSERT_STREQ(config.key_name, "A#");
  ASSERT_EQ(config.scale, static_cast<uint8_t>(app::ProgressionMode::kMixolydian));
  ASSERT_STREQ(config.scale_name, "Mixolydian");
  app::Settings out{}; ASSERT_TRUE(app::config::apply_to_settings(config, out));
  ASSERT_NEAR(out.bpm, 137.0, 0.001); ASSERT_STREQ(out.key, "A#"); ASSERT_STREQ(out.scale, "Mixolydian");
  ASSERT_EQ(out.play_trigger, app::PlayTrigger::kRepeat); ASSERT_EQ(out.arp_gate_percent, 72);
  ASSERT_EQ(out.strum_rhythm, 3); ASSERT_EQ(out.voice_leading, 2); ASSERT_EQ(out.voicing_spread, 2);
  ASSERT_EQ(out.bend_range_semitones, 5); ASSERT_EQ(out.joystick_rotation, 2); ASSERT_TRUE(out.joystick_flip_h);
}
static void test_nonfinite_legacy_is_rejected() {
  app::Settings old{}; old.size = sizeof(old); old.bpm = std::numeric_limits<float>::quiet_NaN(); Config out{}; ASSERT_FALSE(app::config::migrate_v1(old, out));
}

static void test_v4_roundtrip_preserves_joy_families() {
  Config in{};
  in.joy_families.notes.source = app::NoteSource::kIntervals;
  in.joy_families.notes.octave = -2;
  in.joy_families.notes.mono = true;
  in.joy_families.chord.order = app::ChordOrder::kRanking;
  in.joy_families.chord.palette = 5;
  in.joy_families.chord.retrigger = true;
  in.joy_families.cont.x_cc = 74;
  in.joy_families.param.nw_se.target = app::AxisTarget::kArpOctaves;
  in.joy_families.param.nw_se.hold = app::AxisHold::kSticky;
  in.joy_families.param.retrigger_on_change = true;
  in.active_preset = app::PresetId::kMacro;

  uint8_t blob[128]{}; size_t n = 0;
  ASSERT_TRUE(app::config::encode(in, blob, sizeof(blob), n));
  Config out{}; ASSERT_EQ(app::config::decode(blob, n, out).status, app::config::DecodeStatus::Valid);

  ASSERT_TRUE(out.joy_families.notes.source == app::NoteSource::kIntervals);
  ASSERT_EQ(out.joy_families.notes.octave, -2);
  ASSERT_TRUE(out.joy_families.notes.mono);
  ASSERT_TRUE(out.joy_families.chord.order == app::ChordOrder::kRanking);
  ASSERT_EQ(out.joy_families.chord.palette, 5u);
  ASSERT_TRUE(out.joy_families.chord.retrigger);
  ASSERT_EQ(out.joy_families.cont.x_cc, 74u);
  ASSERT_TRUE(out.joy_families.param.nw_se.target == app::AxisTarget::kArpOctaves);
  ASSERT_TRUE(out.joy_families.param.nw_se.hold == app::AxisHold::kSticky);
  ASSERT_TRUE(out.joy_families.param.retrigger_on_change);
  ASSERT_TRUE(out.active_preset == app::PresetId::kMacro);
}

// Scale walks all 22 ProgressionMode presets, not just the original 9 scale
// names — validate() must accept the full range and encode()/decode() must round-trip it.
static void test_v3_roundtrip_accepts_every_progression_mode_scale() {
  Config in{}; in.scale = 21; std::snprintf(in.scale_name, sizeof(in.scale_name), "Flamenco");
  uint8_t blob[128]{}; size_t n = 0;
  ASSERT_TRUE(app::config::encode(in, blob, sizeof(blob), n));
  Config out{}; ASSERT_EQ(app::config::decode(blob, n, out).status, app::config::DecodeStatus::Valid);
  ASSERT_EQ(out.scale, 21u); ASSERT_STREQ(out.scale_name, "Flamenco");
}
static void test_validate_rejects_scale_past_the_last_progression_mode() {
  Config c{}; c.scale = 22;
  ASSERT_EQ(app::contract::validate(c).error, app::contract::ValidationError::Range);
}

// from_settings() reverse-maps a legacy Settings::scale string back onto the
// closest ProgressionMode index, now over all 22 display names rather than
// the original 9 scale names.
static void test_from_settings_maps_scale_name_to_progression_mode_index() {
  app::Settings s{}; std::snprintf(s.scale, sizeof(s.scale), "Bossa Nova");
  auto config = app::config::from_settings(s);
  ASSERT_EQ(config.scale, static_cast<uint8_t>(app::ProgressionMode::kBossaNova));
}
static void test_from_settings_defaults_unknown_scale_name_to_major() {
  // "Lydian" was a legacy scale name the old 9-entry table recognized; it has
  // no ProgressionMode equivalent, so it now falls back to the default (0).
  app::Settings s{}; std::snprintf(s.scale, sizeof(s.scale), "Lydian");
  auto config = app::config::from_settings(s);
  ASSERT_EQ(config.scale, 0u);
}

namespace {
// Hand-rolled v2 blob builder mirroring the pre-v3 encode() layout exactly
// (kVersion=2, kWireSize=96, CRC at 92) — the production encoder no longer
// emits this format, so migrate_v2() can only be exercised against a blob
// built independently, the same way a real device's old NVS data would look.
uint32_t v2_crc32(const uint8_t* p, size_t n) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; ++i) { crc ^= p[i]; for (int j = 0; j < 8; ++j) crc = (crc >> 1) ^ (0xEDB88320u & (-(crc & 1))); }
  return ~crc;
}
void write32_test(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (i * 8)); }
void build_v2_blob(uint8_t* b, app::contract::RecipeId recipe) {
  std::memset(b, 0, 96);
  b[0] = 0x46; b[1] = 0x43; b[2] = 0x33; b[3] = 0x4F;  // kMagic, little-endian
  b[4] = 2; b[5] = 0;                                  // kVersion = 2
  b[6] = 96; b[7] = 0;                                 // kWireSize = 96
  b[8] = 0;   // Articulation::Chord
  b[9] = 100; // velocity
  b[10] = 244; b[11] = 1;  // duration_ms = 500
  b[16] = 1;  // arp_octaves
  b[24] = static_cast<uint8_t>(recipe);
  b[25] = 1; b[26] = 11;  // x_target/y_target
  b[29] = static_cast<uint8_t>(-127); b[30] = 0xFF;  // x_min = -127
  b[31] = 127; b[32] = 0;                            // x_max = 127
  b[33] = static_cast<uint8_t>(-127); b[34] = 0xFF;  // y_min = -127
  b[35] = 127; b[36] = 0;                            // y_max = 127
  float bpm = 120.0f; std::memcpy(b + 42, &bpm, sizeof(bpm));
  b[46] = 'C';
  b[50] = 'M'; b[51] = 'a'; b[52] = 'j'; b[53] = 'o'; b[54] = 'r';
  b[67] = 80;   // arp_gate_percent
  b[70] = 1;    // voicing_spread
  b[71] = 12;   // bend_range_semitones
  b[74] = 100;  // solo_velocity
  b[75] = 240; b[76] = 0;  // solo_note_length_ms
  write32_test(b + 92, v2_crc32(b, 92));
}

// Hand-rolled v3 blob builder mirroring the pre-part-2 encode() layout
// (kVersion=3, kWireSize=128, 7-byte ChordParams block, CRC at 124) — the
// production encoder now emits v4 directly, so migrate_v3() can only be
// exercised against a blob built independently, the same way a real
// device's old NVS data would look. All-zero joy_families bytes are valid
// defaults for every family; only the active_preset byte varies per test.
void build_v3_blob(uint8_t* b, uint8_t v3_preset) {
  std::memset(b, 0, 128);
  b[0] = 0x46; b[1] = 0x43; b[2] = 0x33; b[3] = 0x4F;  // kMagic, little-endian
  b[4] = 3; b[5] = 0;    // kVersion = 3
  b[6] = 128; b[7] = 0;  // kWireSize = 128
  b[8] = 0;   // Articulation::Chord
  b[9] = 100; // velocity
  b[10] = 244; b[11] = 1;  // duration_ms = 500
  b[16] = 1;  // arp_octaves
  b[25] = 1; b[26] = 11;  // x_target/y_target
  b[29] = static_cast<uint8_t>(-127); b[30] = 0xFF;  // x_min = -127
  b[31] = 127; b[32] = 0;                            // x_max = 127
  b[33] = static_cast<uint8_t>(-127); b[34] = 0xFF;  // y_min = -127
  b[35] = 127; b[36] = 0;                            // y_max = 127
  float bpm = 120.0f; std::memcpy(b + 42, &bpm, sizeof(bpm));
  b[46] = 'C';
  b[50] = 'M'; b[51] = 'a'; b[52] = 'j'; b[53] = 'o'; b[54] = 'r';
  b[67] = 80;   // arp_gate_percent
  b[70] = 1;    // voicing_spread
  b[71] = 12;   // bend_range_semitones
  b[74] = 100;  // solo_velocity
  b[75] = 240; b[76] = 0;  // solo_note_length_ms
  // bytes 81-122: roll_ms/roll_dir + all four v3 JoyFamilyState blocks, left
  // at their all-zero (valid) defaults.
  b[123] = v3_preset;  // kActivePresetOffsetV3
  write32_test(b + 124, v2_crc32(b, 124));  // kCrcOffsetV3 (same CRC32 algorithm)
}
}  // namespace

static void test_migrate_v2_maps_recipe_to_preset() {
  uint8_t blob[96];
  build_v2_blob(blob, app::contract::RecipeId::BassClock);
  Config out{};
  auto result = app::config::migrate_v2(blob, sizeof(blob), out);
  ASSERT_EQ(result.status, app::config::DecodeStatus::Valid);
  ASSERT_EQ(result.source_version, 2u);
  ASSERT_TRUE(out.active_preset == app::PresetId::kBass);
  ASSERT_TRUE(out.joy_families.notes.gesture == app::NoteGesture::kClock);
  ASSERT_EQ(out.joy_families.notes.octave, -2);
  ASSERT_TRUE(out.joy_families.notes.mono);
  // Untouched families stay at their plain defaults.
  ASSERT_TRUE(out.joy_families.chord.order == app::ChordOrder::kFixed);
}

// v2->v3 config migration round-trips every old RecipeId — the two cases
// above only ever exercised BassClock and Drone. Walk the full 17-value enum against config_codec.h's exported
// preset_for_recipe_v2() table so every legacy recipe's migrated preset is
// pinned, not just the two spot-checked by hand.
static void test_migrate_v2_maps_every_recipe_to_its_preset() {
  using app::contract::RecipeId;
  for (uint8_t i = 0; i < static_cast<uint8_t>(RecipeId::COUNT); ++i) {
    const auto recipe = static_cast<RecipeId>(i);
    uint8_t blob[96];
    build_v2_blob(blob, recipe);
    Config out{};
    auto result = app::config::migrate_v2(blob, sizeof(blob), out);
    ASSERT_EQ(result.status, app::config::DecodeStatus::Valid);
    ASSERT_TRUE(out.active_preset == app::config::preset_for_recipe_v2(recipe));
  }
}

// Every legacy RecipeId and every v3 PresetId lands somewhere valid. The v3
// PresetId numbering (13 values, kHarmony=2, kArp=9, kFeel=10, kDynamics=11, kFlip=12) is frozen inside config_codec.cpp
// (PresetIdV3) precisely because the live enum's values shift once
// kHarmony/kArp/kFeel/kDynamics are gone; this walks all 13 raw byte values
// rather than naming them, since there is no live enum left to name them
// with.
static void test_migrate_v3_maps_every_legacy_preset_to_a_valid_one() {
  for (uint8_t v3_preset = 0; v3_preset < 13; ++v3_preset) {
    uint8_t blob[128];
    build_v3_blob(blob, v3_preset);
    Config out{};
    auto result = app::config::migrate_v3(blob, sizeof(blob), out);
    ASSERT_EQ(result.status, app::config::DecodeStatus::Valid);
    ASSERT_EQ(result.source_version, 3u);
    ASSERT_TRUE(out.active_preset < app::PresetId::kCount);
  }
}

static void test_migrate_v3_folds_legacy_harmony_into_drone() {
  uint8_t blob[128];
  build_v3_blob(blob, 2);  // PresetIdV3::kHarmony
  Config out{};
  auto result = app::config::migrate_v3(blob, sizeof(blob), out);
  ASSERT_EQ(result.status, app::config::DecodeStatus::Valid);
  ASSERT_TRUE(out.active_preset == app::PresetId::kDrone);
  ASSERT_TRUE(out.joy_families.notes.voices == app::NoteVoices::kNote);
  ASSERT_TRUE(out.joy_families.notes.mono);
  ASSERT_TRUE(out.joy_families.notes.channel == app::NoteChannel::kLead);
}

// Flip is disabled, so the PARAM-family presets that used to fold into it
// (and v3's own Flip) now land on the last CONT preset, Macro.
static void test_migrate_v3_folds_legacy_arp_feel_dynamics_flip_into_macro() {
  for (uint8_t v3_preset : {9, 10, 11, 12}) {  // kArp, kFeel, kDynamics, kFlip
    uint8_t blob[128];
    build_v3_blob(blob, v3_preset);
    Config out{};
    auto result = app::config::migrate_v3(blob, sizeof(blob), out);
    ASSERT_EQ(result.status, app::config::DecodeStatus::Valid);
    ASSERT_TRUE(out.active_preset == app::PresetId::kMacro);
  }
}

// Flip (PresetId::kCount) is not a selectable preset: validate() rejects it,
// so it can be neither encoded nor committed.
static void test_flip_preset_is_rejected_by_validate_and_encode() {
  Config c{}; c.active_preset = app::PresetId::kFlip;
  ASSERT_EQ(app::contract::validate(c).error, app::contract::ValidationError::InvalidEnum);
  uint8_t blob[128]{}; size_t n = 0;
  ASSERT_FALSE(app::config::encode(c, blob, sizeof(blob), n));
  ASSERT_EQ(n, 0u);
}

// A v4 blob written while Flip still existed carries preset byte 8. The rest
// of that configuration is kept and the preset falls back to Macro.
static void test_stored_v4_flip_byte_decodes_to_macro() {
  Config in{}; in.velocity = 77; in.joy_families.cont.x_cc = 74;
  uint8_t blob[128]{}; size_t n = 0;
  ASSERT_TRUE(app::config::encode(in, blob, sizeof(blob), n));
  const size_t crc_at = n - 4, preset_at = n - 5;
  ASSERT_EQ(blob[preset_at], static_cast<uint8_t>(app::PresetId::kLead));
  blob[preset_at] = 8;
  write32_test(blob + crc_at, v2_crc32(blob, crc_at));
  Config out{};
  ASSERT_EQ(app::config::decode(blob, n, out).status, app::config::DecodeStatus::Valid);
  ASSERT_TRUE(out.active_preset == app::PresetId::kMacro);
  ASSERT_EQ(out.velocity, 77); ASSERT_EQ(out.joy_families.cont.x_cc, 74u);
  // Anything past the old Flip slot is still corrupt.
  blob[preset_at] = 9;
  write32_test(blob + crc_at, v2_crc32(blob, crc_at));
  ASSERT_EQ(app::config::decode(blob, n, out).status, app::config::DecodeStatus::Corrupt);
}

// What a device with nothing stored boots into, and what Reset restores:
// Settings' own defaults (not Config{}'s member defaults), with the active
// preset's family parameters recalled.
static void test_factory_defaults_are_settings_defaults_with_lead_recalled() {
  const auto c = app::config::factory_defaults();
  ASSERT_EQ(app::contract::validate(c).error, app::contract::ValidationError::None);
  ASSERT_EQ(c.velocity, 110); ASSERT_EQ(c.duration_ms, 240);
  ASSERT_EQ(c.usb_role, app::contract::UsbRole::Device);
  ASSERT_EQ(c.clock_source, app::contract::ClockSource::Auto);
  ASSERT_EQ(c.play_trigger, 1); ASSERT_EQ(c.arp_rate, 1); ASSERT_EQ(c.strum_feel, 14); ASSERT_EQ(c.humanize, 6);
  ASSERT_TRUE(c.ble_enabled);
  ASSERT_EQ(c.key, 0u); ASSERT_EQ(c.scale, 0u);
  ASSERT_STREQ(c.key_name, "C"); ASSERT_STREQ(c.scale_name, "Major");
  ASSERT_TRUE(c.active_preset == app::PresetId::kLead);
  const auto lead = app::preset_defaults(app::PresetId::kLead).notes;
  ASSERT_TRUE(c.joy_families.notes.source == app::NoteSource::kScale);
  ASSERT_TRUE(c.joy_families.notes.gesture == app::NoteGesture::kWalk);
  ASSERT_TRUE(c.joy_families.notes.velocity_mode == app::NoteVelocityMode::kByDistance);
  ASSERT_TRUE(c.joy_families.notes.channel == app::NoteChannel::kLead);
  ASSERT_TRUE(c.joy_families.notes.voices == lead.voices);
  ASSERT_TRUE(c.joy_families.notes.sustain == lead.sustain);
  ASSERT_EQ(c.joy_families.notes.mono, lead.mono);
  ASSERT_EQ(c.joy_families.notes.octave, lead.octave);

  // Field for field the same thing as projecting default Settings.
  const auto f = app::config::from_settings(app::Settings{});
  uint8_t a[128]{}, b[128]{}; size_t na = 0, nb = 0;
  ASSERT_TRUE(app::config::encode(c, a, sizeof(a), na));
  ASSERT_TRUE(app::config::encode(f, b, sizeof(b), nb));
  ASSERT_EQ(na, nb);
  ASSERT_TRUE(std::memcmp(a, b, na) == 0);
}

// A v1 blob has no joystick families at all; the migrated Config must run on
// its (default) preset's own parameters, not the structs' bare member values.
static void test_v1_migration_recalls_lead_and_derives_indexes() {
  app::Settings old{}; old.size = 112;
  std::snprintf(old.key, sizeof(old.key), "Eb"); std::snprintf(old.scale, sizeof(old.scale), "Dorian");
  Config out{}; ASSERT_TRUE(app::config::migrate_v1(old, out));
  ASSERT_EQ(out.key, 3u); ASSERT_STREQ(out.key_name, "D#");
  ASSERT_EQ(out.scale, static_cast<uint8_t>(app::ProgressionMode::kDorian)); ASSERT_STREQ(out.scale_name, "Dorian");
  ASSERT_TRUE(out.active_preset == app::PresetId::kLead);
  ASSERT_TRUE(out.joy_families.notes.source == app::NoteSource::kScale);
  ASSERT_TRUE(out.joy_families.notes.gesture == app::NoteGesture::kWalk);
  ASSERT_TRUE(out.joy_families.notes.velocity_mode == app::NoteVelocityMode::kByDistance);
  ASSERT_TRUE(out.joy_families.notes.channel == app::NoteChannel::kLead);
}

// Every legacy key spelling, sharp or flat, lands on its pitch class.
static void test_from_settings_maps_sharp_and_flat_keys_to_the_same_index() {
  struct Case { const char* name; uint8_t index; };
  const Case cases[] = {{"C", 0}, {"C#", 1}, {"Db", 1}, {"D", 2}, {"Eb", 3}, {"E", 4}, {"F", 5}, {"F#", 6},
                        {"Gb", 6}, {"G", 7}, {"Ab", 8}, {"A", 9}, {"Bb", 10}, {"B", 11}, {"Cb", 11}};
  for (const auto& item : cases) {
    app::Settings s{}; std::snprintf(s.key, sizeof(s.key), "%s", item.name);
    ASSERT_EQ(app::config::from_settings(s).key, item.index);
  }
}

// Config.key / Config.scale are the authority; the names follow them.
static void test_sync_names_rewrites_names_from_the_indexes() {
  Config c{}; c.key = 6; c.scale = static_cast<uint8_t>(app::ProgressionMode::kNeoSoul);
  std::snprintf(c.key_name, sizeof(c.key_name), "Bb"); std::snprintf(c.scale_name, sizeof(c.scale_name), "Stale");
  app::config::sync_names(c);
  ASSERT_STREQ(c.key_name, "F#"); ASSERT_STREQ(c.scale_name, "Neo Soul");
}

// Settings::scale is the ENGINE-facing scale: only the four real non-major
// scales keep their name; Sus2, Sevenths and every genre mode resolve chord
// tones on Major. The Config keeps the display name.
static void test_apply_to_settings_writes_the_engine_scale_and_key_from_index() {
  using M = app::ProgressionMode;
  for (uint8_t i = 0; i < static_cast<uint8_t>(M::kCOUNT); ++i) {
    Config c{}; c.scale = i; c.key = 8; app::config::sync_names(c);
    app::Settings s{}; ASSERT_TRUE(app::config::apply_to_settings(c, s));
    const auto mode = static_cast<M>(i);
    const bool own = mode == M::kNaturalMinor || mode == M::kHarmonicMinor || mode == M::kDorian || mode == M::kMixolydian;
    ASSERT_STREQ(s.scale, own ? app::progression_mode_full_name(mode) : "Major");
    ASSERT_STREQ(s.key, "G#");
    ASSERT_STREQ(c.scale_name, app::progression_mode_full_name(mode));
  }
  Config jazz{}; jazz.scale = static_cast<uint8_t>(M::kJazz); app::config::sync_names(jazz);
  app::Settings s{}; ASSERT_TRUE(app::config::apply_to_settings(jazz, s));
  ASSERT_STREQ(s.scale, "Major"); ASSERT_STREQ(jazz.scale_name, "Jazz");
}

static void test_migrate_v3_rejects_v4_blob() {
  Config in{}; uint8_t blob[128]{}; size_t n = 0;
  ASSERT_TRUE(app::config::encode(in, blob, sizeof(blob), n));
  Config out{};
  ASSERT_EQ(app::config::migrate_v3(blob, n, out).status, app::config::DecodeStatus::Unsupported);
}

static void test_migrate_v2_rejects_v3_blob() {
  Config in{}; uint8_t blob[128]{}; size_t n = 0;
  ASSERT_TRUE(app::config::encode(in, blob, sizeof(blob), n));
  Config out{};
  ASSERT_EQ(app::config::migrate_v2(blob, n, out).status, app::config::DecodeStatus::Unsupported);
}

// A real v2 blob predates Roll entirely, so its bytes 81-91 are unspecified
// padding, not roll data — migrate_v2() must default roll_ms/roll_dir to 0
// rather than reading that padding as if it meant something.
static void test_migrate_v2_defaults_roll_to_off() {
  uint8_t blob[96];
  build_v2_blob(blob, app::contract::RecipeId::Drone);
  // Poison the padding bytes roll_ms/roll_dir will live at in v3, then
  // recompute the CRC over the modified bytes, to prove migrate_v2() does
  // NOT interpret this padding as roll data.
  for (int i = 81; i < 92; ++i) blob[i] = 0xAA;
  write32_test(blob + 92, v2_crc32(blob, 92));
  Config out{};
  auto migrated = app::config::migrate_v2(blob, sizeof(blob), out);
  ASSERT_EQ(migrated.status, app::config::DecodeStatus::Valid);
  ASSERT_EQ(out.roll_ms, 0); ASSERT_EQ(out.roll_dir, 0);
}
int main() { TEST_SUITE("Config v4 codec and v1/v2/v3 migration"); RUN_TEST(test_v3_roundtrip); RUN_TEST(test_corruption_and_unknown_are_protected); RUN_TEST(test_v1_migration_preserves_arp_and_usb_policy); RUN_TEST(test_v3_roundtrip_preserves_complete_persistent_preferences); RUN_TEST(test_harp_legacy_is_explicitly_unsupported); RUN_TEST(test_runtime_projection_roundtrip_preserves_persisted_fields); RUN_TEST(test_nonfinite_legacy_is_rejected); RUN_TEST(test_v4_roundtrip_preserves_joy_families); RUN_TEST(test_v3_roundtrip_accepts_every_progression_mode_scale); RUN_TEST(test_validate_rejects_scale_past_the_last_progression_mode); RUN_TEST(test_from_settings_maps_scale_name_to_progression_mode_index); RUN_TEST(test_from_settings_defaults_unknown_scale_name_to_major); RUN_TEST(test_migrate_v3_maps_every_legacy_preset_to_a_valid_one); RUN_TEST(test_migrate_v3_folds_legacy_harmony_into_drone); RUN_TEST(test_migrate_v3_folds_legacy_arp_feel_dynamics_flip_into_macro); RUN_TEST(test_flip_preset_is_rejected_by_validate_and_encode); RUN_TEST(test_stored_v4_flip_byte_decodes_to_macro); RUN_TEST(test_factory_defaults_are_settings_defaults_with_lead_recalled); RUN_TEST(test_v1_migration_recalls_lead_and_derives_indexes); RUN_TEST(test_from_settings_maps_sharp_and_flat_keys_to_the_same_index); RUN_TEST(test_sync_names_rewrites_names_from_the_indexes); RUN_TEST(test_apply_to_settings_writes_the_engine_scale_and_key_from_index); RUN_TEST(test_migrate_v3_rejects_v4_blob); RUN_TEST(test_migrate_v2_maps_recipe_to_preset); RUN_TEST(test_migrate_v2_maps_every_recipe_to_its_preset); RUN_TEST(test_migrate_v2_rejects_v3_blob); RUN_TEST(test_migrate_v2_defaults_roll_to_off); test_print_result(); return g_fail ? 1 : 0; }
