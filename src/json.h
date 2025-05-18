#ifndef ZTRACING_SRC_JSON_H_
#define ZTRACING_SRC_JSON_H_

#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"

typedef enum JsonTokenType {
  JsonTokenType_Eof,
  JsonTokenType_Error,

  JsonTokenType_OpenBrace,
  JsonTokenType_CloseBrace,
  JsonTokenType_OpenBracket,
  JsonTokenType_CloseBracket,
  JsonTokenType_Comma,
  JsonTokenType_Colon,
  JsonTokenType_StringLiteral,
  JsonTokenType_Number,
  JsonTokenType_True,
  JsonTokenType_False,
  JsonTokenType_Null,

  JsonTokenType_Count,
} JsonTokenType;

typedef struct JsonToken {
  JsonTokenType type;
  Str value;
} JsonToken;

typedef Str JsonGetInputFn(void *context);

typedef enum JsonValueType {
  JsonValueType_Error,
  JsonValueType_Object,
  JsonValueType_Array,
  JsonValueType_String,
  JsonValueType_Number,
  JsonValueType_True,
  JsonValueType_False,
  JsonValueType_Null,
} JsonValueType;

static inline const char *JsonValueType_ToString(JsonValueType type) {
  switch (type) {
    case JsonValueType_Error:
      return "error";
    case JsonValueType_Object:
      return "object";
    case JsonValueType_Array:
      return "array";
    case JsonValueType_String:
      return "string";
    case JsonValueType_Number:
      return "number";
    case JsonValueType_True:
      return "true";
    case JsonValueType_False:
      return "false";
    case JsonValueType_Null:
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
  Str label;
  Str value;
};

f64 JsonValue_ToF64(JsonValue *value);

typedef struct JsonParser {
  u8 tmp;
  Str buf;
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

JsonToken JsonParser_ParseToken(JsonParser *self, Arena *arena);
JsonValue *JsonParser_ParseValue(JsonParser *self, Arena *arena);

#endif  // ZTRACING_SRC_JSON_H_
