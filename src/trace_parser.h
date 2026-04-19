#ifndef ZTRACING_SRC_TRACE_PARSER_H_
#define ZTRACING_SRC_TRACE_PARSER_H_

// This parser implements the Chrome Trace Event Format.
// For the format specification, see:
// https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/preview

#include <stdint.h>

#include "src/allocator.h"
#include "src/array_list.h"
#include "src/str.h"

struct TraceArg {
  Str key;
  Str val;
};

struct TraceEvent {
  Str name;
  Str cat;
  Str ph;
  Str cname;
  int64_t ts;
  int64_t dur;
  int32_t pid;
  int32_t tid;
  TraceArg* args;
  size_t args_count;
};

enum TraceParserState {
  TRACE_PARSER_STATE_INITIAL,
  TRACE_PARSER_STATE_LOOKING_FOR_TRACE_EVENTS,
  TRACE_PARSER_STATE_IN_ARRAY,
  TRACE_PARSER_STATE_COMPLETE,
};

struct TraceParser {
  Allocator a;
  ArrayList<char> buffer;
  ArrayList<TraceArg> args_buffer;
  TraceParserState state;
  size_t pos;
  bool is_eof;
  bool is_array_format;
};

void trace_parser_init(TraceParser* p, Allocator a);
void trace_parser_deinit(TraceParser* p);

// Feed data to the parser.
void trace_parser_feed(TraceParser* p, const char* buf, size_t len,
                       bool is_eof);

// Pull the next event.
bool trace_parser_next(TraceParser* p, TraceEvent* event);

#endif  // ZTRACING_SRC_TRACE_PARSER_H_
