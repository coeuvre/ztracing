#include "src/trace_parser.h"

#include <ctype.h>
#include <charconv>
#include <stdlib.h>
#include <string.h>
#include <string_view>

enum JsonTokenType {
  JSON_TOKEN_NONE,
  JSON_TOKEN_OBJECT_START,  // {
  JSON_TOKEN_OBJECT_END,    // }
  JSON_TOKEN_ARRAY_START,   // [
  JSON_TOKEN_ARRAY_END,     // ]
  JSON_TOKEN_STRING,
  JSON_TOKEN_NUMBER,
  JSON_TOKEN_BOOLEAN,
  JSON_TOKEN_NULL_VAL,
  JSON_TOKEN_COLON,  // :
  JSON_TOKEN_COMMA,  // ,
  JSON_TOKEN_ERROR,
};

struct JsonToken {
  JsonTokenType type;
  std::string_view str;
};

struct JsonReader {
  const char* buf;
  size_t len;
  size_t pos;
};

static bool json_reader_done(const JsonReader* r) { return r->pos >= r->len; }

static char json_reader_peek(const JsonReader* r) {
  return r->pos < r->len ? r->buf[r->pos] : 0;
}

static void json_reader_advance(JsonReader* r) {
  if (r->pos < r->len) r->pos++;
}

static void json_reader_skip_whitespace(JsonReader* r) {
  while (!json_reader_done(r) && isspace(json_reader_peek(r))) {
    json_reader_advance(r);
  }
}

static bool json_reader_read_string(JsonReader* r, std::string_view* out) {
  if (json_reader_peek(r) != '"') return false;
  json_reader_advance(r);
  size_t start = r->pos;
  while (!json_reader_done(r) && json_reader_peek(r) != '"') {
    if (json_reader_peek(r) == '\\') {
      json_reader_advance(r);
      if (json_reader_done(r)) return false;
    }
    json_reader_advance(r);
  }
  if (json_reader_done(r)) return false;
  *out = {r->buf + start, r->pos - start};
  json_reader_advance(r);  // skip closing '"'
  return true;
}

static bool json_reader_read_number(JsonReader* r, std::string_view* out) {
  size_t start = r->pos;
  if (json_reader_peek(r) == '-') json_reader_advance(r);
  if (json_reader_done(r) || !isdigit(json_reader_peek(r))) return false;
  while (!json_reader_done(r) &&
         (isdigit(json_reader_peek(r)) || json_reader_peek(r) == '.' ||
          json_reader_peek(r) == 'e' || json_reader_peek(r) == 'E' ||
          json_reader_peek(r) == '+' || json_reader_peek(r) == '-')) {
    json_reader_advance(r);
  }
  *out = {r->buf + start, r->pos - start};
  return true;
}

static bool json_reader_read_literal(JsonReader* r, const char* literal,
                                     JsonTokenType type, JsonToken* out) {
  size_t literal_len = strlen(literal);
  if (r->pos + literal_len > r->len) return false;
  if (memcmp(r->buf + r->pos, literal, literal_len) == 0) {
    if (out) {
      out->type = type;
      out->str = {r->buf + r->pos, literal_len};
    }
    r->pos += literal_len;
    return true;
  }
  return false;
}

