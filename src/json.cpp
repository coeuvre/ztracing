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

enum JsonValueType {
    JsonValue_Error,
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
    u8 tmp;
    Buffer buffer;
    isize cursor;
    GetJsonInputFunc get_json_input;
    void *get_json_input_data;
};

static JsonParser
InitJsonParser(GetJsonInputFunc get_json_input, void *data) {
    JsonParser parser = {};
    parser.get_json_input = get_json_input;
    parser.get_json_input_data = data;
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
TakeInput(Arena *arena, JsonParser *parser, isize count) {
    Buffer buffer = PushBufferNoZero(arena, count);
    for (isize index = 0; index < count; ++index) {
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

static inline void
Append(Arena *arena, Buffer *buffer, isize *cursor, u8 val) {
    if (*cursor >= buffer->size) {
        Buffer new_buffer = PushBufferNoZero(arena, buffer->size << 1);
        CopyMemory(new_buffer.data, buffer->data, buffer->size);
        *buffer = new_buffer;
    }
    buffer->data[(*cursor)++] = val;
}

static inline bool
ParseDigits(JsonParser *parser, Arena *arena, Buffer *buffer, isize *cursor) {
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

static JsonToken
GetJsonToken(Arena *arena, JsonParser *parser) {
    JsonToken token = {};

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
        Buffer suffix = TakeInput(arena, parser, expected_suffix.size);
        if (Equal(expected_suffix, suffix)) {
            token.type = JsonToken_True;
        } else {
            token.type = JsonToken_Error;
            token.value = PushFormat(
                arena,
                "expecting 'true', but got 't%.*s'",
                (int)suffix.size,
                suffix.data
            );
        }
    } break;

    case 'f': {
        Buffer expected_suffix = STRING_LITERAL("alse");
        Buffer suffix = TakeInput(arena, parser, expected_suffix.size);
        if (Equal(expected_suffix, suffix)) {
            token.type = JsonToken_False;
        } else {
            token.type = JsonToken_Error;
            token.value = PushFormat(
                arena,
                "expecting 'false', but got 'f%.*s'",
                (int)suffix.size,
                suffix.data
            );
        }
    } break;

    case 'n': {
        Buffer expected_suffix = STRING_LITERAL("ull");
        Buffer suffix = TakeInput(arena, parser, expected_suffix.size);
        if (Equal(expected_suffix, suffix)) {
            token.type = JsonToken_Null;
        } else {
            token.type = JsonToken_Error;
            token.value = PushFormat(
                arena,
                "expecting 'null', but got 'n%.*s'",
                (int)suffix.size,
                suffix.data
            );
        }
    } break;

    case '"': {
        u8 prev[2] = {};
        bool done = false;
        bool found_close_quote = false;

        // TODO: Prefer use input directly
        Buffer buffer = PushBufferNoZero(arena, 1024);
        isize cursor = 0;

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
        Buffer buffer = PushBufferNoZero(arena, 1024);
        isize cursor = 0;
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
                    (int)buffer.size,
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
                        "Invalid number '%.*s', expecting digits after '.' but "
                        "got '%c'",
                        (int)buffer.size,
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

                val = TakeInput(parser);
                if (val == '-' || val >= '0' && val <= '9') {
                    Append(arena, &buffer, &cursor, val);

                    ParseDigits(parser, arena, &buffer, &cursor);

                    token.type = JsonToken_Number;
                    token.value.data = buffer.data;
                    token.value.size = cursor;
                    done = true;
                } else {
                    ReturnInput(parser, val);

                    token.type = JsonToken_Error;
                    token.value = PushFormat(
                        arena,
                        "Invalid number '%.*s', expecting sign or digits after "
                        "'E' but got '%c'",
                        (int)buffer.size,
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

static void ParseJsonValue(
    Arena *arena,
    Arena scratch,
    JsonParser *parser,
    JsonValue *result
);

static void
ParseJsonObject(
    Arena *arena,
    Arena scratch,
    JsonParser *parser,
    JsonValue *result
) {
    JsonValue *last_child = 0;
    bool has_value = false;
    bool done = false;
    while (!done) {
        JsonToken token = GetJsonToken(&scratch, parser);
        switch (token.type) {
        case JsonToken_StringLiteral: {
            Buffer key = PushBuffer(arena, token.value);
            token = GetJsonToken(&scratch, parser);
            if (token.type == JsonToken_Colon) {
                JsonValue *child = PushStruct(arena, JsonValue);
                ParseJsonValue(arena, scratch, parser, child);
                if (child->type != JsonValue_Error) {
                    has_value = true;
                    child->label = key;
                    if (last_child) {
                        last_child->next = child;
                    } else {
                        result->child = child;
                    }
                    last_child = child;
                } else {
                    *result = *child;
                    done = true;
                }
            } else if (token.type == JsonToken_Error) {
                result->type = JsonValue_Error;
                result->value = PushBuffer(arena, token.value);
                done = true;
            } else {
                result->type = JsonValue_Error;
                result->value = PushFormat(
                    arena,
                    "expecting ':', but got %.*s",
                    (int)token.value.size,
                    token.value.data
                );
                done = true;
            }
        } break;

        case JsonToken_Comma: {
            if (!has_value) {
                result->type = JsonValue_Error;
                result->value =
                    PushFormat(arena, "expecting string or '}', but got ','");
                done = true;
            }
        } break;

        case JsonToken_CloseBrace: {
            result->type = JsonValue_Object;
            done = true;
        } break;

        case JsonToken_Error: {
            result->type = JsonValue_Error;
            result->value = PushBuffer(arena, token.value);
            done = true;
        } break;

        default: {
            result->type = JsonValue_Error;
            result->value = PushFormat(
                arena,
                "Unexpected token '%.*s'",
                (int)token.value.size,
                token.value.data
            );
            done = true;
        };
        }
    }
}

static void
ParseJsonArray(
    Arena *arena,
    Arena scratch,
    JsonParser *parser,
    JsonValue *result
) {
    JsonValue *last_child = 0;
    bool done = false;
    while (!done) {
        JsonValue *child = PushStruct(arena, JsonValue);
        ParseJsonValue(arena, scratch, parser, child);
        if (child->type != JsonValue_Error) {
            if (last_child) {
                last_child->next = child;
            } else {
                result->child = child;
            }
            last_child = child;

            JsonToken token = GetJsonToken(&scratch, parser);
            switch (token.type) {
            case JsonToken_Comma: {
            } break;
            case JsonToken_CloseBracket: {
                result->type = JsonValue_Array;
                done = true;
            } break;
            case JsonToken_Error: {
                result->type = JsonValue_Error;
                result->value = PushBuffer(arena, token.value);
                done = true;
            } break;
            default: {
                result->type = JsonValue_Error;
                result->value = PushFormat(
                    arena,
                    "expecting token ',' or '], but got %.*s",
                    (int)token.value.size,
                    token.value.data
                );
                done = true;
            } break;
            }
        } else {
            *result = *child;
            done = true;
        }
    }
}

static void
ParseJsonValue(
    Arena *arena,
    Arena scratch,
    JsonParser *parser,
    JsonValue *result
) {
    JsonToken token = GetJsonToken(&scratch, parser);
    switch (token.type) {
    case JsonToken_OpenBrace: {
        ParseJsonObject(arena, scratch, parser, result);
    } break;

    case JsonToken_OpenBracket: {
        ParseJsonArray(arena, scratch, parser, result);
    } break;

    case JsonToken_StringLiteral: {
        result->type = JsonValue_String;
        result->value = PushBuffer(arena, token.value);
    } break;

    case JsonToken_Number: {
        result->type = JsonValue_Number;
        result->value = PushBuffer(arena, token.value);
    } break;

    case JsonToken_True: {
        result->type = JsonValue_True;
    } break;

    case JsonToken_False: {
        result->type = JsonValue_False;
    } break;

    case JsonToken_Null: {
        result->type = JsonValue_Null;
    } break;

    case JsonToken_Error: {
        result->value = PushBuffer(arena, token.value);
    } break;

    default: {
        result->value = PushFormat(
            arena,
            "Unexpected token '%.*s'",
            (int)token.value.size,
            token.value.data
        );
    } break;
    }
}

static JsonValue *
GetJsonValue(Arena *arena, Arena scratch, JsonParser *parser) {
    JsonValue *result = PushStruct(arena, JsonValue);
    ParseJsonValue(arena, scratch, parser, result);
    return result;
}

static f64
ConvertSign(Buffer buffer, isize *cursor) {
    f64 sign = 1.0;
    if (buffer.data[*cursor] == '-') {
        sign = -1.0;
        (*cursor)++;
    }
    return sign;
}

static f64
ConvertNumber(Buffer buffer, isize *out_cursor) {
    f64 result = 0.0;
    isize cursor = *out_cursor;
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

static f64
ConvertJsonValueToF64(JsonValue *value) {
    Buffer buffer = value->value;
    isize cursor = 0;
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
