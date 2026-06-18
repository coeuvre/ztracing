#ifndef ZTRACING_SRC_TRACE_PARSER_H_
#define ZTRACING_SRC_TRACE_PARSER_H_

// This parser implements the Chrome Trace Event Format.
// For the format specification, see:
// https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/preview

#include <stdbool.h>
#include <stdint.h>

#include "src/allocator.h"
#include "src/array_list.h"
#include "src/string.h"

#ifdef __cplusplus
extern "C" {
#endif

// A single argument parsed from an event.
// string_t fields are non-owning views pointing into the parser's stream
// buffer.
typedef struct trace_arg {
  string_t key;
  string_t val;
  double val_double;
} trace_arg_t;

// A transient, non-owning view of a parsed trace event.
//
// All string fields are pointers directly into the parser's internal stream
// buffer, and the args array points into the parser's recycled arguments
// buffer. The lifetime of this view is strictly bounded by the parser instance;
// any subsequent call to trace_parser_feed, trace_parser_next, or
// trace_parser_deinit will invalidate this view and its pointers.
typedef struct trace_event {
  string_t name;
  string_t cat;
  string_t ph;
  string_t cname;
  string_t id;
  int64_t ts;
  int64_t dur;
  int32_t pid;
  int32_t tid;
  trace_arg_t* args;
  size_t args_count;
} trace_event_t;

typedef enum trace_parser_state {
  TRACE_PARSER_STATE_INITIAL,
  TRACE_PARSER_STATE_LOOKING_FOR_TRACE_EVENTS,
  TRACE_PARSER_STATE_IN_ARRAY,
  TRACE_PARSER_STATE_COMPLETE,
} trace_parser_state_t;

typedef struct trace_parser {
  array_list_t buffer;
  array_list_t args_buffer;
  trace_parser_state_t state;
  size_t pos;
  bool is_eof;
  bool is_array_format;
} trace_parser_t;

void trace_parser_deinit(trace_parser_t* p, allocator_t a);

// Feed data to the parser. Returns the number of bytes discarded from the
// internal buffer.
size_t trace_parser_feed(trace_parser_t* p, const char* buf, size_t len,
                         bool is_eof, allocator_t a);

// Pull the next event.
bool trace_parser_next(trace_parser_t* p, trace_event_t* event, allocator_t a);

#ifdef __cplusplus
}
#endif

// ============================================================================
// C++ Source-Level Compatibility Wrapper
// ============================================================================
#ifdef __cplusplus

using TraceArg = trace_arg_t;
using TraceEvent = trace_event_t;
using TraceParserState = trace_parser_state_t;
using TraceParser = trace_parser_t;

inline size_t trace_parser_feed(trace_parser_t* p, allocator_t a,
                                const char* buf, size_t len, bool is_eof) {
  return trace_parser_feed(p, buf, len, is_eof, a);
}

inline bool trace_parser_next(trace_parser_t* p, allocator_t a,
                              trace_event_t* event) {
  return trace_parser_next(p, event, a);
}

#endif  // __cplusplus

#endif  // ZTRACING_SRC_TRACE_PARSER_H_
