#include "src/track.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "src/colors.h"
#include "src/hash_table.h"

typedef struct stack_event {
  int64_t end;
  uint32_t depth;
} stack_event_t;

typedef struct track_key {
  int32_t pid;
  int32_t tid;
  string_ref_t name_ref;
  string_ref_t id_ref;
} track_key_t;

typedef struct sort_key {
  int64_t ts;
  int64_t dur;
  size_t idx;
} sort_key_t;

typedef struct track_sort_key {
  track_t* track;
  string_t name;
  string_t id;
} track_sort_key_t;

typedef struct counter_sort_key {
  string_ref_t ref;
  string_t str;
} counter_sort_key_t;

static uint32_t compute_hash(string_t s) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < s.len; ++i) {
    hash ^= (uint8_t)s.ptr[i];
    hash *= 16777619u;
  }
  return hash;
}

static string_ref_t trace_data_find_string_ref_const(const trace_data_t* td,
                                                     string_t s) {
  string_ref_t result = 0;
  if (td->string_lookup.hash_fn != nullptr && s.ptr != nullptr) {
    trace_data_t* mutable_td = (trace_data_t*)td;
    mutable_td->tmp.current_str = s;
    uint32_t h = compute_hash(s);
    mutable_td->tmp.current_hash = h;

    uint32_t sentinel = 0;
    const uint32_t* existing_index = (const uint32_t*)hash_table_get_with_hash(
        &td->string_lookup, &sentinel, h);
    if (existing_index != nullptr) {
      result = *existing_index;
    }
  }
  return result;
}

static int str_compare_ignore_case(string_t a, string_t b) {
  int result = 0;
  size_t min_len = a.len < b.len ? a.len : b.len;
  for (size_t i = 0; i < min_len; i++) {
    char ca = a.ptr[i];
    char cb = b.ptr[i];
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + ('a' - 'A'));
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + ('a' - 'A'));
    if (ca < cb) {
      result = -1;
      break;
    }
    if (ca > cb) {
      result = 1;
      break;
    }
  }
  if (result == 0) {
    if (a.len < b.len) {
      result = -1;
    } else if (a.len > b.len) {
      result = 1;
    }
  }
  return result;
}

static int32_t to_int32(string_t s) {
  int32_t val = 0;
  int sign = 1;
  size_t i = 0;
  if (s.len > 0 && s.ptr[0] == '-') {
    sign = -1;
    i++;
  } else if (s.len > 0 && s.ptr[0] == '+') {
    i++;
  }
  for (; i < s.len; i++) {
    if (s.ptr[i] >= '0' && s.ptr[i] <= '9') {
      val = val * 10 + (s.ptr[i] - '0');
    } else {
      break;
    }
  }
  return val * sign;
}

static int string_compare(string_t a, string_t b) {
  int result = 0;
  size_t min_len = a.len < b.len ? a.len : b.len;
  int res = memcmp(a.ptr, b.ptr, min_len);
  if (res != 0) {
    result = res;
  } else {
    if (a.len < b.len) {
      result = -1;
    } else if (a.len > b.len) {
      result = 1;
    }
  }
  return result;
}

static int sort_key_compare(const void* a, const void* b) {
  const sort_key_t* sk1 = (const sort_key_t*)a;
  const sort_key_t* sk2 = (const sort_key_t*)b;
  int result = 0;
  if (sk1->ts != sk2->ts) {
    result = sk1->ts < sk2->ts ? -1 : 1;
  } else {
    if (sk1->dur != sk2->dur) {
      result = sk1->dur > sk2->dur ? -1 : 1;
    }
  }
  return result;
}

static int counter_sort_key_compare(const void* a, const void* b) {
  const counter_sort_key_t* sk1 = (const counter_sort_key_t*)a;
  const counter_sort_key_t* sk2 = (const counter_sort_key_t*)b;
  return string_compare(sk1->str, sk2->str);
}

