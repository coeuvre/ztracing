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

#define Min(x, y) ((x) < (y) ? (x) : (y))
#define Max(x, y) ((x) > (y) ? (x) : (y))

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

// ----------------------------------------------------------------------------
// Vectors

typedef union Vec2 Vec2;
union Vec2 {
  struct {
    f32 x;
    f32 y;
  };
  f32 v[2];
};

static inline Vec2 V2(f32 x, f32 y) {
  Vec2 result = {x, y};
  return result;
}

typedef union Vec2I Vec2I;
union Vec2I {
  struct {
    i32 x;
    i32 y;
  };
  i32 v[2];
};

static inline b32 EqualVec2I(Vec2I a, Vec2I b) {
  b32 result = a.x == b.x && a.y == b.y;
  return result;
}

static inline Vec2 Vec2FromVec2I(Vec2I value) {
  Vec2 result = {value.x, value.y};
  return result;
}

typedef union Rect2 Rect2;
union Rect2 {
  struct {
    Vec2 min;
    Vec2 max;
  };
  struct {
    Vec2 p0;
    Vec2 p1;
  };
};

static inline Rect2 R2(Vec2 min, Vec2 max) {
  Rect2 result = {min, max};
  return result;
}

// ----------------------------------------------------------------------------
// Math

static inline f32 ClampF32(f32 value, f32 min, f32 max) {
  f32 result = Max(value, min);
  result = Min(value, max);
  return result;
}

static inline Vec2 ClampVec2(Vec2 value, Vec2 min, Vec2 max) {
  Vec2 result = {ClampF32(value.x, min.x, max.x),
                 ClampF32(value.y, min.y, max.y)};
  return result;
}

static inline i32 RoundF32(f32 value) {
  i32 result = roundf(value);
  return result;
}