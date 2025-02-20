#ifndef ZTRACING_SRC_ASSERT_H_
#define ZTRACING_SRC_ASSERT_H_

#include "src/config.h"
#include "src/log.h"

#if COMPILER_MSVC
#define break_debugger() __debugbreak()
#elif COMPILER_CLANG || COMPILER_GCC
#define break_debugger() __builtin_trap()
#else
#error Unknown trap intrinsic for this compiler.
#endif

#define ASSERT(x)                                 \
  do {                                            \
    if (!(x)) {                                   \
      ERROR("%s:%d: %s", __FILE__, __LINE__, #x); \
      break_debugger();                           \
    }                                             \
  } while (0)

#define ASSERTF(x, fmt, ...)                                  \
  do {                                                        \
    if (!(x)) {                                               \
      ERROR("%s:%d:" fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
      break_debugger();                                       \
    }                                                         \
  } while (0)

#if BUILD_DEBUG
#define DEBUG_ASSERT(x) ASSERT(x)
#define DEBUG_ASSERTF(x, fmt, ...) ASSERTF(x, fmt, ##__VA_ARGS__)
#else
#define DEBUG_ASSERT(x)
#define DEBUG_ASSERTF(x, fmt, ...)
#endif
#define NOT_IMPLEMENTED ASSERT(!"Not Implemented")

#if COMPILER_GCC || COMPILER_CLANG
#define UNREACHABLE __builtin_unreachable()
#else
#define UNREACHABLE ASSERT(!"Unreachable")
#endif

#endif  // ZTRACING_SRC_ASSERT_H_
