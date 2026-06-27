#ifndef CORE_ASSERT_H
#define CORE_ASSERT_H

#include <stdio.h>
#include <stdlib.h>

#if defined(__GNUC__) || defined(__clang__)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely(x) __builtin_expect(!!(x), 1)
#else
#define unlikely(x) (x)
#define likely(x) (x)
#endif

#define panic(format, ...)                                \
  do {                                                    \
    fprintf(stderr, "%s:%d: " format "\n", __FILE_NAME__, \
            __LINE__ __VA_OPT__(, ) __VA_ARGS__);         \
    abort();                                              \
  } while (0)

#define expect(cond)                     \
  do {                                   \
    if (unlikely(!(cond))) {             \
      panic("expect failed: %s", #cond); \
    }                                    \
  } while (0)

#endif  // CORE_ASSERT_H
