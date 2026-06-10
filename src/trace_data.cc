#include "src/trace_data.h"

#include <string.h>

#include "src/colors.h"

static uint32_t compute_hash(std::string_view s) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < s.size(); ++i) {
    hash ^= (uint8_t)s[i];
    hash *= 16777619u;
  }
  return hash;
}

uint32_t TraceData::StringLookupHash::operator()(uint32_t index) const {
  if (index == 0) return td->tmp.current_hash;
  return td->string_table[index - 1].hash;
}

bool TraceData::StringLookupEq::operator()(uint32_t a, uint32_t b) const {
  std::string_view sa, sb;
  if (a == 0)
    sa = td->tmp.current_str;
  else {
    const StringEntry& e = td->string_table[a - 1];
    sa = {&td->string_buffer[e.offset], (size_t)e.len};
  }

  if (b == 0)
    sb = td->tmp.current_str;
  else {
    const StringEntry& e = td->string_table[b - 1];
    sb = {&td->string_buffer[e.offset], (size_t)e.len};
  }

  return sa == sb;
}

void trace_data_deinit(TraceData* td, Allocator a) {
  array_list_deinit(&td->string_buffer, a);
  array_list_deinit(&td->string_table, a);
  hash_table_deinit(&td->string_lookup, a);
  array_list_deinit(&td->events, a);
  array_list_deinit(&td->args, a);
}

void trace_data_clear(TraceData* td, Allocator /*a*/) {
  array_list_clear(&td->string_buffer);
  array_list_clear(&td->string_table);
  hash_table_clear(&td->string_lookup);
  array_list_clear(&td->events);
  array_list_clear(&td->args);
}

StringRef trace_data_push_string(TraceData* td, Allocator a, std::string_view s) {
  if (s.data() == nullptr) return 0;

  // Ensure functors are pointing to us (ZII support)
  td->string_lookup.hash_fn.td = td;
  td->string_lookup.eq_fn.td = td;

  td->tmp.current_str = s;
  uint32_t h = compute_hash(s);
  td->tmp.current_hash = h;

  uint32_t* existing_index = hash_table_get_with_hash(&td->string_lookup, 0u, h);
  if (existing_index) return *existing_index;

  // New string
  StringEntry entry;
  entry.offset = (uint32_t)td->string_buffer.size;
  entry.len = (uint32_t)s.size();
  entry.hash = h;

  array_list_append(&td->string_buffer, a, s.data(), s.size());
  // Add null terminator for C-string compatibility if needed, though not
  // strictly necessary with lengths.
  char null_terminator = '\0';
  array_list_push_back(&td->string_buffer, a, null_terminator);

  array_list_push_back(&td->string_table, a, entry);
  uint32_t new_index = (uint32_t)td->string_table.size;

  hash_table_put_with_hash(&td->string_lookup, a, new_index, new_index, h);

  return new_index;
}


static uint32_t compute_event_color(const Theme* theme,
                                    const TraceEvent* event) {
  const size_t palette_size =
      sizeof(theme->event_palette) / sizeof(theme->event_palette[0]);

  // 1. Check for explicit cname
  if (!event->cname.empty()) {
    if (event->cname == "thread_state_running")
      return theme->event_palette[3];  // Dark Green
    if (event->cname == "thread_state_runnable")
      return theme->event_palette[2];  // Gold
    if (event->cname == "thread_state_sleeping")
      return theme->event_palette[4];  // Dark Teal
    if (event->cname == "thread_state_uninterruptible")
      return theme->event_palette[0];  // Dark Red
    if (event->cname == "thread_state_iowait")
      return theme->event_palette[1];  // Burnt Orange

    if (event->cname == "rail_idle") return theme->event_palette[3];
    if (event->cname == "rail_animation")
      return theme->event_palette[6];  // Dark Purple
    if (event->cname == "rail_response")
      return theme->event_palette[5];  // Deep Blue
    if (event->cname == "rail_load")
      return theme->event_palette[1];  // Burnt Orange

    if (event->cname == "background_memory_dump")
      return theme->event_palette[4];  // Dark Teal
    if (event->cname == "light_memory_dump")
      return theme->event_palette[5];  // Deep Blue
    if (event->cname == "detailed_memory_dump")
      return theme->event_palette[7];  // Dark Maroon
  }

  // 2. Fallback to hashing the name
  uint32_t hash = compute_hash(event->name);
  return theme->event_palette[hash % palette_size];
}

void trace_event_matcher_deinit(TraceEventMatcher* matcher, Allocator a) {
  if (matcher->active_b_events.entries) {
    for (size_t i = 0; i < matcher->active_b_events.capacity; i++) {
      if (matcher->active_b_events.entries[i].occupied) {
        array_list_deinit(&matcher->active_b_events.entries[i].value.stack, a);
      }
    }
    hash_table_deinit(&matcher->active_b_events, a);
  }
}

