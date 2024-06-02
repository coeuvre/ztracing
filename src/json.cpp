#include "json.h"

#include <math.h>

JsonParser *
BeginJsonParse(Arena *arena, GetJsonInputFunc get_json_input, void *data) {
    JsonParser *parser = PushStruct(arena, JsonParser);
    parser->get_json_input = get_json_input;
    parser->get_json_input_data = data;
    parser->value_arena = arena;
    return parser;
}

static void
ReturnInput(JsonParser *parser, u8 val) {
    ASSERT(parser->tmp == 0);
    parser->tmp = val;
}

static u8
TakeInput(JsonParser *parser) {
    u8 val = parser->tmp;
    if (val) {
        parser->tmp = 0;
    } else {
        if (parser->cursor >= parser->buffer.size) {
            // TODO: Handle read error
            parser->buffer =
                parser->get_json_input(parser->get_json_input_data);
            parser->cursor = 0;
        }
        if (parser->cursor < parser->buffer.size) {
            val = parser->buffer.data[parser->cursor];
            parser->cursor++;
        }
    }
    return val;
}

static Buffer
TakeInput(JsonParser *parser, usize count) {
    Buffer buffer = PushBuffer(parser->token_temp_arena.arena, count);
    for (usize index = 0; index < count; ++index) {
        u8 val = TakeInput(parser);
        if (val == 0) {
            break;
        }
        buffer.data[index] = val;
    }
    return buffer;
}

static bool
IsJsonWhitespace(u8 val) {
    bool result = val == ' ' || val == '\t' || val == '\n' || val == '\r';
    return result;
}

static void
SkipWhitespace(JsonParser *parser) {
    while (true) {
        u8 val = TakeInput(parser);
        if (!IsJsonWhitespace(val)) {
            ReturnInput(parser, val);
            return;
        }
    }
}

static void
MaybeEndTokenTempArena(JsonParser *parser) {
    if (parser->token_temp_arena.arena) {
        EndTempArena(parser->token_temp_arena);
        parser->token_temp_arena.arena = 0;
    }
}

static Arena *
BeginTempTokenArena(JsonParser *parser) {
    ASSERT(!parser->token_temp_arena.arena);
    parser->token_temp_arena = BeginTempArena(&parser->token_arena);
    return parser->token_temp_arena.arena;
}

static void
Append(Arena *arena, Buffer *buffer, usize *cursor, u8 val) {
    if (*cursor >= buffer->size) {
        Buffer new_buffer = PushBuffer(arena, buffer->size << 1);
        CopyMemory(new_buffer.data, buffer->data, buffer->size);
        *buffer = new_buffer;
    }
    buffer->data[(*cursor)++] = val;
}

static bool
ParseDigits(JsonParser *parser, Arena *arena, Buffer *buffer, usize *cursor) {
    bool has_digits = false;
    bool done = false;
    while (!done) {
        u8 val = TakeInput(parser);
        if (val >= '0' && val <= '9') {
            Append(arena, buffer, cursor, val);
            has_digits = true;
        } else {
            ReturnInput(parser, val);
            done = true;
        }
    }
    return has_digits;
}

