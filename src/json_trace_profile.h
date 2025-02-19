#ifndef ZTRACING_SRC_JSON_TRACE_H_
#define ZTRACING_SRC_JSON_TRACE_H_

#include "src/hash_trie.h"
#include "src/json.h"
#include "src/string.h"

typedef struct JsonTraceProfileSample {
} JsonTraceProfileSample;

typedef struct JsonTraceEvent {
  Str8 name;
  Str8 id;
  Str8 cat;
  u8 ph;
  i64 ts;
  i64 tts;
  i64 pid;
  i64 tid;
  i64 dur;
  JsonValue *args;
} JsonTraceEvent;

typedef struct JsonTraceProcess {
} JsonTraceProcess;

typedef struct JsonTraceProfile {
  i64 min_time;
  i64 max_time;
  HashTrie processes;
  usize process_size;
  Str8 error;
} JsonTraceProfile;

JsonTraceProfile *json_trace_profile_parse(Arena *arena, JsonParser *parser);

#endif  // ZTRACING_SRC_JSON_TRACE_H_
