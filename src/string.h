#ifndef ZTRACING_SRC_STRING_H_
#define ZTRACING_SRC_STRING_H_

#include <stdarg.h>
#include <string.h>

#include "src/memory.h"
#include "src/types.h"

// Null terminated utf-8 string.
typedef struct Str8 {
  u8 *ptr;
  // The number of bytes of the string, excluding NULL-terminator. The buffer
  // pointed by `ptr` MUST be at least (len + 1) large to hold both the content
  // the of string AND the NULL-terminator.
  usize len;
} Str8;

#define str8_lit(s) (Str8){(u8 *)(s), sizeof(s) - 1}

static inline Str8 str8_from_cstr(const char *str) {
  Str8 result;
  result.ptr = (u8 *)str;
  result.len = strlen(str);
  return result;
}

static inline Str8 str8_zero(void) {
  Str8 result = {0};
  return result;
}

static inline b32 str8_is_empty(Str8 str) {
  b32 result = str.len == 0;
  return result;
}

static inline b32 str8_is_equal(Str8 a, Str8 b) {
  b32 result;
  if (a.len == b.len) {
    if (a.ptr != b.ptr) {
      if (a.ptr && b.ptr) {
        result = memcmp(a.ptr, b.ptr, a.len) == 0;
      } else {
        result = 0;
      }
    } else {
      result = 1;
    }
  } else {
    result = 0;
  }
  return result;
}

u64 str8_hash_with_seed(Str8 str, u64 seed);
static inline u64 str8_hash(Str8 str) { return str8_hash_with_seed(str, 5381); }

Str8 arena_push_str8(Arena *arena, Str8 str);
Str8 arena_push_str8f(Arena *arena, const char *format, ...);
Str8 arena_push_str8fv(Arena *arena, const char *format, va_list ap);

typedef struct Str32 Str32;
struct Str32 {
  u32 *ptr;
  usize len;
};

Str32 arena_push_str32_from_str8(Arena *arena, Str8 str);

#endif  // ZTRACING_SRC_STRING_H_
