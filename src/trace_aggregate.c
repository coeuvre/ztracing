#include "src/trace_aggregate.h"

#include <stdlib.h>
#include <string.h>

#include "core/hash_table.h"

typedef struct {
  double total_duration;
  size_t count;
} agg_value_t;

typedef hash_table_t(uint32_t, agg_value_t) agg_map_t;

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

static int compare_aggregate_duration(const void* a_ptr, const void* b_ptr) {
  const trace_aggregate_entry_t* am = (const trace_aggregate_entry_t*)a_ptr;
  const trace_aggregate_entry_t* bm = (const trace_aggregate_entry_t*)b_ptr;
  if (am->total_duration > bm->total_duration) return -1;
  if (am->total_duration < bm->total_duration) return 1;
  return 0;
}

static int compare_aggregate_count(const void* a_ptr, const void* b_ptr) {
  const trace_aggregate_entry_t* am = (const trace_aggregate_entry_t*)a_ptr;
  const trace_aggregate_entry_t* bm = (const trace_aggregate_entry_t*)b_ptr;
  if (am->count > bm->count) return -1;
  if (am->count < bm->count) return 1;
  return 0;
}

void trace_aggregate_compute(const trace_data_t* td, string_view_t group_by,
                             string_view_t sort_by,
                             darray_trace_aggregate_entry_t* out_entries,
                             allocator_t* a) {
  if (!td || !out_entries) {
    return;
  }

  bool by_cat = string_view_eq(group_by, SV("category"));

  agg_map_t map = {};
  hash_table_init(&map, hash_uint32, eq_uint32, nullptr);

  const trace_event_persisted_t* events = td->events.ptr;
  for (size_t i = 0; i < td->events.len; i++) {
    const trace_event_persisted_t* e = &events[i];
    uint32_t key = by_cat ? e->cat_ref : e->name_ref;

    agg_value_t* val = hash_table_get(&map, &key);
    if (val) {
      val->total_duration += (double)e->dur;
      val->count++;
    } else {
      agg_value_t new_val = {.total_duration = (double)e->dur, .count = 1};
      hash_table_put(&map, &key, new_val, a);
    }
  }

  // Collect into darray
  if (map.capacity > 0) {
    for (size_t i = 0; i < map.capacity; i++) {
      if (map.entries[i].occupied) {
        trace_aggregate_entry_t entry = {
            .key_ref = map.entries[i].key,
            .total_duration = map.entries[i].value.total_duration,
            .count = map.entries[i].value.count,
        };
        darray_push(out_entries, entry, a);
      }
    }
  }

  // Sort
  if (out_entries->len > 0) {
    if (string_view_eq(sort_by, SV("count"))) {
      qsort(out_entries->ptr, out_entries->len, sizeof(trace_aggregate_entry_t),
            compare_aggregate_count);
    } else {
      qsort(out_entries->ptr, out_entries->len, sizeof(trace_aggregate_entry_t),
            compare_aggregate_duration);
    }
  }

  hash_table_deinit(&map, a);
}
