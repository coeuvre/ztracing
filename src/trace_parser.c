#include "src/trace_parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "src/allocator.h"
#include "src/array_list.h"
#include "src/string.h"

typedef enum json_token_type {
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
} json_token_type_t;

typedef struct json_token {
  json_token_type_t type;
  string_t str;
} json_token_t;

typedef struct json_reader {
  const char* buf;
  size_t len;
  size_t pos;
} json_reader_t;

static bool json_reader_done(const json_reader_t* r) {
  bool result = r->pos >= r->len;
  return result;
}

static char json_reader_peek(const json_reader_t* r) {
  char c = r->pos < r->len ? r->buf[r->pos] : 0;
  return c;
}

static void json_reader_advance(json_reader_t* r) {
  if (r->pos < r->len) {
    r->pos++;
  }
}

static void json_reader_skip_whitespace(json_reader_t* r) {
  while (!json_reader_done(r) && isspace((unsigned char)json_reader_peek(r))) {
    json_reader_advance(r);
  }
}

static bool json_reader_read_string(json_reader_t* r, string_t* out) {
  bool success = false;
  if (json_reader_peek(r) == '"') {
    json_reader_advance(r);
    size_t start = r->pos;
    bool ok = true;
    while (!json_reader_done(r) && json_reader_peek(r) != '"' && ok) {
      if (json_reader_peek(r) == '\\') {
        json_reader_advance(r);
        if (json_reader_done(r)) {
          ok = false;
        }
      }
      if (ok) {
        json_reader_advance(r);
      }
    }
    if (ok && !json_reader_done(r)) {
      *out = string_from_parts(r->buf + start, r->pos - start);
      json_reader_advance(r);  // skip closing '"'
      success = true;
    }
  }
  return success;
}

static bool json_reader_read_number(json_reader_t* r, string_t* out) {
  bool success = false;
  size_t start = r->pos;
  if (json_reader_peek(r) == '-') {
    json_reader_advance(r);
  }
  if (!json_reader_done(r) && isdigit((unsigned char)json_reader_peek(r))) {
    while (!json_reader_done(r) &&
           (isdigit((unsigned char)json_reader_peek(r)) ||
            json_reader_peek(r) == '.' || json_reader_peek(r) == 'e' ||
            json_reader_peek(r) == 'E' || json_reader_peek(r) == '+' ||
            json_reader_peek(r) == '-')) {
      json_reader_advance(r);
    }
    *out = string_from_parts(r->buf + start, r->pos - start);
    success = true;
  }
  return success;
}

static bool json_reader_read_literal(json_reader_t* r, const char* literal,
                                     json_token_type_t type,
                                     json_token_t* out) {
  bool success = false;
  size_t literal_len = strlen(literal);
  if (r->pos + literal_len <= r->len) {
    if (memcmp(r->buf + r->pos, literal, literal_len) == 0) {
      if (out) {
        out->type = type;
        out->str = string_from_parts(r->buf + r->pos, literal_len);
      }
      r->pos += literal_len;
      success = true;
    }
  }
  return success;
}

static json_token_t json_reader_next(json_reader_t* r) {
  json_token_t tok = {JSON_TOKEN_ERROR, {}};
  json_reader_skip_whitespace(r);

  if (json_reader_done(r)) {
    tok = (json_token_t){JSON_TOKEN_NONE, {}};
  } else {
    char c = json_reader_peek(r);
    switch (c) {
      case '{':
        json_reader_advance(r);
        tok = (json_token_t){JSON_TOKEN_OBJECT_START,
                             string_from_parts(r->buf + r->pos - 1, 1)};
        break;
      case '}':
        json_reader_advance(r);
        tok = (json_token_t){JSON_TOKEN_OBJECT_END,
                             string_from_parts(r->buf + r->pos - 1, 1)};
        break;
      case '[':
        json_reader_advance(r);
        tok = (json_token_t){JSON_TOKEN_ARRAY_START,
                             string_from_parts(r->buf + r->pos - 1, 1)};
        break;
      case ']':
        json_reader_advance(r);
        tok = (json_token_t){JSON_TOKEN_ARRAY_END,
                             string_from_parts(r->buf + r->pos - 1, 1)};
        break;
      case ':':
        json_reader_advance(r);
        tok = (json_token_t){JSON_TOKEN_COLON,
                             string_from_parts(r->buf + r->pos - 1, 1)};
        break;
      case ',':
        json_reader_advance(r);
        tok = (json_token_t){JSON_TOKEN_COMMA,
                             string_from_parts(r->buf + r->pos - 1, 1)};
        break;
      case '"': {
        string_t s = {};
        if (json_reader_read_string(r, &s)) {
          tok = (json_token_t){JSON_TOKEN_STRING, s};
        }
        break;
      }
      case 't':
        if (json_reader_read_literal(r, "true", JSON_TOKEN_BOOLEAN, nullptr)) {
          tok = (json_token_t){JSON_TOKEN_BOOLEAN,
                               string_from_parts(r->buf + r->pos - 4, 4)};
        }
        break;
      case 'f':
        if (json_reader_read_literal(r, "false", JSON_TOKEN_BOOLEAN, nullptr)) {
          tok = (json_token_t){JSON_TOKEN_BOOLEAN,
                               string_from_parts(r->buf + r->pos - 5, 5)};
        }
        break;
      case 'n':
        if (json_reader_read_literal(r, "null", JSON_TOKEN_NULL_VAL, nullptr)) {
          tok = (json_token_t){JSON_TOKEN_NULL_VAL,
                               string_from_parts(r->buf + r->pos - 4, 4)};
        }
        break;
      default:
        if (isdigit((unsigned char)c) || c == '-') {
          string_t s = {};
          if (json_reader_read_number(r, &s)) {
            tok = (json_token_t){JSON_TOKEN_NUMBER, s};
          }
        }
        break;
    }
  }
  return tok;
}