JsonToken
GetJsonToken(JsonParser *parser) {
    JsonToken token = {};

    MaybeEndTokenTempArena(parser);
    Arena *arena = BeginTempTokenArena(parser);

    SkipWhitespace(parser);

    u8 val = TakeInput(parser);
    switch (val) {
    case '{': {
        token.type = JsonToken_OpenBrace;
    } break;

    case '}': {
        token.type = JsonToken_CloseBrace;
    } break;

    case '[': {
        token.type = JsonToken_OpenBracket;
    } break;

    case ']': {
        token.type = JsonToken_CloseBracket;
    } break;

    case ',': {
        token.type = JsonToken_Comma;
    } break;

    case ':': {
        token.type = JsonToken_Colon;
    } break;

    case ';': {
        token.type = JsonToken_SemiColon;
    } break;

    case 't': {
        Buffer expected_suffix = STRING_LITERAL("rue");
        Buffer suffix = TakeInput(parser, expected_suffix.size);
        if (AreEqual(expected_suffix, suffix)) {
            token.type = JsonToken_True;
        } else {
            token.type = JsonToken_Error;
            token.value = PushFormat(
                arena,
                "expecting 'true', but got 't%.*s'",
                suffix.size,
                suffix.data
            );
        }
    } break;

    case 'f': {
        Buffer expected_suffix = STRING_LITERAL("alse");
        Buffer suffix = TakeInput(parser, expected_suffix.size);
        if (AreEqual(expected_suffix, suffix)) {
            token.type = JsonToken_False;
        } else {
            token.type = JsonToken_Error;
            token.value = PushFormat(
                arena,
                "expecting 'false', but got 'f%.*s'",
                suffix.size,
                suffix.data
            );
        }
    } break;

    case 'n': {
        Buffer expected_suffix = STRING_LITERAL("ull");
        Buffer suffix = TakeInput(parser, expected_suffix.size);
        if (AreEqual(expected_suffix, suffix)) {
            token.type = JsonToken_Null;
        } else {
            token.type = JsonToken_Error;
            token.value = PushFormat(
                arena,
                "expecting 'null', but got 'n%.*s'",
                suffix.size,
                suffix.data
            );
        }
    } break;

    case '"': {
        u8 prev[2] = {};
        bool done = false;
        bool found_close_quote = false;

        // TODO: Prefer use input directly
        Buffer buffer = PushBuffer(arena, 1024);
        usize cursor = 0;

        while (!done) {
            u8 val = TakeInput(parser);
            if (val == 0) {
                done = true;
            } else if (val == '"' && (prev[1] != '\\' || prev[0] == '\\')) {
                found_close_quote = true;
                done = true;
            } else {
                Append(arena, &buffer, &cursor, val);
            }
            prev[0] = prev[1];
            prev[1] = val;
        }

        if (found_close_quote) {
            token.type = JsonToken_StringLiteral;
            token.value.data = buffer.data;
            token.value.size = cursor;
        } else {
            token.type = JsonToken_Error;
            token.value = PushFormat(arena, "expecting '\"', but got EOF");
        }
    } break;

    case '-':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9': {
        // TODO: Prefer use input directly
        Buffer buffer = PushBuffer(arena, 1024);
        usize cursor = 0;
        Append(arena, &buffer, &cursor, val);

        bool done = false;
        if (val == '-') {
            val = TakeInput(parser);
            if (val >= '0' && val <= '9') {
                Append(arena, &buffer, &cursor, val);
            } else {
                ReturnInput(parser, val);

                token.type = JsonToken_Error;
                token.value = PushFormat(
                    arena,
                    "Invalid number '%.*s', expecting digits but got EOF",
                    buffer.size,
                    buffer.data
                );
                done = true;
            }
        }

        if (!done && val != '0') {
            ParseDigits(parser, arena, &buffer, &cursor);
        }

        if (!done) {
            val = TakeInput(parser);
            if (val == '.') {
                Append(arena, &buffer, &cursor, val);
                if (!ParseDigits(parser, arena, &buffer, &cursor)) {
                    val = TakeInput(parser);
                    ReturnInput(parser, val);

                    token.type = JsonToken_Error;
                    token.value = PushFormat(
                        arena,
                        "Invalid number '%.*s', expecting digits but got '%c'",
                        buffer.size,
                        buffer.data,
                        val
                    );
                    done = true;
                }
            } else {
                ReturnInput(parser, val);

                token.type = JsonToken_Number;
                token.value.data = buffer.data;
                token.value.size = cursor;
                done = true;
            }
        }

        if (!done) {
            val = TakeInput(parser);
            if (val == 'e' || val == 'E') {
                Append(arena, &buffer, &cursor, val);
                if (!ParseDigits(parser, arena, &buffer, &cursor)) {
                    val = TakeInput(parser);
                    ReturnInput(parser, val);

                    token.type = JsonToken_Error;
                    token.value = PushFormat(
                        arena,
                        "Invalid number '%.*s', expecting digits but got '%c'",
                        buffer.size,
                        buffer.data,
                        val
                    );
                    done = true;
                } else {
                    token.type = JsonToken_Number;
                    token.value.data = buffer.data;
                    token.value.size = cursor;
                    done = true;
                }
            } else {
                ReturnInput(parser, val);
                token.type = JsonToken_Number;
                token.value.data = buffer.data;
                token.value.size = cursor;
                done = true;
            }
        }

        ASSERT(done);
    } break;

    // EOF
    case 0: {
    } break;

    default: {
        token.type = JsonToken_Error;
        token.value = PushFormat(arena, "Unexpected character: '%c'", val);
    } break;
    }

    return token;
}

