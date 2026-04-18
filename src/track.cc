#include "src/track.h"

#include <string.h>

#include <algorithm>

void track_deinit(Track* t, Allocator a) {
  array_list_deinit(&t->event_indices, a);
}

void track_sort_events(Track* t, const TraceData* td) {
  std::sort(t->event_indices.data,
            t->event_indices.data + t->event_indices.size,
            [&](size_t a_idx, size_t b_idx) {
              return td->events[a_idx].ts < td->events[b_idx].ts;
            });
}

void track_update_max_dur(Track* t, const TraceData* td) {
  int64_t max_dur = 0;
  for (size_t i = 0; i < t->event_indices.size; i++) {
    size_t event_idx = t->event_indices[i];
    int64_t dur = td->events[event_idx].dur;
    if (dur > max_dur) max_dur = dur;
  }
  t->max_dur = max_dur;
}

size_t track_find_visible_start_index(const Track* t, const TraceData* td,
                                      int64_t viewport_start_ts) {
  if (t->event_indices.size == 0) return 0;

  // We want to find the first event that COULD be visible.
  // An event is visible if e.ts + e.dur > viewport_start_ts (and e.ts <
  // viewport_end_ts). Since we know e.dur <= track.max_dur, if e.ts +
  // track.max_dur <= viewport_start_ts, then e.ts + e.dur <= viewport_start_ts,
  // so it's definitely not visible. Thus we only need to look at events where
  // e.ts > viewport_start_ts - track.max_dur.

  int64_t search_ts = viewport_start_ts - t->max_dur;

  auto it = std::lower_bound(
      t->event_indices.data, t->event_indices.data + t->event_indices.size,
      search_ts,
      [&](size_t idx, int64_t val) { return td->events[idx].ts < val; });

  return (size_t)(it - t->event_indices.data);
}
