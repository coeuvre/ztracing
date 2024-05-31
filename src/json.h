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

struct JsonParser {
    Arena *arena;
    TempArena temp_arena;
    GetJsonInputFunc get_json_input;
    void *get_json_input_data;
    Buffer buffer;
    usize cursor;
};

static JsonToken GetJsonToken(JsonParser *parser);
static void EndJsonParse(JsonParser *parser);
