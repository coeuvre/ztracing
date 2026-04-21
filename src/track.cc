#include "src/track.h"

#include <string.h>

#include <algorithm>
#include <charconv>
#include <string_view>

#include "src/colors.h"

static int str_compare_ignore_case(std::string_view a, std::string_view b) {
  size_t min_len = a.size() < b.size() ? a.size() : b.size();
  for (size_t i = 0; i < min_len; i++) {
    char ca = a[i];
    char cb = b[i];
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + ('a' - 'A'));
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + ('a' - 'A'));
    if (ca < cb) return -1;
    if (ca > cb) return 1;
  }
  if (a.size() < b.size()) return -1;
  if (a.size() > b.size()) return 1;
  return 0;
}

static int32_t to_int32(std::string_view s) {
  int32_t val = 0;
  std::from_chars(s.data(), s.data() + s.size(), val);
  return val;
}

void track_deinit(Track* t, Allocator a) {
  array_list_deinit(&t->event_indices, a);
  array_list_deinit(&t->depths, a);
  array_list_deinit(&t->counter_series, a);
  array_list_deinit(&t->counter_colors, a);
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
    // Events that are not strictly nested in each other are allowed to share
    // the same lane (depth), even if they overlap in time. This creates a
    // denser view similar to modern profilers where the primary hierarchy is
    // containment, and temporal overlaps within a lane are handled by drawing
    // order (Z-order).
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
  StringRef name_ref;
  StringRef id_ref;
};

