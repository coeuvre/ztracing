#include "src/trace_parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "core/allocator.h"
#include "core/json_reader.h"
#include "core/darray.h"
#include "core/string.h"

void trace_parser_deinit(trace_parser_t* p, allocator_t* a) {
  darray_deinit(&p->buffer, a);
  darray_deinit(&p->args_buffer, a);
}

size_t trace_parser_feed(trace_parser_t* p, const char* buf, size_t len,
                         bool is_eof, allocator_t* a) {
  size_t discarded = 0;
  if (p->pos > 0 && p->pos > p->buffer.len / 2) {
    if (p->pos < p->buffer.len) {
      memmove(p->buffer.ptr, p->buffer.ptr + p->pos,
              p->buffer.len - p->pos);
    }
    discarded = p->pos;
    p->buffer.len -= p->pos;
    p->pos = 0;
  }

  size_t old_len = p->buffer.len;
  darray_resize(&p->buffer, old_len + len, a);
  memcpy(p->buffer.ptr + old_len, buf, len);

  p->is_eof = is_eof;
  return discarded;
}

static inline int32_t clamp_to_int32(int64_t val) {
  if (val > INT32_MAX) return INT32_MAX;
  if (val < INT32_MIN) return INT32_MIN;
  return (int32_t)val;
}

