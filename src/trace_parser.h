#ifndef ZTRACING_SRC_TRACE_PARSER_H_
#define ZTRACING_SRC_TRACE_PARSER_H_

// This parser implements the Chrome Trace Event Format.
// For the format specification, see:
// https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/preview

#include <stdint.h>
#include <string_view>

#include "src/allocator.h"
#include "src/array_list.h"

struct TraceArg {
  std::string_view key;
  std::string_view val;
  double val_double;
};

struct TraceEvent {
  std::string_view name;
  std::string_view cat;
  std::string_view ph;
  std::string_view cname;
  std::string_view id;
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
  ArrayList<char> buffer;
  ArrayList<TraceArg> args_buffer;
  TraceParserState state;
  size_t pos;
  bool is_eof;
  bool is_array_format;
};

void trace_parser_deinit(TraceParser* p, Allocator a);

// Feed data to the parser. Returns the number of bytes discarded from the internal
// buffer.
size_t trace_parser_feed(TraceParser* p, Allocator a, const char* buf, size_t len,
                         bool is_eof);

// Pull the next event.
bool trace_parser_next(TraceParser* p, Allocator a, TraceEvent* event);

#endif  // ZTRACING_SRC_TRACE_PARSER_H_
