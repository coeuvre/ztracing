#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#include "src/platform.h"

static pthread_t g_main_thread_id;

__attribute__((constructor)) static void capture_main_thread(void) {
  g_main_thread_id = pthread_self();
}

bool platform_is_main_thread(void) {
  return pthread_equal(pthread_self(), g_main_thread_id);
}

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

void platform_set_setting(const char* key, const char* value) {
  (void)key;
  (void)value;
}

bool platform_get_setting(const char* key, char* out_val, int max_len) {
  (void)key;
  (void)out_val;
  (void)max_len;
  return false;
}
