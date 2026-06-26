#include <stdarg.h>
#include <stdio.h>

#include "core/logging.h"

void log_message(log_level_t level, const char* format, ...) {
  const char* level_str = "DEBUG";
  switch (level) {
    case LOG_LEVEL_DEBUG:
      level_str = "DEBUG";
      break;
    case LOG_LEVEL_INFO:
      level_str = "INFO";
      break;
    case LOG_LEVEL_WARN:
      level_str = "WARN";
      break;
    case LOG_LEVEL_ERROR:
      level_str = "ERROR";
      break;
  }

  fprintf(stderr, "[%s] ", level_str);
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fprintf(stderr, "\n");
}
