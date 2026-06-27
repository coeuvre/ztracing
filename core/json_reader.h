#ifndef CORE_JSON_READER_H
#define CORE_JSON_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/string.h"

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
  struct {
    string_view_t str;
    union {
      int64_t i64;  // Active when type is JSON_TOKEN_NUMBER_I64
      double f64;   // Active when type is JSON_TOKEN_NUMBER_F64
    };
  } val;
} json_token_t;

typedef struct json_reader {
  const char* buf;  // Pointer to the JSON input buffer
  size_t len;       // Total length of the input buffer
  size_t pos;       // Current parsing position (offset from buf)
} json_reader_t;

static inline bool json_reader_done(const json_reader_t* r) {
  return r->pos >= r->len;
}

void json_reader_init(json_reader_t* r, const char* buf, size_t len);
void json_reader_next(json_reader_t* r, json_token_t* out_token);

#ifdef __cplusplus
}
#endif

#endif  // CORE_JSON_READER_H
