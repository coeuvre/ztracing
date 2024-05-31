#include "json.h"

static bool
IsJsonWhitespace(u8 val) {
    bool result = val == ' ' || val == '\t' || val == '\n' || val == '\r';
    return result;
}

static bool
IsJsonWhitespace(Buffer buffer, usize cursor) {
    bool result = false;
    if (cursor < buffer.size) {
        u8 val = buffer.data[cursor];
        result = IsJsonWhitespace(val);
    }
    return result;
}

static void
SkipWhitespace(JsonParser *parser) {
    while (true) {
        while (parser->cursor < parser->buffer.size) {
            if (IsJsonWhitespace(parser->buffer.data[parser->cursor])) {
                parser->cursor++;
            } else {
                return;
            }
        }

        // TODO: Handle read error
        parser->buffer = parser->get_json_input(parser->get_json_input_data);
        parser->cursor = 0;
        if (parser->buffer.size == 0) {
            return;
        }
    }
}

static void
MaybeEndTempArena(JsonParser *parser) {
    if (parser->temp_arena.arena) {
        EndTempArena(parser->temp_arena);
    }
}

static JsonToken
GetJsonToken(JsonParser *parser) {
    JsonToken token = {};

    MaybeEndTempArena(parser);
    parser->temp_arena = BeginTempArena(parser->arena);

    SkipWhitespace(parser);

    if (parser->cursor < parser->buffer.size) {
        u8 val = parser->buffer.data[parser->cursor];
        switch (val) {
        default: {
            token.type = JsonToken_Error;
            token.value =
                PushFormat(parser->arena, "Unexpected character: %c", val);
        } break;
        }
    }

    return token;
}

static void
EndJsonParse(JsonParser *parser) {
    MaybeEndTempArena(parser);
}