static JsonToken json_reader_next(JsonReader* r) {
  json_reader_skip_whitespace(r);
  if (json_reader_done(r)) return {JSON_TOKEN_NONE, {}};

  char c = json_reader_peek(r);
  switch (c) {
    case '{':
      json_reader_advance(r);
      return {JSON_TOKEN_OBJECT_START, {r->buf + r->pos - 1, 1}};
    case '}':
      json_reader_advance(r);
      return {JSON_TOKEN_OBJECT_END, {r->buf + r->pos - 1, 1}};
    case '[':
      json_reader_advance(r);
      return {JSON_TOKEN_ARRAY_START, {r->buf + r->pos - 1, 1}};
    case ']':
      json_reader_advance(r);
      return {JSON_TOKEN_ARRAY_END, {r->buf + r->pos - 1, 1}};
    case ':':
      json_reader_advance(r);
      return {JSON_TOKEN_COLON, {r->buf + r->pos - 1, 1}};
    case ',':
      json_reader_advance(r);
      return {JSON_TOKEN_COMMA, {r->buf + r->pos - 1, 1}};
    case '"': {
      std::string_view s;
      if (json_reader_read_string(r, &s)) return {JSON_TOKEN_STRING, s};
      return {JSON_TOKEN_ERROR, {}};
    }
    case 't':
      if (json_reader_read_literal(r, "true", JSON_TOKEN_BOOLEAN, nullptr))
        return {JSON_TOKEN_BOOLEAN, {r->buf + r->pos - 4, 4}};
      break;
    case 'f':
      if (json_reader_read_literal(r, "false", JSON_TOKEN_BOOLEAN, nullptr))
        return {JSON_TOKEN_BOOLEAN, {r->buf + r->pos - 5, 5}};
      break;
    case 'n':
      if (json_reader_read_literal(r, "null", JSON_TOKEN_NULL_VAL, nullptr))
        return {JSON_TOKEN_NULL_VAL, {r->buf + r->pos - 4, 4}};
      break;
    default:
      if (isdigit(c) || c == '-') {
        std::string_view s;
        if (json_reader_read_number(r, &s)) return {JSON_TOKEN_NUMBER, s};
      }
      break;
  }
  return {JSON_TOKEN_ERROR, {}};
}

void trace_parser_init(TraceParser* p, Allocator a) {
  *p = {.a = a};
}

void trace_parser_deinit(TraceParser* p) {
  array_list_deinit(&p->buffer, p->a);
  array_list_deinit(&p->args_buffer, p->a);
}

void trace_parser_feed(TraceParser* p, const char* buf, size_t len,
                       bool is_eof) {
  if (p->pos > 0 && p->pos > p->buffer.size / 2) {
    if (p->pos < p->buffer.size) {
      memmove(p->buffer.data, p->buffer.data + p->pos, p->buffer.size - p->pos);
    }
    p->buffer.size -= p->pos;
    p->pos = 0;
  }
  array_list_append(&p->buffer, p->a, buf, len);
  p->is_eof = is_eof;
}

static int64_t to_int64(std::string_view s) {
  int64_t val = 0;
  std::from_chars(s.data(), s.data() + s.size(), val);
  return val;
}

static int32_t to_int32(std::string_view s) {
  int32_t val = 0;
  std::from_chars(s.data(), s.data() + s.size(), val);
  return val;
}

static double to_double(std::string_view s) {
  char tmp[64];
  size_t len = s.size() < 63 ? s.size() : 63;
  memcpy(tmp, s.data(), len);
  tmp[len] = '\0';
  return atof(tmp);
}