static void
MaybeEndValueTempArena(JsonParser *parser) {
    if (parser->value_temp_arena.arena) {
        EndTempArena(parser->value_temp_arena);
        parser->value_temp_arena.arena = 0;
    }
}

static Arena *
BeginValueTempArena(JsonParser *parser) {
    ASSERT(!parser->value_temp_arena.arena);
    parser->value_temp_arena = BeginTempArena(parser->value_arena);
    return parser->value_temp_arena.arena;
}

static bool
ExpectToken(JsonParser *parser, Arena *arena, JsonTokenType type) {
    bool got = false;
    JsonToken token = GetJsonToken(parser);
    if (token.type == type) {
        got = true;
    } else {
        parser->error = PushFormat(
            arena,
            "Unexpected token '%.*s'",
            token.value.size,
            token.value.data
        );
    }
    return got;
}

static JsonValue *ParseJsonValue(JsonParser *parser, Arena *arena);

static JsonValue *
ParseJsonObject(JsonParser *parser, Arena *arena) {
    JsonValue *result = PushStruct(arena, JsonValue);
    result->type = JsonValue_Object;
    JsonValue *last_child = 0;
    bool has_value = false;
    bool done = false;
    while (!done) {
        JsonToken token = GetJsonToken(parser);
        switch (token.type) {
        case JsonToken_StringLiteral: {
            Buffer key = PushBuffer(arena, token.value);

            if (ExpectToken(parser, arena, JsonToken_Colon)) {
                JsonValue *child = ParseJsonValue(parser, arena);
                if (child) {
                    has_value = true;
                    child->label = key;
                    if (last_child) {
                        last_child->next = child;
                    } else {
                        result->child = child;
                    }
                    last_child = child;
                } else {
                    result = 0;
                    done = true;
                }
            } else {
                parser->error = PushFormat(
                    arena,
                    "expecting ':', but got %.*s",
                    token.value.size,
                    token.value.data
                );
                result = 0;
                done = true;
            }
        } break;

        case JsonToken_Comma: {
            if (!has_value) {
                parser->error =
                    PushFormat(arena, "expecting string or '}', but got ','");
                result = 0;
                done = true;
            }
        } break;

        case JsonToken_CloseBrace: {
            done = true;
        } break;

        default: {
            parser->error = PushFormat(
                arena,
                "Unexpected token '%.*s'",
                token.value.size,
                token.value.data
            );
            result = 0;
            done = true;
        };
        }
    }
    return result;
}

static JsonValue *
ParseJsonArray(JsonParser *parser, Arena *arena) {
    JsonValue *result = PushStruct(arena, JsonValue);
    result->type = JsonValue_Array;
    JsonValue *last_child = 0;
    bool done = false;
    while (!done) {
        JsonValue *child = ParseJsonValue(parser, arena);
        if (child) {
            if (last_child) {
                last_child->next = child;
            } else {
                result->child = child;
            }
            last_child = child;

            JsonToken token = GetJsonToken(parser);
            switch (token.type) {
            case JsonToken_Comma: {
            } break;
            case JsonToken_CloseBracket: {
                done = true;
            } break;
            default: {
                parser->error = PushFormat(
                    arena,
                    "Unexpected token %.*s",
                    token.value.size,
                    token.value.data
                );
                result = 0;
                done = true;
            } break;
            }
        } else {
            result = 0;
            done = true;
        }
    }
    return result;
}

