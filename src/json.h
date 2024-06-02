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

typedef Buffer (*GetJsonInputFunc)(void *data);

struct JsonParser {
    GetJsonInputFunc get_json_input;
    void *get_json_input_data;

    Arena token_arena;
    TempArena token_temp_arena;
    u8 tmp;
    Buffer buffer;
    usize cursor;

    Arena *value_arena;
    TempArena value_temp_arena;
    Buffer error;
};

static JsonParser *BeginJsonParse(
    Arena *arena,
    GetJsonInputFunc get_json_input,
    void *data
);
static JsonToken GetJsonToken(JsonParser *parser);
static JsonValue *GetJsonValue(JsonParser *parser);
static Buffer GetJsonError(JsonParser *parser);
static void EndJsonParse(JsonParser *parser);

static f64 ConvertJsonValueToF64(JsonValue *value);