struct TrackKeyHash {
  uint32_t operator()(const TrackKey& k) const {
    uint32_t h = (uint32_t)k.pid;
    h ^= (uint32_t)k.tid + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= (uint32_t)k.name_ref + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= (uint32_t)k.id_ref + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

struct TrackKeyEq {
  bool operator()(const TrackKey& a, const TrackKey& b) const {
    return a.pid == b.pid && a.tid == b.tid && a.name_ref == b.name_ref &&
           a.id_ref == b.id_ref;
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
              [](const SortKey& sk1, const SortKey& sk2) {
                if (sk1.ts != sk2.ts) return sk1.ts < sk2.ts;
                return sk1.dur > sk2.dur;
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

void track_update_colors(ArrayList<Track>* tracks, const TraceData* td,
                          const Theme* theme) {
  for (size_t i = 0; i < tracks->size; i++) {
    Track& t = (*tracks)[i];
    if (t.type == TRACK_TYPE_COUNTER) {
      for (size_t s_idx = 0; s_idx < t.counter_series.size; s_idx++) {
        std::string_view key_str = trace_data_get_string(td, t.counter_series[s_idx]);
        uint32_t hash = 2166136261u;
        for (size_t char_idx = 0; char_idx < key_str.size(); ++char_idx) {
          hash ^= (uint8_t)key_str[char_idx];
          hash *= 16777619u;
        }
        t.counter_colors[s_idx] =
            theme->event_palette[hash % (sizeof(theme->event_palette) /
                                         sizeof(theme->event_palette[0]))];
      }
    }
  }
}

void track_organize(const TraceData* td, Allocator a, const Theme* theme,
                    ArrayList<Track>* out_tracks, int64_t* out_min_ts,
                    int64_t* out_max_ts) {
  for (size_t i = 0; i < out_tracks->size; i++) {
    track_deinit(&(*out_tracks)[i], a);
  }
  array_list_clear(out_tracks);

  if (td->events.size == 0) return;

  int64_t min_ts = 0;
  int64_t max_ts = 0;
  bool first_event = true;

  HashTable<TrackKey, size_t, TrackKeyHash, TrackKeyEq> track_map = {};

  ArrayList<size_t> event_counts = {};

  // Track cache to avoid hash lookups for consecutive events in the same thread/counter
  TrackKey last_key = {-1, -1, 0, 0};
  size_t last_track_idx = (size_t)-1;

  // Pass 1: Discovery, Counting, and Metadata
  for (size_t i = 0; i < td->events.size; i++) {
    const TraceEventPersisted& e = td->events[i];
    std::string_view ph = trace_data_get_string(td, e.ph_ref);
    bool is_counter = (ph.size() == 1 && ph[0] == 'C');

    TrackKey key;
    if (is_counter) {
      key = {e.pid, -1, e.name_ref, e.id_ref};
    } else {
      key = {e.pid, e.tid, 0, 0};
    }

    size_t track_idx;
    if (key.pid == last_key.pid && key.tid == last_key.tid &&
        key.name_ref == last_key.name_ref && key.id_ref == last_key.id_ref) {
      track_idx = last_track_idx;
    } else {
      size_t* track_idx_ptr = hash_table_get(&track_map, key);
      if (track_idx_ptr == nullptr) {
        Track t = {
            .type = is_counter ? TRACK_TYPE_COUNTER : TRACK_TYPE_THREAD,
            .pid = e.pid,
            .tid = is_counter ? -1 : e.tid,
            .name_ref = is_counter ? e.name_ref : (StringRef)0,
            .id_ref = is_counter ? e.id_ref : (StringRef)0,
        };
        array_list_push_back(out_tracks, a, t);
        track_idx = out_tracks->size - 1;
        hash_table_put(&track_map, a, key, track_idx);
        array_list_push_back(&event_counts, a, (size_t)0);
      } else {
        track_idx = *track_idx_ptr;
      }
      last_key = key;
      last_track_idx = track_idx;
    }

    Track& t = (*out_tracks)[track_idx];

    // Check for metadata events
    if (ph.size() == 1 && ph[0] == 'M') {
      std::string_view name_str = trace_data_get_string(td, e.name_ref);
      if (name_str == "thread_name") {
        for (size_t k = 0; k < e.args_count; k++) {
          const TraceArgPersisted& arg = td->args[e.args_offset + k];
          std::string_view key_str = trace_data_get_string(td, arg.key_ref);
          if (key_str == "name") {
            t.name_ref = arg.val_ref;
            break;
          }
        }
      } else if (name_str == "thread_sort_index") {
        for (size_t k = 0; k < e.args_count; k++) {
          const TraceArgPersisted& arg = td->args[e.args_offset + k];
          std::string_view key_str = trace_data_get_string(td, arg.key_ref);
          if (key_str == "sort_index") {
            std::string_view val = trace_data_get_string(td, arg.val_ref);
            t.sort_index = to_int32(val);
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
  last_key = {-1, -1, 0, 0};
  last_track_idx = (size_t)-1;

  // Pass 2: Grouping
  for (size_t i = 0; i < td->events.size; i++) {
    const TraceEventPersisted& e = td->events[i];
    std::string_view ph = trace_data_get_string(td, e.ph_ref);
    if (ph.size() == 1 && ph[0] == 'M') continue;

    bool is_counter = (ph.size() == 1 && ph[0] == 'C');
    TrackKey key;
    if (is_counter) {
      key = {e.pid, -1, e.name_ref, e.id_ref};
    } else {
      key = {e.pid, e.tid, 0, 0};
    }

    size_t track_idx;
    if (key.pid == last_key.pid && key.tid == last_key.tid &&
        key.name_ref == last_key.name_ref && key.id_ref == last_key.id_ref) {
      track_idx = last_track_idx;
    } else {
      track_idx = *hash_table_get(&track_map, key);
      last_key = key;
      last_track_idx = track_idx;
    }
    array_list_push_back(&(*out_tracks)[track_idx].event_indices, a, i);
  }

  // Sort events, calculate depths
  for (size_t i = 0; i < out_tracks->size; i++) {
    Track& t = (*out_tracks)[i];
    track_sort_events(&t, td);
    if (t.type == TRACK_TYPE_THREAD) {
      track_calculate_depths(&t, td, a);
    } else {
      // Counter tracks don't have nested depths.
      t.max_depth = 0;
      array_list_resize(&t.depths, a, t.event_indices.size);
      for (size_t k = 0; k < t.depths.size; k++) t.depths[k] = 0;

      // Discover unique series (argument keys) and calculate max total
      t.counter_max_total = 0.0;
      for (size_t idx_k = 0; idx_k < t.event_indices.size; idx_k++) {
        size_t idx = t.event_indices[idx_k];
        const TraceEventPersisted& e = td->events[idx];
        double event_total = 0.0;
        for (uint32_t k = 0; k < e.args_count; k++) {
          const TraceArgPersisted& arg = td->args[e.args_offset + k];
          StringRef key_ref = arg.key_ref;
          bool found = false;
          for (size_t s_idx = 0; s_idx < t.counter_series.size; s_idx++) {
            if (t.counter_series[s_idx] == key_ref) {
              found = true;
              break;
            }
          }
          if (!found) {
            array_list_push_back(&t.counter_series, a, key_ref);
          }
          event_total += arg.val_double;
        }
        if (event_total > t.counter_max_total) t.counter_max_total = event_total;
      }

      // Sort series by key for consistent stacking order
      std::sort(t.counter_series.data,
                t.counter_series.data + t.counter_series.size,
                [&](StringRef a_ref, StringRef b_ref) {
                  std::string_view sa = trace_data_get_string(td, a_ref);
                  std::string_view sb = trace_data_get_string(td, b_ref);
                  return sa < sb;
                });

      // Cache colors
      array_list_resize(&t.counter_colors, a, t.counter_series.size);
    }
  }

  // Update colors based on the current theme
  track_update_colors(out_tracks, td, theme);

  // Final track sort
  std::sort(out_tracks->data, out_tracks->data + out_tracks->size,
            [&](const Track& tr1, const Track& tr2) {
              if (tr1.sort_index != tr2.sort_index)
                return tr1.sort_index < tr2.sort_index;
              if (tr1.pid != tr2.pid) return tr1.pid < tr2.pid;
              if (tr1.type != tr2.type) return (int)tr1.type < (int)tr2.type;

              if (tr1.type == TRACK_TYPE_COUNTER) {
                std::string_view na = trace_data_get_string(td, tr1.name_ref);
                std::string_view nb = trace_data_get_string(td, tr2.name_ref);
                int res = str_compare_ignore_case(na, nb);
                if (res != 0) return res < 0;

                std::string_view ia = trace_data_get_string(td, tr1.id_ref);
                std::string_view ib = trace_data_get_string(td, tr2.id_ref);
                return ia < ib;
              } else {
                if (tr1.tid != tr2.tid) return tr1.tid < tr2.tid;
                if (tr1.name_ref != tr2.name_ref) return tr1.name_ref < tr2.name_ref;
                return tr1.id_ref < tr2.id_ref;
              }
            });

  *out_min_ts = min_ts;
  *out_max_ts = max_ts;

  hash_table_deinit(&track_map, a);
  array_list_deinit(&event_counts, a);
}
