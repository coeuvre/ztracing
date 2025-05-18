#include "src/string.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "src/assert.h"
#include "src/flick.h"
#include "src/types.h"

u64 Str_HashWithSeed(Str s, u64 seed) {
  // FNV-style hash
  u64 h = seed;
  for (isize i = 0; i < s.len; i++) {
    h ^= s.ptr[i];
    h *= 1111111111111111111u;
  }
  return h;
}

typedef struct UnicodeDecode UnicodeDecode;
struct UnicodeDecode {
  u32 codepoint;
  u32 increment;
};

static UnicodeDecode DecodeUTF8(u8 *ptr, usize len) {
#define VALID_PREFIX(b) (((b) & 0b11000000) == 0b10000000)
  UnicodeDecode result;
  result.codepoint = 0xFFFD;
  result.increment = 1;
  if (len >= 1) {
    u32 b0 = ptr[0];
    if ((b0 & 0b10000000) == 0) {
      result.codepoint = b0 & 0b01111111;
      result.increment = 1;
    } else if (((b0 & 0b11100000) == 0b11000000) && len >= 2) {
      u32 b1 = ptr[1];
      if (VALID_PREFIX(b1)) {
        result.codepoint = ((b0 & 0b00011111) << 6) | (b1 & 0b00111111);
        result.increment = 2;
      }
    } else if (len >= 3 && ((b0 & 0b11110000) == 0b11100000)) {
      u32 b1 = ptr[1];
      u32 b2 = ptr[2];
      if (VALID_PREFIX(b1) && VALID_PREFIX(b2)) {
        result.codepoint = ((b0 & 0b00001111) << 12) |
                           ((b1 & 0b00111111) << 6) | (b2 & 0b00111111);
        result.increment = 3;
      }
    } else if (len >= 4 && ((b0 & 0b11111000) == 0b11110000)) {
      u32 b1 = ptr[1];
      u32 b2 = ptr[2];
      u32 b3 = ptr[3];
      if (VALID_PREFIX(b1) && VALID_PREFIX(b2) && VALID_PREFIX(b3)) {
        result.codepoint = ((b0 & 0b00000111) << 16) |
                           ((b1 & 0b00111111) << 12) |
                           ((b2 & 0b00111111) << 6) | (b3 & 0b00111111);
        result.increment = 4;
      }
    }
  }
  return result;
}

Str32 Arena_PushStr32FromStr(FL_Arena *arena, Str str) {
  if (Str_IsEmpty(str)) {
    return (Str32){0};
  }

  usize cap = str.len;
  u32 *ptr = FL_Arena_PushArray(arena, u32, cap);
  u32 len = 0;
  char *end = str.ptr + str.len;
  UnicodeDecode decode;
  for (char *cursor = str.ptr; cursor < end; cursor += decode.increment) {
    decode = DecodeUTF8((u8 *)cursor, end - cursor);
    ptr[len++] = decode.codepoint;
  }
  DEBUG_ASSERT(len <= cap);
  FL_Arena_Pop(arena, (cap - len) * 4);
  return (Str32){.ptr = ptr, .len = len};
}
