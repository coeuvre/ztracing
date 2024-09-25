#ifndef ZTRACING_SRC_ASSERT_H_
#define ZTRACING_SRC_ASSERT_H_

#include "src/config.h"

#if COMPILER_MSVC
#define BreakDebugger() __debugbreak()
#elif COMPILER_CLANG || COMPILER_GCC
#define BreakDebugger() __builtin_trap()
#else
#error Unknown trap intrinsic for this compiler.
#endif

#define ASSERT(x)      \
  do {                 \
    if (!(x)) {        \
      BreakDebugger(); \
    }                  \
  } while (0)

#if BUILD_DEBUG
#define DEBUG_ASSERT(x) ASSERT(x)
#else
#define DEBUG_ASSERT(x) (void)(x)
#endif
#define UNREACHABLE ASSERT(!"Unreachable")
#define NOT_IMPLEMENTED ASSERT(!"Not Implemented")

#endif  // ZTRACING_SRC_ASSERT_H_