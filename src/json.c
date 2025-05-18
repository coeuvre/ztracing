#include "src/json.h"

#include <stdbool.h>
#include <stddef.h>

#include "src/assert.h"
#include "src/list.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"

static inline f64 ParseSign(Str buf, usize *cursor) {
  f64 sign = 1.0;
  if (*cursor < buf.len) {
    u8 ch = buf.ptr[*cursor];
    if (ch == '-') {
      sign = -1.0;
      (*cursor)++;
    } else if (ch == '+') {
      (*cursor)++;
    }
  }
  return sign;
}

static f64 ParseNumber(Str buf, usize *cursor) {
  f64 result = 0;
  while (*cursor < buf.len) {
    u8 val = buf.ptr[*cursor] - '0';
    if (val < 10) {
      result = 10.0 * result + (f64)val;
      (*cursor)++;
    } else {
      break;
    }
  }
  return result;
}

f64 JsonValue_ToF64(JsonValue *value) {
  Str buf = value->value;
  if (Str_IsEmpty(buf)) {
    return 0;
  }
  usize cursor = 0;
  f64 sign = ParseSign(buf, &cursor);
  f64 number = ParseNumber(buf, &cursor);

  if (cursor < buf.len && buf.ptr[cursor] == '.') {
    ++cursor;
    f64 c = 1.0 / 10.0;
    while (cursor < buf.len) {
      u8 val = buf.ptr[cursor] - '0';
      if (val < 10) {
        number = number + c * (f64)val;
        c *= 1.0 / 10.0;
        cursor++;
      } else {
        break;
      }
    }
  }

  if (cursor < buf.len && (buf.ptr[cursor] == 'e' || buf.ptr[cursor] == 'E')) {
    ++cursor;

    f64 exp_sign = ParseSign(buf, &cursor);
    f64 exp = exp_sign * ParseNumber(buf, &cursor);
    number *= PowF64(10.0, exp);
  }

  return sign * number;
}

static inline bool IsWhitespace(u8 val) {
  return val == ' ' || val == '\t' || val == '\n' || val == '\r';
}

static void ReturnInput(JsonParser *self, u8 val) {
  DEBUG_ASSERT(self->tmp == 0);
  self->tmp = val;
}

