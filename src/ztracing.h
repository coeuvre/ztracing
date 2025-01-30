#ifndef ZTRACING_SRC_ZTRACING_H_
#define ZTRACING_SRC_ZTRACING_H_

#include "src/types.h"

u64 get_perf_counter(void);
u64 get_perf_freq(void);

void do_frame(void);

#endif  // ZTRACING_SRC_ZTRACING_H_
