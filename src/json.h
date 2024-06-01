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

struct JsonValue {
    Buffer label;
    Buffer value;
    JsonValue *first_child;
    JsonValue *next_sibling;
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
};

static JsonParser *BeginJsonParse(
    Arena *arena,
    GetJsonInputFunc get_json_input,
    void *data
);
static JsonToken GetJsonToken(JsonParser *parser);
static JsonValue GetJsonValue(JsonParser *parser);
static void EndJsonParse(JsonParser *parser);
