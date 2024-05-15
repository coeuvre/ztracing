#include "core.h"
#include <stdarg.h>

SDL_LogCategory to_sdl_log_category[NUM_LOG_CATEGORY] = {
    [LOG_CATEGORY_APPLICATION] = SDL_LOG_CATEGORY_APPLICATION,
    [LOG_CATEGORY_ASSERT] = SDL_LOG_CATEGORY_ASSERT,
};

SDL_LogPriority to_sdl_log_priority[NUM_LOG_LEVEL] = {
    [LOG_LEVEL_DEBUG] = SDL_LOG_PRIORITY_DEBUG,
    [LOG_LEVEL_INFO] = SDL_LOG_PRIORITY_INFO,
    [LOG_LEVEL_WARNING] = SDL_LOG_PRIORITY_WARN,
    [LOG_LEVEL_ERROR] = SDL_LOG_PRIORITY_ERROR,
};

static void
log_message(LogCategory category, LogLevel level, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    SDL_LogMessageV(
        to_sdl_log_category[category],
        to_sdl_log_priority[level],
        fmt,
        args
    );
    va_end(args);
}
