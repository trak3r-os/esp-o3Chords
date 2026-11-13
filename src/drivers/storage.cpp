#include "drivers/storage.h"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "Storage";

namespace drivers {

bool Storage::begin() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    settings_write_blocked_ = true;
    ESP_LOGE(TAG, "NVS version/space error; refusing automatic erase");
    return false;
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NVS flash init failed: %s", esp_err_to_name(err));
    return false;
  }

  err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
    return false;
  }

  ESP_LOGI(TAG, "NVS initialized successfully");
  return true;
}

bool Storage::mount() {
  return true;
}

bool Storage::save_settings(const void* settings_blob, size_t size) {
  if (settings_write_blocked_) {
    ESP_LOGW(TAG, "Refusing legacy settings write after unknown/corrupt blob");
    return false;
  }
  if (nvs_handle_ == 0) {
    ESP_LOGE(TAG, "NVS not initialized");
    return false;
  }

  esp_err_t err = nvs_set_blob(nvs_handle_, kSettingsKey, settings_blob, size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save settings: %s", esp_err_to_name(err));
    return false;
  }

  err = nvs_commit(nvs_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to commit settings: %s", esp_err_to_name(err));
    return false;
  }

  ESP_LOGI(TAG, "Settings saved successfully (%zu bytes)", size);
  return true;
}

bool Storage::load_settings(void* settings_blob, size_t size) {
  if (nvs_handle_ == 0) {
    ESP_LOGE(TAG, "NVS not initialized");
    return false;
  }

  size_t required_size = 0;
  esp_err_t err = nvs_get_blob(nvs_handle_, kSettingsKey, nullptr, &required_size);

  if (err == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGI(TAG, "No settings found in NVS, using defaults");
    return false;
  }

  if (err != ESP_OK) {
    settings_write_blocked_ = true;
    ESP_LOGE(TAG, "Failed to query settings size: %s", esp_err_to_name(err));
    return false;
  }

  if (required_size == size) {
    // Exact match — load directly.
    err = nvs_get_blob(nvs_handle_, kSettingsKey, settings_blob, &required_size);
    if (err != ESP_OK) {
      settings_write_blocked_ = true;
      ESP_LOGE(TAG, "Failed to load settings: %s", esp_err_to_name(err));
      return false;
    }
  } else {
    // The legacy ABI is frozen. Never reinterpret a blob from another layout.
    settings_write_blocked_ = true;
    ESP_LOGW(TAG, "Unsupported legacy settings layout (%zu, expected %zu)", required_size, size);
    return false;
  }

  const auto* header = static_cast<const uint8_t*>(settings_blob);
  const uint32_t magic = uint32_t(header[0]) | uint32_t(header[1]) << 8 |
                         uint32_t(header[2]) << 16 | uint32_t(header[3]) << 24;
  const uint16_t version = uint16_t(header[4]) | uint16_t(header[5]) << 8;
  const uint16_t stored_size = uint16_t(header[6]) | uint16_t(header[7]) << 8;
  if (magic != 0x4F334348 || version != 1 || stored_size != size) {
    settings_write_blocked_ = true;
    ESP_LOGW(TAG, "Legacy settings envelope is unsupported or corrupt");
    return false;
  }

  ESP_LOGI(TAG, "Settings loaded successfully (%zu bytes from NVS, %zu expected)", required_size, size);
  return true;
}

app::contract::LoadResult Storage::load_config() {
  app::contract::LoadResult result{};
  if (nvs_handle_ == 0) { result.status = app::contract::LoadStatus::IOError; return result; }
  size_t size = 0;
  esp_err_t err = nvs_get_blob(nvs_handle_, kConfigV2Key, nullptr, &size);
  if (err == ESP_OK) {
    uint8_t blob[128]{};
    if (size > sizeof(blob)) { settings_write_blocked_ = true; result.status = app::contract::LoadStatus::Unsupported; return result; }
    size_t read = size;
    if (nvs_get_blob(nvs_handle_, kConfigV2Key, blob, &read) != ESP_OK) { settings_write_blocked_ = true; result.status = app::contract::LoadStatus::IOError; return result; }
    auto decoded = app::config::decode(blob, read, result.config);
    bool migrated = false;
    if (decoded.status == app::config::DecodeStatus::Unsupported && decoded.source_version == 3) {
      // v3 -> v4: the v4 wire format is smaller (fewer joystick fields and
      // presets). A v3 blob is still a valid, readable config —
      // migrate it rather than rejecting it outright.
      decoded = app::config::migrate_v3(blob, read, result.config);
      migrated = decoded.status == app::config::DecodeStatus::Valid;
    } else if (decoded.status == app::config::DecodeStatus::Unsupported && decoded.source_version == 2) {
      // v2 -> v4 (through v3's migration table): v3 grew the wire format to
      // carry the joystick family parameters. A v2 blob is still a valid,
      // readable config — migrate it rather than rejecting it outright.
      decoded = app::config::migrate_v2(blob, read, result.config);
      migrated = decoded.status == app::config::DecodeStatus::Valid;
    }
    result.source_version = decoded.source_version;
    result.status = migrated ? app::contract::LoadStatus::Migrated :
                     decoded.status == app::config::DecodeStatus::Valid ? app::contract::LoadStatus::Valid :
                     decoded.status == app::config::DecodeStatus::Unsupported ? app::contract::LoadStatus::Unsupported : app::contract::LoadStatus::Corrupt;
    if (result.status == app::contract::LoadStatus::Unsupported || result.status == app::contract::LoadStatus::Corrupt)
      settings_write_blocked_ = true;
    return result;
  }
  if (err != ESP_ERR_NVS_NOT_FOUND) { settings_write_blocked_ = true; result.status = app::contract::LoadStatus::IOError; return result; }

  app::Settings legacy{};
  if (!load_settings(&legacy, sizeof(legacy))) {
    result.status = settings_write_blocked_ ? app::contract::LoadStatus::IOError : app::contract::LoadStatus::Missing;
    return result;
  }
  const auto migration = app::config::migrate_v1_result(legacy, result.config);
  if (migration != app::config::MigrationStatus::Valid) {
    result.status = migration == app::config::MigrationStatus::Unsupported
                        ? app::contract::LoadStatus::Unsupported
                        : app::contract::LoadStatus::Corrupt;
    settings_write_blocked_ = true;
    return result;
  }
  result.source_version = legacy.version;
  result.status = app::contract::LoadStatus::Migrated;
  return result;
}

