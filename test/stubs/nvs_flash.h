#pragma once
#include <cstdint>
#include "esp_err.h"
namespace nvs_flash_stub { inline esp_err_t init_error = ESP_OK; inline bool erased = false; }
static inline esp_err_t nvs_flash_init() { return nvs_flash_stub::init_error; }
static inline esp_err_t nvs_flash_erase() { return 0; }
