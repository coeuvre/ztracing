#include <SDL3/SDL.h>

#include "src/platform.h"
#include "src/types.h"

u64 platform_get_perf_counter(void) {
  u64 result = SDL_GetPerformanceCounter();
  return result;
}

u64 platform_get_perf_freq(void) {
  u64 result = SDL_GetPerformanceFrequency();
  return result;
}
