#ifndef ZTRACING_SRC_JSON_TRACE_H_
#define ZTRACING_SRC_JSON_TRACE_H_

#include "src/hash_trie.h"
#include "src/json.h"
#include "src/string.h"

typedef struct JsonTraceSample JsonTraceSample;
struct JsonTraceSample {
  JsonTraceSample *prev;
  JsonTraceSample *next;
  i64 time;
  f64 value;
};

typedef struct JsonTraceSeries {
  Str8 name;
  JsonTraceSample *first;
  JsonTraceSample *last;
  usize sample_len;
} JsonTraceSeries;

typedef struct JsonTraceCounter {
  Str8 name;
  HashTrie *series;
  usize series_len;
  f64 min_value;
  f64 max_value;
} JsonTraceCounter;

typedef struct JsonTraceProcess {
  i64 pid;
  HashTrie *counters;
  usize counter_len;
} JsonTraceProcess;

typedef struct JsonTraceProfile {
  i64 min_time_ns;
  i64 max_time_ns;
  HashTrie *processes;
  usize process_len;
  Str8 error;
} JsonTraceProfile;

JsonTraceProfile *json_trace_profile_parse(Arena *arena, JsonParser *parser);

#endif  // ZTRACING_SRC_JSON_TRACE_H_
