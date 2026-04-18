#include "src/track.h"

#include <string.h>

#include <algorithm>

void track_deinit(Track* t, Allocator a) {
  array_list_deinit(&t->event_indices, a);
  array_list_deinit(&t->depths, a);
}

void track_sort_events(Track* t, const TraceData* td) {
  std::sort(t->event_indices.data,
            t->event_indices.data + t->event_indices.size,
            [&](size_t a_idx, size_t b_idx) {
              if (td->events[a_idx].ts != td->events[b_idx].ts) {
                return td->events[a_idx].ts < td->events[b_idx].ts;
              }
              // For same start time, longer duration comes first (it's the parent)
              return td->events[a_idx].dur > td->events[b_idx].dur;
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

struct StackEvent {
  int64_t end;
  uint32_t depth;
};

void track_calculate_depths(Track* t, const TraceData* td, Allocator a) {
  array_list_resize(&t->depths, a, t->event_indices.size);
  t->max_depth = 0;

  ArrayList<StackEvent> stack = {};

  for (size_t i = 0; i < t->event_indices.size; i++) {
    size_t event_idx = t->event_indices[i];
    const TraceEventPersisted& e = td->events[event_idx];
    int64_t end_ts = e.ts + e.dur;

    // Pop events that have finished.
    while (stack.size > 0 && stack[stack.size - 1].end <= e.ts) {
      stack.size--;
    }

    // Find the deepest parent that strictly contains this event.
    // Since the stack might contain events that are not strictly nested in each
    // other (due to our "move up" rule), we search backwards.
    uint32_t depth = 0;
    for (int j = (int)stack.size - 1; j >= 0; j--) {
      if (stack[(size_t)j].end >= end_ts) {
        depth = stack[(size_t)j].depth + 1;
        break;
      }
    }

    t->depths[i] = depth;
    if (depth > t->max_depth) t->max_depth = depth;

    StackEvent se = {end_ts, depth};
    array_list_push_back(&stack, a, se);
  }

  array_list_deinit(&stack, a);
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
