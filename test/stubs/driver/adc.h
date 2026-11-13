#pragma once
#include <cstdint>
// Minimal ESP-IDF ADC driver stub for native host builds.
typedef uint8_t  adc_unit_t;
typedef uint8_t  adc_channel_t;
typedef uint8_t  adc_atten_t;
typedef uint8_t  adc_bits_width_t;
#define ADC_UNIT_1          ((adc_unit_t)1)
#define ADC_CHANNEL_0       ((adc_channel_t)0)
#define ADC_CHANNEL_1       ((adc_channel_t)1)
#define ADC_ATTEN_DB_11     ((adc_atten_t)3)
#define ADC_WIDTH_BIT_12    ((adc_bits_width_t)3)
static inline int adc1_config_width(adc_bits_width_t) { return 0; }
static inline int adc1_config_channel_atten(adc_channel_t, adc_atten_t) { return 0; }
static inline int adc1_get_raw(adc_channel_t) { return 2048; }  // mid-scale
