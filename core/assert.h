#ifndef CORE_ASSERT_H
#define CORE_ASSERT_H

#include <stdlib.h>

#include "core/logging.h"

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

#endif  // CORE_ASSERT_H
