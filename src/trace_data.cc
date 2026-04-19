#include "src/trace_data.h"

#include <string.h>
#include "src/colors.h"

static uint32_t trace_data_push_string(TraceData* td, Allocator a, Str s) {
  if (s.buf == nullptr) return 0;

  uint32_t offset = (uint32_t)td->string_pool.size;
  array_list_append(&td->string_pool, a, s.buf, s.len);
  char null_terminator = '\0';
  array_list_push_back(&td->string_pool, a, null_terminator);
  return offset;
}

void trace_data_init(TraceData* td, Allocator a) {
  *td = {};
  // Offset 0 is reserved for null/empty string.
  char null_terminator = '\0';
  array_list_push_back(&td->string_pool, a, null_terminator);
}

void trace_data_deinit(TraceData* td, Allocator a) {
  array_list_deinit(&td->string_pool, a);
  array_list_deinit(&td->events, a);
  array_list_deinit(&td->args, a);
}

void trace_data_clear(TraceData* td, Allocator a) {
  array_list_clear(&td->string_pool);
  array_list_clear(&td->events);
  array_list_clear(&td->args);

  // Offset 0 is reserved for null/empty string.
  char null_terminator = '\0';
  array_list_push_back(&td->string_pool, a, null_terminator);
}

static uint32_t compute_event_color(const Theme* theme, const TraceEvent* event) {
  const size_t palette_size = sizeof(theme->event_palette) / sizeof(theme->event_palette[0]);

  // 1. Check for explicit cname
  if (event->cname.len > 0) {
    if (str_eq(event->cname, STR("thread_state_running"))) return theme->event_palette[3]; // Dark Green
    if (str_eq(event->cname, STR("thread_state_runnable"))) return theme->event_palette[2]; // Gold
    if (str_eq(event->cname, STR("thread_state_sleeping"))) return theme->event_palette[4]; // Dark Teal
    if (str_eq(event->cname, STR("thread_state_uninterruptible"))) return theme->event_palette[0]; // Dark Red
    if (str_eq(event->cname, STR("thread_state_iowait"))) return theme->event_palette[1]; // Burnt Orange
    
    if (str_eq(event->cname, STR("rail_idle"))) return theme->event_palette[3];
    if (str_eq(event->cname, STR("rail_animation"))) return theme->event_palette[6]; // Dark Purple
    if (str_eq(event->cname, STR("rail_response"))) return theme->event_palette[5]; // Deep Blue
    if (str_eq(event->cname, STR("rail_load"))) return theme->event_palette[1]; // Burnt Orange

    if (str_eq(event->cname, STR("background_memory_dump"))) return theme->event_palette[4]; // Dark Teal
    if (str_eq(event->cname, STR("light_memory_dump"))) return theme->event_palette[5]; // Deep Blue
    if (str_eq(event->cname, STR("detailed_memory_dump"))) return theme->event_palette[7]; // Dark Maroon
  }

  // 2. Fallback to hashing the name
  // FNV-1a hash
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < event->name.len; ++i) {
    hash ^= (uint8_t)event->name.buf[i];
    hash *= 16777619u;
  }

  return theme->event_palette[hash % palette_size];
}

void trace_data_add_event(TraceData* td, Allocator a, const Theme* theme, const TraceEvent* event) {
  TraceEventPersisted p = {};
  p.name_offset = trace_data_push_string(td, a, event->name);
  p.cat_offset = trace_data_push_string(td, a, event->cat);
  p.ph_offset = trace_data_push_string(td, a, event->ph);
  p.cname_offset = trace_data_push_string(td, a, event->cname);
  p.color = compute_event_color(theme, event);
  p.ts = event->ts;
  p.dur = event->dur;
  p.pid = event->pid;
  p.tid = event->tid;
  p.args_count = (uint32_t)event->args_count;
  p.args_offset = (uint32_t)td->args.size;

  for (size_t i = 0; i < event->args_count; ++i) {
    TraceArgPersisted arg = {};
    arg.key_offset = trace_data_push_string(td, a, event->args[i].key);
    arg.val_offset = trace_data_push_string(td, a, event->args[i].val);
    array_list_push_back(&td->args, a, arg);
  }

  array_list_push_back(&td->events, a, p);
}

void trace_data_update_event_color(TraceData* td, uint32_t event_idx, const Theme* theme) {
  if (event_idx >= td->events.size) return;
  TraceEventPersisted& p = td->events[event_idx];
  TraceEvent event = {};
  event.name = trace_data_get_string(td, p.name_offset);
  event.cname = trace_data_get_string(td, p.cname_offset);
  p.color = compute_event_color(theme, &event);
}
