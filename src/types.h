#ifndef ZTRACING_SRC_TYPES_H_
#define ZTRACING_SRC_TYPES_H_

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

#define I32_MAX INT_MAX

#define I64_MIN INT64_MIN
#define I64_MAX INT64_MAX

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

static inline i64 i64_min(i64 a, i64 b) { return MIN(a, b); }
static inline i64 i64_max(i64 a, i64 b) { return MAX(a, b); }
static inline int i64_cmp(i64 a, i64 b) {
  return (a == b) ? 0 : (a < b ? -1 : 1);
}

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

static inline usize i32_max(i32 a, i32 b) {
  usize result = MAX(a, b);
  return result;
}

static inline usize usize_min(usize a, usize b) {
  usize result = MIN(a, b);
  return result;
}

static inline usize usize_max(usize a, usize b) {
  usize result = MAX(a, b);
  return result;
}

#if COMPILER_MSVC
#define THREAD_LOCAL __declspec(thread)
#elif COMPILER_CLANG || COMPILER_GCC
#define THREAD_LOCAL __thread
#endif

// clang-format off
#ifdef __cplusplus
#define ZERO_INIT {}
#else
#define ZERO_INIT {0}
#endif
// clang-format on

#define OPTIONAL_TYPE(Name, Type, prefix)        \
  typedef struct Name {                          \
    Type value;                                  \
    bool present;                                \
  } Name;                                        \
                                                 \
  static inline Name prefix##_some(Type value) { \
    Name result;                                 \
    result.present = true;                       \
    result.value = value;                        \
    return result;                               \
  }                                              \
                                                 \
  static inline Name prefix##_none(void) {       \
    Name result = ZERO_INIT;                     \
    return result;                               \
  }

OPTIONAL_TYPE(i32o, i32, i32);
OPTIONAL_TYPE(i64o, i64, i64);

#endif  // ZTRACING_SRC_TYPES_H_
