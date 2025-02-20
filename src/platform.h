#ifndef ZTRACING_SRC_PLATFORM_H_
#define ZTRACING_SRC_PLATFORM_H_

#include "src/types.h"

u64 platform_get_perf_counter(void);
u64 platform_get_perf_freq(void);

#endif  // ZTRACING_SRC_PLATFORM_H_
