#include "src/string.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "src/assert.h"
#include "src/memory.h"
#include "src/types.h"

u64 str8_hash_with_seed(Str8 str, u64 seed) {
  u64 hash = seed;
  // djb2 hash function
  for (usize i = 0; i < str.len; i += 1) {
    // hash * 33 + c
    hash = ((hash << 5) + hash) + str.ptr[i];
  }
  return hash;
}

Str8 arena_push_str8fv(Arena *arena, const char *format, va_list ap) {
#define INIT_BUFFER_SIZE 256
  usize buf_len = INIT_BUFFER_SIZE;
  char *buf_ptr = arena_push_array(arena, char, buf_len);

  va_list args;
  va_copy(args, ap);
  usize str_len = vsnprintf(buf_ptr, buf_len, format, args);

  if (str_len <= buf_len) {
    // Free the unused part of the buffer.
    arena_pop(arena, buf_len - str_len);
  } else {
    // The buffer was too small. We need to resize it and try again.
    arena_pop(arena, buf_len);
    buf_len = str_len;
    buf_ptr = arena_push_array(arena, char, buf_len);
    va_copy(args, ap);
    vsnprintf(buf_ptr, buf_len, format, args);
  }

  Str8 result = {(u8 *)buf_ptr, str_len};
  return result;
}

typedef struct UnicodeDecode UnicodeDecode;
struct UnicodeDecode {
  u32 codepoint;
  u32 increment;
};

static UnicodeDecode utf_decode(u8 *ptr, usize len) {
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

Str32 arena_push_str32_from_str8(Arena *arena, Str8 str) {
  usize cap = str.len;
  u32 *ptr = arena_push_array_no_zero(arena, u32, cap);
  u32 len = 0;
  u8 *end = str.ptr + str.len;
  UnicodeDecode decode;
  for (u8 *cursor = str.ptr; cursor < end; cursor += decode.increment) {
    decode = utf_decode(cursor, end - cursor);
    ptr[len++] = decode.codepoint;
  }
  DEBUG_ASSERT(len <= cap);
  arena_pop(arena, (cap - len) * 4);
  return (Str32){.ptr = ptr, .len = len};
}
