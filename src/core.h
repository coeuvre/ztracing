#pragma once

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

typedef float f32;
typedef double f64;

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

enum LogLevel {
    LogLevel_Debug,
    LogLevel_Info,
    LogLevel_Warn,
    LogLevel_Error,
    LogLevel_Critical,

    LogLevel_COUNT,
};

void LogMessage(LogLevel level, const char *fmt, ...);

#define INFO(fmt, ...) LogMessage(LogLevel_Info, fmt, ##__VA_ARGS__)
#define WARN(fmt, ...) LogMessage(LogLevel_Warn, fmt, ##__VA_ARGS__)
#define ERROR(fmt, ...)                                                        \
    LogMessage(LogLevel_Error, "%s:%d: " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

#ifdef _MSC_VER
#define DEBUGTRAP __debugbreak
#elif __has_builtin(__builtin_debugtrap)
#define DEBUGTRAP __builtin_debugtrap
#else
#define DEBUGTRAP __builtin_trap
#endif

#define ABORT(fmt, ...)                                                        \
    do {                                                                       \
        LogMessage(                                                            \
            LogLevel_Critical,                                                 \
            "%s:%d: " fmt,                                                     \
            __FILE__,                                                          \
            __LINE__,                                                          \
            ##__VA_ARGS__                                                      \
        );                                                                     \
        DEBUGTRAP();                                                           \
    } while (0)

#define ASSERT(x)                                                              \
    if (!(x)) {                                                                \
        ABORT("%s", #x);                                                       \
    }

#define UNREACHABLE ABORT("UNREACHABLE")

struct Buffer {
    u8 *data;
    usize size;
};

inline bool
Equal(Buffer a, Buffer b) {
    if (a.size != b.size) {
        return false;
    }

    for (usize index = 0; index < a.size; ++index) {
        if (a.data[index] != b.data[index]) {
            return false;
        }
    }

    return true;
}

#define STRING_LITERAL(string)                                                 \
    { (u8 *)(string), sizeof(string) - 1 }
