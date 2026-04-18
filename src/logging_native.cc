#include <stdarg.h>
#include <stdio.h>

#include "src/logging.h"

void log_message(LogLevel level, const char* format, ...) {
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

  printf("[%s] ", level_str);
  va_list args;
  va_start(args, format);
  vprintf(format, args);
  va_end(args);
  printf("\n");
}