static int track_sort_key_compare(const void* a, const void* b) {
  int result = 0;
  const track_sort_key_t* sk1 = (const track_sort_key_t*)a;
  const track_sort_key_t* sk2 = (const track_sort_key_t*)b;
  const track_t* tr1 = sk1->track;
  const track_t* tr2 = sk2->track;

  if (tr1->sort_index != tr2->sort_index) {
    result = tr1->sort_index < tr2->sort_index ? -1 : 1;
  } else if (tr1->pid != tr2->pid) {
    result = tr1->pid < tr2->pid ? -1 : 1;
  } else if (tr1->type != tr2->type) {
    result = (int)tr1->type < (int)tr2->type ? -1 : 1;
  } else {
    if (tr1->type == TRACK_TYPE_COUNTER) {
      int res = str_compare_ignore_case(sk1->name, sk2->name);
      if (res != 0) {
        result = res;
      } else {
        result = string_compare(sk1->id, sk2->id);
      }
    } else {
      if (tr1->tid != tr2->tid) {
        result = tr1->tid < tr2->tid ? -1 : 1;
      } else if (tr1->name_ref != tr2->name_ref) {
        result = tr1->name_ref < tr2->name_ref ? -1 : 1;
      } else {
        result = tr1->id_ref < tr2->id_ref ? -1 : 1;
      }
    }
  }
  return result;
}

static uint32_t track_key_hash(const void* key, void* ctx) {
  const track_key_t* k = (const track_key_t*)key;
  uint32_t h = (uint32_t)k->pid;
  (void)ctx;
  h ^= (uint32_t)k->tid + 0x9e3779b9 + (h << 6) + (h >> 2);
  h ^= (uint32_t)k->name_ref + 0x9e3779b9 + (h << 6) + (h >> 2);
  h ^= (uint32_t)k->id_ref + 0x9e3779b9 + (h << 6) + (h >> 2);
  return h;
}

static bool track_key_eq(const void* a, const void* b, void* ctx) {
  const track_key_t* ka = (const track_key_t*)a;
  const track_key_t* kb = (const track_key_t*)b;
  (void)ctx;
  return ka->pid == kb->pid && ka->tid == kb->tid &&
         ka->name_ref == kb->name_ref && ka->id_ref == kb->id_ref;
}

void track_deinit(track_t* t, allocator_t a) {
  array_list_deinit(&t->event_indices, a);
  array_list_deinit(&t->depths, a);
  array_list_deinit(&t->counter_series, a);
  array_list_deinit(&t->counter_colors, a);
  array_list_deinit(&t->block_max_durs, a);
}

void track_sort_events(track_t* t, const trace_data_t* td, allocator_t a) {
  if (t->event_indices.len > 1) {
    sort_key_t* keys = nullptr;
    sort_key_t stack_keys[1024];
    if (t->event_indices.len <= 1024) {
      keys = stack_keys;
    } else {
      keys = (sort_key_t*)allocator_alloc(
          a, t->event_indices.len * sizeof(sort_key_t));
    }

    const size_t* event_indices = (const size_t*)t->event_indices.ptr;
    const trace_event_persisted_t* events =
        (const trace_event_persisted_t*)td->events.ptr;

    for (size_t i = 0; i < t->event_indices.len; i++) {
      size_t idx = event_indices[i];
      keys[i].ts = events[idx].ts;
      keys[i].dur = events[idx].dur;
      keys[i].idx = idx;
    }

    qsort(keys, t->event_indices.len, sizeof(sort_key_t), sort_key_compare);

    size_t* mutable_event_indices = (size_t*)t->event_indices.ptr;
    for (size_t i = 0; i < t->event_indices.len; i++) {
      mutable_event_indices[i] = keys[i].idx;
    }

    if (t->event_indices.len > 1024) {
      allocator_free(a, keys, t->event_indices.len * sizeof(sort_key_t));
    }
  }
}

