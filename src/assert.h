#ifndef ZTRACING_SRC_ASSERT_H_
#define ZTRACING_SRC_ASSERT_H_

#include <stdlib.h>

#include "src/logging.h"

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#ifndef CHECK
#define CHECK(cond)                                                      \
  do {                                                                   \
    if (unlikely(!(cond))) {                                             \
      LOG_ERROR("CHECK failed: %s at %s:%d", #cond, __FILE__, __LINE__); \
      abort();                                                           \
    }                                                                    \
  } while (0)
#endif

#endif  // ZTRACING_SRC_ASSERT_H_
