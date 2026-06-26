#include "src/track.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "core/arena.h"
#include "src/colors.h"
#include "src/hash_table.h"

typedef struct stack_event {
  int64_t end;
  uint32_t depth;
  size_t track_event_idx;
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
  string_view_t name;
  string_view_t id;
} track_sort_key_t;

typedef struct counter_sort_key {
  string_ref_t ref;
  string_view_t str;
} counter_sort_key_t;

static uint32_t compute_hash(string_view_t s) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < s.len; ++i) {
    hash ^= (uint8_t)s.ptr[i];
    hash *= 16777619u;
  }
  return hash;
}

static string_ref_t trace_data_find_string_ref_const(const trace_data_t* td,
                                                     string_view_t s) {
  string_ref_t result = 0;
  const string_lookup_table_t* lt = &td->string_lookup;
  if (lt->capacity > 0 && s.ptr != nullptr && s.len > 0) {
    uint32_t h = compute_hash(s);
    size_t idx = h & lt->capacity_mask;
    const string_entry_t* st_table =
        (const string_entry_t*)td->string_table.ptr;
    const char* st_buffer = (const char*)td->string_buffer.ptr;

    while (lt->entries[idx].index != 0) {
      const string_lookup_entry_t* entry = &lt->entries[idx];
      if (entry->hash == h) {
        const string_entry_t* e = &st_table[entry->index - 1];
        if (s.len == e->len &&
            memcmp(s.ptr, st_buffer + e->offset, s.len) == 0) {
          result = entry->index;
          break;
        }
      }
      idx = (idx + 1) & lt->capacity_mask;
    }
  }
  return result;
}

