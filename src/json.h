#ifndef ZTRACING_SRC_JSON_H_
#define ZTRACING_SRC_JSON_H_

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "src/allocator.h"
#include "src/array_list.h"
#include "src/string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum json_token_type {
  JSON_TOKEN_NONE,
  JSON_TOKEN_OBJECT_START,  // {
  JSON_TOKEN_OBJECT_END,    // }
  JSON_TOKEN_ARRAY_START,   // [
  JSON_TOKEN_ARRAY_END,     // ]
  JSON_TOKEN_STRING,
  JSON_TOKEN_NUMBER,
  JSON_TOKEN_BOOLEAN,
  JSON_TOKEN_NULL_VAL,
  JSON_TOKEN_COLON,  // :
  JSON_TOKEN_COMMA,  // ,
  JSON_TOKEN_ERROR,
} json_token_type_t;

typedef struct json_token {
  json_token_type_t type;
  string_t str;
} json_token_t;

typedef struct json_reader {
  const char* buf;
  size_t len;
  size_t pos;
} json_reader_t;

static inline bool json_reader_done(const json_reader_t* r) {
  return r->pos >= r->len;
}

static inline char json_reader_peek(const json_reader_t* r) {
  return r->pos < r->len ? r->buf[r->pos] : 0;
}

static inline void json_reader_advance(json_reader_t* r) {
  if (r->pos < r->len) {
    r->pos++;
  }
}

static inline void json_reader_skip_whitespace(json_reader_t* r) {
  while (!json_reader_done(r) && isspace((unsigned char)json_reader_peek(r))) {
    json_reader_advance(r);
  }
}

void json_reader_init(json_reader_t* r, const char* buf, size_t len);
bool json_reader_read_string(json_reader_t* r, string_t* out_val);
bool json_reader_read_number(json_reader_t* r, string_t* out_val);
bool json_reader_read_literal(json_reader_t* r, const char* literal,
                              json_token_type_t type, json_token_t* out_token);
json_token_t json_reader_next(json_reader_t* r);

// ==========================================
// 2. JSON WRITER (Formatter)
// ==========================================
typedef struct json_writer {
  array_list_t* buf;  // Destination buffer (dynamic array of char)
  allocator_t allocator;
  bool first_item[32];  // Nesting stack to track if we need to write commas
  size_t depth;
  bool after_key;  // State flag: true if we just wrote a key name
  bool indent;     // Pretty-print flag: true to indent, false to minify
} json_writer_t;

void json_writer_init(json_writer_t* w, bool indent, array_list_t* out_buf,
                      allocator_t a);
void json_writer_begin_object(json_writer_t* w);
void json_writer_end_object(json_writer_t* w);
void json_writer_begin_array(json_writer_t* w);
void json_writer_end_array(json_writer_t* w);

// Write key names (only valid inside objects)
void json_writer_name(json_writer_t* w, string_t name);

// Write values (automatically handles leading commas based on nesting state)
void json_writer_string(json_writer_t* w, string_t val);
void json_writer_number_double(json_writer_t* w, double val);
void json_writer_number_int(json_writer_t* w, int64_t val);
void json_writer_bool(json_writer_t* w, bool val);
void json_writer_null(json_writer_t* w);

#ifdef __cplusplus
}
#endif

#endif  // ZTRACING_SRC_JSON_H_
