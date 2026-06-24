#include "src/trace_data.h"

#include <stdatomic.h>
#include <string.h>

#include "src/assert.h"
#include "src/colors.h"

static uint32_t compute_hash(string_view_t s) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < s.len; ++i) {
    hash ^= (uint8_t)s.ptr[i];
    hash *= 16777619u;
  }
  return hash;
}

static uint32_t hash_uint64(const void* key, void* ctx) {
  (void)ctx;
  uint64_t v = *(const uint64_t*)key;
  return (uint32_t)(v ^ (v >> 32));
}

static bool eq_uint64(const void* a, const void* b, void* ctx) {
  (void)ctx;
  return *(const uint64_t*)a == *(const uint64_t*)b;
}

static void string_lookup_table_resize(string_lookup_table_t* lt,
                                       size_t new_capacity, allocator_t a) {
  size_t cap = 16;
  while (cap < new_capacity) {
    cap <<= 1;
  }

  string_lookup_entry_t* new_entries = (string_lookup_entry_t*)allocator_alloc(
      a, cap * sizeof(string_lookup_entry_t));
  memset(new_entries, 0, cap * sizeof(string_lookup_entry_t));

  size_t mask = cap - 1;

  if (lt->entries != nullptr) {
    for (size_t i = 0; i < lt->capacity; i++) {
      uint32_t index = lt->entries[i].index;
      if (index != 0) {
        uint32_t h = lt->entries[i].hash;
        size_t idx = h & mask;
        while (new_entries[idx].index != 0) {
          idx = (idx + 1) & mask;
        }
        new_entries[idx].index = index;
        new_entries[idx].hash = h;
      }
    }
    allocator_free(a, lt->entries,
                   lt->capacity * sizeof(string_lookup_entry_t));
  }

  lt->entries = new_entries;
  lt->capacity = cap;
  lt->capacity_mask = mask;
}

static void trace_data_deinit(trace_data_t* td, allocator_t a) {
  array_list_deinit(&td->string_buffer, a);
  array_list_deinit(&td->string_table, a);
  if (td->string_lookup.entries != nullptr) {
    allocator_free(a, td->string_lookup.entries,
                   td->string_lookup.capacity * sizeof(string_lookup_entry_t));
  }
  array_list_deinit(&td->events, a);
  array_list_deinit(&td->args, a);
  *td = (trace_data_t){};
}

trace_data_t* trace_data_create(allocator_t a) {
  trace_data_t* td = (trace_data_t*)allocator_alloc(a, sizeof(trace_data_t));
  CHECK(td != nullptr);
  *td = (trace_data_t){.ref_count = 1};
  return td;
}

void trace_data_retain(trace_data_t* td) {
  CHECK(td != nullptr);
  int prev = atomic_fetch_add_explicit(&td->ref_count, 1, memory_order_relaxed);
  CHECK(prev > 0);
}

void trace_data_release(trace_data_t* td, allocator_t a) {
  if (td == nullptr) return;
  int prev = atomic_fetch_sub_explicit(&td->ref_count, 1, memory_order_acq_rel);
  CHECK(prev > 0);
  if (prev == 1) {
    trace_data_deinit(td, a);
    allocator_free(a, td, sizeof(trace_data_t));
  }
}

string_ref_t trace_data_push_string(trace_data_t* td, string_view_t s,
                                    allocator_t a) {
  string_ref_t result = 0;
  if (s.ptr == nullptr || s.len == 0) {
    return result;
  }

  string_lookup_table_t* lt = &td->string_lookup;
  if (lt->capacity == 0) {
    string_lookup_table_resize(lt, 16, a);
  }

  uint32_t h = compute_hash(s);

  size_t idx = h & lt->capacity_mask;
  const string_entry_t* st_table = (const string_entry_t*)td->string_table.ptr;
  const char* st_buffer = (const char*)td->string_buffer.ptr;

  while (lt->entries[idx].index != 0) {
    string_lookup_entry_t* entry = &lt->entries[idx];
    if (entry->hash == h) {
      const string_entry_t* e = &st_table[entry->index - 1];
      if (s.len == e->len && memcmp(s.ptr, st_buffer + e->offset, s.len) == 0) {
        return entry->index;
      }
    }
    idx = (idx + 1) & lt->capacity_mask;
  }

  string_entry_t entry = {
      .offset = (uint32_t)td->string_buffer.len,
      .len = (uint32_t)s.len,
      .hash = h,
  };

  array_list_ensure(&td->string_buffer, s.len, char, a);
  memcpy((char*)td->string_buffer.ptr + td->string_buffer.len, s.ptr, s.len);
  td->string_buffer.len += s.len;

  char null_terminator = '\0';
  *array_list_push(&td->string_buffer, char, a) = null_terminator;

  *array_list_push(&td->string_table, string_entry_t, a) = entry;
  uint32_t new_index = (uint32_t)td->string_table.len;

  lt->entries[idx].index = new_index;
  lt->entries[idx].hash = h;
  lt->size++;

  if (lt->size * 2 > lt->capacity) {
    string_lookup_table_resize(lt, lt->capacity * 2, a);
  }

  return new_index;
}