static void trace_data_merge_args(TraceData* td, Allocator a, TraceEventPersisted* b_ev,
                                  const TraceEvent* e_ev) {
  if (e_ev->args_count == 0) return;

  size_t new_args_count = 0;
  bool* is_new = (bool*)allocator_alloc(a, e_ev->args_count * sizeof(bool));
  memset(is_new, 0, e_ev->args_count * sizeof(bool));

  for (size_t i = 0; i < e_ev->args_count; i++) {
    std::string_view e_key = e_ev->args[i].key;
    bool found = false;
    for (uint32_t j = 0; j < b_ev->args_count; j++) {
      const TraceArgPersisted& b_arg = td->args[b_ev->args_offset + j];
      std::string_view b_key = trace_data_get_string(td, b_arg.key_ref);
      if (b_key == e_key) {
        TraceArgPersisted& mutable_b_arg = td->args[b_ev->args_offset + j];
        mutable_b_arg.val_ref = trace_data_push_string(td, a, e_ev->args[i].val);
        mutable_b_arg.val_double = e_ev->args[i].val_double;
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
    uint32_t new_offset = (uint32_t)td->args.size;
    uint32_t new_count = old_count + (uint32_t)new_args_count;

    array_list_reserve(&td->args, a, td->args.size + new_count);

    for (uint32_t j = 0; j < old_count; j++) {
      array_list_push_back(&td->args, a, td->args[old_offset + j]);
    }

    for (size_t i = 0; i < e_ev->args_count; i++) {
      if (is_new[i]) {
        TraceArgPersisted arg = {};
        arg.key_ref = trace_data_push_string(td, a, e_ev->args[i].key);
        arg.val_ref = trace_data_push_string(td, a, e_ev->args[i].val);
        arg.val_double = e_ev->args[i].val_double;
        array_list_push_back(&td->args, a, arg);
      }
    }

    b_ev->args_offset = new_offset;
    b_ev->args_count = new_count;
  }

  allocator_free(a, is_new, e_ev->args_count * sizeof(bool));
}

void trace_data_add_event(TraceData* td, Allocator a, const Theme* theme,
                          const TraceEvent* event, TraceEventMatcher* matcher) {
  std::string_view ph = event->ph;
  bool is_begin = (ph.size() == 1 && (ph[0] == 'B' || ph[0] == 'b'));
  bool is_end = (ph.size() == 1 && (ph[0] == 'E' || ph[0] == 'e'));

  if (is_begin) {
    TraceEventPersisted p = {};
    p.name_ref = trace_data_push_string(td, a, event->name);
    p.cat_ref = trace_data_push_string(td, a, event->cat);
    p.ph_ref = trace_data_push_string(td, a, event->ph);
    p.cname_ref = trace_data_push_string(td, a, event->cname);
    p.id_ref = trace_data_push_string(td, a, event->id);
    p.color = compute_event_color(theme, event);
    p.ts = event->ts;
    p.dur = 0;
    p.pid = event->pid;
    p.tid = event->tid;
    p.args_count = (uint32_t)event->args_count;
    p.args_offset = (uint32_t)td->args.size;

    for (size_t i = 0; i < event->args_count; ++i) {
      TraceArgPersisted arg = {};
      arg.key_ref = trace_data_push_string(td, a, event->args[i].key);
      arg.val_ref = trace_data_push_string(td, a, event->args[i].val);
      arg.val_double = event->args[i].val_double;
      array_list_push_back(&td->args, a, arg);
    }

    size_t new_idx = td->events.size;
    array_list_push_back(&td->events, a, p);

    uint64_t thread_id = ((uint64_t)(uint32_t)event->pid << 32) | (uint32_t)event->tid;
    ThreadStack* ts_stack_ptr = hash_table_get(&matcher->active_b_events, thread_id);
    if (ts_stack_ptr == nullptr) {
      ThreadStack ts_stack = {};
      hash_table_put(&matcher->active_b_events, a, thread_id, ts_stack);
      ts_stack_ptr = hash_table_get(&matcher->active_b_events, thread_id);
    }
    ActiveEventB active_ev = {new_idx};
    array_list_push_back(&ts_stack_ptr->stack, a, active_ev);

  } else if (is_end) {
    uint64_t thread_id = ((uint64_t)(uint32_t)event->pid << 32) | (uint32_t)event->tid;
    ThreadStack* ts_stack_ptr = hash_table_get(&matcher->active_b_events, thread_id);
    if (ts_stack_ptr && ts_stack_ptr->stack.size > 0) {
      ActiveEventB active_ev = ts_stack_ptr->stack[ts_stack_ptr->stack.size - 1];
      ts_stack_ptr->stack.size--;

      TraceEventPersisted& b_ev = td->events[active_ev.event_idx];
      b_ev.dur = event->ts - b_ev.ts;
      if (b_ev.dur < 0) b_ev.dur = 0;

      trace_data_merge_args(td, a, &b_ev, event);
    }
  } else {
    TraceEventPersisted p = {};
    p.name_ref = trace_data_push_string(td, a, event->name);
    p.cat_ref = trace_data_push_string(td, a, event->cat);
    p.ph_ref = trace_data_push_string(td, a, event->ph);
    p.cname_ref = trace_data_push_string(td, a, event->cname);
    p.id_ref = trace_data_push_string(td, a, event->id);
    p.color = compute_event_color(theme, event);
    p.ts = event->ts;
    p.dur = event->dur;
    p.pid = event->pid;
    p.tid = event->tid;
    p.args_count = (uint32_t)event->args_count;
    p.args_offset = (uint32_t)td->args.size;

    for (size_t i = 0; i < event->args_count; ++i) {
      TraceArgPersisted arg = {};
      arg.key_ref = trace_data_push_string(td, a, event->args[i].key);
      arg.val_ref = trace_data_push_string(td, a, event->args[i].val);
      arg.val_double = event->args[i].val_double;
      array_list_push_back(&td->args, a, arg);
    }

    array_list_push_back(&td->events, a, p);
  }
}

void trace_data_update_event_color(TraceData* td, uint32_t event_idx,
                                   const Theme* theme) {
  if (event_idx >= td->events.size) return;
  TraceEventPersisted& p = td->events[event_idx];
  TraceEvent event = {};
  event.name = trace_data_get_string(td, p.name_ref);
  event.cname = trace_data_get_string(td, p.cname_ref);
  p.color = compute_event_color(theme, &event);
}
