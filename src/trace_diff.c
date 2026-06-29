#include "src/trace_diff.h"

#include <stdlib.h>
#include <string.h>

#include "core/hash_table.h"
#include "src/trace_aggregate.h"

typedef hash_table_t(string_view_t, trace_diff_entry_t) diff_map_t;

static uint32_t hash_string_view(const string_view_t* key, void* ctx) {
  (void)ctx;
  uint32_t hash = 2166136261U;
  for (size_t i = 0; i < key->len; i++) {
    hash ^= (unsigned char)key->ptr[i];
    hash *= 16777619ULL;
  }
  return hash;
}

static bool eq_string_view(const string_view_t* a, const string_view_t* b, void* ctx) {
  (void)ctx;
  return string_view_eq(*a, *b);
}

static int compare_string_views(string_view_t a, string_view_t b) {
  size_t min_len = a.len < b.len ? a.len : b.len;
  int cmp = memcmp(a.ptr, b.ptr, min_len);
  if (cmp != 0) return cmp;
  if (a.len < b.len) return -1;
  if (a.len > b.len) return 1;
  return 0;
}

static int compare_diff_duration(const void* a_ptr, const void* b_ptr) {
  const trace_diff_entry_t* am = (const trace_diff_entry_t*)a_ptr;
  const trace_diff_entry_t* bm = (const trace_diff_entry_t*)b_ptr;
  if (am->delta_duration > bm->delta_duration) return -1;
  if (am->delta_duration < bm->delta_duration) return 1;
  return compare_string_views(am->key, bm->key);
}

static int compare_diff_count(const void* a_ptr, const void* b_ptr) {
  const trace_diff_entry_t* am = (const trace_diff_entry_t*)a_ptr;
  const trace_diff_entry_t* bm = (const trace_diff_entry_t*)b_ptr;
  if (am->delta_count > bm->delta_count) return -1;
  if (am->delta_count < bm->delta_count) return 1;
  return compare_string_views(am->key, bm->key);
}

void trace_diff_compute(const trace_data_t* td_baseline,
                        const trace_data_t* td_target, string_view_t group_by,
                        string_view_t sort_by,
                        darray_trace_diff_entry_t* out_entries,
                        allocator_t* a) {
  if (!td_baseline || !td_target || !out_entries) {
    return;
  }

  darray_trace_aggregate_entry_t agg_baseline = {};
  darray_trace_aggregate_entry_t agg_target = {};

  trace_aggregate_compute(td_baseline, group_by, SV(""), &agg_baseline, a);
  trace_aggregate_compute(td_target, group_by, SV(""), &agg_target, a);

  diff_map_t map = {};
  hash_table_init(&map, hash_string_view, eq_string_view, nullptr);

  // Populate baseline
  trace_aggregate_entry_t* base_ptr = agg_baseline.ptr;
  for (size_t i = 0; i < agg_baseline.len; i++) {
    const trace_aggregate_entry_t* e = &base_ptr[i];
    string_view_t name = trace_data_get_string(td_baseline, e->key_ref);
    trace_diff_entry_t entry = {
        .key = name,
        .baseline_duration = e->total_duration,
        .baseline_count = e->count,
        .target_duration = 0.0,
        .target_count = 0,
        .delta_duration = 0.0,
        .delta_count = 0,
    };
    hash_table_put(&map, &name, entry, a);
  }

  // Populate target
  trace_aggregate_entry_t* target_ptr = agg_target.ptr;
  for (size_t i = 0; i < agg_target.len; i++) {
    const trace_aggregate_entry_t* e = &target_ptr[i];
    string_view_t name = trace_data_get_string(td_target, e->key_ref);
    trace_diff_entry_t* entry = hash_table_get(&map, &name);
    if (entry) {
      entry->target_duration = e->total_duration;
      entry->target_count = e->count;
    } else {
      trace_diff_entry_t new_entry = {
          .key = name,
          .baseline_duration = 0.0,
          .baseline_count = 0,
          .target_duration = e->total_duration,
          .target_count = e->count,
          .delta_duration = 0.0,
          .delta_count = 0,
      };
      hash_table_put(&map, &name, new_entry, a);
    }
  }

  // Collect and calculate deltas
  if (map.capacity > 0) {
    for (size_t i = 0; i < map.capacity; i++) {
      if (map.entries[i].occupied) {
        trace_diff_entry_t* e = &map.entries[i].value;
        e->delta_duration = e->target_duration - e->baseline_duration;
        e->delta_count = (int64_t)e->target_count - (int64_t)e->baseline_count;
        darray_push(out_entries, *e, a);
      }
    }
  }

  // Sort
  if (out_entries->len > 0) {
    if (string_view_eq(sort_by, SV("count-delta"))) {
      qsort(out_entries->ptr, out_entries->len, sizeof(trace_diff_entry_t),
            compare_diff_count);
    } else {
      qsort(out_entries->ptr, out_entries->len, sizeof(trace_diff_entry_t),
            compare_diff_duration);
    }
  }

  hash_table_deinit(&map, a);
  darray_deinit(&agg_baseline, a);
  darray_deinit(&agg_target, a);
}