static string_ref_t trace_data_push_string_cached(trace_data_t* td, string_view_t s,
                                                  string_ref_t* cache_ref,
                                                  allocator_t a) {
  if (s.ptr == nullptr || s.len == 0) {
    return 0;
  }
  if (*cache_ref > 0) {
    string_view_t last_str = trace_data_get_string(td, *cache_ref);
    if (string_view_eq(s, last_str)) {
      return *cache_ref;
    }
  }
  string_ref_t new_ref = trace_data_push_string(td, s, a);
  *cache_ref = new_ref;
  return new_ref;
}


static uint8_t compute_event_palette_index(const trace_data_t* td,
                                           string_ref_t name_ref,
                                           string_ref_t cname_ref) {
  uint8_t index = 0;
  bool resolved = false;

  if (cname_ref > 0) {
    string_view_t cname = trace_data_get_string(td, cname_ref);
    if (cname.len > 0) {
      if (string_view_eq(cname, SV("thread_state_running"))) {
        index = 3;
        resolved = true;
      } else if (string_view_eq(cname, SV("thread_state_runnable"))) {
        index = 2;
        resolved = true;
      } else if (string_view_eq(cname, SV("thread_state_sleeping"))) {
        index = 4;
        resolved = true;
      } else if (string_view_eq(cname, SV("thread_state_uninterruptible"))) {
        index = 0;
        resolved = true;
      } else if (string_view_eq(cname, SV("thread_state_iowait"))) {
        index = 1;
        resolved = true;
      } else if (string_view_eq(cname, SV("rail_idle"))) {
        index = 3;
        resolved = true;
      } else if (string_view_eq(cname, SV("rail_animation"))) {
        index = 6;
        resolved = true;
      } else if (string_view_eq(cname, SV("rail_response"))) {
        index = 5;
        resolved = true;
      } else if (string_view_eq(cname, SV("rail_load"))) {
        index = 1;
        resolved = true;
      } else if (string_view_eq(cname, SV("background_memory_dump"))) {
        index = 4;
        resolved = true;
      } else if (string_view_eq(cname, SV("light_memory_dump"))) {
        index = 5;
        resolved = true;
      } else if (string_view_eq(cname, SV("detailed_memory_dump"))) {
        index = 7;
        resolved = true;
      }
    }
  }

  if (!resolved && name_ref > 0) {
    uint32_t hash = 0;
    if (name_ref <= td->string_table.len) {
      const string_entry_t* table = (const string_entry_t*)td->string_table.ptr;
      hash = table[name_ref - 1].hash;
    }
    index = (uint8_t)(hash % 8);
  }

  return index;
}

void trace_event_matcher_deinit(trace_event_matcher_t* matcher) {
  allocator_t a = matcher->allocator;
  if (a.alloc == nullptr) return;
  if (matcher->active_b_events.entries != nullptr) {
    for (size_t i = 0; i < matcher->active_b_events.capacity; i++) {
      void* entry = (char*)matcher->active_b_events.entries +
                    i * matcher->active_b_events.entry_size;
      if (*hash_table_entry_occupied(&matcher->active_b_events, entry)) {
        thread_stack_t* stack = (thread_stack_t*)hash_table_entry_value(
            &matcher->active_b_events, entry);
        array_list_deinit(&stack->stack, a);
      }
    }
    hash_table_deinit(&matcher->active_b_events, a);
  }
}

