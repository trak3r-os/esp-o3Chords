#pragma once

#include "app/contracts/contracts.h"
#include "app/state.h"
#include <cstddef>
#include <cstdint>

namespace app::config {

constexpr uint32_t kMagic = 0x4F334346;  // O3CF
constexpr uint16_t kVersion = 4;

enum class DecodeStatus : uint8_t { Valid, Missing, Corrupt, Unsupported };
enum class MigrationStatus : uint8_t { Valid, Corrupt, Unsupported };

struct DecodeResult {
  DecodeStatus status = DecodeStatus::Corrupt;
  uint16_t source_version = 0;
};

// Wire format is explicit and independent from Settings ABI/padding.
size_t encoded_size();
bool encode(const contract::Config& config, uint8_t* buffer, size_t capacity,
            size_t& written);
DecodeResult decode(const uint8_t* buffer, size_t size, contract::Config& out);

// v2 -> v3: the joystick family model grew the wire format to carry all
// four JoyFamilyState parameter sets plus the active preset, which no v2
// blob has. Call this when decode() reports a v2 source_version on an
// otherwise-valid blob; it derives family parameters from the old recipe_id
// via the migration table (preset_for_recipe_v2()).
DecodeResult migrate_v2(const uint8_t* buffer, size_t size, contract::Config& out);

// v3 -> v4: the v4 format has four fewer ChordParams fields and four fewer
// presets, so the wire format is smaller. Call this when decode() reports a
// v3 source_version on an otherwise-valid blob.
DecodeResult migrate_v3(const uint8_t* buffer, size_t size, contract::Config& out);

// Maps a v2 RecipeId onto the family/preset that absorbed it. Shared by
// migrate_v2() and by the production Recipe-slot selector
// (input_context.cpp), which resolves the legacy JoystickPlayMode
// compatibility fixtures the same way.
app::PresetId preset_for_recipe_v2(contract::RecipeId recipe);

// Recalls preset `id`: overwrites only the family it belongs to, leaving the
// other three families' live tuning untouched (persisting all four families
// is what makes this safe).
void recall_preset(app::JoyFamilyState& state, app::PresetId id);

// The only legacy adapter: v1 Settings enters the canonical Config model.
bool migrate_v1(const Settings& legacy, contract::Config& out);
MigrationStatus migrate_v1_result(const Settings& legacy, contract::Config& out);
contract::Config from_settings(const Settings& settings);
bool apply_to_settings(const contract::Config& config, Settings& settings);

// What a device with nothing stored boots into, and what Reset restores:
// Settings' own defaults projected into Config, with the active preset's
// family parameters recalled.
contract::Config factory_defaults();

// `key`/`scale` are the authority; `key_name`/`scale_name` are their display
// strings. Rewrites the names from the indexes so the two can never disagree.
void sync_names(contract::Config& config);

}  // namespace app::config