void trace_parser_deinit(trace_parser_t* p, allocator_t a) {
  array_list_deinit(&p->buffer, a);
  array_list_deinit(&p->args_buffer, a);
}

size_t trace_parser_feed(trace_parser_t* p, const char* buf, size_t len,
                         bool is_eof, allocator_t a) {
  size_t discarded = 0;
  if (p->pos > 0 && p->pos > p->buffer.len / 2) {
    if (p->pos < p->buffer.len) {
      memmove(p->buffer.ptr, (char*)p->buffer.ptr + p->pos,
              p->buffer.len - p->pos);
    }
    discarded = p->pos;
    p->buffer.len -= p->pos;
    p->pos = 0;
  }

  size_t old_len = p->buffer.len;
  array_list_resize(&p->buffer, old_len + len, sizeof(char), a);
  memcpy((char*)p->buffer.ptr + old_len, buf, len);

  p->is_eof = is_eof;
  return discarded;
}

static int64_t to_int64(string_t s) {
  int64_t val = 0;
  size_t i = 0;
  bool neg = false;
  if (s.len > 0 && s.ptr[0] == '-') {
    neg = true;
    i++;
  }
  for (; i < s.len && s.ptr[i] != '.'; ++i) {
    val = val * 10 + (s.ptr[i] - '0');
  }
  return neg ? -val : val;
}

static int32_t to_int32(string_t s) {
  int32_t val = 0;
  size_t i = 0;
  bool neg = false;
  if (s.len > 0 && s.ptr[0] == '-') {
    neg = true;
    i++;
  }
  for (; i < s.len && s.ptr[i] != '.'; ++i) {
    val = val * 10 + (s.ptr[i] - '0');
  }
  return neg ? -val : val;
}

static double to_double(string_t s) {
  bool has_exponent = false;
  for (size_t i = 0; i < s.len; ++i) {
    if (s.ptr[i] == 'e' || s.ptr[i] == 'E') {
      has_exponent = true;
      break;
    }
  }

  double val = 0.0;
  if (has_exponent) {
    char tmp[64];
    size_t len = s.len < 63 ? s.len : 63;
    memcpy(tmp, s.ptr, len);
    tmp[len] = '\0';
    val = atof(tmp);
  } else {
    size_t i = 0;
    bool neg = false;
    if (s.len > 0 && s.ptr[0] == '-') {
      neg = true;
      i++;
    }
    double integer_part = 0.0;
    for (; i < s.len && s.ptr[i] != '.'; ++i) {
      integer_part = integer_part * 10.0 + (s.ptr[i] - '0');
    }
    val = integer_part;
    if (i < s.len && s.ptr[i] == '.') {
      i++;
      double fraction_part = 0.0;
      double divisor = 1.0;
      for (; i < s.len; ++i) {
        fraction_part = fraction_part * 10.0 + (s.ptr[i] - '0');
        divisor *= 10.0;
      }
      val += fraction_part / divisor;
    }
    if (neg) {
      val = -val;
    }
  }
  return val;
}

