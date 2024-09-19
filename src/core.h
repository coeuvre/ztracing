#pragma once

// ----------------------------------------------------------------------------
// Foreign Includes

#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ----------------------------------------------------------------------------
// Asserts

#if COMPILER_MSVC
#define BreakDebugger() __debugbreak()
#elif COMPILER_CLANG || COMPILER_GCC
#define BreakDebugger() __builtin_trap()
#else
#error Unknown trap intrinsic for this compiler.
#endif

#define Assert(x)      \
  do {                 \
    if (!(x)) {        \
      BreakDebugger(); \
    }                  \
  } while (0)
#if BUILD_DEBUG
#define DebugAssert(x) Assert(x)
#else
#define DebugAssert(x) (void)(x)
#endif
#define Unreachable Assert(!"Unreachable")
#define NotImplemented Assert(!"Not Implemented")

// ----------------------------------------------------------------------------
// Misc. Helper Macros

#define ArrayCount(a) (sizeof(a) / sizeof((a)[0]))

#define Kilobytes(n) (((u64)(n)) << 10)
#define Megabytes(n) (((u64)(n)) << 20)
#define Gigabytes(n) (((u64)(n)) << 30)
#define Terabytes(n) (((u64)(n)) << 40)

// ----------------------------------------------------------------------------
// Base types

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef i8 b8;
typedef i16 b16;
typedef i32 b32;
typedef i64 b64;
typedef float f32;
typedef double f64;
typedef size_t usize;
typedef ptrdiff_t isize;

// ----------------------------------------------------------------------------
// Strings

typedef struct String String;
struct String {
  u8 *ptr;
  usize len;
};