static void trace_data_merge_args(trace_data_t* td,
                                  trace_event_persisted_t* b_ev,
                                  const trace_event_t* e_ev, allocator_t a) {
  if (e_ev->args_count > 0) {
    // 1. Pre-resolve/push all end event argument keys and values.
    // This populates the string table and gives us stable integer references.
    string_ref_t e_key_refs_stack[16];
    string_ref_t e_val_refs_stack[16];
    string_ref_t* e_key_refs = e_key_refs_stack;
    string_ref_t* e_val_refs = e_val_refs_stack;
    if (e_ev->args_count > 16) {
      e_key_refs = (string_ref_t*)allocator_alloc(
          a, e_ev->args_count * sizeof(string_ref_t));
      e_val_refs = (string_ref_t*)allocator_alloc(
          a, e_ev->args_count * sizeof(string_ref_t));
    }

    for (size_t i = 0; i < e_ev->args_count; i++) {
      e_key_refs[i] = trace_data_push_string(td, e_ev->args[i].key, a);
      e_val_refs[i] = trace_data_push_string(td, e_ev->args[i].val, a);
    }

    size_t new_args_count = 0;
    bool is_new_stack[16];
    bool* is_new = is_new_stack;
    if (e_ev->args_count > 16) {
      is_new = (bool*)allocator_alloc(a, e_ev->args_count * sizeof(bool));
    }
    memset(is_new, 0, e_ev->args_count * sizeof(bool));

    trace_arg_persisted_t* td_args = (trace_arg_persisted_t*)td->args.ptr;

    // 2. Perform fast O(1) integer comparisons in the search loop!
    for (size_t i = 0; i < e_ev->args_count; i++) {
      string_ref_t key_ref = e_key_refs[i];
      bool found = false;
      for (uint32_t j = 0; j < b_ev->args_count; j++) {
        trace_arg_persisted_t* b_arg = &td_args[b_ev->args_offset + j];
        if (b_arg->key_ref == key_ref) {
          b_arg->val_ref = e_val_refs[i];
          b_arg->val_double = e_ev->args[i].val_double;
          found = true;
          break;
        }
      }
      if (!found) {
        is_new[i] = true;
        new_args_count++;
      }
    }

    if (new_args_count > 0) {
      uint32_t old_count = b_ev->args_count;
      uint32_t old_offset = b_ev->args_offset;
      uint32_t new_offset = (uint32_t)td->args.len;
      uint32_t new_count = old_count + (uint32_t)new_args_count;

      array_list_ensure(&td->args, new_count, trace_arg_persisted_t, a);

      td_args = (trace_arg_persisted_t*)td->args.ptr;

      memcpy((trace_arg_persisted_t*)td->args.ptr + new_offset,
             td_args + old_offset, old_count * sizeof(trace_arg_persisted_t));
      td->args.len += old_count;

      td_args = (trace_arg_persisted_t*)td->args.ptr;

      // 3. Insert new arguments using the pre-resolved references!
      for (size_t i = 0; i < e_ev->args_count; i++) {
        if (is_new[i]) {
          trace_arg_persisted_t arg = {
              .key_ref = e_key_refs[i],
              .val_ref = e_val_refs[i],
              .val_double = e_ev->args[i].val_double,
          };
          *array_list_push(&td->args, trace_arg_persisted_t, a) = arg;
        }
      }

      b_ev->args_offset = new_offset;
      b_ev->args_count = new_count;
    }

    // Clean up temporary arrays
    if (e_ev->args_count > 16) {
      allocator_free(a, e_key_refs, e_ev->args_count * sizeof(string_ref_t));
      allocator_free(a, e_val_refs, e_ev->args_count * sizeof(string_ref_t));
    }
    if (is_new != is_new_stack) {
      allocator_free(a, is_new, e_ev->args_count * sizeof(bool));
    }
  }
}

static inline void trace_event_matcher_ensure_init(
    trace_event_matcher_t* matcher, allocator_t default_allocator) {
  if (matcher->active_b_events.hash_fn == nullptr) {
    allocator_t a = matcher->allocator.alloc != nullptr ? matcher->allocator
                                                        : default_allocator;
    matcher->active_b_events = hash_table_init(uint64_t, thread_stack_t,
                                               hash_uint64, eq_uint64, nullptr);
    matcher->allocator = a;
  }
}

