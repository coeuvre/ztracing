#include "src/trace_data.h"

#include <string.h>

static uint32_t trace_data_push_string(TraceData* td, Allocator a, Str s) {
  if (s.buf == nullptr) return 0;

  uint32_t offset = (uint32_t)td->string_pool.size;
  array_list_append(&td->string_pool, a, s.buf, s.len);
  char null_terminator = '\0';
  array_list_push_back(&td->string_pool, a, null_terminator);
  return offset;
}

void trace_data_init(TraceData* td, Allocator a) {
  *td = {};
  // Offset 0 is reserved for null/empty string.
  char null_terminator = '\0';
  array_list_push_back(&td->string_pool, a, null_terminator);
}

void trace_data_deinit(TraceData* td, Allocator a) {
  array_list_deinit(&td->string_pool, a);
  array_list_deinit(&td->events, a);
  array_list_deinit(&td->args, a);
}

void trace_data_clear(TraceData* td, Allocator a) {
  array_list_clear(&td->string_pool);
  array_list_clear(&td->events);
  array_list_clear(&td->args);

  // Offset 0 is reserved for null/empty string.
  char null_terminator = '\0';
  array_list_push_back(&td->string_pool, a, null_terminator);
}

void trace_data_add_event(TraceData* td, Allocator a, const TraceEvent* event) {
  TraceEventPersisted p = {};
  p.name_offset = trace_data_push_string(td, a, event->name);
  p.cat_offset = trace_data_push_string(td, a, event->cat);
  p.ph_offset = trace_data_push_string(td, a, event->ph);
  p.ts = event->ts;
  p.dur = event->dur;
  p.pid = event->pid;
  p.tid = event->tid;
  p.args_count = (uint32_t)event->args_count;
  p.args_offset = (uint32_t)td->args.size;

  for (size_t i = 0; i < event->args_count; ++i) {
    TraceArgPersisted arg = {};
    arg.key_offset = trace_data_push_string(td, a, event->args[i].key);
    arg.val_offset = trace_data_push_string(td, a, event->args[i].val);
    array_list_push_back(&td->args, a, arg);
  }

  array_list_push_back(&td->events, a, p);
}
