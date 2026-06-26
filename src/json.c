#include "src/json.h"

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

#include "core/allocator.h"
#include "src/array_list.h"
#include "src/string.h"

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

  if (pos < len && is_digit(buf[pos])) {
    int64_t int_val = 0;
    bool overflowed = false;

    // Scan integer part
    while (pos < len && is_digit(buf[pos])) {
      int digit = buf[pos] - '0';
      // Highly optimized overflow check (predicts false with near-100%
      // accuracy)
      if (int_val >= 92233720368547758LL) {
        if (int_val > (INT64_MAX - digit) / 10) {
          overflowed = true;
        }
      }
      if (!overflowed) {
        int_val = int_val * 10 + digit;
      }
      pos++;
    }

    // Check if it is a double
    if (pos < len && buf[pos] == '.') {
      pos++;
      double frac_val = 0.0;
      double divisor = 1.0;
      while (pos < len && is_digit(buf[pos])) {
        frac_val = frac_val * 10.0 + (buf[pos] - '0');
        divisor *= 10.0;
        pos++;
      }

      double double_val = (double)int_val + (frac_val / divisor);

      // Handle exponent part if present (rare fallback)
      if (pos < len && (buf[pos] == 'e' || buf[pos] == 'E')) {
        while (pos < len) {
          char c = buf[pos];
          if (is_digit(c) || c == '.' || c == 'e' || c == 'E' || c == '+' ||
              c == '-') {
            pos++;
          } else {
            break;
          }
        }
        char tmp[64];
        size_t slice_len = pos - start;
        if (slice_len < 63) {
          memcpy(tmp, buf + start, slice_len);
          tmp[slice_len] = '\0';
          out_token->val.f64 = atof(tmp);
        } else {
          out_token->val.f64 = 0.0;
        }
      } else {
        out_token->val.f64 = neg ? -double_val : double_val;
      }
      out_token->val.str = string_view_from_parts(buf + start, pos - start);
      out_token->type = JSON_TOKEN_NUMBER_F64;
    } else if (pos < len && (buf[pos] == 'e' || buf[pos] == 'E')) {
      // Exponent directly after integer part (e.g. 1e9)
      while (pos < len) {
        char c = buf[pos];
        if (is_digit(c) || c == '.' || c == 'e' || c == 'E' || c == '+' ||
            c == '-') {
          pos++;
        } else {
          break;
        }
      }
      char tmp[64];
      size_t slice_len = pos - start;
      if (slice_len < 63) {
        memcpy(tmp, buf + start, slice_len);
        tmp[slice_len] = '\0';
        out_token->val.f64 = atof(tmp);
      } else {
        out_token->val.f64 = 0.0;
      }
      out_token->val.str = string_view_from_parts(buf + start, pos - start);
      out_token->type = JSON_TOKEN_NUMBER_F64;
    } else {
      // It's an integer!
      if (overflowed) {
        out_token->val.i64 = neg ? INT64_MIN : INT64_MAX;
      } else {
        out_token->val.i64 = neg ? -int_val : int_val;
      }
      out_token->val.str = string_view_from_parts(buf + start, pos - start);
      out_token->type = JSON_TOKEN_NUMBER_I64;
    }

    r->pos = pos;
    return true;
  }
  return false;
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
// 2. JSON WRITER (Formatter) Implementation
// ==========================================

static size_t json_escaped_len(string_view_t s) {
  size_t len = 0;
  for (size_t i = 0; i < s.len; i++) {
    char c = s.ptr[i];
    switch (c) {
      case '"':
      case '\\':
      case '\b':
      case '\f':
      case '\n':
      case '\r':
      case '\t':
        len += 2;
        break;
      default:
        if ((unsigned char)c < 0x20) {
          len += 6;  // \u00xx
        } else {
          len += 1;
        }
        break;
    }
  }
  return len;
}

static void json_writer_write_escaped(array_list_t* out_buf, string_view_t s,
                                      allocator_t a) {
  size_t escaped_len = json_escaped_len(s);
  if (escaped_len == s.len) {
    char* dest = array_list_append(out_buf, s.len, char, a);
    memcpy(dest, s.ptr, s.len);
  } else {
    char* dest = array_list_append(out_buf, escaped_len, char, a);
    size_t pos = 0;
    for (size_t i = 0; i < s.len; i++) {
      char c = s.ptr[i];
      switch (c) {
        case '"':
          dest[pos++] = '\\';
          dest[pos++] = '"';
          break;
        case '\\':
          dest[pos++] = '\\';
          dest[pos++] = '\\';
          break;
        case '\b':
          dest[pos++] = '\\';
          dest[pos++] = 'b';
          break;
        case '\f':
          dest[pos++] = '\\';
          dest[pos++] = 'f';
          break;
        case '\n':
          dest[pos++] = '\\';
          dest[pos++] = 'n';
          break;
        case '\r':
          dest[pos++] = '\\';
          dest[pos++] = 'r';
          break;
        case '\t':
          dest[pos++] = '\\';
          dest[pos++] = 't';
          break;
        default:
          if ((unsigned char)c < 0x20) {
            dest[pos++] = '\\';
            dest[pos++] = 'u';
            dest[pos++] = '0';
            dest[pos++] = '0';
            const char hex[] = "0123456789abcdef";
            dest[pos++] = hex[((unsigned char)c >> 4) & 0xf];
            dest[pos++] = hex[(unsigned char)c & 0xf];
          } else {
            dest[pos++] = c;
          }
          break;
      }
    }
  }
}

