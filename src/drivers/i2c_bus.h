#pragma once

#include "driver/gpio.h"
#include "driver/i2c.h"

namespace drivers {

struct I2cBusConfig {
  i2c_port_t port = I2C_NUM_0;
  gpio_num_t sda = GPIO_NUM_NC;
  gpio_num_t scl = GPIO_NUM_NC;
  uint32_t hz = 400000;
};

class I2cBus {
 public:
  bool begin(const I2cBusConfig& cfg);
  void reset();
  i2c_port_t port() const { return cfg_.port; }

 private:
  I2cBusConfig cfg_{};
  bool initialized_ = false;
};

}  // namespace drivers