static bool parse_event(json_reader_t* r, trace_parser_t* p, allocator_t* a,
                        trace_event_t* event) {
  bool success = false;
  *event = (trace_event_t){};
  darray_clear(&p->args_buffer);
  bool ok = true;
  json_token_t tok;

  while (ok) {
    json_reader_next(r, &tok);
    if (tok.type == JSON_TOKEN_OBJECT_END) {
      success = true;
      break;
    }
    if (tok.type != JSON_TOKEN_STRING) {
      ok = false;
      break;
    }

    string_view_t key = tok.val.str;
    json_reader_next(r, &tok);
    if (tok.type != JSON_TOKEN_COLON) {
      ok = false;
      break;
    }

    if (string_view_eq(key, SV("name"))) {
      json_reader_next(r, &tok);
      if (tok.type != JSON_TOKEN_STRING) {
        ok = false;
        break;
      }
      event->name = tok.val.str;
    } else if (string_view_eq(key, SV("cat"))) {
      json_reader_next(r, &tok);
      if (tok.type != JSON_TOKEN_STRING) {
        ok = false;
        break;
      }
      event->cat = tok.val.str;
    } else if (string_view_eq(key, SV("ph"))) {
      json_reader_next(r, &tok);
      if (tok.type != JSON_TOKEN_STRING) {
        ok = false;
        break;
      }
      event->ph = tok.val.str;
    } else if (string_view_eq(key, SV("cname"))) {
      json_reader_next(r, &tok);
      if (tok.type != JSON_TOKEN_STRING) {
        ok = false;
        break;
      }
      event->cname = tok.val.str;
    } else if (string_view_eq(key, SV("ts"))) {
      json_reader_next(r, &tok);
      if (tok.type != JSON_TOKEN_NUMBER_I64 &&
          tok.type != JSON_TOKEN_NUMBER_F64) {
        ok = false;
        break;
      }
      if (tok.type == JSON_TOKEN_NUMBER_I64) {
        event->ts = tok.val.i64;
      } else {
        event->ts = (int64_t)tok.val.f64;
      }
    } else if (string_view_eq(key, SV("dur"))) {
      json_reader_next(r, &tok);
      if (tok.type != JSON_TOKEN_NUMBER_I64 &&
          tok.type != JSON_TOKEN_NUMBER_F64) {
        ok = false;
        break;
      }
      if (tok.type == JSON_TOKEN_NUMBER_I64) {
        event->dur = tok.val.i64;
      } else {
        event->dur = (int64_t)tok.val.f64;
      }
    } else if (string_view_eq(key, SV("pid"))) {
      json_reader_next(r, &tok);
      if (tok.type != JSON_TOKEN_NUMBER_I64 &&
          tok.type != JSON_TOKEN_NUMBER_F64) {
        ok = false;
        break;
      }
      if (tok.type == JSON_TOKEN_NUMBER_I64) {
        event->pid = clamp_to_int32(tok.val.i64);
      } else {
        event->pid = clamp_to_int32((int64_t)tok.val.f64);
      }
    } else if (string_view_eq(key, SV("tid"))) {
      json_reader_next(r, &tok);
      if (tok.type != JSON_TOKEN_NUMBER_I64 &&
          tok.type != JSON_TOKEN_NUMBER_F64) {
        ok = false;
        break;
      }
      if (tok.type == JSON_TOKEN_NUMBER_I64) {
        event->tid = clamp_to_int32(tok.val.i64);
      } else {
        event->tid = clamp_to_int32((int64_t)tok.val.f64);
      }
    } else if (string_view_eq(key, SV("id"))) {
      json_reader_next(r, &tok);
      if (tok.type == JSON_TOKEN_STRING || tok.type == JSON_TOKEN_NUMBER_I64 ||
          tok.type == JSON_TOKEN_NUMBER_F64) {
        event->id = tok.val.str;
      } else {
        ok = false;
        break;
      }
    } else if (string_view_eq(key, SV("args"))) {
      json_reader_next(r, &tok);
      if (tok.type != JSON_TOKEN_OBJECT_START) {
        ok = false;
        break;
      }
      while (ok) {
        json_reader_next(r, &tok);
        if (tok.type == JSON_TOKEN_OBJECT_END) {
          break;
        }
        if (tok.type != JSON_TOKEN_STRING) {
          ok = false;
          break;
        }
        trace_arg_t arg = {};
        arg.key = tok.val.str;
        json_reader_next(r, &tok);
        if (tok.type != JSON_TOKEN_COLON) {
          ok = false;
          break;
        }

        json_reader_next(r, &tok);
        arg.val_double = 0.0;
        if (tok.type == JSON_TOKEN_STRING ||
            tok.type == JSON_TOKEN_NUMBER_I64 ||
            tok.type == JSON_TOKEN_NUMBER_F64 || tok.type == JSON_TOKEN_TRUE ||
            tok.type == JSON_TOKEN_FALSE || tok.type == JSON_TOKEN_NULL) {
          if (tok.type == JSON_TOKEN_NUMBER_I64) {
            arg.val_double = (double)tok.val.i64;
            arg.val = (string_view_t){};
          } else if (tok.type == JSON_TOKEN_NUMBER_F64) {
            arg.val_double = tok.val.f64;
            arg.val = (string_view_t){};
          } else {
            arg.val = tok.val.str;
          }
        } else if (tok.type == JSON_TOKEN_OBJECT_START ||
                   tok.type == JSON_TOKEN_ARRAY_START) {
          size_t start = r->pos - tok.val.str.len;
          int depth = 1;
          while (depth > 0) {
            json_reader_next(r, &tok);
            if (tok.type == JSON_TOKEN_EOF || tok.type == JSON_TOKEN_ERROR) {
              break;
            }
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

        darray_push(&p->args_buffer, arg, a);

        json_reader_next(r, &tok);
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
      json_reader_next(r, &tok);
      if (tok.type == JSON_TOKEN_OBJECT_START ||
          tok.type == JSON_TOKEN_ARRAY_START) {
        int depth = 1;
        while (depth > 0) {
          json_reader_next(r, &tok);
          if (tok.type == JSON_TOKEN_EOF || tok.type == JSON_TOKEN_ERROR) {
            break;
          }
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

    json_reader_next(r, &tok);
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

  if (success) {
    event->args = p->args_buffer.ptr;
    event->args_count = p->args_buffer.len;
  }
  return success;
}

bool trace_parser_next(trace_parser_t* p, trace_event_t* event,
                       allocator_t* a) {
  bool found = false;
  json_reader_t r = {(const char*)p->buffer.ptr, p->buffer.len, p->pos};
  bool loop = !json_reader_done(&r);
  json_token_t tok;

  while (loop && !found) {
    switch (p->state) {
      case TRACE_PARSER_STATE_INITIAL: {
        json_reader_next(&r, &tok);
        if (tok.type == JSON_TOKEN_EOF) {
          loop = false;  // need more data
        } else if (tok.type == JSON_TOKEN_ARRAY_START) {
          p->is_array_format = true;
          p->state = TRACE_PARSER_STATE_IN_ARRAY;
        } else if (tok.type == JSON_TOKEN_OBJECT_START) {
          p->is_array_format = false;
          p->state = TRACE_PARSER_STATE_LOOKING_FOR_TRACE_EVENTS;
        } else {
          loop = false;  // Error! Invalid start character
        }
        break;
      }

      case TRACE_PARSER_STATE_LOOKING_FOR_TRACE_EVENTS: {
        json_reader_next(&r, &tok);
        if (tok.type == JSON_TOKEN_EOF) {
          loop = false;  // need more data
        } else if (tok.type == JSON_TOKEN_OBJECT_END) {
          p->state = TRACE_PARSER_STATE_COMPLETE;
          loop = false;  // done
        } else if (tok.type == JSON_TOKEN_STRING) {
          bool is_trace_events = string_view_eq(tok.val.str, SV("traceEvents"));
          json_reader_next(&r, &tok);
          if (tok.type != JSON_TOKEN_COLON) {
            loop = false;  // Error!
          } else {
            if (is_trace_events) {
              json_reader_next(&r, &tok);
              if (tok.type != JSON_TOKEN_ARRAY_START) {
                loop = false;  // Error!
              } else {
                p->state = TRACE_PARSER_STATE_IN_ARRAY;
              }
            } else {
              // Skip value
              json_reader_next(&r, &tok);
              if (tok.type == JSON_TOKEN_OBJECT_START ||
                  tok.type == JSON_TOKEN_ARRAY_START) {
                int depth = 1;
                while (depth > 0) {
                  json_reader_next(&r, &tok);
                  if (tok.type == JSON_TOKEN_EOF ||
                      tok.type == JSON_TOKEN_ERROR) {
                    break;
                  }
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
        size_t checkpoint = r.pos;
        json_reader_next(&r, &tok);

        if (tok.type == JSON_TOKEN_EOF) {
          loop = false;  // need more data
        } else if (tok.type == JSON_TOKEN_ARRAY_END) {
          if (p->is_array_format) {
            p->state = TRACE_PARSER_STATE_COMPLETE;
            loop = false;  // done
          } else {
            p->state = TRACE_PARSER_STATE_LOOKING_FOR_TRACE_EVENTS;
          }
        } else {
          // We expect either OBJECT_START (first event) or COMMA (subsequent
          // events)
          if (tok.type == JSON_TOKEN_COMMA) {
            checkpoint = r.pos;
            json_reader_next(&r, &tok);
          }

          if (tok.type == JSON_TOKEN_OBJECT_START) {
            if (parse_event(&r, p, a, event)) {
              p->pos = r.pos;
              found = true;
            } else {
              if (json_reader_done(&r)) {
                r.pos = checkpoint;
                loop = false;  // need more data
              } else {
                loop = false;  // Real parsing error
              }
            }
          } else if (tok.type == JSON_TOKEN_EOF) {
            r.pos = checkpoint;
            loop = false;  // need more data (chunk ended after comma)
          } else {
            loop = false;  // Error! Expected object start
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
