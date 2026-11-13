#pragma once
#include "FreeRTOS.h"
#include <mutex>

// Use a plain std::recursive_mutex so tests can call take/give from the same thread.
struct StubMutex {
  std::recursive_mutex mtx;
};

typedef StubMutex* SemaphoreHandle_t;

static inline SemaphoreHandle_t xSemaphoreCreateMutex() {
  return new StubMutex();
}
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t) {
  if (!s) return pdFALSE;
  s->mtx.lock();
  return pdTRUE;
}
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t s) {
  if (!s) return pdFALSE;
  s->mtx.unlock();
  return pdTRUE;
}
static inline void vSemaphoreDelete(SemaphoreHandle_t s) {
  delete s;
}
