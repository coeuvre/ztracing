#include "src/track.h"

#include <string.h>

#include <algorithm>

void track_deinit(Track* t, Allocator a) {
  array_list_deinit(&t->event_indices, a);
  array_list_deinit(&t->depths, a);
}

void track_sort_events(Track* t, const TraceData* td);

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

#include "src/hash_table.h"

struct TrackKey {
  int32_t pid;
  int32_t tid;
};

struct TrackKeyHash {
  uint32_t operator()(const TrackKey& k) const {
    // A slightly better hash function to reduce collisions
    uint32_t h = (uint32_t)k.pid;
    h ^= (uint32_t)k.tid + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

struct TrackKeyEq {
  bool operator()(const TrackKey& a, const TrackKey& b) const {
    return a.pid == b.pid && a.tid == b.tid;
  }
};

struct SortKey {
  int64_t ts;
  int64_t dur;
  size_t idx;
};

void track_sort_events(Track* t, const TraceData* td) {
  if (t->event_indices.size <= 1) return;

  // For large tracks, use a temporary SortKey array to avoid cache misses
  // during indirect lookups into td->events.
  if (t->event_indices.size > 1024) {
    Allocator a = allocator_get_default();
    SortKey* keys =
        (SortKey*)allocator_alloc(a, t->event_indices.size * sizeof(SortKey));

    for (size_t i = 0; i < t->event_indices.size; i++) {
      size_t idx = t->event_indices[i];
      keys[i].ts = td->events[idx].ts;
      keys[i].dur = td->events[idx].dur;
      keys[i].idx = idx;
    }

    std::sort(keys, keys + t->event_indices.size,
              [](const SortKey& a, const SortKey& b) {
                if (a.ts != b.ts) return a.ts < b.ts;
                return a.dur > b.dur;
              });

    for (size_t i = 0; i < t->event_indices.size; i++) {
      t->event_indices[i] = keys[i].idx;
    }

    allocator_free(a, keys, t->event_indices.size * sizeof(SortKey));
  } else {
    std::sort(t->event_indices.data,
              t->event_indices.data + t->event_indices.size,
              [&](size_t a_idx, size_t b_idx) {
                const TraceEventPersisted& ea = td->events[a_idx];
                const TraceEventPersisted& eb = td->events[b_idx];
                if (ea.ts != eb.ts) return ea.ts < eb.ts;
                return ea.dur > eb.dur;
              });
  }
}

static int32_t parse_int32(Str s) {
  int32_t val = 0;
  bool neg = false;
  size_t i = 0;
  if (s.len > 0 && s.buf[0] == '-') {
    neg = true;
    i++;
  }
  for (; i < s.len; i++) {
    if (s.buf[i] >= '0' && s.buf[i] <= '9') {
      val = val * 10 + (s.buf[i] - '0');
    }
  }
  return neg ? -val : val;
}

void track_organize(const TraceData* td, Allocator a, ArrayList<Track>* out_tracks,
                    int64_t* out_min_ts, int64_t* out_max_ts) {
  for (size_t i = 0; i < out_tracks->size; i++) {
    track_deinit(&(*out_tracks)[i], a);
  }
  array_list_clear(out_tracks);

  if (td->events.size == 0) return;

  int64_t min_ts = 0;
  int64_t max_ts = 0;
  bool first_event = true;

  HashTable<TrackKey, size_t, TrackKeyHash, TrackKeyEq> track_map;
  hash_table_init(&track_map, a);

  ArrayList<size_t> event_counts = {};

  // Track cache to avoid hash lookups for consecutive events in the same thread
  int32_t last_pid = -1;
  int32_t last_tid = -1;
  size_t last_track_idx = (size_t)-1;

  // Pass 1: Discovery, Counting, and Metadata
  for (size_t i = 0; i < td->events.size; i++) {
    const TraceEventPersisted& e = td->events[i];

    size_t track_idx;
    if (e.pid == last_pid && e.tid == last_tid) {
      track_idx = last_track_idx;
    } else {
      TrackKey key = {e.pid, e.tid};
      size_t* track_idx_ptr = hash_table_get(&track_map, key);
      if (track_idx_ptr == nullptr) {
        Track t = {};
        t.pid = e.pid;
        t.tid = e.tid;
        array_list_push_back(out_tracks, a, t);
        track_idx = out_tracks->size - 1;
        hash_table_put(&track_map, a, key, track_idx);
        array_list_push_back(&event_counts, a, (size_t)0);
      } else {
        track_idx = *track_idx_ptr;
      }
      last_pid = e.pid;
      last_tid = e.tid;
      last_track_idx = track_idx;
    }

    Track& t = (*out_tracks)[track_idx];

    // Check for metadata events
    Str ph = trace_data_get_string(td, e.ph_ref);
    if (ph.len == 1 && ph.buf[0] == 'M') {
      Str name_str = trace_data_get_string(td, e.name_ref);
      if (str_eq(name_str, STR("thread_name"))) {
        for (size_t k = 0; k < e.args_count; k++) {
          const TraceArgPersisted& arg = td->args[e.args_offset + k];
          Str key_str = trace_data_get_string(td, arg.key_ref);
          if (str_eq(key_str, STR("name"))) {
            t.name_ref = arg.val_ref;
            break;
          }
        }
      } else if (str_eq(name_str, STR("thread_sort_index"))) {
        for (size_t k = 0; k < e.args_count; k++) {
          const TraceArgPersisted& arg = td->args[e.args_offset + k];
          Str key_str = trace_data_get_string(td, arg.key_ref);
          if (str_eq(key_str, STR("sort_index"))) {
            Str val = trace_data_get_string(td, arg.val_ref);
            t.sort_index = parse_int32(val);
            break;
          }
        }
      }
    } else {
      if (first_event) {
        min_ts = e.ts;
        max_ts = e.ts + e.dur;
        first_event = false;
      } else {
        if (e.ts < min_ts) min_ts = e.ts;
        if (e.ts + e.dur > max_ts) max_ts = e.ts + e.dur;
      }
      event_counts[track_idx]++;
      if (e.dur > t.max_dur) t.max_dur = e.dur;
    }
  }

  // Pre-allocate event_indices
  for (size_t i = 0; i < out_tracks->size; i++) {
    array_list_reserve(&(*out_tracks)[i].event_indices, a, event_counts[i]);
  }

  // Reset cache for Pass 2
  last_pid = -1;
  last_tid = -1;
  last_track_idx = (size_t)-1;

  // Pass 2: Grouping
  for (size_t i = 0; i < td->events.size; i++) {
    const TraceEventPersisted& e = td->events[i];
    Str ph = trace_data_get_string(td, e.ph_ref);
    if (ph.len == 1 && ph.buf[0] == 'M') continue;

    size_t track_idx;
    if (e.pid == last_pid && e.tid == last_tid) {
      track_idx = last_track_idx;
    } else {
      TrackKey key = {e.pid, e.tid};
      track_idx = *hash_table_get(&track_map, key);
      last_pid = e.pid;
      last_tid = e.tid;
      last_track_idx = track_idx;
    }
    array_list_push_back(&(*out_tracks)[track_idx].event_indices, a, i);
  }

  // Sort events, calculate depths
  for (size_t i = 0; i < out_tracks->size; i++) {
    Track& t = (*out_tracks)[i];
    track_sort_events(&t, td);
    track_calculate_depths(&t, td, a);
  }

  // Final track sort
  std::sort(out_tracks->data, out_tracks->data + out_tracks->size,
            [](const Track& a, const Track& b) {
              if (a.sort_index != b.sort_index)
                return a.sort_index < b.sort_index;
              if (a.pid != b.pid) return a.pid < b.pid;
              return a.tid < b.tid;
            });

  *out_min_ts = min_ts;
  *out_max_ts = max_ts;

  hash_table_deinit(&track_map, a);
  array_list_deinit(&event_counts, a);
}