static bool parse_event(json_reader_t* r, trace_parser_t* p, allocator_t a,
                        trace_event_t* event) {
  bool success = false;
  json_token_t tok = json_reader_next(r);

  if (tok.type == JSON_TOKEN_OBJECT_START) {
    *event = (trace_event_t){};
    array_list_clear(&p->args_buffer);
    bool ok = true;

    while (ok) {
      tok = json_reader_next(r);
      if (tok.type == JSON_TOKEN_OBJECT_END) {
        success = true;
        break;
      }
      if (tok.type != JSON_TOKEN_STRING) {
        ok = false;
        break;
      }

      string_t key = tok.str;
      tok = json_reader_next(r);
      if (tok.type != JSON_TOKEN_COLON) {
        ok = false;
        break;
      }

      if (string_eq(key, string_lit("name"))) {
        tok = json_reader_next(r);
        if (tok.type != JSON_TOKEN_STRING) {
          ok = false;
          break;
        }
        event->name = tok.str;
      } else if (string_eq(key, string_lit("cat"))) {
        tok = json_reader_next(r);
        if (tok.type != JSON_TOKEN_STRING) {
          ok = false;
          break;
        }
        event->cat = tok.str;
      } else if (string_eq(key, string_lit("ph"))) {
        tok = json_reader_next(r);
        if (tok.type != JSON_TOKEN_STRING) {
          ok = false;
          break;
        }
        event->ph = tok.str;
      } else if (string_eq(key, string_lit("cname"))) {
        tok = json_reader_next(r);
        if (tok.type != JSON_TOKEN_STRING) {
          ok = false;
          break;
        }
        event->cname = tok.str;
      } else if (string_eq(key, string_lit("ts"))) {
        tok = json_reader_next(r);
        if (tok.type != JSON_TOKEN_NUMBER) {
          ok = false;
          break;
        }
        event->ts = to_int64(tok.str);
      } else if (string_eq(key, string_lit("dur"))) {
        tok = json_reader_next(r);
        if (tok.type != JSON_TOKEN_NUMBER) {
          ok = false;
          break;
        }
        event->dur = to_int64(tok.str);
      } else if (string_eq(key, string_lit("pid"))) {
        tok = json_reader_next(r);
        if (tok.type != JSON_TOKEN_NUMBER) {
          ok = false;
          break;
        }
        event->pid = to_int32(tok.str);
      } else if (string_eq(key, string_lit("tid"))) {
        tok = json_reader_next(r);
        if (tok.type != JSON_TOKEN_NUMBER) {
          ok = false;
          break;
        }
        event->tid = to_int32(tok.str);
      } else if (string_eq(key, string_lit("id"))) {
        tok = json_reader_next(r);
        if (tok.type == JSON_TOKEN_STRING || tok.type == JSON_TOKEN_NUMBER) {
          event->id = tok.str;
        } else {
          ok = false;
          break;
        }
      } else if (string_eq(key, string_lit("args"))) {
        tok = json_reader_next(r);
        if (tok.type != JSON_TOKEN_OBJECT_START) {
          ok = false;
          break;
        }
        while (ok) {
          tok = json_reader_next(r);
          if (tok.type == JSON_TOKEN_OBJECT_END) {
            break;
          }
          if (tok.type != JSON_TOKEN_STRING) {
            ok = false;
            break;
          }
          trace_arg_t arg = {};
          arg.key = tok.str;
          tok = json_reader_next(r);
          if (tok.type != JSON_TOKEN_COLON) {
            ok = false;
            break;
          }

          tok = json_reader_next(r);
          arg.val_double = 0.0;
          if (tok.type == JSON_TOKEN_STRING || tok.type == JSON_TOKEN_NUMBER ||
              tok.type == JSON_TOKEN_BOOLEAN ||
              tok.type == JSON_TOKEN_NULL_VAL) {
            if (tok.type == JSON_TOKEN_NUMBER) {
              arg.val_double = to_double(tok.str);
              arg.val = (string_t){};
            } else {
              arg.val = tok.str;
            }
          } else if (tok.type == JSON_TOKEN_OBJECT_START ||
                     tok.type == JSON_TOKEN_ARRAY_START) {
            size_t start = r->pos - tok.str.len;
            int depth = 1;
            while (depth > 0 && !json_reader_done(r)) {
              tok = json_reader_next(r);
              if (tok.type == JSON_TOKEN_OBJECT_START ||
                  tok.type == JSON_TOKEN_ARRAY_START) {
                depth++;
              } else if (tok.type == JSON_TOKEN_OBJECT_END ||
                         tok.type == JSON_TOKEN_ARRAY_END) {
                depth--;
              }
            }
            arg.val = string_from_parts(r->buf + start, r->pos - start);
          } else {
            ok = false;
            break;
          }

          *array_list_push(&p->args_buffer, trace_arg_t, a) = arg;

          tok = json_reader_next(r);
          if (tok.type == JSON_TOKEN_COMMA) {
            continue;
          }
          if (tok.type == JSON_TOKEN_OBJECT_END) {
            break;
          }
          ok = false;
          break;
        }
        if (!ok) {
          break;
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
                tok.type == JSON_TOKEN_ARRAY_START) {
              depth++;
            } else if (tok.type == JSON_TOKEN_OBJECT_END ||
                       tok.type == JSON_TOKEN_ARRAY_END) {
              depth--;
            }
          }
        }
      }

      tok = json_reader_next(r);
      if (tok.type == JSON_TOKEN_COMMA) {
        continue;
      }
      if (tok.type == JSON_TOKEN_OBJECT_END) {
        success = true;
        break;
      }
      ok = false;
      break;
    }
  }

  if (success) {
    event->args = (trace_arg_t*)p->args_buffer.ptr;
    event->args_count = p->args_buffer.len;
  }
  return success;
}

