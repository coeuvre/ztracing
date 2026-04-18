#ifndef ZTRACING_SRC_TRACE_DATA_H_
#define ZTRACING_SRC_TRACE_DATA_H_

#include <stdint.h>

#include "src/allocator.h"
#include "src/array_list.h"
#include "src/str.h"
#include "src/trace_parser.h"

struct TraceArgPersisted {
  uint32_t key_offset;
  uint32_t val_offset;
};

struct TraceEventPersisted {
  uint32_t name_offset;
  uint32_t cat_offset;
  uint32_t ph_offset;
  int64_t ts;
  int64_t dur;
  int32_t pid;
  int32_t tid;
  uint32_t args_offset;
  uint32_t args_count;
};

struct TraceData {
  ArrayList<char> string_pool;
  ArrayList<TraceEventPersisted> events;
  ArrayList<TraceArgPersisted> args;
};

void trace_data_init(TraceData* td, Allocator a);
void trace_data_deinit(TraceData* td, Allocator a);
void trace_data_clear(TraceData* td, Allocator a);

void trace_data_add_event(TraceData* td, Allocator a, const TraceEvent* event);

// Helper to get a string from an offset.
inline Str trace_data_get_string(const TraceData* td, uint32_t offset) {
  if (offset == 0) return {nullptr, 0};
  const char* s = &td->string_pool[offset];
  return {s, strlen(s)};
}

#endif  // ZTRACING_SRC_TRACE_DATA_H_
