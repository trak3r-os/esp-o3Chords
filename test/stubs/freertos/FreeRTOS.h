#pragma once
#include <cstdint>

typedef long         BaseType_t;
typedef uint32_t     TickType_t;
typedef unsigned long UBaseType_t;

#define portMAX_DELAY    ((TickType_t)0xFFFFFFFFUL)
#define pdTRUE           ((BaseType_t)1)
#define pdFALSE          ((BaseType_t)0)
#define pdPASS           pdTRUE
#define pdFAIL           pdFALSE

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
