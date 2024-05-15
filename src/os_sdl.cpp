#include "core.h"
#include <stdarg.h>

SDL_LogPriority to_sdl_log_priority[NUM_LOG_LEVEL] = {
    [LOG_LEVEL_DEBUG] = SDL_LOG_PRIORITY_DEBUG,
    [LOG_LEVEL_INFO] = SDL_LOG_PRIORITY_INFO,
    [LOG_LEVEL_WARNING] = SDL_LOG_PRIORITY_WARN,
    [LOG_LEVEL_ERROR] = SDL_LOG_PRIORITY_ERROR,
};

static void os_log_message(LogLevel level, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    SDL_LogMessageV(
        SDL_LOG_CATEGORY_APPLICATION,
        to_sdl_log_priority[level],
        fmt,
        args
    );
    va_end(args);
}