static JsonValue *
ParseJsonValue(JsonParser *parser, Arena *arena) {
    JsonValue *result = 0;

    JsonToken token = GetJsonToken(parser);
    switch (token.type) {
    case JsonToken_OpenBrace: {
        result = ParseJsonObject(parser, arena);
    } break;

    case JsonToken_OpenBracket: {
        result = ParseJsonArray(parser, arena);
    } break;

    case JsonToken_StringLiteral: {
        result = PushStruct(arena, JsonValue);
        result->type = JsonValue_String;
        result->value = PushBuffer(arena, token.value);
    } break;

    case JsonToken_Number: {
        result = PushStruct(arena, JsonValue);
        result->type = JsonValue_Number;
        result->value = PushBuffer(arena, token.value);
    } break;

    case JsonToken_True: {
        result = PushStruct(arena, JsonValue);
        result->type = JsonValue_True;
    } break;

    case JsonToken_False: {
        result = PushStruct(arena, JsonValue);
        result->type = JsonValue_False;
    } break;

    case JsonToken_Null: {
        result = PushStruct(arena, JsonValue);
        result->type = JsonValue_Null;
    } break;

    default: {
        parser->error = PushFormat(
            arena,
            "Unexpected token '%.*s'",
            token.value.size,
            token.value.data
        );
    } break;
    }
    return result;
}

JsonValue *
GetJsonValue(JsonParser *parser) {
    MaybeEndValueTempArena(parser);
    Arena *arena = BeginValueTempArena(parser);
    JsonValue *result = ParseJsonValue(parser, arena);
    return result;
}

Buffer
GetJsonError(JsonParser *parser) {
    return parser->error;
}

void
EndJsonParse(JsonParser *parser) {
    MaybeEndTokenTempArena(parser);
    ClearArena(&parser->token_arena);

    MaybeEndValueTempArena(parser);
}

static f64
ConvertSign(Buffer buffer, usize *cursor) {
    f64 sign = 1.0;
    if (buffer.data[*cursor] == '-') {
        sign = -1.0;
        (*cursor)++;
    }
    return sign;
}

static f64
ConvertNumber(Buffer buffer, usize *out_cursor) {
    f64 result = 0.0;
    usize cursor = *out_cursor;
    while (cursor < buffer.size) {
        u8 val = buffer.data[cursor] - '0';
        if (val < 10) {
            result = 10.0 * result + (f64)val;
            ++cursor;
        } else {
            break;
        }
    }
    *out_cursor = cursor;
    return result;
}

f64
ConvertJsonValueToF64(JsonValue *value) {
    Buffer buffer = value->value;
    usize cursor = 0;
    f64 sign = ConvertSign(buffer, &cursor);
    f64 number = ConvertNumber(buffer, &cursor);

    if (cursor < buffer.size && buffer.data[cursor] == '.') {
        ++cursor;
        f64 c = 1.0 / 10.0;
        while (cursor < buffer.size) {
            u8 val = buffer.data[cursor] - '0';
            if (val < 10) {
                number = number + c * (f64)val;
                c *= 1.0 / 10.0;
                ++cursor;
            } else {
                break;
            }
        }
    }

    if (cursor < buffer.size &&
        (buffer.data[cursor] == 'e' || buffer.data[cursor] == 'E')) {
        ++cursor;
        if (cursor < buffer.size && buffer.data[cursor] == '+') {
            ++cursor;
        }

        f64 exponent_sign = ConvertSign(buffer, &cursor);
        f64 exponent = exponent_sign * ConvertNumber(buffer, &cursor);
        number *= pow(10.0, exponent);
    }

    f64 result = sign * number;
    return result;
}
