#ifndef ZTRACING_SRC_TYPES_H_
#define ZTRACING_SRC_TYPES_H_

#include <stdint.h>
#include <stddef.h>

#include "src/config.h"

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

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

static inline usize MaxUSize(usize a, usize b) {
  usize result = MAX(a, b);
  return result;
}

#if COMPILER_MSVC
#define thread_local __declspec(thread)
#elif COMPILER_CLANG || COMPILER_GCC
#define thread_local __thread
#endif

#endif  // ZTRACING_SRC_TYPES_H_
