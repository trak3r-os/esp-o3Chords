#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <functional>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal self-contained test framework — no external dependencies.
// ---------------------------------------------------------------------------

static int g_pass = 0;
static int g_fail = 0;
static const char* g_current_test = nullptr;

static inline void test_print_result() {
  printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
}

#define ASSERT_EQ(a, b) do { \
  auto _a = (a); auto _b = (b); \
  if (_a != _b) { \
    fprintf(stderr, "  FAIL %s:%d  %s == %s  (%lld != %lld)\n", \
            __FILE__, __LINE__, #a, #b, (long long)_a, (long long)_b); \
    ++g_fail; return; \
  } \
} while(0)

#define ASSERT_NE(a, b) do { \
  auto _a = (a); auto _b = (b); \
  if (_a == _b) { \
    fprintf(stderr, "  FAIL %s:%d  %s != %s  (both = %lld)\n", \
            __FILE__, __LINE__, #a, #b, (long long)_a); \
    ++g_fail; return; \
  } \
} while(0)

#define ASSERT_TRUE(x) do { \
  if (!(x)) { \
    fprintf(stderr, "  FAIL %s:%d  expected true: %s\n", __FILE__, __LINE__, #x); \
    ++g_fail; return; \
  } \
} while(0)

#define ASSERT_FALSE(x) do { \
  if ((x)) { \
    fprintf(stderr, "  FAIL %s:%d  expected false: %s\n", __FILE__, __LINE__, #x); \
    ++g_fail; return; \
  } \
} while(0)

#define ASSERT_STREQ(a, b) do { \
  if (strcmp((a),(b)) != 0) { \
    fprintf(stderr, "  FAIL %s:%d  \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); \
    ++g_fail; return; \
  } \
} while(0)

#define ASSERT_GT(a, b) do { \
  auto _a = (a); auto _b = (b); \
  if (!(_a > _b)) { \
    fprintf(stderr, "  FAIL %s:%d  expected %s > %s  (%lld <= %lld)\n", \
            __FILE__, __LINE__, #a, #b, (long long)_a, (long long)_b); \
    ++g_fail; return; \
  } \
} while(0)

#define ASSERT_GE(a, b) do { \
  auto _a = (a); auto _b = (b); \
  if (!(_a >= _b)) { \
    fprintf(stderr, "  FAIL %s:%d  expected %s >= %s  (%lld < %lld)\n", \
            __FILE__, __LINE__, #a, #b, (long long)_a, (long long)_b); \
    ++g_fail; return; \
  } \
} while(0)

#define ASSERT_LT(a, b) do { \
  auto _a = (a); auto _b = (b); \
  if (!(_a < _b)) { \
    fprintf(stderr, "  FAIL %s:%d  expected %s < %s  (%lld >= %lld)\n", \
            __FILE__, __LINE__, #a, #b, (long long)_a, (long long)_b); \
    ++g_fail; return; \
  } \
} while(0)

#define ASSERT_LE(a, b) do { \
  auto _a = (a); auto _b = (b); \
  if (!(_a <= _b)) { \
    fprintf(stderr, "  FAIL %s:%d  expected %s <= %s  (%lld > %lld)\n", \
            __FILE__, __LINE__, #a, #b, (long long)_a, (long long)_b); \
    ++g_fail; return; \
  } \
} while(0)

#define ASSERT_NEAR(a, b, eps) do { \
  auto _a = (double)(a); auto _b = (double)(b); auto _e = (double)(eps); \
  if ((_a - _b > _e) || (_b - _a > _e)) { \
    fprintf(stderr, "  FAIL %s:%d  |%s - %s| > %s  (|%g - %g| > %g)\n", \
            __FILE__, __LINE__, #a, #b, #eps, _a, _b, _e); \
    ++g_fail; return; \
  } \
} while(0)

#define RUN_TEST(fn) do { \
  g_current_test = #fn; \
  printf("  TEST %-60s", #fn); fflush(stdout); \
  int _before = g_fail; \
  fn(); \
  if (g_fail == _before) { printf(" OK\n"); ++g_pass; } \
  else { printf(" FAILED\n"); } \
} while(0)

#define TEST_SUITE(name) \
  printf("\n--- %s ---\n", name)
