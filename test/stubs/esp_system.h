#pragma once
#include <cstdint>
#include <cstdlib>  // for rand()
// esp_system.h stub — minimal surface needed for native host build.

static inline uint32_t esp_get_free_heap_size() { return 200000; }
static inline void esp_restart() {}
