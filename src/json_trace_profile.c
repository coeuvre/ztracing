#include "src/json_trace_profile.h"

#include <stdbool.h>

#include "src/hash_trie.h"
#include "src/json.h"
#include "src/list.h"
#include "src/math.h"
#include "src/memory.h"
#include "src/string.h"
#include "src/types.h"

typedef struct JsonTraceEvent {
  Str8 name;
  Str8 id;
  Str8 cat;
  u8 ph;
  i64 ts;
  i64 tts;
  i64 pid;
  i64 tid;
  i64 dur;
  JsonValue *args;
} JsonTraceEvent;

static JsonTraceSeries *json_trace_counter_upsert_series(JsonTraceCounter *self,
                                                         Arena *arena,
                                                         Str8 name) {
  HashTrie *slot = hash_trie_upsert(arena, &self->series, name);
  if (!slot->value) {
    JsonTraceSeries *new_series = arena_push_struct(arena, JsonTraceSeries);
    new_series->name = slot->key;
    slot->value = new_series;
    self->series_count += 1;
  }
  return slot->value;
}

static void json_trace_counter_add_sample(JsonTraceCounter *self, Arena *arena,
                                          Str8 name, i64 time, f64 value) {
  JsonTraceSeries *series = json_trace_counter_upsert_series(self, arena, name);
  JsonTraceSample *sample = arena_push_struct(arena, JsonTraceSample);
  sample->time = time;
  sample->value = value;
  DLL_APPEND(series->first, series->last, sample, prev, next);
  series->sample_count++;

  self->min_value = f64_min(self->min_value, value);
  self->max_value = f64_max(self->max_value, value);
}

static JsonTraceSpan *json_trace_thread_add_span(JsonTraceThread *self,
                                                 Arena *arena, Str8 name,
                                                 i64 begin_time_us,
                                                 i64 end_time_us) {
  JsonTraceSpan *span = arena_push_struct(arena, JsonTraceSpan);
  span->name = str8_dup(arena, name);
  span->begin_time_ns = begin_time_us;
  span->end_time_ns = end_time_us;
  DLL_APPEND(self->first, self->last, span, prev, next);
  self->span_count += 1;
  return span;
}

static JsonTraceCounter *json_trace_process_upsert_counter(
    JsonTraceProcess *self, Arena *arena, Str8 name) {
  HashTrie *slot = hash_trie_upsert(arena, &self->counters, name);
  if (!slot->value) {
    JsonTraceCounter *new_counter = arena_push_struct(arena, JsonTraceCounter);
    new_counter->name = slot->key;
    slot->value = new_counter;
    self->counter_count += 1;
  }
  return slot->value;
}

static JsonTraceThread *json_trace_process_upsert_thread(JsonTraceProcess *self,
                                                         Arena *arena,
                                                         i64 tid) {
  Str8 key = str8((u8 *)&tid, sizeof(tid));
  HashTrie *slot = hash_trie_upsert(arena, &self->threads, key);
  if (!slot->value) {
    JsonTraceThread *new_thread = arena_push_struct(arena, JsonTraceThread);
    new_thread->tid = tid;
    slot->value = new_thread;
    self->thread_count += 1;
  }
  return slot->value;
}

static JsonTraceProcess *json_trace_profile_upsert_process(
    JsonTraceProfile *self, Arena *arena, i64 pid) {
  Str8 key = str8((u8 *)&pid, sizeof(pid));
  HashTrie *slot = hash_trie_upsert(arena, &self->processes, key);
  if (!slot->value) {
    JsonTraceProcess *new_process = arena_push_struct(arena, JsonTraceProcess);
    new_process->pid = pid;
    slot->value = new_process;
    self->process_count += 1;
  }
  return slot->value;
}

