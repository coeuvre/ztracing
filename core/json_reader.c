#include "core/json_reader.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#elif defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__wasm_simd128__)
#include <wasm_simd128.h>
#endif

#include "core/string.h"

// ==========================================
// 1. JSON READER (Parser) Implementation
// ==========================================

static inline bool is_json_whitespace(char c) {
  return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

static inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

// Skips any whitespace characters at the current parsing position.
static inline void json_reader_skip_whitespace(json_reader_t* r) {
  const char* buf = r->buf;
  size_t len = r->len;
  size_t pos = r->pos;
  while (pos < len && is_json_whitespace(buf[pos])) {
    pos++;
  }
  r->pos = pos;
}

void json_reader_init(json_reader_t* r, const char* buf, size_t len) {
  r->buf = buf;
  r->len = len;
  r->pos = 0;
}

// Returns the index of the first occurrence of '"' or '\\' in buf starting at
// pos. Returns 'len' if no occurrence is found before the end of the buffer.
static inline size_t find_quote_or_backslash(const char* buf, size_t pos,
                                             size_t len) {
#if defined(__x86_64__) || defined(_M_X64)
  // SSE2 Fast Path: Scan in 16-byte chunks
  const __m128i quotes = _mm_set1_epi8('"');
  const __m128i backslashes = _mm_set1_epi8('\\');

  while (pos + 15 < len) {
    // Load 16 bytes unaligned
    __m128i chunk = _mm_loadu_si128((const __m128i*)(buf + pos));

    // Compare for quotes and backslashes
    __m128i eq_quote = _mm_cmpeq_epi8(chunk, quotes);
    __m128i eq_backslash = _mm_cmpeq_epi8(chunk, backslashes);

    // OR the matches
    __m128i matched = _mm_or_si128(eq_quote, eq_backslash);

    // Extract match mask (1 bit per byte)
    int mask = _mm_movemask_epi8(matched);

    if (mask != 0) {
      size_t index = (size_t)__builtin_ctz((unsigned int)mask);
      return pos + index;
    }
    pos += 16;
  }
#elif defined(__ARM_NEON) || defined(__aarch64__)
  // ARM NEON Fast Path: Scan in 16-byte chunks
  const uint8x16_t quotes = vdupq_n_u8('"');
  const uint8x16_t backslashes = vdupq_n_u8('\\');

  while (pos + 15 < len) {
    uint8x16_t chunk = vld1q_u8((const uint8_t*)(buf + pos));

    uint8x16_t eq_quote = vceqq_u8(chunk, quotes);
    uint8x16_t eq_backslash = vceqq_u8(chunk, backslashes);

    uint8x16_t matched = vorrq_u8(eq_quote, eq_backslash);

    uint64x2_t matched64 = vreinterpretq_u64_u8(matched);
    uint64_t low = vgetq_lane_u64(matched64, 0);
    uint64_t high = vgetq_lane_u64(matched64, 1);

    if (low != 0 || high != 0) {
      if (low != 0) {
        size_t index = (size_t)(__builtin_ctzll(low) / 8);
        return pos + index;
      } else {
        size_t index = (size_t)(__builtin_ctzll(high) / 8) + 8;
        return pos + index;
      }
    }
    pos += 16;
  }
#elif defined(__wasm_simd128__)
  // WASM 128-bit SIMD Fast Path: Scan in 16-byte chunks
  const v128_t quotes = wasm_i8x16_splat('"');
  const v128_t backslashes = wasm_i8x16_splat('\\');

  while (pos + 15 < len) {
    v128_t chunk = wasm_v128_load((const v128_t*)(buf + pos));
    v128_t eq_quote = wasm_i8x16_eq(chunk, quotes);
    v128_t eq_backslash = wasm_i8x16_eq(chunk, backslashes);
    v128_t matched = wasm_v128_or(eq_quote, eq_backslash);
    uint32_t mask = wasm_i8x16_bitmask(matched);

    if (mask != 0) {
      size_t index = (size_t)__builtin_ctz((unsigned int)mask);
      return pos + index;
    }
    pos += 16;
  }
#endif

  // Scalar Fallback (handles remaining bytes or non-x86_64 platforms)
  while (pos < len) {
    char c = buf[pos];
    if (c == '"' || c == '\\') {
      return pos;
    }
    pos++;
  }

  return len;
}

// Attempts to read a JSON string starting at the current position.
// Expects the current character to be '"'.
// On success, 'out_val' is populated with a view of the string content
// (excluding the surrounding quotes) and the reader is advanced past the
// closing quote. Handles escaped characters (e.g., \", \\). Returns true on
// success, or false if a parsing error occurs (e.g., missing closing quote).
static bool json_reader_read_string(json_reader_t* r, string_view_t* out_val) {
  const char* buf = r->buf;
  size_t len = r->len;
  size_t pos = r->pos;

  if (pos < len && buf[pos] == '"') {
    pos++;
    size_t start = pos;

    while (pos < len) {
      pos = find_quote_or_backslash(buf, pos, len);
      if (pos >= len) {
        break;  // EOF, unclosed string
      }

      char c = buf[pos];
      if (c == '"') {
        *out_val = string_view_from_parts(buf + start, pos - start);
        r->pos = pos + 1;  // advance past closing quote
        return true;
      }
      if (c == '\\') {
        pos++;
        if (pos >= len) {
          break;  // error, unclosed escaped char
        }
        pos++;  // skip the escaped character and continue
      }
    }
    r->pos = pos;
  }
  return false;
}

// Attempts to read a JSON number starting at the current position.
// Parses integers and doubles on the fly into out_token.
// Returns true on success, or false otherwise.
static bool json_reader_read_number(json_reader_t* r, json_token_t* out_token) {
  const char* buf = r->buf;
  size_t len = r->len;
  size_t pos = r->pos;
  size_t start = pos;

  bool neg = false;
  if (pos < len && buf[pos] == '-') {
    neg = true;
    pos++;
  }

  if (pos >= len) {
    return false;
  }

  // Integer part
  if (buf[pos] == '0') {
    pos++;
  } else if (buf[pos] >= '1' && buf[pos] <= '9') {
    pos++;
    while (pos < len && is_digit(buf[pos])) {
      pos++;
    }
  } else {
    return false;
  }

  bool is_double = false;

  // Fractional part
  if (pos < len && buf[pos] == '.') {
    pos++;
    if (pos >= len || !is_digit(buf[pos])) {
      return false;
    }
    is_double = true;
    pos++;
    while (pos < len && is_digit(buf[pos])) {
      pos++;
    }
  }

  // Exponent part
  if (pos < len && (buf[pos] == 'e' || buf[pos] == 'E')) {
    pos++;
    is_double = true;
    if (pos < len && (buf[pos] == '+' || buf[pos] == '-')) {
      pos++;
    }
    if (pos >= len || !is_digit(buf[pos])) {
      return false;
    }
    pos++;
    while (pos < len && is_digit(buf[pos])) {
      pos++;
    }
  }

  // Parsed successfully! Let's fill out_token
  size_t slice_len = pos - start;
  out_token->val.str = string_view_from_parts(buf + start, slice_len);

  if (is_double) {
    double double_val;
    if (slice_len < 63) {
      char tmp[64];
      memcpy(tmp, buf + start, slice_len);
      tmp[slice_len] = '\0';
      double_val = atof(tmp);
    } else {
      char* tmp = (char*)malloc(slice_len + 1);
      if (tmp) {
        memcpy(tmp, buf + start, slice_len);
        tmp[slice_len] = '\0';
        double_val = atof(tmp);
        free(tmp);
      } else {
        double_val = 0.0;
      }
    }
    out_token->val.f64 = double_val;
    out_token->type = JSON_TOKEN_NUMBER_F64;
  } else {
    // Parse integer
    int64_t int_val = 0;
    bool overflowed = false;
    size_t scan_pos = start;
    if (buf[scan_pos] == '-') {
      scan_pos++;
    }
    while (scan_pos < pos) {
      int digit = buf[scan_pos] - '0';
      if (int_val >= 92233720368547758LL) {
        if (int_val > (INT64_MAX - digit) / 10) {
          overflowed = true;
        }
      }
      if (!overflowed) {
        int_val = int_val * 10 + digit;
      }
      scan_pos++;
    }

    if (overflowed) {
      out_token->val.i64 = neg ? INT64_MIN : INT64_MAX;
    } else {
      out_token->val.i64 = neg ? -int_val : int_val;
    }
    out_token->type = JSON_TOKEN_NUMBER_I64;
  }

  r->pos = pos;
  return true;
}

// Checks if the buffer starting at the current position matches 'literal'.
// If a match is found, 'out_token' (if non-null) is populated with the
// specified 'type' and a view of the matched text, and the reader is advanced
// past the literal. Returns true if matched, or false otherwise.
static bool json_reader_read_literal(json_reader_t* r, const char* literal,
                                     json_token_type_t type,
                                     json_token_t* out_token) {
  bool success = false;
  size_t literal_len = strlen(literal);
  if (r->pos + literal_len <= r->len) {
    if (memcmp(r->buf + r->pos, literal, literal_len) == 0) {
      if (out_token) {
        out_token->type = type;
        out_token->val.str =
            string_view_from_parts(r->buf + r->pos, literal_len);
      }
      r->pos += literal_len;
      success = true;
    }
  }
  return success;
}

void json_reader_next(json_reader_t* r, json_token_t* out_token) {
  out_token->type = JSON_TOKEN_ERROR;
  out_token->val.str.ptr = NULL;
  out_token->val.str.len = 0;

  json_reader_skip_whitespace(r);

  const char* buf = r->buf;
  size_t len = r->len;
  size_t pos = r->pos;

  if (pos >= len) {
    out_token->type = JSON_TOKEN_EOF;
  } else {
    char c = buf[pos];
    switch (c) {
      case '{':
        r->pos = pos + 1;
        out_token->type = JSON_TOKEN_OBJECT_START;
        out_token->val.str = string_view_from_parts(buf + pos, 1);
        break;
      case '}':
        r->pos = pos + 1;
        out_token->type = JSON_TOKEN_OBJECT_END;
        out_token->val.str = string_view_from_parts(buf + pos, 1);
        break;
      case '[':
        r->pos = pos + 1;
        out_token->type = JSON_TOKEN_ARRAY_START;
        out_token->val.str = string_view_from_parts(buf + pos, 1);
        break;
      case ']':
        r->pos = pos + 1;
        out_token->type = JSON_TOKEN_ARRAY_END;
        out_token->val.str = string_view_from_parts(buf + pos, 1);
        break;
      case ':':
        r->pos = pos + 1;
        out_token->type = JSON_TOKEN_COLON;
        out_token->val.str = string_view_from_parts(buf + pos, 1);
        break;
      case ',':
        r->pos = pos + 1;
        out_token->type = JSON_TOKEN_COMMA;
        out_token->val.str = string_view_from_parts(buf + pos, 1);
        break;
      case '"':
        out_token->type = JSON_TOKEN_STRING;
        if (!json_reader_read_string(r, &out_token->val.str)) {
          out_token->type = JSON_TOKEN_ERROR;
        }
        break;
      case 't':
        if (!json_reader_read_literal(r, "true", JSON_TOKEN_TRUE, out_token)) {
          out_token->type = JSON_TOKEN_ERROR;
        }
        break;
      case 'f':
        if (!json_reader_read_literal(r, "false", JSON_TOKEN_FALSE,
                                      out_token)) {
          out_token->type = JSON_TOKEN_ERROR;
        }
        break;
      case 'n':
        if (!json_reader_read_literal(r, "null", JSON_TOKEN_NULL, out_token)) {
          out_token->type = JSON_TOKEN_ERROR;
        }
        break;
      default:
        if (is_digit(c) || c == '-') {
          if (!json_reader_read_number(r, out_token)) {
            out_token->type = JSON_TOKEN_ERROR;
          }
        } else {
          r->pos = pos + 1;
        }
        break;
    }
  }
}

// ==========================================
