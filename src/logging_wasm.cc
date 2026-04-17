#include <emscripten/console.h>
#include <stdarg.h>
#include <stdio.h>

#include "src/logging.h"

void log_message(LogLevel level, const char* format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  switch (level) {
    case LOG_LEVEL_DEBUG:
      emscripten_console_log(buffer);
      break;
    case LOG_LEVEL_INFO:
      emscripten_console_log(buffer);
      break;
    case LOG_LEVEL_WARN:
      emscripten_console_warn(buffer);
      break;
    case LOG_LEVEL_ERROR:
      emscripten_console_error(buffer);
      break;
  }
}
