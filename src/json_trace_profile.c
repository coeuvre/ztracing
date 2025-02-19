#include "src/json_trace_profile.h"

#include <stdbool.h>

#include "src/json.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"

static bool json_trace_profile__parse_array_format(JsonTraceProfile *self,
                                                   Arena *arena,
                                                   JsonParser *parser,
                                                   Arena scratch) {
  bool eof = false;
  bool on_going = true;
  while (on_going) {
    Arena scratch_ = scratch;
    JsonValue *value = json_parser_parse_value(parser, &scratch_);
    switch (value->type) {
      case JSON_VALUE_OBJECT: {
        // TODO: process trace event

        Arena scratch_ = scratch;
        JsonToken token = json_parser_parse_token(parser, &scratch_);
        switch (token.type) {
          case JSON_TOKEN_COMMA: {
          } break;

          case JSON_TOKEN_CLOSE_BRACKET: {
            on_going = false;
          } break;

          case JSON_TOKEN_ERROR: {
            self->error = arena_dup_str8(arena, token.value);
            on_going = false;
            eof = true;
          } break;

          default: {
            self->error =
                arena_push_str8f(arena, "expecting ',' or ']', got '%.*s'",
                                 (int)token.value.len, token.value.ptr);
          } break;
        }
      } break;

      case JSON_VALUE_ERROR: {
        self->error = arena_dup_str8(arena, value->value);
        on_going = false;
        eof = true;
      } break;

      default: {
        self->error =
            arena_push_str8f(arena, "expecting 'object', but got '%s'",
                             json_value_type_string(value->type));
        on_going = false;
        eof = true;
      } break;
    }
  }
  return eof;
}

static bool json_trace_profile__parse_array_format_expecting_open_bracket(
    JsonTraceProfile *self, Arena *arena, JsonParser *parser, Arena scratch) {
  bool eof = false;
  Arena scratch_ = scratch;
  JsonToken token = json_parser_parse_token(parser, &scratch_);
  switch (token.type) {
    case JSON_TOKEN_OPEN_BRACKET: {
      eof =
          json_trace_profile__parse_array_format(self, arena, parser, scratch);
    } break;

    case JSON_TOKEN_ERROR: {
      self->error = arena_dup_str8(arena, token.value);
      eof = true;
    } break;

    default: {
      self->error = arena_push_str8f(arena, "expecting '[', got '%.*s'",
                                     (int)token.value.len, token.value.ptr);
    } break;
  }

  return eof;
}

/// Skip tokens until next key-value pair in this object. Returns true if EOF
/// reached.
static bool json_trace_profile__skip_object_value(JsonTraceProfile *self,
                                                  Arena *arena,
                                                  JsonParser *parser,
                                                  Arena scratch) {
  bool eof = false;
  bool on_going = true;
  u32 open = 0;

  while (on_going) {
    Arena scratch_ = scratch;
    JsonToken token = json_parser_parse_token(parser, &scratch_);
    switch (token.type) {
      case JSON_TOKEN_COMMA: {
        if (open == 0) {
          on_going = false;
        }
      } break;

      case JSON_TOKEN_OPEN_BRACE:
      case JSON_TOKEN_OPEN_BRACKET: {
        open++;
      } break;

      case JSON_TOKEN_CLOSE_BRACE:
      case JSON_TOKEN_CLOSE_BRACKET: {
        open--;
      } break;

      case JSON_TOKEN_EOF: {
        on_going = false;
        eof = true;
      } break;

      case JSON_TOKEN_ERROR: {
        self->error = arena_dup_str8(arena, token.value);
        on_going = false;
        eof = true;
      } break;

      default: {
      } break;
    }
  }
  return eof;
}

static void json_trace_profile__parse_object_format(JsonTraceProfile *self,
                                                    Arena *arena,
                                                    JsonParser *parser,
                                                    Arena scratch) {
  bool on_going = true;
  while (on_going) {
    Arena scratch_ = scratch;
    JsonToken token = json_parser_parse_token(parser, &scratch_);
    switch (token.type) {
      case JSON_TOKEN_STRING_LITERAL: {
        if (str8_eq(token.value, STR8_LIT("traceEvents"))) {
          JsonToken token = json_parser_parse_token(parser, &scratch_);
          switch (token.type) {
            case JSON_TOKEN_COLON: {
              on_going =
                  !json_trace_profile__parse_array_format_expecting_open_bracket(
                      self, arena, parser, scratch);
            } break;

            case JSON_TOKEN_ERROR: {
              self->error = arena_dup_str8(arena, token.value);
              on_going = false;
            } break;
            default: {
              self->error =
                  arena_push_str8f(arena, "expecting ':', but got '%.*s'",
                                   (int)token.value.len, token.value.ptr);
              on_going = false;
            } break;
          }
        } else {
          on_going = !json_trace_profile__skip_object_value(self, arena, parser,
                                                            scratch);
        }
      } break;

      case JSON_TOKEN_CLOSE_BRACE: {
        on_going = false;
      } break;

      case JSON_TOKEN_ERROR: {
        self->error = arena_dup_str8(arena, token.value);
        on_going = false;
      } break;

      default: {
        self->error =
            arena_push_str8f(arena, "expecting 'string' or '}', got '%.*s'",
                             (int)token.value.len, token.value.ptr);
        on_going = false;
      } break;
    }
  }
}

JsonTraceProfile *json_trace_profile_parse(Arena *arena, JsonParser *parser) {
  JsonTraceProfile *self = arena_push_struct(arena, JsonTraceProfile);
  self->min_time = I64_MAX;
  self->max_time = I64_MIN;

  Scratch scratch = scratch_begin(&arena, 1);
  Arena scratch_ = *scratch.arena;
  JsonToken token = json_parser_parse_token(parser, &scratch_);
  switch (token.type) {
    case JSON_TOKEN_OPEN_BRACE: {
      json_trace_profile__parse_object_format(self, arena, parser,
                                              *scratch.arena);
    } break;

    case JSON_TOKEN_OPEN_BRACKET: {
      json_trace_profile__parse_array_format(self, arena, parser,
                                             *scratch.arena);
    } break;

    case JSON_TOKEN_ERROR: {
      self->error = arena_dup_str8(arena, token.value);
    } break;

    default: {
      self->error = arena_push_str8f(arena, "expecting '{' or '[', got '%.*s'",
                                     (int)token.value.len, token.value.ptr);
    } break;
  }
  scratch_end(scratch);

  return self;
}