static int str_compare_ignore_case(string_view_t a, string_view_t b) {
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

static int32_t to_int32(string_view_t s) {
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

static int string_compare(string_view_t a, string_view_t b) {
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
    } else {
      if (sk1->idx != sk2->idx) {
        result = sk1->idx < sk2->idx ? -1 : 1;
      }
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

void track_deinit(track_t* t, allocator_t* a) {
  array_list_deinit(&t->event_indices, a);
  array_list_deinit(&t->depths, a);
  array_list_deinit(&t->self_durs, a);
  array_list_deinit(&t->counter_series, a);
  array_list_deinit(&t->counter_palette_indices, a);
  array_list_deinit(&t->block_max_durs, a);
  *t = (track_t){};
}

void track_sort_events(track_t* t, const trace_data_t* td, allocator_t* a) {
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

void track_update_max_dur(track_t* t, const trace_data_t* td, allocator_t* a) {
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

void track_calculate_depths(track_t* t, const trace_data_t* td,
                            allocator_t* a) {
  array_list_resize(&t->depths, t->event_indices.len, sizeof(uint32_t), a);
  array_list_resize(&t->self_durs, t->event_indices.len, sizeof(int64_t), a);
  t->max_depth = 0;

  array_list_t stack = {};
  const size_t* event_indices = (const size_t*)t->event_indices.ptr;
  const trace_event_persisted_t* events =
      (const trace_event_persisted_t*)td->events.ptr;
  uint32_t* depths = (uint32_t*)t->depths.ptr;
  int64_t* self_durs = (int64_t*)t->self_durs.ptr;

  for (size_t i = 0; i < t->event_indices.len; i++) {
    size_t event_idx = event_indices[i];
    const trace_event_persisted_t* e = &events[event_idx];
    int64_t end_ts = e->ts + e->dur;

    self_durs[i] = e->dur;

    // Pop events that have finished.
    const stack_event_t* stack_elements = (const stack_event_t*)stack.ptr;
    while (stack.len > 0 && stack_elements[stack.len - 1].end <= e->ts) {
      stack.len--;
    }

    // Find the deepest parent that strictly contains this event.
    uint32_t depth = 0;
    stack_elements = (const stack_event_t*)stack.ptr;
    for (size_t j = stack.len; j > 0; j--) {
      if (stack_elements[j - 1].end >= end_ts) {
        depth = stack_elements[j - 1].depth + 1;

        // j - 1 is the direct parent! Subtract our duration from its
        // self-duration.
        size_t parent_track_idx = stack_elements[j - 1].track_event_idx;
        self_durs[parent_track_idx] -= e->dur;

        break;
      }
    }

    depths[i] = depth;
    if (depth > t->max_depth) {
      t->max_depth = depth;
    }

    stack_event_t se = {
        .end = end_ts,
        .depth = depth,
        .track_event_idx = i,
    };
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

void track_organize(const trace_data_t* td, array_list_t* out_tracks,
                    int64_t* out_min_ts, int64_t* out_max_ts,
                    allocator_t* output_allocator,
                    allocator_t* scratch_allocator) {
  track_t* out_tracks_data = (track_t*)out_tracks->ptr;
  for (size_t i = 0; i < out_tracks->len; i++) {
    track_deinit(&out_tracks_data[i], output_allocator);
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

    string_ref_t ph_c_ref = trace_data_find_string_ref_const(td, SV("C"));
    string_ref_t ph_m_ref = trace_data_find_string_ref_const(td, SV("M"));

    array_list_t event_counts = {};

    // Allocate temporary array to store resolved track index for each event
    uint32_t* event_track_indices = (uint32_t*)allocator_alloc(
        scratch_allocator, td->events.len * sizeof(uint32_t));

    // Pass 1: Discovery, Counting, Metadata, and Index Caching!
    for (size_t i = 0; i < td->events.len; i++) {
      const trace_event_persisted_t* e = &events[i];
      bool is_counter = (e->ph_ref == ph_c_ref);
      bool is_metadata = (e->ph_ref == ph_m_ref);

      if (is_metadata) {
        event_track_indices[i] = (uint32_t)-1;  // Sentinel for metadata
      }

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
          *array_list_push(out_tracks, track_t, output_allocator) = t;
          track_idx = out_tracks->len - 1;
          *(size_t*)hash_table_put(&track_map, &key, scratch_allocator) =
              track_idx;
          *array_list_push(&event_counts, size_t, scratch_allocator) = 0;
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
        string_view_t name_str = trace_data_get_string(td, e->name_ref);
        if (string_view_eq(name_str, SV("thread_name"))) {
          const trace_arg_persisted_t* args =
              (const trace_arg_persisted_t*)td->args.ptr;
          for (size_t k = 0; k < e->args_count; k++) {
            const trace_arg_persisted_t* arg = &args[e->args_offset + k];
            string_view_t key_str = trace_data_get_string(td, arg->key_ref);
            if (string_view_eq(key_str, SV("name"))) {
              t->name_ref = arg->val_ref;
              break;
            }
          }
        } else if (string_view_eq(name_str, SV("thread_sort_index"))) {
          const trace_arg_persisted_t* args =
              (const trace_arg_persisted_t*)td->args.ptr;
          for (size_t k = 0; k < e->args_count; k++) {
            const trace_arg_persisted_t* arg = &args[e->args_offset + k];
            string_view_t key_str = trace_data_get_string(td, arg->key_ref);
            if (string_view_eq(key_str, SV("sort_index"))) {
              string_view_t val = trace_data_get_string(td, arg->val_ref);
              t->sort_index = to_int32(val);
              break;
            }
          }
        }
      } else {
        event_track_indices[i] = (uint32_t)track_idx;
        size_t* counts_data = (size_t*)event_counts.ptr;
        counts_data[track_idx]++;

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

    // Pre-allocate event_indices for all tracks
    size_t* counts_data = (size_t*)event_counts.ptr;
    track_t* tracks_data = (track_t*)out_tracks->ptr;
    for (size_t i = 0; i < out_tracks->len; i++) {
      array_list_reserve(&tracks_data[i].event_indices, counts_data[i],
                         sizeof(size_t), output_allocator);
    }

    // Pass 2: Grouping (Zero reallocations, zero lookups, zero cache checks!)
    for (size_t i = 0; i < td->events.len; i++) {
      uint32_t track_idx = event_track_indices[i];
      if (track_idx != (uint32_t)-1) {
        track_t* t = &tracks_data[track_idx];
        *array_list_push(&t->event_indices, size_t, output_allocator) = i;
      }
    }

    // Sort events, calculate depths
    tracks_data = (track_t*)out_tracks->ptr;
    for (size_t i = 0; i < out_tracks->len; i++) {
      track_t* t = &tracks_data[i];
      track_sort_events(t, td, output_allocator);
      track_update_max_dur(t, td, output_allocator);
      if (t->type == TRACK_TYPE_THREAD) {
        track_calculate_depths(t, td, output_allocator);
      } else {
        // Counter tracks don't have nested depths.
        t->max_depth = 0;
        array_list_resize(&t->depths, t->event_indices.len, sizeof(uint32_t),
                          output_allocator);
        uint32_t* depths = (uint32_t*)t->depths.ptr;
        for (size_t k = 0; k < t->depths.len; k++) {
          depths[k] = 0;
        }

        array_list_resize(&t->self_durs, t->event_indices.len, sizeof(int64_t),
                          output_allocator);
        int64_t* self_durs = (int64_t*)t->self_durs.ptr;
        for (size_t k = 0; k < t->self_durs.len; k++) {
          self_durs[k] = 0;
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
              *array_list_push(&t->counter_series, string_ref_t,
                               output_allocator) = key_ref;
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
              scratch_allocator,
              t->counter_series.len * sizeof(counter_sort_key_t));
        }

        const string_ref_t* counter_series_data =
            (const string_ref_t*)t->counter_series.ptr;
        for (size_t s_idx = 0; s_idx < t->counter_series.len; s_idx++) {
          counter_keys[s_idx].ref = counter_series_data[s_idx];
          counter_keys[s_idx].str =
              trace_data_get_string(td, counter_series_data[s_idx]);
        }

        qsort(counter_keys, t->counter_series.len, sizeof(counter_sort_key_t),
              counter_sort_key_compare);

        string_ref_t* mutable_counter_series =
            (string_ref_t*)t->counter_series.ptr;
        for (size_t s_idx = 0; s_idx < t->counter_series.len; s_idx++) {
          mutable_counter_series[s_idx] = counter_keys[s_idx].ref;
        }

        if (t->counter_series.len > 32) {
          allocator_free(scratch_allocator, counter_keys,
                         t->counter_series.len * sizeof(counter_sort_key_t));
        }

        // Cache palette indices
        array_list_resize(&t->counter_palette_indices, t->counter_series.len,
                          sizeof(uint8_t), output_allocator);

        string_ref_t* counter_series = (string_ref_t*)t->counter_series.ptr;
        uint8_t* counter_palette_indices =
            (uint8_t*)t->counter_palette_indices.ptr;
        for (size_t s_idx = 0; s_idx < t->counter_series.len; s_idx++) {
          string_view_t key_str =
              trace_data_get_string(td, counter_series[s_idx]);
          uint32_t hash = 2166136261u;
          for (size_t char_idx = 0; char_idx < key_str.len; ++char_idx) {
            hash ^= (uint8_t)key_str.ptr[char_idx];
            hash *= 16777619u;
          }
          counter_palette_indices[s_idx] = (uint8_t)(hash % 8);
        }
      }
    }

    // Final track sort — Context-free using TrackSortKey
    track_sort_key_t* keys = nullptr;
    track_sort_key_t stack_keys[128];
    if (out_tracks->len <= 128) {
      keys = stack_keys;
    } else {
      keys = (track_sort_key_t*)allocator_alloc(
          scratch_allocator, out_tracks->len * sizeof(track_sort_key_t));
    }

    tracks_data = (track_t*)out_tracks->ptr;
    for (size_t i = 0; i < out_tracks->len; i++) {
      keys[i].track = &tracks_data[i];
      if (tracks_data[i].type == TRACK_TYPE_COUNTER) {
        keys[i].name = trace_data_get_string(td, tracks_data[i].name_ref);
        keys[i].id = trace_data_get_string(td, tracks_data[i].id_ref);
      } else {
        keys[i].name = (string_view_t){};
        keys[i].id = (string_view_t){};
      }
    }

    qsort(keys, out_tracks->len, sizeof(track_sort_key_t),
          track_sort_key_compare);

    track_t* sorted_tracks = nullptr;
    track_t stack_sorted_tracks[64];
    bool use_heap_sorted = out_tracks->len > 64;
    if (use_heap_sorted) {
      sorted_tracks = (track_t*)allocator_alloc(
          scratch_allocator, out_tracks->len * sizeof(track_t));
    } else {
      sorted_tracks = stack_sorted_tracks;
    }
    for (size_t i = 0; i < out_tracks->len; i++) {
      sorted_tracks[i] = *keys[i].track;
    }
    memcpy(out_tracks->ptr, sorted_tracks, out_tracks->len * sizeof(track_t));
    *out_min_ts = min_ts;
    *out_max_ts = max_ts;
  }
}
