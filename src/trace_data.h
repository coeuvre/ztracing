#ifndef ZTRACING_SRC_TRACE_DATA_H_
#define ZTRACING_SRC_TRACE_DATA_H_

#include <stdint.h>

#include "src/allocator.h"
#include "src/array_list.h"
#include "src/hash_table.h"
#include "src/string.h"
#include "src/trace_parser.h"

// A reference to a string in the TraceData string pool.
// TODO: Turn string_ref_t into a struct wrapping a single uint32_t once
// the entire project has been fully migrated to C23. This will provide
// compile-time type safety across all files without requiring C/C++ extern "C"
// linkage compatibility scaffolds and warning-suppression hacks.
typedef uint32_t string_ref_t;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct trace_arg_persisted {
  string_ref_t key_ref;
  string_ref_t val_ref;
  double val_double;
} trace_arg_persisted_t;

// Represents a persistent trace event stored in trace_data_t.
//
// Lifetime:
// All string fields (name_ref, cat_ref, ph_ref, cname_ref, id_ref) are stored
// as string_ref_t indices into the global trace_data_t string pool. The
// underlying string memory is managed by the trace_data_t instance. Therefore,
// the lifetime of the resolved strings (retrieved via trace_data_get_string) is
// strictly bound to the lifetime of the parent trace_data_t instance. They
// remain valid and stable until the parent trace_data_t is released.
typedef struct trace_event_persisted {
  string_ref_t name_ref;
  string_ref_t cat_ref;
  string_ref_t ph_ref;
  string_ref_t cname_ref;
  string_ref_t id_ref;
  uint32_t color;
  int64_t ts;
  int64_t dur;
  int32_t pid;
  int32_t tid;
  uint32_t args_offset;
  uint32_t args_count;
} trace_event_persisted_t;

typedef struct string_entry {
  uint32_t offset;
  uint32_t len;
  uint32_t hash;
} string_entry_t;

typedef struct string_lookup_entry {
  uint32_t index;
  uint32_t hash;
} string_lookup_entry_t;

typedef struct string_lookup_table {
  string_lookup_entry_t* entries;
  size_t capacity;
  size_t size;
  size_t capacity_mask;
} string_lookup_table_t;

typedef struct trace_data {
  array_list_t string_buffer;  // Element type: char
  array_list_t string_table;   // Element type: string_entry_t
  string_lookup_table_t string_lookup;
  array_list_t events;  // Element type: trace_event_persisted_t
  array_list_t args;    // Element type: trace_arg_persisted_t

  // Temporary storage for hashing during push
  struct {
    string_t current_str;
    uint32_t current_hash;
  } tmp;

  int ref_count;
} trace_data_t;

trace_data_t* trace_data_create(allocator_t a);
void trace_data_retain(trace_data_t* td);
void trace_data_release(trace_data_t* td, allocator_t a);

typedef struct active_event_b {
  size_t event_idx;
} active_event_b_t;

typedef struct thread_stack {
  array_list_t stack;  // Element type: active_event_b_t
} thread_stack_t;

typedef struct trace_event_matcher {
  hash_table_t active_b_events;
} trace_event_matcher_t;

void trace_event_matcher_deinit(trace_event_matcher_t* matcher, allocator_t a);

struct Theme;
typedef struct Theme theme_t;


string_ref_t trace_data_push_string(trace_data_t* td, string_t s,
                                    allocator_t a);

void trace_data_add_event(trace_data_t* td, const theme_t* theme,
                          const trace_event_t* event,
                          trace_event_matcher_t* matcher, allocator_t a);

void trace_data_update_event_color(trace_data_t* td, uint32_t event_idx,
                                   const theme_t* theme);

static inline string_t trace_data_get_string(const trace_data_t* td,
                                             string_ref_t ref) {
  string_t result = {};
  if (ref > 0 && ref <= td->string_table.len) {
    const string_entry_t* table = (const string_entry_t*)td->string_table.ptr;
    const string_entry_t* e = &table[ref - 1];
    result = string_from_parts((const char*)td->string_buffer.ptr + e->offset,
                               e->len);
  }
  return result;
}


/**
 * Performs a binary search (lower bound) over an array of event indices.
 *
 * This function searches for the first event index in `event_indices` whose
 * corresponding trace event has a timestamp greater than or equal to
 * `target_ts`. The events pointed to by `event_indices` must be sorted in
 * ascending order of their timestamps (i.e., `events[event_indices[i]].ts <=
 * events[event_indices[i+1]].ts`).
 *
 * @param event_indices An array of indices into the trace data events array.
 * @param size The number of elements in the `event_indices` array.
 * @param events The array of persisted trace events.
 * @param target_ts The target timestamp to search for.
 * @return The index of the first element in `event_indices` with a timestamp >=
 * `target_ts`. Returns `size` if all events have timestamps < `target_ts`.
 */
static inline size_t trace_data_events_lower_bound(
    const size_t* event_indices, size_t size,
    const trace_event_persisted_t* events, int64_t target_ts) {
  size_t low = 0;
  size_t high = size;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    if (events[event_indices[mid]].ts < target_ts) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  return low;
}

#ifdef __cplusplus
}
#endif

#endif  // ZTRACING_SRC_TRACE_DATA_H_