static bool parse_event(JsonReader* r, TraceParser* p, TraceEvent* event) {
  JsonToken tok = json_reader_next(r);
  if (tok.type != JSON_TOKEN_OBJECT_START) return false;

  *event = {};
  array_list_clear(&p->args_buffer);

  while (true) {
    tok = json_reader_next(r);
    if (tok.type == JSON_TOKEN_OBJECT_END) break;
    if (tok.type != JSON_TOKEN_STRING) return false;

    std::string_view key = tok.str;
    tok = json_reader_next(r);
    if (tok.type != JSON_TOKEN_COLON) return false;

    if (key == "name") {
      tok = json_reader_next(r);
      if (tok.type != JSON_TOKEN_STRING) return false;
      event->name = tok.str;
    } else if (key == "cat") {
      tok = json_reader_next(r);
      if (tok.type != JSON_TOKEN_STRING) return false;
      event->cat = tok.str;
    } else if (key == "ph") {
      tok = json_reader_next(r);
      if (tok.type != JSON_TOKEN_STRING) return false;
      event->ph = tok.str;
    } else if (key == "cname") {
      tok = json_reader_next(r);
      if (tok.type != JSON_TOKEN_STRING) return false;
      event->cname = tok.str;
    } else if (key == "ts") {
      tok = json_reader_next(r);
      if (tok.type != JSON_TOKEN_NUMBER) return false;
      event->ts = to_int64(tok.str);
    } else if (key == "dur") {
      tok = json_reader_next(r);
      if (tok.type != JSON_TOKEN_NUMBER) return false;
      event->dur = to_int64(tok.str);
    } else if (key == "pid") {
      tok = json_reader_next(r);
      if (tok.type != JSON_TOKEN_NUMBER) return false;
      event->pid = to_int32(tok.str);
    } else if (key == "tid") {
      tok = json_reader_next(r);
      if (tok.type != JSON_TOKEN_NUMBER) return false;
      event->tid = to_int32(tok.str);
    } else if (key == "id") {
      tok = json_reader_next(r);
      if (tok.type == JSON_TOKEN_STRING || tok.type == JSON_TOKEN_NUMBER) {
        event->id = tok.str;
      } else {
        return false;
      }
    } else if (key == "args") {
      tok = json_reader_next(r);
      if (tok.type != JSON_TOKEN_OBJECT_START) return false;
      while (true) {
        tok = json_reader_next(r);
        if (tok.type == JSON_TOKEN_OBJECT_END) break;
        if (tok.type != JSON_TOKEN_STRING) return false;
        TraceArg arg;
        arg.key = tok.str;
        tok = json_reader_next(r);
        if (tok.type != JSON_TOKEN_COLON) return false;

        // Value can be string, number, etc. For now we just take the raw token
        // string.
        tok = json_reader_next(r);
        arg.val_double = 0.0;
        if (tok.type == JSON_TOKEN_STRING || tok.type == JSON_TOKEN_NUMBER ||
            tok.type == JSON_TOKEN_BOOLEAN || tok.type == JSON_TOKEN_NULL_VAL) {
          if (tok.type == JSON_TOKEN_NUMBER) {
            arg.val_double = to_double(tok.str);
            arg.val = {};
          } else {
            arg.val = tok.str;
          }
        } else if (tok.type == JSON_TOKEN_OBJECT_START ||
                   tok.type == JSON_TOKEN_ARRAY_START) {
          // Nested object/array. We should skip it and return raw JSON.
          size_t start = r->pos - tok.str.size();
          int depth = 1;
          while (depth > 0 && !json_reader_done(r)) {
            tok = json_reader_next(r);
            if (tok.type == JSON_TOKEN_OBJECT_START ||
                tok.type == JSON_TOKEN_ARRAY_START)
              depth++;
            else if (tok.type == JSON_TOKEN_OBJECT_END ||
                     tok.type == JSON_TOKEN_ARRAY_END)
              depth--;
          }
          arg.val = {r->buf + start, r->pos - start};
        } else {
          return false;
        }
        array_list_push_back(&p->args_buffer, p->a, arg);

        tok = json_reader_next(r);
        if (tok.type == JSON_TOKEN_COMMA) continue;
        if (tok.type == JSON_TOKEN_OBJECT_END) break;
        return false;
      }
    } else {
      // Unknown key, skip value
      tok = json_reader_next(r);
      if (tok.type == JSON_TOKEN_OBJECT_START ||
          tok.type == JSON_TOKEN_ARRAY_START) {
        int depth = 1;
        while (depth > 0 && !json_reader_done(r)) {
          tok = json_reader_next(r);
          if (tok.type == JSON_TOKEN_OBJECT_START ||
              tok.type == JSON_TOKEN_ARRAY_START)
            depth++;
          else if (tok.type == JSON_TOKEN_OBJECT_END ||
                   tok.type == JSON_TOKEN_ARRAY_END)
            depth--;
        }
      }
    }

    tok = json_reader_next(r);
    if (tok.type == JSON_TOKEN_COMMA) continue;
    if (tok.type == JSON_TOKEN_OBJECT_END) break;
    return false;
  }

  event->args = p->args_buffer.data;
  event->args_count = p->args_buffer.size;
  return true;
}

