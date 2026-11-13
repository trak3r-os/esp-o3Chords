#pragma once
#include "esp_err.h"
#include <cstdint>
#include <functional>
#include <vector>

// Controllable fake clock — tests advance this to simulate time passing.
extern uint64_t g_test_time_us;
static inline uint64_t esp_timer_get_time() { return g_test_time_us; }

// Minimal timer handle — stores callback + arg so tests can fire it manually.
struct EspTimerStub {
  void (*callback)(void*) = nullptr;
  void* arg = nullptr;
  bool running = false;
  uint64_t fire_at_us = 0;
};

typedef EspTimerStub* esp_timer_handle_t;

// Global registry so the test harness can fire pending timers.
extern std::vector<EspTimerStub*> g_test_timers;

#define ESP_TIMER_TASK 0
struct esp_timer_create_args_t {
  void (*callback)(void*);
  void* arg;
  const char* name;
  int dispatch_method;
};

static inline esp_err_t esp_timer_create(const esp_timer_create_args_t* args,
                                          esp_timer_handle_t* out) {
  auto* t = new EspTimerStub();
  t->callback = args->callback;
  t->arg = args->arg;
  g_test_timers.push_back(t);
  *out = t;
  return ESP_OK;
}
static inline esp_err_t esp_timer_start_once(esp_timer_handle_t t, uint64_t us) {
  if (!t) return ESP_FAIL;
  t->running = true;
  t->fire_at_us = g_test_time_us + us;
  return ESP_OK;
}
static inline esp_err_t esp_timer_stop(esp_timer_handle_t t) {
  if (t) t->running = false;
  return ESP_OK;
}
static inline esp_err_t esp_timer_delete(esp_timer_handle_t t) {
  if (t) {
    for (auto it = g_test_timers.begin(); it != g_test_timers.end(); ++it) {
      if (*it == t) { g_test_timers.erase(it); break; }
    }
    delete t;
  }
  return ESP_OK;
}

// Fire all timers whose deadline has passed.  Call after advancing g_test_time_us.
static inline void esp_timer_fire_pending() {
  for (auto* t : g_test_timers) {
    if (t->running && g_test_time_us >= t->fire_at_us) {
      t->running = false;
      if (t->callback) t->callback(t->arg);
    }
  }
}
