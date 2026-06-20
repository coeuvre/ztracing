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
  return true;  // Default to dark theme for headless tests
}

bool platform_is_mac() { return false; }

void platform_open_file_dialog() {
  // Headless stub: do nothing
}
