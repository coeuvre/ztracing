#include "core/json_writer.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/allocator.h"
#include "src/array_list.h"
#include "src/string.h"

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
                                      allocator_t* a) {
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
                      allocator_t* a) {
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
