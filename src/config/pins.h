#pragma once

#include "driver/gpio.h"
#include "driver/adc.h"
#include "driver/i2c.h"

// Hardware pin map.
// Change these constants to match your wiring.
namespace config {

// OLED SSD1315 I2C
static constexpr i2c_port_t kI2cPort = I2C_NUM_0;
static constexpr gpio_num_t kI2cSda = GPIO_NUM_8;
static constexpr gpio_num_t kI2cScl = GPIO_NUM_9;
static constexpr uint32_t kI2cHz = 100000;
static constexpr uint8_t kOledI2cAddr = 0x3C;

// Analog joystick
static constexpr adc_unit_t kJoyAdcUnit = ADC_UNIT_1;
static constexpr gpio_num_t kJoyPinX = GPIO_NUM_1;
static constexpr gpio_num_t kJoyPinY = GPIO_NUM_2;
static constexpr adc_channel_t kJoyChanX = ADC_CHANNEL_0;  // GPIO1 on ESP32-S3 (ADC1_CH0)
static constexpr adc_channel_t kJoyChanY = ADC_CHANNEL_1;  // GPIO2 on ESP32-S3 (ADC1_CH1)
// Joystick push switch, active-low. Nothing is wired to this pin (this
// hardware has no physical joystick button) and process_joystick() does not
// read it. Kept as documentation of which GPIO the driver would use if a
// switch were ever added, not as a live control input.
static constexpr gpio_num_t kJoyPinSw = GPIO_NUM_21;

// 4x4 keypad matrix (rows + cols)
static constexpr gpio_num_t kKeypadRows[4] = {GPIO_NUM_10, GPIO_NUM_11, GPIO_NUM_12, GPIO_NUM_13};
static constexpr gpio_num_t kKeypadCols[4] = {GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18};

// Rotary encoder, active-low switch, common terminal wired to GND.
static constexpr gpio_num_t kRotaryPinA = GPIO_NUM_4;
static constexpr gpio_num_t kRotaryPinB = GPIO_NUM_5;
static constexpr gpio_num_t kRotaryPinSw = GPIO_NUM_6;
static constexpr bool kRotaryInvert = false;

// Dedicated UI buttons, active-low, common terminal wired to GND.
static constexpr gpio_num_t kUiButton1 = GPIO_NUM_7;
static constexpr gpio_num_t kUiButton2 = GPIO_NUM_14;

}  // namespace config