static u8 TakeInputU8(JsonParser *self) {
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

static Str TakeInputStr(JsonParser *self, Arena *arena, usize len) {
  Str buf = arena_push_str8_no_zero(arena, len);
  for (usize i = 0; i < len; ++i) {
    u8 val = TakeInputU8(self);
    buf.ptr[i] = val;
    if (val == 0) {
      break;
    }
  }
  return buf;
}

static inline void SkipWhitespace(JsonParser *self) {
  while (true) {
    u8 val = TakeInputU8(self);
    if (!IsWhitespace(val)) {
      ReturnInput(self, val);
      return;
    }
  }
}

static void Append(Arena *arena, Str *buf, ptrdiff_t *cursor_ptr, u8 val) {
  ptrdiff_t cursor = *cursor_ptr;
  if (cursor >= buf->len) {
    if (arena_seek(arena, buf->len) == buf) {
      arena_pop(arena, buf->len);
    }
    usize new_len = buf->len << 1;
    char *new_ptr = arena_push(arena, new_len, ARENA_PUSH_NO_ZERO);
    if (new_ptr != buf->ptr) {
      memory_copy(new_ptr, buf->ptr, buf->len);
      buf->ptr = new_ptr;
    }
    buf->len = new_len;
  }
  buf->ptr[cursor] = val;
  (*cursor_ptr)++;
}

static void Shrink(Arena *arena, Str *buf, usize cursor) {
  usize free = buf->len - cursor;
  Arena checkpoint = *arena;
  if (arena_pop(arena, free) == (buf->ptr + cursor)) {
    buf->len = cursor;
  } else {
    *arena = checkpoint;
  }
}

static bool ParseDigits(JsonParser *self, Arena *arena, Str *buf,
                        ptrdiff_t *cursor) {
  bool has_digits = false;
  for (;;) {
    u8 val = TakeInputU8(self);
    if (val >= '0' && val <= '9') {
      Append(arena, buf, cursor, val);
      has_digits = true;
    } else {
      ReturnInput(self, val);
      break;
    }
  }
  return has_digits;
}

static Str JsonTokenValue_Comma = STR_C(",");
static Str JsonTokenValue_Colon = STR_C(":");

JsonToken JsonParser_ParseToken(JsonParser *self, Arena *arena) {
  JsonToken token;

  SkipWhitespace(self);

  u8 val = TakeInputU8(self);
  switch (val) {
    case '{': {
      token.type = JsonTokenType_OpenBrace;
      token.value = STR_C("{");
    } break;

    case '}': {
      token.type = JsonTokenType_CloseBrace;
      token.value = STR_C("}");
    } break;

    case '[': {
      token.type = JsonTokenType_OpenBracket;
      token.value = STR_C("[");
    } break;

    case ']': {
      token.type = JsonTokenType_CloseBracket;
      token.value = STR_C("]");
    } break;

    case ',': {
      token.type = JsonTokenType_Comma;
      token.value = JsonTokenValue_Comma;
    } break;

    case ':': {
      token.type = JsonTokenType_Colon;
      token.value = JsonTokenValue_Colon;
    } break;

    case 't': {
      Scratch scratch = scratch_begin(&arena, 1);

      Str expected_suffix = STR_C("rue");
      Str suffix = TakeInputStr(self, scratch.arena, expected_suffix.len);
      if (Str_IsEqual(expected_suffix, suffix)) {
        token.type = JsonTokenType_True;
        token.value = STR_C("true");
      } else {
        token.type = JsonTokenType_Error;
        token.value =
            arena_push_str8f(arena, "expecting 'true', but got 't%.*s'",
                             (int)suffix.len, suffix.ptr);
      }

      scratch_end(scratch);
    } break;

    case 'f': {
      Scratch scratch = scratch_begin(&arena, 1);

      Str expected_suffix = STR_C("alse");
      Str suffix = TakeInputStr(self, scratch.arena, expected_suffix.len);
      if (Str_IsEqual(expected_suffix, suffix)) {
        token.type = JsonTokenType_False;
        token.value = STR_C("false");
      } else {
        token.type = JsonTokenType_Error;
        token.value =
            arena_push_str8f(arena, "expecting 'false', but got 'f%.*s'",
                             (int)suffix.len, suffix.ptr);
      }

      scratch_end(scratch);
    } break;

    case 'n': {
      Scratch scratch = scratch_begin(&arena, 1);

      Str expected_suffix = STR_C("ull");
      Str suffix = TakeInputStr(self, scratch.arena, expected_suffix.len);
      if (Str_IsEqual(expected_suffix, suffix)) {
        token.type = JsonTokenType_Null;
        token.value = STR_C("null");
      } else {
        token.type = JsonTokenType_Error;
        token.value =
            arena_push_str8f(arena, "expecting 'null', but got 'n%.*s'",
                             (int)suffix.len, suffix.ptr);
      }

      scratch_end(scratch);
    } break;

    case '"': {
      u8 prev[2] = {0};
      bool running = true;
      bool found_close_quote = false;

      Str buf = arena_push_str8_no_zero(arena, 1024);
      usize cursor = 0;

      while (running) {
        u8 val = TakeInputU8(self);
        if (val == '"' && (prev[1] != '\\' || prev[0] == '\\')) {
          found_close_quote = true;
          running = false;
        } else if (val == 0) {
          running = false;
        } else {
          Append(arena, &buf, &cursor, val);
        }
        prev[0] = prev[1];
        prev[1] = val;
      }

      if (found_close_quote) {
        token.type = JsonTokenType_StringLiteral;
        Shrink(arena, &buf, cursor);
        token.value = buf;
      } else {
        token.type = JsonTokenType_Error;
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
      Str buf = arena_push_str8_no_zero(arena, 1024);
      usize cursor = 0;
      Append(arena, &buf, &cursor, val);

      bool running = true;
      if (val == '-') {
        val = TakeInputU8(self);
        if (val >= '0' && val <= '9') {
          Append(arena, &buf, &cursor, val);
        } else {
          ReturnInput(self, val);

          token.type = JsonTokenType_Error;
          token.value = arena_push_str8f(
              arena, "Invalid number '%.*s', expecting digits but got EOF",
              (int)buf.len, buf.ptr);
          running = false;
        }
      }

      if (running && val != '0') {
        ParseDigits(self, arena, &buf, &cursor);
      }

      if (running) {
        val = TakeInputU8(self);
        if (val == '.') {
          Append(arena, &buf, &cursor, val);
          if (!ParseDigits(self, arena, &buf, &cursor)) {
            val = TakeInputU8(self);
            ReturnInput(self, val);

            token.type = JsonTokenType_Error;
            token.value = arena_push_str8f(
                arena,
                "Invalid number '%.*s', expecting digits after '.' but "
                "got '%c'",
                (int)buf.len, buf.ptr, val);
            running = false;
          }
        } else {
          ReturnInput(self, val);

          token.type = JsonTokenType_Number;
          Shrink(arena, &buf, cursor);
          token.value = buf;
          running = false;
        }
      }

      if (running) {
        val = TakeInputU8(self);
        if (val == 'e' || val == 'E') {
          Append(arena, &buf, &cursor, val);

          val = TakeInputU8(self);
          if (val == '-' || (val >= '0' && val <= '9')) {
            Append(arena, &buf, &cursor, val);

            ParseDigits(self, arena, &buf, &cursor);

            token.type = JsonTokenType_Number;
            Shrink(arena, &buf, cursor);
            token.value = buf;
            running = false;
          } else {
            ReturnInput(self, val);

            token.type = JsonTokenType_Error;
            token.value = arena_push_str8f(
                arena,
                "Invalid number '%.*s', expecting sign or digits after "
                "'E' but got '%c'",
                (int)buf.len, buf.ptr, val);
            running = false;
          }
        } else {
          ReturnInput(self, val);
          token.type = JsonTokenType_Number;
          Shrink(arena, &buf, cursor);
          token.value = buf;
          running = false;
        }
      }

      DEBUG_ASSERT(!running);
    } break;

    // EOF
    case 0: {
      token.type = JsonTokenType_Eof, token.value = Str_Zero();
    } break;

    default: {
      token.type = JsonTokenType_Error;
      token.value = arena_push_str8f(arena, "Unexpected character: '%c'", val);
    } break;
  }

  return token;
}

static void ParseObject(JsonParser *self, Arena *arena, JsonValue *value) {
  bool has_value = false;
  bool running = true;
  while (running) {
    JsonToken token = JsonParser_ParseToken(self, arena);
    switch (token.type) {
      case JsonTokenType_StringLiteral: {
        Str key = token.value;
        token = JsonParser_ParseToken(self, arena);
        switch (token.type) {
          case JsonTokenType_Colon: {
            JsonValue *child = JsonParser_ParseValue(self, arena);
            if (child->type != JsonValueType_Error) {
              has_value = true;
              child->label = key;
              DLL_APPEND(value->first, value->last, child, prev, next);
            } else {
              *value = *child;
              running = false;
            }
          } break;

          case JsonTokenType_Error: {
            value->type = JsonValueType_Error;
            value->value = token.value;
            running = false;
          } break;

          default: {
            value->type = JsonValueType_Error;
            value->value =
                arena_push_str8f(arena, "expecting ':', but got %.*s",
                                 (int)token.value.len, token.value.ptr);
            running = false;
          } break;
        }
      } break;

      case JsonTokenType_Comma: {
        if (!has_value) {
          value->type = JsonValueType_Error;
          value->value =
              arena_push_str8f(arena, "expecting 'string' or '}', but got ','");
          running = false;
        }
      } break;

      case JsonTokenType_CloseBrace: {
        value->type = JsonValueType_Object;
        running = false;
      } break;

      case JsonTokenType_Error: {
        value->type = JsonValueType_Error;
        value->value = token.value;
        running = false;
      } break;

      default: {
        value->type = JsonValueType_Error;
        value->value = arena_push_str8f(arena, "Unexpected token '%.*s'",
                                        (int)token.value.len, token.value.ptr);
        running = false;
      } break;
    }
  }
}

static void ParseArray(JsonParser *self, Arena *arena, JsonValue *value) {
  bool running = true;
  while (running) {
    JsonValue *child = JsonParser_ParseValue(self, arena);
    if (child->type != JsonValueType_Error) {
      DLL_APPEND(value->first, value->last, child, prev, next);

      JsonToken token = JsonParser_ParseToken(self, arena);
      switch (token.type) {
        case JsonTokenType_Comma: {
        } break;

        case JsonTokenType_CloseBracket: {
          value->type = JsonValueType_Array;
          running = false;
        } break;

        case JsonTokenType_Error: {
          value->type = JsonValueType_Error;
          value->value = token.value;
          running = false;
        } break;

        default: {
          value->type = JsonValueType_Error;
          value->value =
              arena_push_str8f(arena, "expecting token ',' or '], but got %.*s",
                               (int)token.value.len, token.value.ptr);
          running = false;
        } break;
      }
    } else {
      *value = *child;
      running = false;
    }
  }
}

static void ParseValue(JsonParser *self, Arena *arena, JsonValue *value) {
  JsonToken token = JsonParser_ParseToken(self, arena);
  switch (token.type) {
    case JsonTokenType_OpenBrace: {
      ParseObject(self, arena, value);
    } break;

    case JsonTokenType_OpenBracket: {
      ParseArray(self, arena, value);
    } break;

    case JsonTokenType_StringLiteral: {
      value->type = JsonValueType_String;
      value->value = token.value;
    } break;

    case JsonTokenType_Number: {
      value->type = JsonValueType_Number;
      value->value = token.value;
    } break;

    case JsonTokenType_True: {
      value->type = JsonValueType_True;
      value->value = token.value;
    } break;

    case JsonTokenType_False: {
      value->type = JsonValueType_False;
      value->value = token.value;
    } break;

    case JsonTokenType_Null: {
      value->type = JsonValueType_Null;
      value->value = token.value;
    } break;

    case JsonTokenType_Error: {
      value->type = JsonValueType_Error;
      value->value = token.value;
    } break;

    default: {
      value->type = JsonValueType_Error;
      value->value = arena_push_str8f(arena, "Unexpected token '%.*s'",
                                      (int)token.value.len, token.value.ptr);
    } break;
  }
}

JsonValue *JsonParser_ParseValue(JsonParser *self, Arena *arena) {
  JsonValue *value = arena_push_struct_no_zero(arena, JsonValue);
  // This is faster than using memset.
  *value = (JsonValue){0};
  ParseValue(self, arena, value);
  return value;
}
