#ifndef ZTRACING_SRC_STRING_H_
#define ZTRACING_SRC_STRING_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "src/flick.h"
#include "src/memory.h"
#include "src/types.h"

typedef FL_Str Str;

#define STR_C(s) FL_STR_C(s)

static inline Str Str_FromCStr(const char *str) {
  Str result = {(char *)str, (ptrdiff_t)strlen(str)};
  return result;
}

static inline Str Str_Zero(void) { return (Str){0, 0}; }

static inline bool Str_IsEmpty(Str str) { return str.len == 0; }

static inline bool Str_IsEqual(Str a, Str b) {
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

static inline int Str_Compare(Str a, Str b) {
  usize len = MinUsize(a.len, b.len);
  for (usize i = 0; i < len; ++i) {
    int r = a.ptr[i] - b.ptr[i];
    if (r != 0) {
      return r;
    }
  }
  return a.len < b.len ? -1 : 1;
}

u64 Str_HashWithSeed(Str str, u64 seed);
static inline u64 Str_Hash(Str str) { return Str_HashWithSeed(str, 0x100); }

static inline Str arena_push_str8(Arena *arena, usize len) {
  char *ptr = arena_push_array(arena, char, len);
  return (Str){ptr, (ptrdiff_t)len};
}

static inline Str arena_push_str8_no_zero(Arena *arena, usize len) {
  char *ptr = arena_push_array_no_zero(arena, char, len);
  return (Str){ptr, len};
}

Str arena_push_str8fv(Arena *arena, const char *format, va_list ap);

static inline Str arena_push_str8f(Arena *arena, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  Str result = arena_push_str8fv(arena, format, ap);
  va_end(ap);
  return result;
}

Str Arena_PushStrFV(FL_Arena *arena, const char *format, va_list ap);

static inline Str Arena_PushStrF(FL_Arena *arena, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  Str result = Arena_PushStrFV(arena, format, ap);
  va_end(ap);
  return result;
}

static inline Str Str_Dup(Arena *arena, Str s) {
  if (Str_IsEmpty(s)) {
    return Str_Zero();
  }
  return (Str){(char *)arena_dup(arena, s.ptr, s.len), s.len};
}

static inline u8 u8_ToUppercase(u8 c) {
  if (c >= 'a' && c <= 'z') {
    return 'A' + c - 'a';
  }
  return c;
}

static inline Str Str_ToUppercase(Str s, Arena *arena) {
  Str result = arena_push_str8(arena, s.len);
  for (ptrdiff_t i = 0; i < s.len; ++i) {
    result.ptr[i] = u8_ToUppercase(s.ptr[i]);
  }
  return result;
}

typedef struct Str32 Str32;
struct Str32 {
  u32 *ptr;
  usize len;
};

static inline Str32 str32(u32 *ptr, usize len) {
  Str32 s;
  s.ptr = ptr;
  s.len = len;
  return s;
}

Str32 Arena_PushStr32FromStr(FL_Arena *arena, Str str);

#endif  // ZTRACING_SRC_STRING_H_
