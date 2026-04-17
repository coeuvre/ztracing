#include "src/logging.h"

#include <emscripten/console.h>
#include <stdarg.h>
#include <stdio.h>

void Log(LogLevel level, const char* format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  switch (level) {
    case DEBUG:
      emscripten_console_log(buffer);
      break;
    case INFO:
      emscripten_console_log(buffer);
      break;
    case WARN:
      emscripten_console_warn(buffer);
      break;
    case ERROR:
      emscripten_console_error(buffer);
      break;
  }
}
