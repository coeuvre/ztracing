#include "src/json.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/allocator.h"
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
      }
      pos++;
    }
    // We hit EOF before finding the closing quote. We must set the reader's
    // position to the end of the buffer so the caller knows we reached EOF
    // (for chunk-rollback checks).
    r->pos = pos;
  }
  return false;
}

// Attempts to read a JSON number starting at the current position.
// On success, 'out_val' is populated with a view of the parsed number
// and the reader is advanced past the number.
// Performs basic validation for signs, decimals, and scientific notation.
// Returns true on success (if at least one digit is parsed), or false
// otherwise.
static bool json_reader_read_number(json_reader_t* r, string_view_t* out_val) {
  const char* buf = r->buf;
  size_t len = r->len;
  size_t pos = r->pos;
  size_t start = pos;

  if (pos < len && buf[pos] == '-') {
    pos++;
  }

  if (pos < len && is_digit(buf[pos])) {
    while (pos < len) {
      char c = buf[pos];
      if (is_digit(c) || c == '.' || c == 'e' || c == 'E' || c == '+' ||
          c == '-') {
        pos++;
      } else {
        break;
      }
    }
    *out_val = string_view_from_parts(buf + start, pos - start);
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
        out_token->val = string_view_from_parts(r->buf + r->pos, literal_len);
      }
      r->pos += literal_len;
      success = true;
    }
  }
  return success;
}

json_token_t json_reader_next(json_reader_t* r) {
  json_token_t tok = {JSON_TOKEN_ERROR, {}};
  json_reader_skip_whitespace(r);

  const char* buf = r->buf;
  size_t len = r->len;
  size_t pos = r->pos;

  if (pos >= len) {
    tok.type = JSON_TOKEN_EOF;
    tok.val.ptr = NULL;
    tok.val.len = 0;
  } else {
    char c = buf[pos];
    switch (c) {
      case '{':
        r->pos = pos + 1;
        tok.type = JSON_TOKEN_OBJECT_START;
        tok.val = string_view_from_parts(buf + pos, 1);
        break;
      case '}':
        r->pos = pos + 1;
        tok.type = JSON_TOKEN_OBJECT_END;
        tok.val = string_view_from_parts(buf + pos, 1);
        break;
      case '[':
        r->pos = pos + 1;
        tok.type = JSON_TOKEN_ARRAY_START;
        tok.val = string_view_from_parts(buf + pos, 1);
        break;
      case ']':
        r->pos = pos + 1;
        tok.type = JSON_TOKEN_ARRAY_END;
        tok.val = string_view_from_parts(buf + pos, 1);
        break;
      case ':':
        r->pos = pos + 1;
        tok.type = JSON_TOKEN_COLON;
        tok.val = string_view_from_parts(buf + pos, 1);
        break;
      case ',':
        r->pos = pos + 1;
        tok.type = JSON_TOKEN_COMMA;
        tok.val = string_view_from_parts(buf + pos, 1);
        break;
      case '"':
        tok.type = JSON_TOKEN_STRING;
        if (!json_reader_read_string(r, &tok.val)) {
          tok.type = JSON_TOKEN_ERROR;
        }
        break;
      case 't':
        if (!json_reader_read_literal(r, "true", JSON_TOKEN_TRUE, &tok)) {
          tok.type = JSON_TOKEN_ERROR;
        }
        break;
      case 'f':
        if (!json_reader_read_literal(r, "false", JSON_TOKEN_FALSE, &tok)) {
          tok.type = JSON_TOKEN_ERROR;
        }
        break;
      case 'n':
        if (!json_reader_read_literal(r, "null", JSON_TOKEN_NULL, &tok)) {
          tok.type = JSON_TOKEN_ERROR;
        }
        break;
      default:
        if (is_digit(c) || c == '-') {
          tok.type = JSON_TOKEN_NUMBER;
          if (!json_reader_read_number(r, &tok.val)) {
            tok.type = JSON_TOKEN_ERROR;
          }
        } else {
          r->pos = pos + 1;
        }
        break;
    }
  }
  return tok;
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
  // Format beautifully as JSON number
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
