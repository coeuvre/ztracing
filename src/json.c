#include "src/json.h"

#include "src/assert.h"
#include "src/list.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"

static inline bool json_parser__is_whitespace(u8 val) {
  return val == ' ' || val == '\t' || val == '\n' || val == '\r';
}

static void json_parser__return_input(JsonParser *self, u8 val) {
  DEBUG_ASSERT(self->tmp == 0);
  self->tmp = val;
}

static u8 json_parser__take_input_u8(JsonParser *self) {
  u8 val = self->tmp;
  if (val) {
    self->tmp = 0;
    return val;
  }

  if (self->cursor >= self->buf.len) {
    self->buf = self->get_input(self->get_input_context);
    self->cursor = 0;
  }

  if (self->cursor < self->buf.len) {
    val = self->buf.ptr[self->cursor++];
  }

  return val;
}

static Str8 json_parser__take_input_str8(JsonParser *self, Arena *arena,
                                         usize len) {
  Str8 buf = arena_push_str8_no_zero(arena, len);
  for (usize i = 0; i < len; ++i) {
    u8 val = json_parser__take_input_u8(self);
    buf.ptr[i] = val;
    if (val == 0) {
      break;
    }
  }
  return buf;
}

static inline void json_parser__skip_whitespace(JsonParser *self) {
  while (true) {
    u8 val = json_parser__take_input_u8(self);
    if (!json_parser__is_whitespace(val)) {
      json_parser__return_input(self, val);
      return;
    }
  }
}

static void json_parser__append(Arena *arena, Str8 *buf, usize *cursor_ptr,
                                u8 val) {
  usize cursor = *cursor_ptr;
  if (cursor >= buf->len) {
    if (arena_seek(arena, buf->len) == buf) {
      arena_pop(arena, buf->len);
    }
    usize new_len = buf->len << 1;
    u8 *new_ptr = arena_push(arena, new_len, ARENA_PUSH_NO_ZERO);
    if (new_ptr != buf->ptr) {
      memory_copy(new_ptr, buf->ptr, buf->len);
      buf->ptr = new_ptr;
    }
    buf->len = new_len;
  }
  buf->ptr[cursor] = val;
  (*cursor_ptr)++;
}

static void json_parser__shrink(Arena *arena, Str8 *buf, usize cursor) {
  usize free = buf->len - cursor;
  Arena checkpoint = *arena;
  if (arena_pop(arena, free) == (buf->ptr + cursor)) {
    buf->len = cursor;
  } else {
    *arena = checkpoint;
  }
}

static bool json_parser__parse_digits(JsonParser *self, Arena *arena, Str8 *buf,
                                      usize *cursor) {
  bool has_digits = false;
  for (;;) {
    u8 val = json_parser__take_input_u8(self);
    if (val >= '0' && val <= '9') {
      json_parser__append(arena, buf, cursor, val);
      has_digits = true;
    } else {
      json_parser__return_input(self, val);
      break;
    }
  }
  return has_digits;
}

static Str8 JSON_TOKEN_VALUE__COMMA = STR8_LIT(",");
static Str8 JSON_TOKEN_VALUE__COLON = STR8_LIT(":");

