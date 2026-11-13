// Definitions for stub global state (linked once into the test binary).
#include <cstdint>
#include <vector>

// Fake clock — tests set this to control esp_timer_get_time().
uint64_t g_test_time_us = 0;

// All timer handles created during a test run.
struct EspTimerStub;
std::vector<EspTimerStub*> g_test_timers;
