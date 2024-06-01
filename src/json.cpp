#include "json.h"

static JsonParser *
BeginJsonParse(Arena *arena, GetJsonInputFunc get_json_input, void *data) {
    JsonParser *parser = PushStruct(arena, JsonParser);
    parser->get_json_input = get_json_input;
    parser->get_json_input_data = data;
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
MaybeEndTempTokenArena(JsonParser *parser) {
    if (parser->token_temp_arena.arena) {
        EndTempArena(parser->token_temp_arena);
        parser->token_temp_arena = {};
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
    u8 val = TakeInput(parser);
    if (val >= '0' && val <= '9') {
        Append(arena, buffer, cursor, val);
        has_digits = true;
    } else {
        ReturnInput(parser, val);
    }
    return has_digits;
}

static JsonToken
GetJsonToken(JsonParser *parser) {
    JsonToken token = {};

    MaybeEndTempTokenArena(parser);
    Arena *arena = BeginTempTokenArena(parser);

    SkipWhitespace(parser);

    u8 val = TakeInput(parser);
    switch (val) {
    case '{': {
        token.type = JsonToken_OpenBrace;
        token.value = PushBuffer(arena, 1);
        token.value.data[0] = '{';
    } break;

    case '}': {
        token.type = JsonToken_CloseBrace;
        token.value = PushBuffer(arena, 1);
        token.value.data[0] = '}';
    } break;

    case '[': {
        token.type = JsonToken_OpenBracket;
        token.value = PushBuffer(arena, 1);
        token.value.data[0] = '[';
    } break;

    case ']': {
        token.type = JsonToken_CloseBracket;
        token.value = PushBuffer(arena, 1);
        token.value.data[0] = ']';
    } break;

    case ',': {
        token.type = JsonToken_Comma;
        token.value = PushBuffer(arena, 1);
        token.value.data[0] = ',';
    } break;

    case ':': {
        token.type = JsonToken_Colon;
        token.value = PushBuffer(arena, 1);
        token.value.data[0] = ':';
    } break;

    case ';': {
        token.type = JsonToken_SemiColon;
        token.value = PushBuffer(arena, 1);
        token.value.data[0] = ';';
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

        Buffer buffer = PushBuffer(arena, 4096);
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
        Buffer buffer = PushBuffer(arena, 4096);
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
            done = !ParseDigits(parser, arena, &buffer, &cursor);
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
EndJsonParse(JsonParser *parser) {
    MaybeEndTempTokenArena(parser);
    Clear(&parser->token_arena);
}
