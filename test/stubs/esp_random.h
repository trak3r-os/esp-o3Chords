#pragma once
#include <cstdint>
#include <cstdlib>
// Use std::rand as a deterministic substitute for esp_random() in native tests.
static inline uint32_t esp_random() { return static_cast<uint32_t>(rand()); }
