#include <stdbool.h>
#include <time.h>

#include "src/platform.h"

double platform_get_now() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  double now_ms = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
  return now_ms;
}

bool platform_is_dark_mode() {
  bool dark_mode = true;  // Default to dark mode for native
  return dark_mode;
}

bool platform_is_mac() {
  bool is_mac = false;
#ifdef __APPLE__
  is_mac = true;
#endif
  return is_mac;
}