bool trace_parser_next(trace_parser_t* p, trace_event_t* event, allocator_t a) {
  bool found = false;
  json_reader_t r = {p->buffer.ptr, p->buffer.len, p->pos};
  bool loop = !json_reader_done(&r);

  while (loop && !found) {
    switch (p->state) {
      case TRACE_PARSER_STATE_INITIAL: {
        json_reader_skip_whitespace(&r);
        if (json_reader_done(&r)) {
          loop = false;  // need more data
        } else {
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
            loop = false;  // Error! Invalid start character
          }
        }
        break;
      }

      case TRACE_PARSER_STATE_LOOKING_FOR_TRACE_EVENTS: {
        json_token_t tok = json_reader_next(&r);
        if (tok.type == JSON_TOKEN_NONE) {
          loop = false;  // need more data
        } else if (tok.type == JSON_TOKEN_OBJECT_END) {
          p->state = TRACE_PARSER_STATE_COMPLETE;
          loop = false;  // done
        } else if (tok.type == JSON_TOKEN_STRING) {
          bool is_trace_events = string_eq(tok.str, string_lit("traceEvents"));
          tok = json_reader_next(&r);
          if (tok.type != JSON_TOKEN_COLON) {
            loop = false;  // Error!
          } else {
            if (is_trace_events) {
              tok = json_reader_next(&r);
              if (tok.type != JSON_TOKEN_ARRAY_START) {
                loop = false;  // Error!
              } else {
                p->state = TRACE_PARSER_STATE_IN_ARRAY;
              }
            } else {
              // Skip value
              tok = json_reader_next(&r);
              if (tok.type == JSON_TOKEN_OBJECT_START ||
                  tok.type == JSON_TOKEN_ARRAY_START) {
                int depth = 1;
                while (depth > 0 && !json_reader_done(&r)) {
                  tok = json_reader_next(&r);
                  if (tok.type == JSON_TOKEN_OBJECT_START ||
                      tok.type == JSON_TOKEN_ARRAY_START) {
                    depth++;
                  } else if (tok.type == JSON_TOKEN_OBJECT_END ||
                             tok.type == JSON_TOKEN_ARRAY_END) {
                    depth--;
                  }
                }
              }
            }
          }
        }
        break;
      }

      case TRACE_PARSER_STATE_IN_ARRAY: {
        json_reader_skip_whitespace(&r);
        if (json_reader_done(&r)) {
          loop = false;  // need more data
        } else if (json_reader_peek(&r) == ']') {
          json_reader_advance(&r);
          if (p->is_array_format) {
            p->state = TRACE_PARSER_STATE_COMPLETE;
            loop = false;  // done
          } else {
            p->state = TRACE_PARSER_STATE_LOOKING_FOR_TRACE_EVENTS;
          }
        } else {
          if (json_reader_peek(&r) == ',') {
            json_reader_advance(&r);
            json_reader_skip_whitespace(&r);
          }
          if (json_reader_done(&r)) {
            loop = false;  // need more data
          } else {
            size_t start_pos = r.pos;
            if (parse_event(&r, p, a, event)) {
              p->pos = r.pos;
              found = true;
            } else {
              // If it failed because it ran out of data, wait for more
              if (json_reader_done(&r)) {
                r.pos = start_pos;
                loop = false;  // need more data
              } else {
                loop = false;  // Error! Invalid event JSON
              }
            }
          }
        }
        break;
      }

      case TRACE_PARSER_STATE_COMPLETE:
        loop = false;  // done
        break;
    }
    if (!found && loop) {
      loop = !json_reader_done(&r);
    }
  }

  // Update parser position on return
  if (!found) {
    p->pos = r.pos;
  }
  return found;
}