bool trace_parser_next(TraceParser* p, TraceEvent* event) {
  JsonReader r = {p->buffer.data, p->buffer.size, p->pos};

  while (!json_reader_done(&r)) {
    switch (p->state) {
      case TRACE_PARSER_STATE_INITIAL: {
        json_reader_skip_whitespace(&r);
        if (json_reader_done(&r)) goto need_more;
        char c = json_reader_peek(&r);
        if (c == '[') {
          p->is_array_format = true;
          p->state = TRACE_PARSER_STATE_IN_ARRAY;
          json_reader_advance(&r);
        } else if (c == '{') {
          p->is_array_format = false;
          p->state = TRACE_PARSER_STATE_LOOKING_FOR_TRACE_EVENTS;
          json_reader_advance(&r);
        } else {
          return false;  // Error
        }
        break;
      }

      case TRACE_PARSER_STATE_LOOKING_FOR_TRACE_EVENTS: {
        JsonToken tok = json_reader_next(&r);
        if (tok.type == JSON_TOKEN_NONE) goto need_more;
        if (tok.type == JSON_TOKEN_OBJECT_END) {
          p->state = TRACE_PARSER_STATE_COMPLETE;
          goto done;
        }
        if (tok.type == JSON_TOKEN_STRING) {
          bool is_trace_events = (tok.str == "traceEvents");
          tok = json_reader_next(&r);
          if (tok.type != JSON_TOKEN_COLON) return false;
          if (is_trace_events) {
            tok = json_reader_next(&r);
            if (tok.type != JSON_TOKEN_ARRAY_START) return false;
            p->state = TRACE_PARSER_STATE_IN_ARRAY;
          } else {
            // Skip value
            tok = json_reader_next(&r);
            if (tok.type == JSON_TOKEN_OBJECT_START ||
                tok.type == JSON_TOKEN_ARRAY_START) {
              int depth = 1;
              while (depth > 0 && !json_reader_done(&r)) {
                tok = json_reader_next(&r);
                if (tok.type == JSON_TOKEN_OBJECT_START ||
                    tok.type == JSON_TOKEN_ARRAY_START)
                  depth++;
                else if (tok.type == JSON_TOKEN_OBJECT_END ||
                         tok.type == JSON_TOKEN_ARRAY_END)
                  depth--;
              }
            }
          }
        }
        break;
      }

      case TRACE_PARSER_STATE_IN_ARRAY: {
        json_reader_skip_whitespace(&r);
        if (json_reader_done(&r)) goto need_more;
        if (json_reader_peek(&r) == ']') {
          json_reader_advance(&r);
          if (p->is_array_format) {
            p->state = TRACE_PARSER_STATE_COMPLETE;
            goto done;
          } else {
            p->state = TRACE_PARSER_STATE_LOOKING_FOR_TRACE_EVENTS;
            break;
          }
        }
        if (json_reader_peek(&r) == ',') {
          json_reader_advance(&r);
          json_reader_skip_whitespace(&r);
          if (json_reader_done(&r)) goto need_more;
        }

        size_t start_pos = r.pos;
        if (parse_event(&r, p, event)) {
          p->pos = r.pos;
          return true;
        } else {
          // If it failed because it ran out of data, wait for more
          if (json_reader_done(&r)) {
            r.pos = start_pos;
            goto need_more;
          }
          return false;
        }
      }

      case TRACE_PARSER_STATE_COMPLETE:
        goto done;
    }
  }

need_more:
  p->pos = r.pos;
  return false;

done:
  p->pos = r.pos;
  return false;
}
