#ifndef ZTRACING_SRC_STRING_H_
#define ZTRACING_SRC_STRING_H_

#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#include "src/memory.h"
#include "src/types.h"

/// A slice of u8, can be used to represent utf-8 string, or a buffer.
typedef struct Str8 {
  u8 *ptr;
  /// The number of bytes in the buffer
  usize len;
} Str8;

static inline Str8 str8(u8 *ptr, usize len) {
  Str8 s;
  s.ptr = ptr;
  s.len = len;
  return s;
}

#define STR8_LIT(s) (Str8){(u8 *)(s), sizeof(s) - 1}

static inline Str8 str8_from_cstr(const char *str) {
  Str8 result;
  result.ptr = (u8 *)str;
  result.len = strlen(str);
  return result;
}

static inline Str8 str8_zero(void) { return str8(0, 0); }

static inline bool str8_is_empty(Str8 str) { return str.len == 0; }

static inline bool str8_eq(Str8 a, Str8 b) {
  if (a.len != b.len) {
    return false;
  }

  if (a.ptr == b.ptr) {
    return true;
  }

  if (!(a.ptr && b.ptr)) {
    return false;
  }

  return memcmp(a.ptr, b.ptr, a.len) == 0;
}

u64 str8_hash_with_seed(Str8 str, u64 seed);
static inline u64 str8_hash(Str8 str) { return str8_hash_with_seed(str, 5381); }

Str8 arena_push_str8(Arena *arena, usize len);
Str8 arena_push_str8_no_zero(Arena *arena, usize len);
Str8 arena_push_str8f(Arena *arena, const char *format, ...);
Str8 arena_push_str8fv(Arena *arena, const char *format, va_list ap);

typedef struct Str32 Str32;
struct Str32 {
  u32 *ptr;
  usize len;
};

Str32 arena_push_str32_from_str8(Arena *arena, Str8 str);

#endif  // ZTRACING_SRC_STRING_H_
