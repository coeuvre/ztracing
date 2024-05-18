#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

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
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

enum LogLevel {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_CRITICAL,
    NUM_LOG_LEVEL,
};

static void os_log_message(LogLevel level, const char *fmt, ...);

#define INFO(fmt, ...) os_log_message(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define WARN(fmt, ...) os_log_message(LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define ERROR(fmt, ...) os_log_message(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)

#define ABORT(fmt, ...)                                                        \
    do {                                                                       \
        os_log_message(                                                        \
            LOG_LEVEL_CRITICAL,                                                \
            "%s:%d: " fmt,                                                     \
            __FILE__,                                                          \
            __LINE__,                                                          \
            ##__VA_ARGS__                                                      \
        );                                                                     \
        __builtin_trap();                                                      \
    } while (0)

#define ASSERT(x, fmt, ...)                                                    \
    if (!(x)) {                                                                \
        ABORT(fmt, ##__VA_ARGS__);                                             \
    }

#define UNREACHABLE ABORT("UNREACHABLE")

struct OsLoadingFile;
static OsLoadingFile *os_loading_file_open(char *path);
static u32 os_loading_file_next(OsLoadingFile *file, u8 *buf, u32 len);
static void os_loading_file_close(OsLoadingFile *file);

typedef int (*OsThreadFunction)(void *data);

struct OsThread;

static OsThread *os_thread_create(OsThreadFunction fn, void *data);
static void os_thread_join(OsThread *thread);