static void json_trace_profile_process_trace_event(JsonTraceProfile *self,
                                                   Arena *arena,
                                                   JsonValue *value) {
  JsonTraceEvent trace_event = {0};
  for (JsonValue *entry = value->first; entry; entry = entry->next) {
    if (str8_eq(entry->label, STR8_LIT("name"))) {
      trace_event.name = entry->value;
    } else if (str8_eq(entry->label, STR8_LIT("ph"))) {
      if (entry->value.len > 0) {
        trace_event.ph = entry->value.ptr[0];
      }
    } else if (str8_eq(entry->label, STR8_LIT("ts"))) {
      trace_event.ts = json_value_as_f64(entry);
    } else if (str8_eq(entry->label, STR8_LIT("tts"))) {
      trace_event.tts = json_value_as_f64(entry);
    } else if (str8_eq(entry->label, STR8_LIT("pid"))) {
      trace_event.pid = json_value_as_f64(entry);
    } else if (str8_eq(entry->label, STR8_LIT("tid"))) {
      trace_event.tid = json_value_as_f64(entry);
    } else if (str8_eq(entry->label, STR8_LIT("dur"))) {
      trace_event.dur = json_value_as_f64(entry);
    } else if (str8_eq(entry->label, STR8_LIT("args"))) {
      trace_event.args = entry;
    }
  }

  i64 time = trace_event.ts * 1000;

  switch (trace_event.ph) {
    // Counter event
    case 'C': {
      JsonTraceProcess *process =
          json_trace_profile_upsert_process(self, arena, trace_event.pid);
      JsonTraceCounter *counter =
          json_trace_process_upsert_counter(process, arena, trace_event.name);
      if (trace_event.args->type == JSON_VALUE_OBJECT) {
        for (JsonValue *arg = trace_event.args->first; arg; arg = arg->next) {
          f64 value = json_value_as_f64(arg);
          json_trace_counter_add_sample(counter, arena, arg->label, time,
                                        value);
        }
      }
    } break;

    // Complete event
    case 'X': {
      if (!str8_is_empty(trace_event.name)) {
        JsonTraceProcess *process =
            json_trace_profile_upsert_process(self, arena, trace_event.pid);
        JsonTraceThread *thread =
            json_trace_process_upsert_thread(process, arena, trace_event.tid);

        // TODO: Interning string name and cat.

        JsonTraceSpan *span =
            json_trace_thread_add_span(thread, arena, trace_event.name, time,
                                       time + trace_event.dur * 1000);
        if (!str8_is_empty(trace_event.cat)) {
          span->cat = str8_dup(arena, trace_event.cat);
        }
        // TODO: handle trace_event.args.
      }
    } break;

    default: {
    } break;
  }

  self->min_time_ns = i64_min(self->min_time_ns, time);
  self->max_time_ns = i64_max(self->max_time_ns, time);
}

static bool json_trace_profile_parse_array_format(JsonTraceProfile *self,
                                                  Arena *arena,
                                                  JsonParser *parser,
                                                  Arena scratch) {
  bool eof = false;
  bool running = true;
  while (running) {
    Arena scratch_ = scratch;
    JsonValue *value = json_parser_parse_value(parser, &scratch_);
    switch (value->type) {
      case JSON_VALUE_OBJECT: {
        json_trace_profile_process_trace_event(self, arena, value);

        Arena scratch_ = scratch;
        JsonToken token = json_parser_parse_token(parser, &scratch_);
        switch (token.type) {
          case JSON_TOKEN_COMMA: {
          } break;

          case JSON_TOKEN_CLOSE_BRACKET: {
            running = false;
          } break;

          case JSON_TOKEN_ERROR: {
            self->error = str8_dup(arena, token.value);
            running = false;
            eof = true;
          } break;

          default: {
            self->error =
                arena_push_str8f(arena, "expecting ',' or ']', got '%.*s'",
                                 (int)token.value.len, token.value.ptr);
            running = false;
            eof = true;
          } break;
        }
      } break;

      case JSON_VALUE_ERROR: {
        self->error = str8_dup(arena, value->value);
        running = false;
        eof = true;
      } break;

      default: {
        self->error =
            arena_push_str8f(arena, "expecting 'object', but got '%s': %.*s",
                             json_value_type_string(value->type),
                             (int)value->value.len, value->value.ptr);
        running = false;
        eof = true;
      } break;
    }
  }
  return eof;
}

static bool json_trace_profile_parse_array_format_expecting_open_bracket(
    JsonTraceProfile *self, Arena *arena, JsonParser *parser, Arena scratch) {
  bool eof = false;
  Arena scratch_ = scratch;
  JsonToken token = json_parser_parse_token(parser, &scratch_);
  switch (token.type) {
    case JSON_TOKEN_OPEN_BRACKET: {
      eof = json_trace_profile_parse_array_format(self, arena, parser, scratch);
    } break;

    case JSON_TOKEN_ERROR: {
      self->error = str8_dup(arena, token.value);
      eof = true;
    } break;

    default: {
      self->error = arena_push_str8f(arena, "expecting '[', got '%.*s'",
                                     (int)token.value.len, token.value.ptr);
      eof = true;
    } break;
  }

  return eof;
}

