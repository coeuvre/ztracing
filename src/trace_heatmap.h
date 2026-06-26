#ifndef SRC_TRACE_HEATMAP_H
#define SRC_TRACE_HEATMAP_H

#include <stddef.h>
#include <stdint.h>

#include "src/array_list.h"
#include "src/trace_data.h"

#define TRACE_HEATMAP_BUCKET_COUNT 16

typedef struct trace_heatmap {
  size_t event_indices[TRACE_HEATMAP_BUCKET_COUNT];
} trace_heatmap_t;

#ifdef __cplusplus
extern "C" {
#endif

// Computes the dominant depth-0 event index for each track across 16 horizontal
// time slices.
//
// Performance Features:
// - Allocation-Free: Operates entirely in-place on a pre-allocated output
// buffer.
// - Type-Safe: Explicitly accepts a trace_heatmap_t* array of size
// `tracks->len`.
//
// Arguments:
// - tracks: The array_list_t of organized track_t structures.
// - td: The trace_data_t storage containing the persisted events.
// - min_ts: The start timestamp of the trace viewport.
// - max_ts: The end timestamp of the trace viewport.
// - out_heatmaps: Output array of trace_heatmap_t structures (must be of size
// tracks->len).
void trace_heatmap_compute(const array_list_t* tracks, const trace_data_t* td,
                           int64_t min_ts, int64_t max_ts,
                           trace_heatmap_t* out_heatmaps);

#ifdef __cplusplus
}
#endif

#endif  // SRC_TRACE_HEATMAP_H
