#ifndef ZTRACING_SRC_ZTRACING_H_
#define ZTRACING_SRC_ZTRACING_H_

#include "src/types.h"

u64 GetPerformanceCounter(void);
u64 GetPerformanceFrequency(void);

void DoFrame(void);

#endif  // ZTRACING_SRC_ZTRACING_H_
