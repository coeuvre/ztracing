#ifndef SRC_TRACE_AGGREGATE_H
#define SRC_TRACE_AGGREGATE_H

#include <stddef.h>
#include <stdint.h>

#include "core/darray.h"
#include "core/string.h"
#include "src/trace_data.h"

typedef struct trace_aggregate_entry {
  uint32_t key_ref;
  double total_duration;
  size_t count;
} trace_aggregate_entry_t;

typedef darray_t(trace_aggregate_entry_t) darray_trace_aggregate_entry_t;

#ifdef __cplusplus
extern "C" {
#endif

// Computes global aggregation of events grouped by name or category.
//
// Arguments:
// - td: The trace_data_t storage.
// - group_by: "name" or "category".
// - sort_by: "duration" or "count".
// - out_entries: Output darray to be populated with sorted entries.
// - a: Allocator.
void trace_aggregate_compute(const trace_data_t* td, string_view_t group_by,
                             string_view_t sort_by,
                             darray_trace_aggregate_entry_t* out_entries,
                             allocator_t* a);

#ifdef __cplusplus
}
#endif

#endif // SRC_TRACE_AGGREGATE_H
