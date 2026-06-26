#ifndef SRC_TRACE_HISTOGRAM_H
#define SRC_TRACE_HISTOGRAM_H

#include <stddef.h>
#include <stdint.h>

#include "src/array_list.h"
#include "src/trace_data.h"

#define TRACE_HISTOGRAM_MAX_BINS 32

typedef struct trace_histogram_bucket {
  int64_t min_dur;
  int64_t max_dur;
  uint32_t count;
} trace_histogram_bucket_t;

typedef struct trace_histogram {
  trace_histogram_bucket_t buckets[TRACE_HISTOGRAM_MAX_BINS];
  int num_buckets;
  uint32_t max_bucket_count;
  uint32_t total_count;
  bool has_non_zero_durations;
} trace_histogram_t;

#ifdef __cplusplus
extern "C" {
#endif

// Computes the linear or logarithmic duration distribution histogram for a set
// of event indices.
//
// Arguments:
// - results: The array_list_t of int64_t event indices.
// - td: The trace_data_t storage.
// - out_histogram: The trace_histogram_t output to populate.
void trace_histogram_compute(const array_list_t* results,
                             const trace_data_t* td,
                             trace_histogram_t* out_histogram);

#ifdef __cplusplus
}
#endif

#endif  // SRC_TRACE_HISTOGRAM_H
