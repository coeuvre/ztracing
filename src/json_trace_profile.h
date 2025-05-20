#ifndef ZTRACING_SRC_JSON_TRACE_H_
#define ZTRACING_SRC_JSON_TRACE_H_

#include "src/hash_trie.h"
#include "src/json.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"

typedef struct JsonTraceSample JsonTraceSample;
struct JsonTraceSample {
  JsonTraceSample *prev;
  JsonTraceSample *next;
  i64 time;
  f64 value;
};

typedef struct JsonTraceSeries {
  Str name;
  usize sample_count;
  JsonTraceSample *first;
  JsonTraceSample *last;
} JsonTraceSeries;

typedef struct JsonTraceCounter {
  Str name;
  usize series_count;
  HashTrie series;
  f64 min_value;
  f64 max_value;
} JsonTraceCounter;

typedef struct JsonTraceSpan JsonTraceSpan;
struct JsonTraceSpan {
  JsonTraceSpan *prev;
  JsonTraceSpan *next;
  Str name;
  Str cat;
  i64 begin_time_ns;
  i64 end_time_ns;
};

typedef struct JsonTraceThread {
  i64 tid;
  Str name;
  i64o sort_index;
  usize span_count;
  JsonTraceSpan *first_span;
  JsonTraceSpan *last_span;

  JsonTraceSpan *first_open_span;
  JsonTraceSpan *last_open_span;
} JsonTraceThread;

typedef struct JsonTraceProcess {
  i64 pid;

  usize counter_count;
  HashTrie counters;

  usize thread_count;
  HashTrie threads;
} JsonTraceProcess;

typedef struct JsonTraceProfile {
  i64 min_time_ns;
  i64 max_time_ns;
  usize process_count;
  HashTrie processes;
  Str error;
} JsonTraceProfile;

JsonTraceProfile *JsonTraceProfile_Parse(Arena *arena, JsonParser *parser);

#endif  // ZTRACING_SRC_JSON_TRACE_H_
