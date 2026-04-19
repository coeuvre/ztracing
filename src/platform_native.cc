#include <chrono>

#include "src/platform.h"

double platform_get_now() {
  auto now = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch());
  return static_cast<double>(duration.count());
}

bool platform_is_dark_mode() {
  return true; // Default to dark mode for native
}
