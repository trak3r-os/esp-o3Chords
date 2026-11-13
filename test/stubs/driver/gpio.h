#pragma once
#include <cstdint>
// Minimal ESP-IDF GPIO driver stub for native host builds.
typedef uint8_t gpio_num_t;
typedef uint8_t gpio_mode_t;
typedef uint8_t gpio_pull_mode_t;
#define GPIO_MODE_INPUT        ((gpio_mode_t)0x01)
#define GPIO_PULLUP_ENABLE     ((gpio_pull_mode_t)0x01)
#define GPIO_PULLDOWN_DISABLE  ((gpio_pull_mode_t)0x00)
#define GPIO_NUM_NC            ((gpio_num_t)0xFF)
static inline int gpio_set_direction(gpio_num_t, gpio_mode_t) { return 0; }
static inline int gpio_set_pull_mode(gpio_num_t, gpio_pull_mode_t) { return 0; }
static inline int gpio_get_level(gpio_num_t) { return 0; }
static inline int gpio_set_level(gpio_num_t, uint32_t) { return 0; }
