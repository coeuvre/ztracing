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
  JSON_TOKEN_EOF,
  JSON_TOKEN_OBJECT_START,  // {
  JSON_TOKEN_OBJECT_END,    // }
  JSON_TOKEN_ARRAY_START,   // [
  JSON_TOKEN_ARRAY_END,     // ]
  JSON_TOKEN_STRING,
  JSON_TOKEN_NUMBER_I64,
  JSON_TOKEN_NUMBER_F64,
  JSON_TOKEN_TRUE,
  JSON_TOKEN_FALSE,
  JSON_TOKEN_NULL,
  JSON_TOKEN_COLON,  // :
  JSON_TOKEN_COMMA,  // ,
  JSON_TOKEN_ERROR,
} json_token_type_t;

typedef struct json_token {
  json_token_type_t type;
  // We keep 'str' outside the anonymous union so that it is NOT overwritten
  // when parsing numeric values. This is crucial for Chrome trace parsing
  // because polymorphic fields like event 'id' (which can be either a string
  // or a number) must be stored as raw string views pointing directly into
  // the JSON buffer to avoid dynamic memory allocation.
  struct {
    string_view_t str;
    union {
      int64_t i64;  // Active when type is JSON_TOKEN_NUMBER_I64
      double f64;   // Active when type is JSON_TOKEN_NUMBER_F64
    };
  } val;
} json_token_t;

// A lightweight, allocation-free, streaming JSON parser.
// It parses names, numbers, strings, and literals on-the-fly directly from
// a provided character buffer.
typedef struct json_reader {
  const char* buf;  // Pointer to the JSON input buffer
  size_t len;       // Total length of the input buffer
  size_t pos;       // Current parsing position (offset from buf)
} json_reader_t;

// Returns true if the reader has reached the end of the input buffer.
static inline bool json_reader_done(const json_reader_t* r) {
  return r->pos >= r->len;
}

// Initializes the JSON reader with the given buffer and length.
// The buffer is not copied and must remain valid during parsing.
void json_reader_init(json_reader_t* r, const char* buf, size_t len);

// Parses and writes the next JSON token into out_token, skipping any leading
// whitespace. Writes a token with type JSON_TOKEN_EOF if the end of the buffer
// is reached. Writes a token with type JSON_TOKEN_ERROR on parsing failures.
void json_reader_next(json_reader_t* r, json_token_t* out_token);

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
void json_writer_name(json_writer_t* w, string_view_t name);

// Write values (automatically handles leading commas based on nesting state)
void json_writer_string(json_writer_t* w, string_view_t val);
void json_writer_number_double(json_writer_t* w, double val);
void json_writer_number_int(json_writer_t* w, int64_t val);
void json_writer_bool(json_writer_t* w, bool val);
void json_writer_null(json_writer_t* w);

#ifdef __cplusplus
}
#endif

#endif  // ZTRACING_SRC_JSON_H_