static void json_writer_append_char(json_writer_t* w, char c) {
  *array_list_push(w->buf, char, w->allocator) = c;
}

static void json_writer_append_str(json_writer_t* w, const char* str) {
  size_t len = strlen(str);
  char* dest = array_list_append(w->buf, len, char, w->allocator);
  memcpy(dest, str, len);
}

void json_writer_init(json_writer_t* w, bool indent, array_list_t* out_buf,
                      allocator_t a) {
  w->buf = out_buf;
  w->allocator = a;
  w->depth = 0;
  w->first_item[0] = true;
  w->after_key = false;
  w->indent = indent;
  array_list_clear(out_buf);
  // Ensure buf has char type
  array_list_ensure_elem_size(out_buf, sizeof(char));
}

static void json_writer_indent(json_writer_t* w) {
  if (w->indent) {
    for (size_t i = 0; i < w->depth; i++) {
      json_writer_append_str(w, "  ");
    }
  }
}

static void json_writer_prepare_value(json_writer_t* w) {
  if (w->after_key) {
    w->after_key = false;
    w->first_item[w->depth] = false;
  } else {
    if (w->depth > 0) {
      if (!w->first_item[w->depth]) {
        if (w->indent) {
          json_writer_append_str(w, ",\n");
          json_writer_indent(w);
        } else {
          json_writer_append_char(w, ',');
        }
      } else {
        if (w->indent) {
          json_writer_indent(w);
        }
      }
      w->first_item[w->depth] = false;
    }
  }
}

void json_writer_begin_object(json_writer_t* w) {
  json_writer_prepare_value(w);
  if (w->indent) {
    json_writer_append_str(w, "{\n");
  } else {
    json_writer_append_char(w, '{');
  }
  if (w->depth + 1 < 32) {
    w->depth++;
    w->first_item[w->depth] = true;
  }
}

void json_writer_end_object(json_writer_t* w) {
  if (w->depth > 0) {
    w->depth--;
    if (w->indent) {
      json_writer_append_char(w, '\n');
      json_writer_indent(w);
    }
    w->first_item[w->depth] = false;
  }
  json_writer_append_char(w, '}');
}

void json_writer_begin_array(json_writer_t* w) {
  json_writer_prepare_value(w);
  if (w->indent) {
    json_writer_append_str(w, "[\n");
  } else {
    json_writer_append_char(w, '[');
  }
  if (w->depth + 1 < 32) {
    w->depth++;
    w->first_item[w->depth] = true;
  }
}

void json_writer_end_array(json_writer_t* w) {
  if (w->depth > 0) {
    w->depth--;
    if (w->indent) {
      json_writer_append_char(w, '\n');
      json_writer_indent(w);
    }
    w->first_item[w->depth] = false;
  }
  json_writer_append_char(w, ']');
}

void json_writer_name(json_writer_t* w, string_view_t name) {
  if (w->depth > 0) {
    if (!w->first_item[w->depth]) {
      if (w->indent) {
        json_writer_append_str(w, ",\n");
        json_writer_indent(w);
      } else {
        json_writer_append_char(w, ',');
      }
    } else {
      if (w->indent) {
        json_writer_indent(w);
      }
    }
    w->first_item[w->depth] = false;
  }
  json_writer_append_char(w, '"');
  json_writer_write_escaped(w->buf, name, w->allocator);
  if (w->indent) {
    json_writer_append_str(w, "\": ");
  } else {
    json_writer_append_str(w, "\":");
  }
  w->after_key = true;
}

void json_writer_string(json_writer_t* w, string_view_t val) {
  json_writer_prepare_value(w);
  json_writer_append_char(w, '"');
  json_writer_write_escaped(w->buf, val, w->allocator);
  json_writer_append_char(w, '"');
}

void json_writer_number_double(json_writer_t* w, double val) {
  json_writer_prepare_value(w);
  char tmp[64];
  int len = snprintf(tmp, sizeof(tmp), "%.6g", val);
  if (len > 0) {
    char* dest = array_list_append(w->buf, (size_t)len, char, w->allocator);
    memcpy(dest, tmp, (size_t)len);
  }
}

void json_writer_number_int(json_writer_t* w, int64_t val) {
  json_writer_prepare_value(w);
  char tmp[32];
  int len = snprintf(tmp, sizeof(tmp), "%" PRId64, val);
  if (len > 0) {
    char* dest = array_list_append(w->buf, (size_t)len, char, w->allocator);
    memcpy(dest, tmp, (size_t)len);
  }
}

void json_writer_bool(json_writer_t* w, bool val) {
  json_writer_prepare_value(w);
  if (val) {
    json_writer_append_str(w, "true");
  } else {
    json_writer_append_str(w, "false");
  }
}

void json_writer_null(json_writer_t* w) {
  json_writer_prepare_value(w);
  json_writer_append_str(w, "null");
}