/// Skip tokens until next key-value pair in this object. Returns true if EOF
/// reached.
static bool json_trace_profile_skip_object_value(JsonTraceProfile *self,
                                                 Arena *arena,
                                                 JsonParser *parser,
                                                 Arena scratch) {
  bool eof = false;
  bool running = true;
  u32 open = 0;

  while (running) {
    Arena scratch_ = scratch;
    JsonToken token = json_parser_parse_token(parser, &scratch_);
    switch (token.type) {
      case JSON_TOKEN_COMMA: {
        if (open == 0) {
          running = false;
        }
      } break;

      case JSON_TOKEN_OPEN_BRACE:
      case JSON_TOKEN_OPEN_BRACKET: {
        open++;
      } break;

      case JSON_TOKEN_CLOSE_BRACE:
      case JSON_TOKEN_CLOSE_BRACKET: {
        open--;
      } break;

      case JSON_TOKEN_EOF: {
        running = false;
        eof = true;
      } break;

      case JSON_TOKEN_ERROR: {
        self->error = str8_dup(arena, token.value);
        running = false;
        eof = true;
      } break;

      default: {
      } break;
    }
  }
  return eof;
}

static void json_trace_profile_parse_object_format(JsonTraceProfile *self,
                                                   Arena *arena,
                                                   JsonParser *parser,
                                                   Arena scratch) {
  bool has_value = false;
  bool running = true;
  while (running) {
    Arena scratch_ = scratch;
    JsonToken token = json_parser_parse_token(parser, &scratch_);
    switch (token.type) {
      case JSON_TOKEN_STRING_LITERAL: {
        if (str8_eq(token.value, STR8_LIT("traceEvents"))) {
          JsonToken token = json_parser_parse_token(parser, &scratch_);
          switch (token.type) {
            case JSON_TOKEN_COLON: {
              running =
                  !json_trace_profile_parse_array_format_expecting_open_bracket(
                      self, arena, parser, scratch);
              if (running) {
                has_value = true;
              }
            } break;

            case JSON_TOKEN_ERROR: {
              self->error = str8_dup(arena, token.value);
              running = false;
            } break;
            default: {
              self->error =
                  arena_push_str8f(arena, "expecting ':', but got '%.*s'",
                                   (int)token.value.len, token.value.ptr);
              running = false;
            } break;
          }
        } else {
          running = !json_trace_profile_skip_object_value(self, arena, parser,
                                                          scratch);
        }
      } break;

      case JSON_TOKEN_COMMA: {
        if (!has_value) {
          self->error =
              arena_push_str8f(arena, "expecting 'string' or '}', but got ','");
          running = false;
        }
      } break;

      case JSON_TOKEN_CLOSE_BRACE: {
        running = false;
      } break;

      case JSON_TOKEN_ERROR: {
        self->error = str8_dup(arena, token.value);
        running = false;
      } break;

      default: {
        self->error =
            arena_push_str8f(arena, "expecting 'string' or '}', got '%.*s'",
                             (int)token.value.len, token.value.ptr);
        running = false;
      } break;
    }
  }
}

JsonTraceProfile *json_trace_profile_parse(Arena *arena, JsonParser *parser) {
  JsonTraceProfile *self = arena_push_struct(arena, JsonTraceProfile);
  self->min_time_ns = I64_MAX;
  self->max_time_ns = I64_MIN;

  Scratch scratch = scratch_begin(&arena, 1);
  Arena scratch_ = *scratch.arena;
  JsonToken token = json_parser_parse_token(parser, &scratch_);
  switch (token.type) {
    case JSON_TOKEN_OPEN_BRACE: {
      json_trace_profile_parse_object_format(self, arena, parser,
                                             *scratch.arena);
    } break;

    case JSON_TOKEN_OPEN_BRACKET: {
      json_trace_profile_parse_array_format(self, arena, parser,
                                            *scratch.arena);
    } break;

    case JSON_TOKEN_ERROR: {
      self->error = str8_dup(arena, token.value);
    } break;

    default: {
      self->error = arena_push_str8f(arena, "expecting '{' or '[', got '%.*s'",
                                     (int)token.value.len, token.value.ptr);
    } break;
  }
  scratch_end(scratch);

  self->min_time_ns = i64_min(self->min_time_ns, 0);
  self->max_time_ns = i64_max(self->max_time_ns, self->min_time_ns);

  return self;
}
