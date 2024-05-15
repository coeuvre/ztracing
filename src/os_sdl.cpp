#include "core.h"

static SDL_LogPriority TO_SDL_LOG_PRIORITY[NUM_LOG_LEVEL] = {
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
        TO_SDL_LOG_PRIORITY[level],
        fmt,
        args
    );
    va_end(args);
}
