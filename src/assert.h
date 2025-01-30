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

#define ASSERT(x)       \
  do {                  \
    if (!(x)) {         \
      ERROR("%s", #x);  \
      break_debugger(); \
    }                   \
  } while (0)

#define ASSERTF(x, fmt, ...)     \
  do {                           \
    if (!(x)) {                  \
      ERROR(fmt, ##__VA_ARGS__); \
      break_debugger();          \
    }                            \
  } while (0)

#if BUILD_DEBUG
#define DEBUG_ASSERT(x) ASSERT(x)
#else
#define DEBUG_ASSERT(x) (void)(x)
#endif
#define UNREACHABLE ASSERT(!"Unreachable")
#define NOT_IMPLEMENTED ASSERT(!"Not Implemented")

#endif  // ZTRACING_SRC_ASSERT_H_
