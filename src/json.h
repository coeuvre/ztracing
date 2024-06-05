#pragma once

#include "core.h"
#include "memory.h"

enum JsonTokenType {
    JsonToken_Eof,
    JsonToken_Error,

    JsonToken_OpenBrace,
    JsonToken_OpenBracket,
    JsonToken_CloseBrace,
    JsonToken_CloseBracket,
    JsonToken_Comma,
    JsonToken_Colon,
    JsonToken_SemiColon,
    JsonToken_StringLiteral,
    JsonToken_Number,
    JsonToken_True,
    JsonToken_False,
    JsonToken_Null,

    JsonToken_COUNT,
};

struct JsonToken {
    JsonTokenType type;
    Buffer value;
};

typedef Buffer (*GetJsonInputFunc)(void *data);

struct JsonTokenizer {
    Arena arena;
    u8 tmp;
    Buffer buffer;
    isize cursor;
    GetJsonInputFunc get_json_input;
    void *get_json_input_data;
};

JsonToken GetJsonToken(JsonTokenizer *tokenizer);

enum JsonValueType {
    JsonValue_Object,
    JsonValue_Array,
    JsonValue_String,
    JsonValue_Number,
    JsonValue_True,
    JsonValue_False,
    JsonValue_Null,
};

struct JsonValue {
    JsonValueType type;
    Buffer label;
    Buffer value;
    JsonValue *child;
    JsonValue *next;
};

struct JsonParser {
    JsonTokenizer tokenizer;

    Arena *arena;
    Buffer error;
};

JsonParser *BeginJsonParse(
    Arena *arena,
    GetJsonInputFunc get_json_input,
    void *data
);
JsonValue *GetJsonValue(JsonParser *parser);
Buffer GetJsonError(JsonParser *parser);
void EndJsonParse(JsonParser *parser);

f64 ConvertJsonValueToF64(JsonValue *value);
