#include "src/trace_histogram.h"

#include <math.h>
#include <stdbool.h>

// Computes the linear or logarithmic duration distribution histogram for a set
// of event indices.
void trace_histogram_compute(const darray_int64_t* results,
                             const trace_data_t* td,
                             trace_histogram_t* out_histogram) {
  if (results && td && out_histogram) {
    out_histogram->num_buckets = 0;
    out_histogram->max_bucket_count = 0;
    out_histogram->total_count = (uint32_t)results->len;
    out_histogram->has_non_zero_durations = false;

    const int64_t* results_ptr = results->ptr;
    const trace_event_persisted_t* events_ptr = td->events.ptr;

    if (results->len > 0) {
      int64_t min_dur = -1;
      int64_t max_dur = -1;
      uint32_t zero_count = 0;

      // 1. Scan durations of selected events
      for (size_t i = 0; i < results->len; i++) {
        size_t idx = (size_t)results_ptr[i];
        if (idx >= td->events.len) continue;
        const trace_event_persisted_t* e = &events_ptr[idx];
        int64_t d = e->dur;

        if (d <= 0) {
          zero_count++;
        } else {
          if (min_dur == -1 || d < min_dur) min_dur = d;
          if (max_dur == -1 || d > max_dur) max_dur = d;
        }
      }

      int k_bins = 20;

      // 2. Initialize bucket 0 for zero-duration events if present
      if (zero_count > 0) {
        out_histogram->buckets[0].min_dur = 0;
        out_histogram->buckets[0].max_dur = 0;
        out_histogram->buckets[0].count = zero_count;
        out_histogram->num_buckets = 1;
        out_histogram->max_bucket_count = zero_count;
      }

      // 3. Populate non-zero duration buckets
      if (min_dur != -1) {
        out_histogram->has_non_zero_durations = true;

        bool is_logarithmic = false;
        if (min_dur > 0 && max_dur > 0 &&
            (double)max_dur / (double)min_dur > 100.0) {
          is_logarithmic = true;
        }

        int64_t non_zero_range = max_dur - min_dur;
        if (non_zero_range < k_bins) {
          k_bins = (int)non_zero_range + 1;
          is_logarithmic = false;
        }

        // Clamp bins to ensure we don't exceed TRACE_HISTOGRAM_MAX_BINS
        int max_allowed_bins =
            TRACE_HISTOGRAM_MAX_BINS - out_histogram->num_buckets;
        if (k_bins > max_allowed_bins) {
          k_bins = max_allowed_bins;
        }

        double log_min = 0.0;
        double log_max = 0.0;
        double log_width = 0.0;

        if (is_logarithmic) {
          log_min = log10((double)min_dur);
          log_max = log10((double)max_dur);
          log_width = (log_max - log_min) / k_bins;
        }

        int start_bin_idx = out_histogram->num_buckets;
        out_histogram->num_buckets += k_bins;

        // Bucket boundaries L (L needs size k_bins + 1, k_bins <= 32)
        int64_t L[36];
        for (int j = 0; j <= k_bins; j++) {
          if (j == k_bins) {
            L[j] = max_dur + 1;
          } else if (is_logarithmic) {
            double ld = log_min + j * log_width;
            L[j] = (int64_t)round(pow(10.0, ld));
          } else {
            L[j] = min_dur + (int64_t)((non_zero_range * j) / k_bins);
          }

          if (j > 0 && L[j] <= L[j - 1]) {
            L[j] = L[j - 1] + 1;
          }
        }

        // Initialize bucket bounds
        for (int j = 0; j < k_bins; j++) {
          trace_histogram_bucket_t* b =
              &out_histogram->buckets[start_bin_idx + j];
          b->min_dur = L[j];
          b->max_dur = L[j + 1] - 1;
          b->count = 0;
        }

        // Populate counts
        for (size_t i = 0; i < results->len; i++) {
          size_t idx = (size_t)results_ptr[i];
          if (idx >= td->events.len) continue;
          const trace_event_persisted_t* e = &events_ptr[idx];
          int64_t d = e->dur;

          if (d <= 0) continue;

          for (int b_idx = start_bin_idx; b_idx < out_histogram->num_buckets;
               b_idx++) {
            trace_histogram_bucket_t* b = &out_histogram->buckets[b_idx];
            if (d >= b->min_dur && d <= b->max_dur) {
              b->count++;
              if (b->count > out_histogram->max_bucket_count) {
                out_histogram->max_bucket_count = b->count;
              }
              break;
            }
          }
        }
      }
    }
  }
}
