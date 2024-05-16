#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

typedef int8_t i8;
typedef uint8_t u8;
typedef int16_t i16;
typedef uint16_t u16;
typedef int32_t i32;
typedef uint32_t u32;
typedef int64_t i64;
typedef uint64_t u64;
typedef size_t usize;
typedef ptrdiff_t isize;

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

enum LogLevel {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR,
    NUM_LOG_LEVEL,
};

static void os_log_message(LogLevel level, const char *fmt, ...);

#define INFO(...) os_log_message(LOG_LEVEL_INFO, __VA_ARGS__)

#define ASSERT(x, ...)                                                         \
    if (!(x)) {                                                                \
        os_log_message(LOG_LEVEL_ERROR, __VA_ARGS__);                          \
        __builtin_trap();                                                      \
    }

#define ABORT(...) ASSERT(0, __VA_ARGS__)
#define UNREACHABLE ABORT("UNREACHABLE")
