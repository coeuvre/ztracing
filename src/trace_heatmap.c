#include "src/trace_heatmap.h"

#include "src/track.h"

// Computes the dominant depth-0 event index for each track across 16 horizontal
// time slices.
void trace_heatmap_compute(const array_list_t* tracks, const trace_data_t* td,
                           int64_t min_ts, int64_t max_ts,
                           trace_heatmap_t* out_heatmaps) {
  if (tracks && td && out_heatmaps && tracks->len > 0) {
    const track_t* tracks_ptr = (const track_t*)tracks->ptr;
    const trace_event_persisted_t* events =
        (const trace_event_persisted_t*)td->events.ptr;

    // Initialize all buckets of all heatmaps to (size_t)-1 (idle) to prevent
    // out-of-bounds access on zero duration or empty traces.
    for (size_t i = 0; i < tracks->len; i++) {
      trace_heatmap_t* h = &out_heatmaps[i];
      for (int b = 0; b < TRACE_HEATMAP_BUCKET_COUNT; b++) {
        h->event_indices[b] = (size_t)-1;
      }
    }

    double total_dur = (double)(max_ts - min_ts);
    if (total_dur > 0) {
      double bucket_dur = total_dur / TRACE_HEATMAP_BUCKET_COUNT;
      int64_t max_dur[TRACE_HEATMAP_BUCKET_COUNT];

      for (size_t i = 0; i < tracks->len; i++) {
        const track_t* t = &tracks_ptr[i];
        trace_heatmap_t* h = &out_heatmaps[i];

        // Initialize duration cache and double-check buckets for this track
        for (int b = 0; b < TRACE_HEATMAP_BUCKET_COUNT; b++) {
          h->event_indices[b] = (size_t)-1;
          max_dur[b] = -1;
        }

        if (t->event_indices.len > 0) {
          const size_t* t_event_indices = (const size_t*)t->event_indices.ptr;
          const int* t_depths = (const int*)t->depths.ptr;

          // Identify dominant event index in each bucket
          for (size_t k = 0; k < t->event_indices.len; k++) {
            // Thread tracks only consider depth-0 events for heatmap
            // representation
            if (t->type == TRACK_TYPE_THREAD && t_depths[k] != 0) {
              continue;
            }

            size_t event_idx = t_event_indices[k];
            if (event_idx < td->events.len) {
              const trace_event_persisted_t* e = &events[event_idx];
              double rel_ts = (double)(e->ts - min_ts);
              int b_idx = (int)(rel_ts / bucket_dur);

              if (b_idx < 0) {
                b_idx = 0;
              }
              if (b_idx >= TRACE_HEATMAP_BUCKET_COUNT) {
                b_idx = TRACE_HEATMAP_BUCKET_COUNT - 1;
              }

              // Pick dominant event index based on longest duration
              if (e->dur > max_dur[b_idx]) {
                max_dur[b_idx] = e->dur;
                h->event_indices[b_idx] = event_idx;
              }
            }
          }
        }
      }
    }
  }
}
