#ifndef ZTRACING_SRC_JSON_TRACE_H_
#define ZTRACING_SRC_JSON_TRACE_H_

#include "src/hash_trie.h"
#include "src/json.h"
#include "src/string.h"

typedef struct JsonTraceProfileSample {
} JsonTraceProfileSample;

typedef struct JsonTraceProcess {
} JsonTraceProcess;

typedef struct JsonTraceProfile {
  i64 min_time_ns;
  i64 max_time_ns;
  HashTrie processes;
  usize process_size;
  Str8 error;
} JsonTraceProfile;

JsonTraceProfile *json_trace_profile_parse(Arena *arena, JsonParser *parser);

#endif  // ZTRACING_SRC_JSON_TRACE_H_
