#ifndef ZTRACING_SRC_JSON_H_
#define ZTRACING_SRC_JSON_H_

#include "src/string.h"
typedef enum JsonTokenType {
  JSON_TOKEN_EOF,
  JSON_TOKEN_ERROR,

  JSON_TOKEN_OPEN_BRACE,
  JSON_TOKEN_CLOSE_BRACE,
  JSON_TOKEN_OPEN_BRACKET,
  JSON_TOKEN_CLOSE_BRACKET,
  JSON_TOKEN_COMMA,
  JSON_TOKEN_COLON,
  JSON_TOKEN_STRING_LITERAL,
  JSON_TOKEN_NUMBER,
  JSON_TOKEN_TRUE,
  JSON_TOKEN_FALSE,
  JSON_TOKEN_NULL,

  JSON_TOKEN_COUNT,
} JsonTokenType;

typedef struct JsonToken {
  JsonTokenType type;
  Str8 value;
} JsonToken;

typedef Str8 JsonGetInputFn(void *context);

typedef enum JsonValueType {
  JSON_VALUE_ERROR,
  JSON_VALUE_OBJECT,
  JSON_VALUE_ARRAY,
  JSON_VALUE_STRING,
  JSON_VALUE_NUMBER,
  JSON_VALUE_TRUE,
  JSON_VALUE_FALSE,
  JSON_VALUE_NULL,
} JsonValueType;

static inline const char *json_value_type_string(JsonValueType type) {
  switch (type) {
    case JSON_VALUE_ERROR:
      return "error";
    case JSON_VALUE_OBJECT:
      return "object";
    case JSON_VALUE_ARRAY:
      return "array";
    case JSON_VALUE_STRING:
      return "string";
    case JSON_VALUE_NUMBER:
      return "number";
    case JSON_VALUE_TRUE:
      return "true";
    case JSON_VALUE_FALSE:
      return "false";
    case JSON_VALUE_NULL:
      return "null";
    default:
      return "unknown";
  }
}

typedef struct JsonValue JsonValue;
struct JsonValue {
  JsonValue *prev;
  JsonValue *next;
  JsonValue *first;
  JsonValue *last;

  JsonValueType type;
  Str8 label;
  Str8 value;
};

f64 json_value_as_f64(JsonValue *value);

typedef struct JsonParser {
  u8 tmp;
  Str8 buf;
  usize cursor;
  JsonGetInputFn *get_input;
  void *get_input_context;
} JsonParser;

static inline JsonParser json_parser(JsonGetInputFn get_input,
                                     void *get_input_context) {
  JsonParser self = {
      .get_input = get_input,
      .get_input_context = get_input_context,
  };
  return self;
}

JsonToken json_parser_parse_token(JsonParser *self, Arena *arena);
JsonValue *json_parser_parse_value(JsonParser *self, Arena *arena);

#endif  // ZTRACING_SRC_JSON_H_
