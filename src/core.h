#pragma once

#include <stdint.h>

typedef int8_t i8;
typedef uint8_t u8;
typedef int16_t i16;
typedef uint16_t u16;
typedef int32_t i32;
typedef uint32_t u32;
typedef int64_t i64;
typedef uint64_t u64;

enum LogCategory {
    LOG_CATEGORY_APPLICATION,
    LOG_CATEGORY_ASSERT,
    NUM_LOG_CATEGORY,
};

enum LogLevel {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR,
    NUM_LOG_LEVEL,
};

static void
log_message(LogCategory category, LogLevel level, const char *fmt, ...);

#define ASSERT(x, fmt, ...)                                                    \
    if (!(x)) {                                                                \
        log_message(LOG_CATEGORY_ASSERT, LOG_LEVEL_ERROR, fmt, __VA_ARGS__);   \
        abort();                                                               \
    }
