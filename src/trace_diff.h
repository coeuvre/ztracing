#ifndef SRC_TRACE_DIFF_H
#define SRC_TRACE_DIFF_H

#include <stddef.h>
#include <stdint.h>

#include "core/darray.h"
#include "core/string.h"
#include "src/trace_data.h"

typedef struct trace_diff_entry {
  string_view_t key; // The string value (name or category)
  double baseline_duration;
  size_t baseline_count;
  double target_duration;
  size_t target_count;
  double delta_duration;
  int64_t delta_count;
} trace_diff_entry_t;

typedef darray_t(trace_diff_entry_t) darray_trace_diff_entry_t;

#ifdef __cplusplus
extern "C" {
#endif

// Computes delta comparison between two traces.
//
// Arguments:
// - td_baseline: The baseline trace data.
// - td_target: The target trace data.
// - group_by: "name" or "category".
// - sort_by: "dur-delta" or "count-delta".
// - out_entries: Output darray to be populated with sorted entries.
// - a: Allocator.
void trace_diff_compute(const trace_data_t* td_baseline,
                        const trace_data_t* td_target, string_view_t group_by,
                        string_view_t sort_by,
                        darray_trace_diff_entry_t* out_entries,
                        allocator_t* a);

#ifdef __cplusplus
}
#endif

#endif // SRC_TRACE_DIFF_H
