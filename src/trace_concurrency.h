#ifndef SRC_TRACE_CONCURRENCY_H
#define SRC_TRACE_CONCURRENCY_H

#include <stddef.h>
#include <stdint.h>

#include "core/darray.h"
#include "src/trace_data.h"
#include "src/track.h"

#define TRACE_CONCURRENCY_MAX_DOMINANT_EVENTS 3

typedef struct trace_concurrency_bucket {
  double start_ts;
  double end_ts;
  double average_concurrency;
  uint32_t dominant_events[TRACE_CONCURRENCY_MAX_DOMINANT_EVENTS];
  size_t dominant_events_count;
} trace_concurrency_bucket_t;

#ifdef __cplusplus
extern "C" {
#endif

// Computes concurrency metrics and dominant events across N horizontal buckets.
//
// Arguments:
// - tracks: The darray_track_t of organized track_t structures.
// - td: The trace_data_t storage containing the persisted events.
// - min_ts: The start timestamp of the trace.
// - max_ts: The end timestamp of the trace.
// - num_buckets: Number of buckets to divide the trace into.
// - out_buckets: Output array of trace_concurrency_bucket_t (must be of size num_buckets).
// - a: Allocator for temporary hash table.
void trace_concurrency_compute(const darray_track_t* tracks, const trace_data_t* td,
                               int64_t min_ts, int64_t max_ts, int num_buckets,
                               trace_concurrency_bucket_t* out_buckets,
                               allocator_t* a);

#ifdef __cplusplus
}
#endif

#endif // SRC_TRACE_CONCURRENCY_H
