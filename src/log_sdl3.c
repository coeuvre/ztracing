#include <stdarg.h>

#include "SDL3/SDL_log.h"
#include "src/log.h"

static SDL_LogPriority kLogLevelToPriority[kLogLevelCount] = {
    SDL_LOG_PRIORITY_DEBUG,
    SDL_LOG_PRIORITY_INFO,
    SDL_LOG_PRIORITY_WARN,
    SDL_LOG_PRIORITY_ERROR,
};

void LogMessage(LogLevel level, const char *fmt, ...) {
  int category = SDL_LOG_CATEGORY_APPLICATION;
  SDL_LogPriority priority = kLogLevelToPriority[level];
  if (priority >= SDL_GetLogPriority(category)) {
    SDL_SetLogPriorityPrefix(priority, "");
    va_list ap;
    va_start(ap, fmt);
    SDL_LogMessageV(category, priority, fmt, ap);
    va_end(ap);
  }
}