app::contract::SaveResult Storage::save_config(const app::contract::Config& config) {
  app::contract::SaveResult result{};
  if (settings_write_blocked_) { result.status = app::contract::SaveStatus::Unsupported; return result; }
  if (nvs_handle_ == 0) { result.status = app::contract::SaveStatus::IOError; return result; }
  uint8_t blob[128]{}; size_t size = 0;
  if (!app::config::encode(config, blob, sizeof(blob), size)) { result.status = app::contract::SaveStatus::Rejected; return result; }
  if (nvs_set_blob(nvs_handle_, kConfigV2Key, blob, size) != ESP_OK || nvs_commit(nvs_handle_) != ESP_OK) {
    result.status = app::contract::SaveStatus::IOError; return result;
  }
  result.status = app::contract::SaveStatus::Saved; result.revision = config.revision; return result;
}

bool Storage::save_preset(const char* name, const void* data, size_t size) {
  if (nvs_handle_ == 0) {
    ESP_LOGE(TAG, "NVS not initialized");
    return false;
  }

  char key[32];
  snprintf(key, sizeof(key), "preset_%s", name);

  esp_err_t err = nvs_set_blob(nvs_handle_, key, data, size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to save preset '%s': %s", name, esp_err_to_name(err));
    return false;
  }

  err = nvs_commit(nvs_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to commit preset '%s': %s", name, esp_err_to_name(err));
    return false;
  }

  ESP_LOGI(TAG, "Preset '%s' saved successfully", name);
  return true;
}

bool Storage::load_preset(const char* name, void* buffer, size_t max_size, size_t& out_size) {
  if (nvs_handle_ == 0) {
    ESP_LOGE(TAG, "NVS not initialized");
    return false;
  }

  char key[32];
  snprintf(key, sizeof(key), "preset_%s", name);

  size_t required_size = 0;
  esp_err_t err = nvs_get_blob(nvs_handle_, key, nullptr, &required_size);

  if (err == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGW(TAG, "Preset '%s' not found", name);
    return false;
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to query preset '%s' size: %s", name, esp_err_to_name(err));
    return false;
  }

  if (required_size > max_size) {
    ESP_LOGE(TAG, "Preset '%s' too large (%zu > %zu)", name, required_size, max_size);
    return false;
  }

  err = nvs_get_blob(nvs_handle_, key, buffer, &required_size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to load preset '%s': %s", name, esp_err_to_name(err));
    return false;
  }

  out_size = required_size;
  ESP_LOGI(TAG, "Preset '%s' loaded successfully (%zu bytes)", name, required_size);
  return true;
}

bool Storage::delete_preset(const char* name) {
  if (nvs_handle_ == 0) {
    ESP_LOGE(TAG, "NVS not initialized");
    return false;
  }

  char key[32];
  snprintf(key, sizeof(key), "preset_%s", name);

  esp_err_t err = nvs_erase_key(nvs_handle_, key);
  if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGE(TAG, "Failed to delete preset '%s': %s", name, esp_err_to_name(err));
    return false;
  }

  err = nvs_commit(nvs_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to commit delete of preset '%s': %s", name, esp_err_to_name(err));
    return false;
  }

  ESP_LOGI(TAG, "Preset '%s' deleted successfully", name);
  return true;
}

bool Storage::list_presets(char** names, size_t max_count, size_t& out_count) {
  if (nvs_handle_ == 0) {
    ESP_LOGE(TAG, "NVS not initialized");
    return false;
  }

  out_count = 0;
  nvs_iterator_t it = nullptr;
  esp_err_t err = nvs_entry_find(NVS_DEFAULT_PART_NAME, kNvsNamespace, NVS_TYPE_BLOB, &it);

  if (err != ESP_OK) {
    ESP_LOGI(TAG, "No presets found");
    return true;
  }

  while (it != nullptr && out_count < max_count) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);

    // Check if this is a preset key (starts with "preset_")
    if (strncmp(info.key, "preset_", 7) == 0) {
      // Copy preset name (skip "preset_" prefix)
      strncpy(names[out_count], info.key + 7, 31);
      names[out_count][31] = '\0';
      out_count++;
    }

    err = nvs_entry_next(&it);
    if (err != ESP_OK) {
      break;
    }
  }

  if (it != nullptr) {
    nvs_release_iterator(it);
  }

  ESP_LOGI(TAG, "Found %zu presets", out_count);
  return true;
}

bool Storage::factory_reset() {
  if (nvs_handle_ == 0) {
    ESP_LOGE(TAG, "NVS not initialized");
    return false;
  }

  esp_err_t err = nvs_erase_all(nvs_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(err));
    return false;
  }

  err = nvs_commit(nvs_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to commit factory reset: %s", esp_err_to_name(err));
    return false;
  }

  ESP_LOGI(TAG, "Factory reset completed successfully");
  return true;
}

}  // namespace drivers