void track_update_max_dur(track_t* t, const trace_data_t* td, allocator_t a) {
  int64_t max_dur = 0;
  size_t num_blocks =
      (t->event_indices.len + TRACK_BLOCK_SIZE - 1) / TRACK_BLOCK_SIZE;
  array_list_resize(&t->block_max_durs, num_blocks, sizeof(int64_t), a);

  const size_t* event_indices = (const size_t*)t->event_indices.ptr;
  const trace_event_persisted_t* events =
      (const trace_event_persisted_t*)td->events.ptr;
  int64_t* block_max_durs = (int64_t*)t->block_max_durs.ptr;

  for (size_t b = 0; b < num_blocks; b++) {
    int64_t block_max_dur = 0;
    size_t start = b * TRACK_BLOCK_SIZE;
    size_t end = start + TRACK_BLOCK_SIZE;
    if (end > t->event_indices.len) {
      end = t->event_indices.len;
    }
    for (size_t i = start; i < end; i++) {
      size_t event_idx = event_indices[i];
      int64_t dur = events[event_idx].dur;
      if (dur > block_max_dur) {
        block_max_dur = dur;
      }
    }
    block_max_durs[b] = block_max_dur;
    if (block_max_dur > max_dur) {
      max_dur = block_max_dur;
    }
  }
  t->max_dur = max_dur;
}

void track_calculate_depths(track_t* t, const trace_data_t* td, allocator_t a) {
  array_list_resize(&t->depths, t->event_indices.len, sizeof(uint32_t), a);
  t->max_depth = 0;

  array_list_t stack = {};
  const size_t* event_indices = (const size_t*)t->event_indices.ptr;
  const trace_event_persisted_t* events =
      (const trace_event_persisted_t*)td->events.ptr;
  uint32_t* depths = (uint32_t*)t->depths.ptr;

  for (size_t i = 0; i < t->event_indices.len; i++) {
    size_t event_idx = event_indices[i];
    const trace_event_persisted_t* e = &events[event_idx];
    int64_t end_ts = e->ts + e->dur;

    // Pop events that have finished.
    const stack_event_t* stack_elements = (const stack_event_t*)stack.ptr;
    while (stack.len > 0 && stack_elements[stack.len - 1].end <= e->ts) {
      stack.len--;
    }

    // Find the deepest parent that strictly contains this event.
    uint32_t depth = 0;
    stack_elements = (const stack_event_t*)stack.ptr;
    for (int j = (int)stack.len - 1; j >= 0; j--) {
      if (stack_elements[(size_t)j].end >= end_ts) {
        depth = stack_elements[(size_t)j].depth + 1;
        break;
      }
    }

    depths[i] = depth;
    if (depth > t->max_depth) {
      t->max_depth = depth;
    }

    stack_event_t se = {.end = end_ts, .depth = depth};
    *array_list_push(&stack, stack_event_t, a) = se;
  }

  array_list_deinit(&stack, a);
}

size_t track_find_visible_start_index(const track_t* t, const trace_data_t* td,
                                      int64_t viewport_start_ts) {
  size_t result = 0;
  if (t->event_indices.len > 0) {
    size_t num_blocks = t->block_max_durs.len;
    size_t first_block = 0;
    const size_t* event_indices = (const size_t*)t->event_indices.ptr;
    const trace_event_persisted_t* events =
        (const trace_event_persisted_t*)td->events.ptr;
    const int64_t* block_max_durs = (const int64_t*)t->block_max_durs.ptr;

    for (size_t b = 0; b < num_blocks; b++) {
      size_t start_idx = b * TRACK_BLOCK_SIZE;
      size_t end_idx = start_idx + TRACK_BLOCK_SIZE;
      if (end_idx > t->event_indices.len) {
        end_idx = t->event_indices.len;
      }
      int64_t block_last_ts = events[event_indices[end_idx - 1]].ts;

      if (block_last_ts + block_max_durs[b] < viewport_start_ts) {
        first_block = b + 1;
      } else {
        break;
      }
    }

    size_t start_search_idx = first_block * TRACK_BLOCK_SIZE;
    if (start_search_idx > t->event_indices.len) {
      start_search_idx = t->event_indices.len;
    }

    size_t low = start_search_idx;
    size_t high = t->event_indices.len;

    while (low < high) {
      size_t mid = low + (high - low) / 2;
      size_t idx = event_indices[mid];
      if (events[idx].ts < viewport_start_ts - t->max_dur) {
        low = mid + 1;
      } else {
        high = mid;
      }
    }
    result = low;
  }
  return result;
}

