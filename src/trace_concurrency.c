#include "src/trace_concurrency.h"

#include <stdlib.h>
#include <string.h>

#include "core/hash_table.h"
#include "core/darray.h"

typedef struct {
  uint32_t name_ref;
  double duration;
} event_duration_t;

typedef hash_table_t(uint32_t, double) duration_map_t;

static uint32_t hash_uint32(const uint32_t* key, void* ctx) {
  (void)ctx;
  uint32_t a = *key;
  a = (a ^ 61) ^ (a >> 16);
  a = a + (a << 3);
  a = a ^ (a >> 4);
  a = a * 0x27d4eb2d;
  a = a ^ (a >> 15);
  return a;
}

static bool eq_uint32(const uint32_t* a, const uint32_t* b, void* ctx) {
  (void)ctx;
  return *a == *b;
}

static int compare_event_durations(const void* a_ptr, const void* b_ptr) {
  const event_duration_t* ad = (const event_duration_t*)a_ptr;
  const event_duration_t* bd = (const event_duration_t*)b_ptr;
  if (ad->duration > bd->duration) return -1;
  if (ad->duration < bd->duration) return 1;
  return 0;
}

void trace_concurrency_compute(const darray_track_t* tracks, const trace_data_t* td,
                               int64_t min_ts, int64_t max_ts, int num_buckets,
                               trace_concurrency_bucket_t* out_buckets,
                               allocator_t* a) {
  if (!tracks || !td || !out_buckets || num_buckets <= 0 || tracks->len == 0) {
    return;
  }

  double total_dur = (double)(max_ts - min_ts);
  if (total_dur <= 0) {
    return;
  }

  double bucket_dur = total_dur / num_buckets;
  const track_t* tracks_ptr = tracks->ptr;
  const trace_event_persisted_t* events = td->events.ptr;

  duration_map_t duration_map = {};
  hash_table_init(&duration_map, hash_uint32, eq_uint32, nullptr);

  darray_t(event_duration_t) duration_list = {};

  for (int b = 0; b < num_buckets; b++) {
    double b_start = (double)min_ts + b * bucket_dur;
    double b_end = b_start + bucket_dur;

    out_buckets[b].start_ts = b_start;
    out_buckets[b].end_ts = b_end;
    out_buckets[b].average_concurrency = 0.0;
    out_buckets[b].dominant_events_count = 0;

    double total_overlap_fraction = 0.0;
    hash_table_clear(&duration_map);

    for (size_t i = 0; i < tracks->len; i++) {
      const track_t* t = &tracks_ptr[i];
      if (t->type != TRACK_TYPE_THREAD) {
        continue;
      }

      const size_t* t_event_indices = t->event_indices.ptr;
      const uint32_t* t_depths = t->depths.ptr;

      for (size_t k = 0; k < t->event_indices.len; k++) {
        // Only consider depth-0 events for concurrency and dominant events
        if (t_depths[k] != 0) {
          continue;
        }

        size_t event_idx = t_event_indices[k];
        if (event_idx < td->events.len) {
          const trace_event_persisted_t* e = &events[event_idx];
          double e_start = (double)e->ts;
          double e_end = e_start + (double)e->dur;

          // Calculate overlap
          double overlap_start = e_start > b_start ? e_start : b_start;
          double overlap_end = e_end < b_end ? e_end : b_end;
          double overlap = overlap_end - overlap_start;

          if (overlap > 0) {
            total_overlap_fraction += overlap / bucket_dur;

            // Accumulate duration for dominant events
            double* accumulated = hash_table_get(&duration_map, &e->name_ref);
            if (accumulated) {
              *accumulated += overlap;
            } else {
              hash_table_put(&duration_map, &e->name_ref, overlap, a);
            }
          }
        }
      }
    }

    out_buckets[b].average_concurrency = total_overlap_fraction;

    // Collect and sort dominant events
    darray_clear(&duration_list);
    if (duration_map.capacity > 0) {
      for (size_t entry_idx = 0; entry_idx < duration_map.capacity; entry_idx++) {
        if (duration_map.entries[entry_idx].occupied) {
          event_duration_t ev = {
              .name_ref = duration_map.entries[entry_idx].key,
              .duration = duration_map.entries[entry_idx].value,
          };
          darray_push(&duration_list, ev, a);
        }
      }
    }

    if (duration_list.len > 0) {
      qsort(duration_list.ptr, duration_list.len, sizeof(event_duration_t),
            compare_event_durations);

      size_t to_copy = duration_list.len < TRACE_CONCURRENCY_MAX_DOMINANT_EVENTS
                           ? duration_list.len
                           : TRACE_CONCURRENCY_MAX_DOMINANT_EVENTS;
      for (size_t copy_idx = 0; copy_idx < to_copy; copy_idx++) {
        out_buckets[b].dominant_events[copy_idx] =
            ((event_duration_t*)duration_list.ptr)[copy_idx].name_ref;
      }
      out_buckets[b].dominant_events_count = to_copy;
    }
  }

  hash_table_deinit(&duration_map, a);
  darray_deinit(&duration_list, a);
}
