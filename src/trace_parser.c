#include "src/trace_parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "src/allocator.h"
#include "src/array_list.h"
#include "src/json.h"
#include "src/string.h"

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

static double to_double(string_view_t s) {
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
    if (s.len == 0) return 0.0;

    size_t i = 0;
    bool neg = false;
    if (s.ptr[0] == '-') {
      neg = true;
      i++;
    } else if (s.ptr[0] == '+') {
      i++;
    }

    double integer_part = 0.0;
    for (; i < s.len && s.ptr[i] != '.'; ++i) {
      char c = s.ptr[i];
      if (c < '0' || c > '9') {
        break;
      }
      integer_part = integer_part * 10.0 + (c - '0');
    }

    val = integer_part;
    if (i < s.len && s.ptr[i] == '.') {
      i++;
      double fraction_part = 0.0;
      double divisor = 1.0;
      for (; i < s.len; ++i) {
        char c = s.ptr[i];
        if (c < '0' || c > '9') {
          break;
        }
        fraction_part = fraction_part * 10.0 + (c - '0');
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

static int64_t to_int64(string_view_t s) {
  bool is_float = false;
  for (size_t i = 0; i < s.len; ++i) {
    if (s.ptr[i] == '.' || s.ptr[i] == 'e' || s.ptr[i] == 'E') {
      is_float = true;
      break;
    }
  }
  if (is_float) {
    return (int64_t)to_double(s);
  }

  if (s.len == 0) return 0;

  size_t i = 0;
  bool neg = false;
  if (s.ptr[0] == '-') {
    neg = true;
    i++;
  } else if (s.ptr[0] == '+') {
    i++;
  }

  int64_t val = 0;
  for (; i < s.len; ++i) {
    char c = s.ptr[i];
    if (c < '0' || c > '9') {
      break;
    }
    int digit = c - '0';
    if (val > (INT64_MAX - digit) / 10) {
      return neg ? INT64_MIN : INT64_MAX;
    }
    val = val * 10 + digit;
  }
  return neg ? -val : val;
}

static int32_t to_int32(string_view_t s) {
  bool is_float = false;
  for (size_t i = 0; i < s.len; ++i) {
    if (s.ptr[i] == '.' || s.ptr[i] == 'e' || s.ptr[i] == 'E') {
      is_float = true;
      break;
    }
  }
  if (is_float) {
    return (int32_t)to_double(s);
  }

  if (s.len == 0) return 0;

  size_t i = 0;
  bool neg = false;
  if (s.ptr[0] == '-') {
    neg = true;
    i++;
  } else if (s.ptr[0] == '+') {
    i++;
  }

  int32_t val = 0;
  for (; i < s.len; ++i) {
    char c = s.ptr[i];
    if (c < '0' || c > '9') {
      break;
    }
    int digit = c - '0';
    if (val > (INT32_MAX - digit) / 10) {
      return neg ? INT32_MIN : INT32_MAX;
    }
    val = val * 10 + digit;
  }
  return neg ? -val : val;
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

      string_view_t key = tok.str;
      tok = json_reader_next(r);
      if (tok.type != JSON_TOKEN_COLON) {
        ok = false;
        break;
      }

      if (string_view_eq(key, SV("name"))) {
        tok = json_reader_next(r);
        if (tok.type != JSON_TOKEN_STRING) {
          ok = false;
          break;
        }
        event->name = tok.str;
      } else if (string_view_eq(key, SV("cat"))) {
        tok = json_reader_next(r);
        if (tok.type != JSON_TOKEN_STRING) {
          ok = false;
          break;
        }
        event->cat = tok.str;
      } else if (string_view_eq(key, SV("ph"))) {
        tok = json_reader_next(r);
        if (tok.type != JSON_TOKEN_STRING) {
          ok = false;
          break;
        }
        event->ph = tok.str;
      } else if (string_view_eq(key, SV("cname"))) {
        tok = json_reader_next(r);
        if (tok.type != JSON_TOKEN_STRING) {
          ok = false;
          break;
        }
        event->cname = tok.str;
      } else if (string_view_eq(key, SV("ts"))) {
        tok = json_reader_next(r);
        if (tok.type != JSON_TOKEN_NUMBER) {
          ok = false;
          break;
        }
        event->ts = to_int64(tok.str);
      } else if (string_view_eq(key, SV("dur"))) {
        tok = json_reader_next(r);
        if (tok.type != JSON_TOKEN_NUMBER) {
          ok = false;
          break;
        }
        event->dur = to_int64(tok.str);
      } else if (string_view_eq(key, SV("pid"))) {
        tok = json_reader_next(r);
        if (tok.type != JSON_TOKEN_NUMBER) {
          ok = false;
          break;
        }
        event->pid = to_int32(tok.str);
      } else if (string_view_eq(key, SV("tid"))) {
        tok = json_reader_next(r);
        if (tok.type != JSON_TOKEN_NUMBER) {
          ok = false;
          break;
        }
        event->tid = to_int32(tok.str);
      } else if (string_view_eq(key, SV("id"))) {
        tok = json_reader_next(r);
        if (tok.type == JSON_TOKEN_STRING || tok.type == JSON_TOKEN_NUMBER) {
          event->id = tok.str;
        } else {
          ok = false;
          break;
        }
      } else if (string_view_eq(key, SV("args"))) {
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
              arg.val = (string_view_t){};
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
            arg.val = string_view_from_parts(r->buf + start, r->pos - start);
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
          bool is_trace_events = string_view_eq(tok.str, SV("traceEvents"));
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
