#ifndef ZTRACING_SRC_STRING_H_
#define ZTRACING_SRC_STRING_H_

#include "src/memory.h"
#include "src/types.h"

typedef struct Str8 Str8;
// Null terminated utf-8 string.
struct Str8 {
  u8 *ptr;
  // The number of bytes of the string, excluding NULL-terminator. The buffer
  // pointed by `ptr` MUST be at least (len + 1) large to hold both the content
  // the of string AND the NULL-terminator.
  usize len;
};

#define Str8Literal(s) \
  (Str8) { (u8 *)(s), sizeof(s) - 1 }

static inline b32 IsEmptyStr8(Str8 str) {
  b32 result = str.len == 0;
  return result;
}

Str8 PushStr8(Arena *arena, Str8 str);

typedef struct Str32 Str32;
struct Str32 {
  u32 *ptr;
  usize len;
};

Str32 PushStr32FromStr8(Arena *arena, Str8 str);

#endif  // ZTRACING_SRC_STRING_H_
