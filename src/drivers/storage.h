#pragma once

#include <stddef.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "app/config_codec.h"

namespace drivers {

class Storage {
 public:
  bool begin();
  bool mount();

  // Settings persistence
  bool save_settings(const void* settings_blob, size_t size);
  bool load_settings(void* settings_blob, size_t size);

  // Canonical Config v2. The legacy settings key is read-only here so an
  // unsuccessful migration can never overwrite the user's original blob.
  app::contract::LoadResult load_config();
  app::contract::SaveResult save_config(const app::contract::Config& config);

  // Preset management
  bool save_preset(const char* name, const void* data, size_t size);
  bool load_preset(const char* name, void* buffer, size_t max_size, size_t& out_size);
  bool delete_preset(const char* name);
  bool list_presets(char** names, size_t max_count, size_t& out_count);

  // Factory reset
  bool factory_reset();

  // Saving is refused after a load found stored settings it could not read
  // (corrupt, or written by a newer firmware), so they are not overwritten
  // behind the user's back. A deliberate Reset is the user's decision to
  // replace them: allow_overwrite() lifts the block.
  bool writes_blocked() const { return settings_write_blocked_; }
  void allow_overwrite() { settings_write_blocked_ = false; }

 private:
  nvs_handle_t nvs_handle_ = 0;
  static constexpr const char* kNvsNamespace = "chord_esp";
  static constexpr const char* kSettingsKey = "settings";
  static constexpr const char* kConfigV2Key = "settings_v2";
  bool settings_write_blocked_ = false;
};

}  // namespace drivers
