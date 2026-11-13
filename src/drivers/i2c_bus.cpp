#include "drivers/i2c_bus.h"

#include "esp_log.h"

namespace drivers {

static const char* TAG = "I2C";

bool I2cBus::begin(const I2cBusConfig& cfg) {
  cfg_ = cfg;
  if (initialized_) {
    return true;
  }

  i2c_config_t conf = {};
  conf.mode = I2C_MODE_MASTER;
  conf.sda_io_num = cfg_.sda;
  conf.scl_io_num = cfg_.scl;
  conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
  conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
  conf.master.clk_speed = cfg_.hz;

  esp_err_t err = i2c_param_config(cfg_.port, &conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
    return false;
  }

  err = i2c_driver_install(cfg_.port, conf.mode, 0, 0, 0);
  if (err == ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "I2C driver already installed, continuing");
  } else if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
    return false;
  }

  initialized_ = true;
  ESP_LOGI(TAG, "I2C init OK (port=%d SDA=%d SCL=%d %u Hz)", (int)cfg_.port, (int)cfg_.sda, (int)cfg_.scl, cfg_.hz);
  return true;
}

void I2cBus::reset() {
  if (!initialized_) return;
  const esp_err_t err = i2c_driver_delete(cfg_.port);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "i2c_driver_delete failed: %s", esp_err_to_name(err));
  }
  initialized_ = false;
}

}  // namespace drivers
