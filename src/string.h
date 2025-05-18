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
  isize len = MinIsize(a.len, b.len);
  for (isize i = 0; i < len; ++i) {
    int r = a.ptr[i] - b.ptr[i];
    if (r != 0) {
      return r;
    }
  }
  return a.len < b.len ? -1 : 1;
}

static inline u8 ToUppercase(u8 c) {
  if (c >= 'a' && c <= 'z') {
    return 'A' + c - 'a';
  }
  return c;
}

static inline int Str_CompareIgnoringCase(Str a, Str b) {
  isize len = MinIsize(a.len, b.len);
  for (isize i = 0; i < len; ++i) {
    int r = ToUppercase(a.ptr[i]) - ToUppercase(b.ptr[i]);
    if (r != 0) {
      return r;
    }
  }
  return a.len < b.len ? -1 : 1;
}

u64 Str_HashWithSeed(Str str, u64 seed);
static inline u64 Str_Hash(Str str) { return Str_HashWithSeed(str, 0x100); }

#define Str_Format(arena, format, ...) \
  FL_Str_Format(arena, format, ##__VA_ARGS__)

static inline Str Str_Dup(FL_Arena *arena, Str s) {
  if (Str_IsEmpty(s)) {
    return Str_Zero();
  }
  return (Str){(char *)Arena_Dup(arena, s.ptr, s.len, 1), s.len};
}

static inline Str Arena_PushStr(Arena *arena, isize len) {
  char *ptr = Arena_PushArray(arena, char, len);
  return (Str){ptr, len};
}

static inline Str Str_ToUppercase(Str s, Arena *arena) {
  Str result = Arena_PushStr(arena, s.len);
  for (isize i = 0; i < s.len; ++i) {
    result.ptr[i] = ToUppercase(s.ptr[i]);
  }
  return result;
}

typedef struct Str32 Str32;
struct Str32 {
  u32 *ptr;
  isize len;
};

Str32 Arena_PushStr32FromStr(FL_Arena *arena, Str str);

#endif  // ZTRACING_SRC_STRING_H_