JsonToken json_parser_parse_token(JsonParser *self, Arena *arena) {
  JsonToken token;

  json_parser__skip_whitespace(self);

  u8 val = json_parser__take_input_u8(self);
  switch (val) {
    case '{': {
      token.type = JSON_TOKEN_OPEN_BRACE;
      token.value = STR8_LIT("{");
    } break;

    case '}': {
      token.type = JSON_TOKEN_CLOSE_BRACE;
      token.value = STR8_LIT("}");
    } break;

    case '[': {
      token.type = JSON_TOKEN_OPEN_BRACKET;
      token.value = STR8_LIT("[");
    } break;

    case ']': {
      token.type = JSON_TOKEN_CLOSE_BRACKET;
      token.value = STR8_LIT("]");
    } break;

    case ',': {
      token.type = JSON_TOKEN_COMMA;
      token.value = JSON_TOKEN_VALUE__COMMA;
    } break;

    case ':': {
      token.type = JSON_TOKEN_COLON;
      token.value = JSON_TOKEN_VALUE__COLON;
    } break;

    case 't': {
      Scratch scratch = scratch_begin(&arena, 1);

      Str8 expected_suffix = STR8_LIT("rue");
      Str8 suffix = json_parser__take_input_str8(self, scratch.arena,
                                                 expected_suffix.len);
      if (str8_eq(expected_suffix, suffix)) {
        token.type = JSON_TOKEN_TRUE;
        token.value = STR8_LIT("true");
      } else {
        token.type = JSON_TOKEN_ERROR;
        token.value =
            arena_push_str8f(arena, "expecting 'true', but got 't%.*s'",
                             (int)suffix.len, suffix.ptr);
      }

      scratch_end(scratch);
    } break;

    case 'f': {
      Scratch scratch = scratch_begin(&arena, 1);

      Str8 expected_suffix = STR8_LIT("alse");
      Str8 suffix = json_parser__take_input_str8(self, scratch.arena,
                                                 expected_suffix.len);
      if (str8_eq(expected_suffix, suffix)) {
        token.type = JSON_TOKEN_FALSE;
        token.value = STR8_LIT("false");
      } else {
        token.type = JSON_TOKEN_ERROR;
        token.value =
            arena_push_str8f(arena, "expecting 'false', but got 'f%.*s'",
                             (int)suffix.len, suffix.ptr);
      }

      scratch_end(scratch);
    } break;

    case 'n': {
      Scratch scratch = scratch_begin(&arena, 1);

      Str8 expected_suffix = STR8_LIT("ull");
      Str8 suffix = json_parser__take_input_str8(self, scratch.arena,
                                                 expected_suffix.len);
      if (str8_eq(expected_suffix, suffix)) {
        token.type = JSON_TOKEN_NULL;
        token.value = STR8_LIT("null");
      } else {
        token.type = JSON_TOKEN_ERROR;
        token.value =
            arena_push_str8f(arena, "expecting 'null', but got 'n%.*s'",
                             (int)suffix.len, suffix.ptr);
      }

      scratch_end(scratch);
    } break;

    case '"': {
      u8 prev[2] = {0};
      bool done = false;
      bool found_close_quote = false;

      Str8 buf = arena_push_str8_no_zero(arena, 1024);
      usize cursor = 0;

      while (!done) {
        u8 val = json_parser__take_input_u8(self);
        if (val == '"' && (prev[1] != '\\' || prev[0] == '\\')) {
          found_close_quote = true;
          done = true;
        } else if (val == 0) {
          done = true;
        } else {
          json_parser__append(arena, &buf, &cursor, val);
        }
        prev[0] = prev[1];
        prev[1] = val;
      }

      if (found_close_quote) {
        token.type = JSON_TOKEN_STRING_LITERAL;
        json_parser__shrink(arena, &buf, cursor);
        token.value = buf;
      } else {
        token.type = JSON_TOKEN_ERROR;
        token.value = arena_push_str8f(arena, "expecting '\"', but got EOF");
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
      Str8 buf = arena_push_str8_no_zero(arena, 1024);
      usize cursor = 0;
      json_parser__append(arena, &buf, &cursor, val);

      bool done = false;
      if (val == '-') {
        val = json_parser__take_input_u8(self);
        if (val >= '0' && val <= '9') {
          json_parser__append(arena, &buf, &cursor, val);
        } else {
          json_parser__return_input(self, val);

          token.type = JSON_TOKEN_ERROR;
          token.value = arena_push_str8f(
              arena, "Invalid number '%.*s', expecting digits but got EOF",
              (int)buf.len, buf.ptr);
          done = true;
        }
      }

      if (!done && val != '0') {
        json_parser__parse_digits(self, arena, &buf, &cursor);
      }

      if (!done) {
        val = json_parser__take_input_u8(self);
        if (val == '.') {
          json_parser__append(arena, &buf, &cursor, val);
          if (!json_parser__parse_digits(self, arena, &buf, &cursor)) {
            val = json_parser__take_input_u8(self);
            json_parser__return_input(self, val);

            token.type = JSON_TOKEN_ERROR;
            token.value = arena_push_str8f(
                arena,
                "Invalid number '%.*s', expecting digits after '.' but "
                "got '%c'",
                (int)buf.len, buf.ptr, val);
            done = true;
          }
        } else {
          json_parser__return_input(self, val);

          token.type = JSON_TOKEN_NUMBER;
          json_parser__shrink(arena, &buf, cursor);
          token.value = buf;
          done = true;
        }
      }

      if (!done) {
        val = json_parser__take_input_u8(self);
        if (val == 'e' || val == 'E') {
          json_parser__append(arena, &buf, &cursor, val);

          val = json_parser__take_input_u8(self);
          if (val == '-' || (val >= '0' && val <= '9')) {
            json_parser__append(arena, &buf, &cursor, val);

            json_parser__parse_digits(self, arena, &buf, &cursor);

            token.type = JSON_TOKEN_NUMBER;
            json_parser__shrink(arena, &buf, cursor);
            token.value = buf;
            done = true;
          } else {
            json_parser__return_input(self, val);

            token.type = JSON_TOKEN_ERROR;
            token.value = arena_push_str8f(
                arena,
                "Invalid number '%.*s', expecting sign or digits after "
                "'E' but got '%c'",
                (int)buf.len, buf.ptr, val);
            done = true;
          }
        } else {
          json_parser__return_input(self, val);
          token.type = JSON_TOKEN_NUMBER;
          json_parser__shrink(arena, &buf, cursor);
          token.value = buf;
          done = true;
        }
      }

      ASSERT(done);
    } break;

    // EOF
    case 0: {
      token.type = JSON_TOKEN_EOF, token.value = str8_zero();
    } break;

    default: {
      token.type = JSON_TOKEN_ERROR;
      token.value = arena_push_str8f(arena, "Unexpected character: '%c'", val);
    } break;
  }

  return token;
}

static void json_parser__parse_object(JsonParser *self, Arena *arena,
                                      JsonValue *value) {
  bool has_value = false;
  bool done = false;
  while (!done) {
    JsonToken token = json_parser_parse_token(self, arena);
    switch (token.type) {
      case JSON_TOKEN_STRING_LITERAL: {
        Str8 key = token.value;
        token = json_parser_parse_token(self, arena);
        switch (token.type) {
          case JSON_TOKEN_COLON: {
            JsonValue *child = json_parser_parse_value(self, arena);
            if (child->type != JSON_VALUE_ERROR) {
              has_value = true;
              child->label = key;
              DLL_APPEND(value->first, value->last, child, prev, next);
            } else {
              *value = *child;
              done = true;
            }
          } break;

          case JSON_TOKEN_ERROR: {
            value->type = JSON_VALUE_ERROR;
            value->value = token.value;
            done = true;
          } break;

          default: {
            value->type = JSON_VALUE_ERROR;
            value->value =
                arena_push_str8f(arena, "expecting ':', but got %.*s",
                                 (int)token.value.len, token.value.ptr);
            done = true;
          } break;
        }
      } break;

      case JSON_TOKEN_COMMA: {
        if (!has_value) {
          value->type = JSON_VALUE_ERROR;
          value->value =
              arena_push_str8f(arena, "expecting string or '}', but got ','");
          done = true;
        }
      } break;

      case JSON_TOKEN_CLOSE_BRACE: {
        value->type = JSON_VALUE_OBJECT;
        done = true;
      } break;

      case JSON_TOKEN_ERROR: {
        value->type = JSON_VALUE_ERROR;
        value->value = token.value;
        done = true;
      } break;

      default: {
        value->type = JSON_VALUE_ERROR;
        value->value = arena_push_str8f(arena, "Unexpected token '%.*s'",
                                        (int)token.value.len, token.value.ptr);
        done = true;
      } break;
    }
  }
}

static void json_parser__parse_array(JsonParser *self, Arena *arena,
                                     JsonValue *value) {
  bool done = false;
  while (!done) {
    JsonValue *child = json_parser_parse_value(self, arena);
    if (child->type != JSON_VALUE_ERROR) {
      DLL_APPEND(value->first, value->last, child, prev, next);

      JsonToken token = json_parser_parse_token(self, arena);
      switch (token.type) {
        case JSON_TOKEN_COMMA: {
        } break;

        case JSON_TOKEN_CLOSE_BRACKET: {
          value->type = JSON_VALUE_ARRAY;
          done = true;
        } break;

        case JSON_TOKEN_ERROR: {
          value->type = JSON_VALUE_ERROR;
          value->value = token.value;
          done = true;
        } break;

        default: {
          value->type = JSON_VALUE_ERROR;
          value->value =
              arena_push_str8f(arena, "expecting token ',' or '], but got %.*s",
                               (int)token.value.len, token.value.ptr);
          done = true;
        } break;
      }
    } else {
      *value = *child;
      done = true;
    }
  }
}

static void json_parser__parse_value(JsonParser *self, Arena *arena,
                                     JsonValue *value) {
  JsonToken token = json_parser_parse_token(self, arena);
  switch (token.type) {
    case JSON_TOKEN_OPEN_BRACE: {
      json_parser__parse_object(self, arena, value);
    } break;

    case JSON_TOKEN_OPEN_BRACKET: {
      json_parser__parse_array(self, arena, value);
    } break;

    case JSON_TOKEN_STRING_LITERAL: {
      value->type = JSON_VALUE_STRING;
      value->value = token.value;
    } break;

    case JSON_TOKEN_NUMBER: {
      value->type = JSON_VALUE_NUMBER;
      value->value = token.value;
    } break;

    case JSON_TOKEN_TRUE: {
      value->type = JSON_VALUE_TRUE;
      value->value = token.value;
    } break;

    case JSON_TOKEN_FALSE: {
      value->type = JSON_VALUE_FALSE;
      value->value = token.value;
    } break;

    case JSON_TOKEN_NULL: {
      value->type = JSON_VALUE_NULL;
      value->value = token.value;
    } break;

    case JSON_TOKEN_ERROR: {
      value->type = JSON_VALUE_ERROR;
      value->value = token.value;
    } break;

    default: {
      value->type = JSON_VALUE_ERROR;
      value->value = arena_push_str8f(arena, "Unexpected token '%.*s'",
                                      (int)token.value.len, token.value.ptr);
    } break;
  }
}

JsonValue *json_parser_parse_value(JsonParser *self, Arena *arena) {
  JsonValue *value = arena_push_struct_no_zero(arena, JsonValue);
  // This is faster than using memset.
  *value = (JsonValue){0};
  json_parser__parse_value(self, arena, value);
  return value;
}