// TODO: Remove track_update_colors after the migration is done. We might just
// store the color_index instead of the real color, allowing theme colors to be
// resolved dynamically at draw time.
void track_update_colors(array_list_t* tracks, const trace_data_t* td,
                         const theme_t* theme) {
  track_t* tracks_data = (track_t*)tracks->ptr;
  for (size_t i = 0; i < tracks->len; i++) {
    track_t* t = &tracks_data[i];
    if (t->type == TRACK_TYPE_COUNTER) {
      string_ref_t* counter_series = (string_ref_t*)t->counter_series.ptr;
      uint32_t* counter_colors = (uint32_t*)t->counter_colors.ptr;
      for (size_t s_idx = 0; s_idx < t->counter_series.len; s_idx++) {
        string_t key_str = trace_data_get_string_c(td, counter_series[s_idx]);
        uint32_t hash = 2166136261u;
        for (size_t char_idx = 0; char_idx < key_str.len; ++char_idx) {
          hash ^= (uint8_t)key_str.ptr[char_idx];
          hash *= 16777619u;
        }
        counter_colors[s_idx] =
            theme->event_palette[hash % (sizeof(theme->event_palette) /
                                         sizeof(theme->event_palette[0]))];
      }
    }
  }
}

void track_organize(const trace_data_t* td, const theme_t* theme,
                    array_list_t* out_tracks, int64_t* out_min_ts,
                    int64_t* out_max_ts, allocator_t a) {
  track_t* out_tracks_data = (track_t*)out_tracks->ptr;
  for (size_t i = 0; i < out_tracks->len; i++) {
    track_deinit(&out_tracks_data[i], a);
  }
  array_list_clear(out_tracks);

  if (td->events.len > 0) {
    int64_t min_ts = 0;
    int64_t max_ts = 0;
    bool first_event = true;

    hash_table_t track_map = hash_table_init(
        track_key_t, size_t, track_key_hash, track_key_eq, nullptr);

    track_key_t last_key = {-1, -1, 0, 0};
    size_t last_track_idx = (size_t)-1;

    const trace_event_persisted_t* events =
        (const trace_event_persisted_t*)td->events.ptr;

    string_ref_t ph_c_ref =
        trace_data_find_string_ref_const(td, string_lit("C"));
    string_ref_t ph_m_ref =
        trace_data_find_string_ref_const(td, string_lit("M"));

    // Single Pass: Discovery, Grouping, and Metadata!
    for (size_t i = 0; i < td->events.len; i++) {
      const trace_event_persisted_t* e = &events[i];
      bool is_counter = (e->ph_ref == ph_c_ref);
      bool is_metadata = (e->ph_ref == ph_m_ref);

      track_key_t key = {};
      if (is_counter) {
        key.pid = e->pid;
        key.tid = -1;
        key.name_ref = e->name_ref;
        key.id_ref = e->id_ref;
      } else {
        key.pid = e->pid;
        key.tid = e->tid;
      }

      size_t track_idx = 0;
      if (key.pid == last_key.pid && key.tid == last_key.tid &&
          key.name_ref == last_key.name_ref && key.id_ref == last_key.id_ref) {
        track_idx = last_track_idx;
      } else {
        size_t* track_idx_ptr = (size_t*)hash_table_get(&track_map, &key);
        if (track_idx_ptr == nullptr) {
          track_t t = {
              .type = is_counter ? TRACK_TYPE_COUNTER : TRACK_TYPE_THREAD,
              .pid = e->pid,
              .tid = is_counter ? -1 : e->tid,
              .name_ref = is_counter ? e->name_ref : 0,
              .id_ref = is_counter ? e->id_ref : 0,
          };
          *array_list_push(out_tracks, track_t, a) = t;
          track_idx = out_tracks->len - 1;
          *(size_t*)hash_table_put(&track_map, &key, a) = track_idx;
        } else {
          track_idx = *track_idx_ptr;
        }
        last_key = key;
        last_track_idx = track_idx;
      }

      track_t* tracks_data = (track_t*)out_tracks->ptr;
      track_t* t = &tracks_data[track_idx];

      // Check for metadata events
      if (is_metadata) {
        string_t name_str = trace_data_get_string_c(td, e->name_ref);
        if (string_eq(name_str, string_lit("thread_name"))) {
          const trace_arg_persisted_t* args =
              (const trace_arg_persisted_t*)td->args.ptr;
          for (size_t k = 0; k < e->args_count; k++) {
            const trace_arg_persisted_t* arg = &args[e->args_offset + k];
            string_t key_str = trace_data_get_string_c(td, arg->key_ref);
            if (string_eq(key_str, string_lit("name"))) {
              t->name_ref = arg->val_ref;
              break;
            }
          }
        } else if (string_eq(name_str, string_lit("thread_sort_index"))) {
          const trace_arg_persisted_t* args =
              (const trace_arg_persisted_t*)td->args.ptr;
          for (size_t k = 0; k < e->args_count; k++) {
            const trace_arg_persisted_t* arg = &args[e->args_offset + k];
            string_t key_str = trace_data_get_string_c(td, arg->key_ref);
            if (string_eq(key_str, string_lit("sort_index"))) {
              string_t val = trace_data_get_string_c(td, arg->val_ref);
              t->sort_index = to_int32(val);
              break;
            }
          }
        }
      } else {
        *array_list_push(&t->event_indices, size_t, a) = i;
        if (first_event) {
          min_ts = e->ts;
          max_ts = e->ts + e->dur;
          first_event = false;
        } else {
          if (e->ts < min_ts) {
            min_ts = e->ts;
          }
          if (e->ts + e->dur > max_ts) {
            max_ts = e->ts + e->dur;
          }
        }
      }
    }

    hash_table_deinit(&track_map, a);

    // Sort events, calculate depths
    track_t* tracks_data = (track_t*)out_tracks->ptr;
    for (size_t i = 0; i < out_tracks->len; i++) {
      track_t* t = &tracks_data[i];
      track_sort_events(t, td, a);
      track_update_max_dur(t, td, a);
      if (t->type == TRACK_TYPE_THREAD) {
        track_calculate_depths(t, td, a);
      } else {
        // Counter tracks don't have nested depths.
        t->max_depth = 0;
        array_list_resize(&t->depths, t->event_indices.len, sizeof(uint32_t),
                          a);
        uint32_t* depths = (uint32_t*)t->depths.ptr;
        for (size_t k = 0; k < t->depths.len; k++) {
          depths[k] = 0;
        }

        // Discover unique series (argument keys) and calculate max total
        t->counter_max_total = 0.0;
        const size_t* event_indices = (const size_t*)t->event_indices.ptr;
        const trace_arg_persisted_t* args =
            (const trace_arg_persisted_t*)td->args.ptr;
        for (size_t idx_k = 0; idx_k < t->event_indices.len; idx_k++) {
          size_t idx = event_indices[idx_k];
          const trace_event_persisted_t* e = &events[idx];
          double event_total = 0.0;
          for (uint32_t k = 0; k < e->args_count; k++) {
            const trace_arg_persisted_t* arg = &args[e->args_offset + k];
            string_ref_t key_ref = arg->key_ref;
            bool found = false;
            const string_ref_t* counter_series_data =
                (const string_ref_t*)t->counter_series.ptr;
            for (size_t s_idx = 0; s_idx < t->counter_series.len; s_idx++) {
              if (counter_series_data[s_idx] == key_ref) {
                found = true;
                break;
              }
            }
            if (!found) {
              *array_list_push(&t->counter_series, string_ref_t, a) = key_ref;
            }
            event_total += arg->val_double;
          }
          if (event_total > t->counter_max_total) {
            t->counter_max_total = event_total;
          }
        }

        // Pre-resolve strings for counter series sorting
        counter_sort_key_t* counter_keys = nullptr;
        counter_sort_key_t stack_counter_keys[32];
        if (t->counter_series.len <= 32) {
          counter_keys = stack_counter_keys;
        } else {
          counter_keys = (counter_sort_key_t*)allocator_alloc(
              a, t->counter_series.len * sizeof(counter_sort_key_t));
        }

        const string_ref_t* counter_series_data =
            (const string_ref_t*)t->counter_series.ptr;
        for (size_t s_idx = 0; s_idx < t->counter_series.len; s_idx++) {
          counter_keys[s_idx].ref = counter_series_data[s_idx];
          counter_keys[s_idx].str =
              trace_data_get_string_c(td, counter_series_data[s_idx]);
        }

        qsort(counter_keys, t->counter_series.len, sizeof(counter_sort_key_t),
              counter_sort_key_compare);

        string_ref_t* mutable_counter_series =
            (string_ref_t*)t->counter_series.ptr;
        for (size_t s_idx = 0; s_idx < t->counter_series.len; s_idx++) {
          mutable_counter_series[s_idx] = counter_keys[s_idx].ref;
        }

        if (t->counter_series.len > 32) {
          allocator_free(a, counter_keys,
                         t->counter_series.len * sizeof(counter_sort_key_t));
        }

        // Cache colors
        array_list_resize(&t->counter_colors, t->counter_series.len,
                          sizeof(uint32_t), a);
      }
    }

    // Update colors based on the current theme
    track_update_colors(out_tracks, td, theme);

    // Final track sort — Context-free using TrackSortKey
    track_sort_key_t* keys = nullptr;
    track_sort_key_t stack_keys[128];
    if (out_tracks->len <= 128) {
      keys = stack_keys;
    } else {
      keys = (track_sort_key_t*)allocator_alloc(
          a, out_tracks->len * sizeof(track_sort_key_t));
    }

    tracks_data = (track_t*)out_tracks->ptr;
    for (size_t i = 0; i < out_tracks->len; i++) {
      keys[i].track = &tracks_data[i];
      if (tracks_data[i].type == TRACK_TYPE_COUNTER) {
        keys[i].name = trace_data_get_string_c(td, tracks_data[i].name_ref);
        keys[i].id = trace_data_get_string_c(td, tracks_data[i].id_ref);
      } else {
        keys[i].name = (string_t){};
        keys[i].id = (string_t){};
      }
    }

    qsort(keys, out_tracks->len, sizeof(track_sort_key_t),
          track_sort_key_compare);

    track_t* sorted_tracks =
        (track_t*)allocator_alloc(a, out_tracks->len * sizeof(track_t));
    for (size_t i = 0; i < out_tracks->len; i++) {
      sorted_tracks[i] = *keys[i].track;
    }
    memcpy(out_tracks->ptr, sorted_tracks, out_tracks->len * sizeof(track_t));
    allocator_free(a, sorted_tracks, out_tracks->len * sizeof(track_t));

    if (out_tracks->len > 128) {
      allocator_free(a, keys, out_tracks->len * sizeof(track_sort_key_t));
    }

    *out_min_ts = min_ts;
    *out_max_ts = max_ts;
  }
}