void trace_data_add_event(trace_data_t* td, const trace_event_t* event,
                          trace_event_matcher_t* matcher, allocator_t a) {
  string_view_t ph = event->ph;
  bool is_begin = (ph.len == 1 && (ph.ptr[0] == 'B' || ph.ptr[0] == 'b'));
  bool is_end = (ph.len == 1 && (ph.ptr[0] == 'E' || ph.ptr[0] == 'e'));

  if (is_begin) {
    trace_event_persisted_t p = {};
    p.name_ref = trace_data_push_string(td, event->name, a);
    p.cat_ref = trace_data_push_string_cached(td, event->cat, &td->last_cat_ref, a);
    p.ph_ref = trace_data_push_string_cached(td, event->ph, &td->last_ph_ref, a);
    p.cname_ref = trace_data_push_string_cached(td, event->cname, &td->last_cname_ref, a);
    p.id_ref = trace_data_push_string(td, event->id, a);
    p.palette_index = compute_event_palette_index(td, p.name_ref, p.cname_ref);
    p.ts = event->ts;
    p.dur = 0;
    p.pid = event->pid;
    p.tid = event->tid;
    p.args_count = (uint32_t)event->args_count;
    p.args_offset = (uint32_t)td->args.len;

    for (size_t i = 0; i < event->args_count; ++i) {
      trace_arg_persisted_t arg = {};
      arg.key_ref = trace_data_push_string(td, event->args[i].key, a);
      arg.val_ref = trace_data_push_string(td, event->args[i].val, a);
      arg.val_double = event->args[i].val_double;
      *array_list_push(&td->args, trace_arg_persisted_t, a) = arg;
    }

    size_t new_idx = td->events.len;
    *array_list_push(&td->events, trace_event_persisted_t, a) = p;

    uint64_t thread_id =
        ((uint64_t)(uint32_t)event->pid << 32) | (uint32_t)event->tid;
    trace_event_matcher_ensure_init(matcher, a);

    thread_stack_t* ts_stack_ptr =
        (thread_stack_t*)hash_table_get(&matcher->active_b_events, &thread_id);
    if (ts_stack_ptr == nullptr) {
      thread_stack_t ts_stack = {};
      thread_stack_t* val_slot = (thread_stack_t*)hash_table_put(
          &matcher->active_b_events, &thread_id, matcher->allocator);
      *val_slot = ts_stack;
      ts_stack_ptr = val_slot;
    }
    active_event_b_t active_ev = {new_idx};
    *array_list_push(&ts_stack_ptr->stack, active_event_b_t,
                     matcher->allocator) = active_ev;

  } else if (is_end) {
    uint64_t thread_id =
        ((uint64_t)(uint32_t)event->pid << 32) | (uint32_t)event->tid;
    trace_event_matcher_ensure_init(matcher, a);

    thread_stack_t* ts_stack_ptr =
        (thread_stack_t*)hash_table_get(&matcher->active_b_events, &thread_id);
    if (ts_stack_ptr != nullptr && ts_stack_ptr->stack.len > 0) {
      active_event_b_t* stack = (active_event_b_t*)ts_stack_ptr->stack.ptr;
      active_event_b_t active_ev = stack[ts_stack_ptr->stack.len - 1];
      ts_stack_ptr->stack.len--;

      trace_event_persisted_t* events =
          (trace_event_persisted_t*)td->events.ptr;
      trace_event_persisted_t* b_ev = &events[active_ev.event_idx];
      b_ev->dur = event->ts - b_ev->ts;
      if (b_ev->dur < 0) {
        b_ev->dur = 0;
      }

      trace_data_merge_args(td, b_ev, event, a);
    }
  } else {
    trace_event_persisted_t p = {};
    p.name_ref = trace_data_push_string(td, event->name, a);
    p.cat_ref = trace_data_push_string_cached(td, event->cat, &td->last_cat_ref, a);
    p.ph_ref = trace_data_push_string_cached(td, event->ph, &td->last_ph_ref, a);
    p.cname_ref = trace_data_push_string_cached(td, event->cname, &td->last_cname_ref, a);
    p.id_ref = trace_data_push_string(td, event->id, a);
    p.palette_index = compute_event_palette_index(td, p.name_ref, p.cname_ref);
    p.ts = event->ts;
    p.dur = event->dur;
    p.pid = event->pid;
    p.tid = event->tid;
    p.args_count = (uint32_t)event->args_count;
    p.args_offset = (uint32_t)td->args.len;

    for (size_t i = 0; i < event->args_count; ++i) {
      trace_arg_persisted_t arg = {};
      arg.key_ref = trace_data_push_string(td, event->args[i].key, a);
      arg.val_ref = trace_data_push_string(td, event->args[i].val, a);
      arg.val_double = event->args[i].val_double;
      *array_list_push(&td->args, trace_arg_persisted_t, a) = arg;
    }

    *array_list_push(&td->events, trace_event_persisted_t, a) = p;
  }
}
