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
  Str name;
  Str id;
  Str cat;
  u8 ph;
  f64 ts;
  f64 tts;
  i64 pid;
  i64 tid;
  f64 dur;
  JsonValue *args;
} JsonTraceEvent;

static JsonTraceSeries *UpsertSeries(JsonTraceCounter *self, Arena *arena,
                                     Str name) {
  JsonTraceSeries *series;
  if (HashTrie_Upsert(&self->series, name, &series, arena)) {
    *series = (JsonTraceSeries){
        .name = HashTrie_GetKey(series),
    };
    self->series_count += 1;
  }
  return series;
}

static void AddSample(JsonTraceCounter *self, Arena *arena, Str name, i64 time,
                      f64 value) {
  JsonTraceSeries *series = UpsertSeries(self, arena, name);
  JsonTraceSample *sample = Arena_PushStruct(arena, JsonTraceSample);
  *sample = (JsonTraceSample){
      .time = time,
      .value = value,
  };
  DLL_APPEND(series->first, series->last, sample, prev, next);
  series->sample_count++;

  self->min_value = MinF64(self->min_value, value);
  self->max_value = MaxF64(self->max_value, value);
}

static JsonTraceSpan *CreateSpan(Arena *arena, Str name, Str cat,
                                 i64 begin_time_us) {
  JsonTraceSpan *span = Arena_PushStruct(arena, JsonTraceSpan);
  *span = (JsonTraceSpan){
      .name = Str_Dup(arena, name),
      .cat = Str_Dup(arena, cat),
      .begin_time_ns = begin_time_us,
  };
  return span;
}

static void AddOpenSpan(JsonTraceThread *self, Arena *arena, Str name, Str cat,
                        i64 begin_time_us) {
  JsonTraceSpan *span = CreateSpan(arena, name, cat, begin_time_us);
  DLL_APPEND(self->first_open_span, self->last_open_span, span, prev, next);
}

static void AddSpan(JsonTraceThread *self, Arena *arena, Str name, Str cat,
                    i64 begin_time_us, i64 end_time_us) {
  JsonTraceSpan *span = CreateSpan(arena, name, cat, begin_time_us);
  span->end_time_ns = end_time_us;
  DLL_APPEND(self->first_span, self->last_span, span, prev, next);
  self->span_count += 1;
}

static JsonTraceCounter *UpsertCounter(JsonTraceProcess *self, Arena *arena,
                                       Str name) {
  JsonTraceCounter *counter;
  if (HashTrie_Upsert(&self->counters, name, &counter, arena)) {
    *counter = (JsonTraceCounter){
        .name = HashTrie_GetKey(counter),
    };
    self->counter_count += 1;
  }
  return counter;
}

static JsonTraceThread *UpsertThread(JsonTraceProcess *self, Arena *arena,
                                     i64 tid) {
  Str key = (Str){(char *)&tid, sizeof(tid)};
  JsonTraceThread *thread;
  if (HashTrie_Upsert(&self->threads, key, &thread, arena)) {
    *thread = (JsonTraceThread){
        .tid = tid,
    };
    self->thread_count += 1;
  }
  return thread;
}

static JsonTraceProcess *UpsertProcess(JsonTraceProfile *self, Arena *arena,
                                       i64 pid) {
  Str key = (Str){(char *)&pid, sizeof(pid)};
  JsonTraceProcess *process;
  if (HashTrie_Upsert(&self->processes, key, &process, arena)) {
    *process = (JsonTraceProcess){
        .pid = pid,
    };
    self->process_count += 1;
  }
  return process;
}

static void ProcessTraceEvent(JsonTraceProfile *self, Arena *arena,
                              JsonValue *value) {
  JsonTraceEvent trace_event = {0};
  for (JsonValue *entry = value->first; entry; entry = entry->next) {
    if (Str_IsEqual(entry->label, STR_C("name"))) {
      trace_event.name = entry->value;
    } else if (Str_IsEqual(entry->label, STR_C("cat"))) {
      trace_event.cat = entry->value;
    } else if (Str_IsEqual(entry->label, STR_C("ph"))) {
      if (entry->value.len > 0) {
        trace_event.ph = entry->value.ptr[0];
      }
    } else if (Str_IsEqual(entry->label, STR_C("ts"))) {
      trace_event.ts = JsonValue_ToF64(entry);
    } else if (Str_IsEqual(entry->label, STR_C("tts"))) {
      trace_event.tts = JsonValue_ToF64(entry);
    } else if (Str_IsEqual(entry->label, STR_C("pid"))) {
      trace_event.pid = (i64)JsonValue_ToF64(entry);
    } else if (Str_IsEqual(entry->label, STR_C("tid"))) {
      trace_event.tid = (i64)JsonValue_ToF64(entry);
    } else if (Str_IsEqual(entry->label, STR_C("dur"))) {
      trace_event.dur = JsonValue_ToF64(entry);
    } else if (Str_IsEqual(entry->label, STR_C("args"))) {
      trace_event.args = entry;
    }
  }

  i64 time = (i64)trace_event.ts * 1000;

  switch (trace_event.ph) {
    // Counter event
    case 'C': {
      JsonTraceProcess *process = UpsertProcess(self, arena, trace_event.pid);
      JsonTraceCounter *counter =
          UpsertCounter(process, arena, trace_event.name);
      if (trace_event.args->type == JsonValueType_Object) {
        for (JsonValue *arg = trace_event.args->first; arg; arg = arg->next) {
          f64 value = JsonValue_ToF64(arg);
          AddSample(counter, arena, arg->label, time, value);
        }
      }
    } break;

    // Duration event
    case 'B': {
      JsonTraceProcess *process = UpsertProcess(self, arena, trace_event.pid);
      JsonTraceThread *thread = UpsertThread(process, arena, trace_event.tid);

      // TODO: Interning string name and cat.

      AddOpenSpan(thread, arena, trace_event.name, trace_event.cat, time);
    } break;

    case 'E': {
      JsonTraceProcess *process = UpsertProcess(self, arena, trace_event.pid);
      JsonTraceThread *thread = UpsertThread(process, arena, trace_event.tid);

      if (thread->last_open_span) {
        JsonTraceSpan *span = thread->last_open_span;
        DLL_REMOVE(thread->first_open_span, thread->last_open_span, span, prev,
                   next);

        span->end_time_ns = time;

        // TODO: Interning string name and cat.

        if (Str_IsEqual(span->name, trace_event.name) != 0) {
          span->name = Str_Dup(arena, trace_event.name);
        }
        if (Str_IsEqual(span->cat, trace_event.cat) != 0) {
          span->cat = Str_Dup(arena, trace_event.cat);
        }

        // TODO: handle trace_event.args.

        DLL_APPEND(thread->first_span, thread->last_span, span, prev, next);
        thread->span_count += 1;
      }
    } break;

    // Complete event and Instant event
    case 'i':
    case 'X': {
      JsonTraceProcess *process = UpsertProcess(self, arena, trace_event.pid);
      JsonTraceThread *thread = UpsertThread(process, arena, trace_event.tid);

      // TODO: Interning string name and cat.

      AddSpan(thread, arena, trace_event.name, trace_event.cat, time,
              time + (i64)trace_event.dur * 1000);

      // TODO: handle trace_event.args.
    } break;

    // Metadata event
    case 'M': {
      if (Str_IsEqual(trace_event.name, STR_C("thread_name"))) {
        if (trace_event.args->type == JsonValueType_Object) {
          for (JsonValue *value = trace_event.args->first; value;
               value = value->next) {
            if (Str_IsEqual(value->label, STR_C("name"))) {
              JsonTraceProcess *process =
                  UpsertProcess(self, arena, trace_event.pid);
              JsonTraceThread *thread =
                  UpsertThread(process, arena, trace_event.tid);
              thread->name = Str_Dup(arena, value->value);
              break;
            }
          }
        }
      } else if (Str_IsEqual(trace_event.name, STR_C("thread_sort_index"))) {
        if (trace_event.args->type == JsonValueType_Object) {
          for (JsonValue *value = trace_event.args->first; value;
               value = value->next) {
            if (Str_IsEqual(value->label, STR_C("sort_index"))) {
              JsonTraceProcess *process =
                  UpsertProcess(self, arena, trace_event.pid);
              JsonTraceThread *thread =
                  UpsertThread(process, arena, trace_event.tid);
              thread->sort_index = i64_Some((i64)JsonValue_ToF64(value));
              break;
            }
          }
        }
      }
    } break;

    default: {
    } break;
  }

  self->min_time_ns = MinI64(self->min_time_ns, time);
  self->max_time_ns = MaxI64(self->max_time_ns, time);
}

static bool ParseArrayFormat(JsonTraceProfile *self, Arena *arena,
                             JsonParser *parser, Arena scratch) {
  bool eof = false;
  bool running = true;
  while (running) {
    Arena scratch_ = scratch;
    JsonValue *value = JsonParser_ParseValue(parser, &scratch_);
    switch (value->type) {
      case JsonValueType_Object: {
        ProcessTraceEvent(self, arena, value);

        JsonToken token = JsonParser_ParseToken(parser, &scratch_);
        switch (token.type) {
          case JsonTokenType_Comma: {
          } break;

          case JsonTokenType_CloseBracket: {
            running = false;
          } break;

          case JsonTokenType_Error: {
            self->error = Str_Dup(arena, token.value);
            running = false;
            eof = true;
          } break;

          default: {
            self->error = Str_Format(arena, "expecting ',' or ']', got '%.*s'",
                                     (int)token.value.len, token.value.ptr);
            running = false;
            eof = true;
          } break;
        }
      } break;

      case JsonValueType_Error: {
        self->error = Str_Dup(arena, value->value);
        running = false;
        eof = true;
      } break;

      default: {
        self->error =
            Str_Format(arena, "expecting 'object', but got '%s': %.*s",
                       JsonValueType_ToString(value->type),
                       (int)value->value.len, value->value.ptr);
        running = false;
        eof = true;
      } break;
    }
  }
  return eof;
}

static bool ExpectingOpenBracket(JsonTraceProfile *self, Arena *arena,
                                 JsonParser *parser, Arena scratch) {
  bool eof = false;
  Arena scratch_ = scratch;
  JsonToken token = JsonParser_ParseToken(parser, &scratch_);
  switch (token.type) {
    case JsonTokenType_OpenBracket: {
      eof = ParseArrayFormat(self, arena, parser, scratch);
    } break;

    case JsonTokenType_Error: {
      self->error = Str_Dup(arena, token.value);
      eof = true;
    } break;

    default: {
      self->error = Str_Format(arena, "expecting '[', got '%.*s'",
                               (int)token.value.len, token.value.ptr);
      eof = true;
    } break;
  }

  return eof;
}

/// Skip tokens until next key-value pair in this object. Returns true if EOF
/// reached.
static bool SkipObjectValue(JsonTraceProfile *self, Arena *arena,
                            JsonParser *parser, Arena scratch) {
  bool eof = false;
  bool running = true;
  u32 open = 0;

  while (running) {
    Arena scratch_ = scratch;
    JsonToken token = JsonParser_ParseToken(parser, &scratch_);
    switch (token.type) {
      case JsonTokenType_Comma: {
        if (open == 0) {
          running = false;
        }
      } break;

      case JsonTokenType_OpenBrace:
      case JsonTokenType_OpenBracket: {
        open++;
      } break;

      case JsonTokenType_CloseBrace:
      case JsonTokenType_CloseBracket: {
        open--;
        if (open == 0) {
          running = false;
        }
      } break;

      case JsonTokenType_Eof: {
        running = false;
        eof = true;
      } break;

      case JsonTokenType_Error: {
        self->error = Str_Dup(arena, token.value);
        running = false;
        eof = true;
      } break;

      default: {
      } break;
    }
  }
  return eof;
}

static void ParseObjectFormat(JsonTraceProfile *self, Arena *arena,
                              JsonParser *parser, Arena scratch) {
  bool has_value = false;
  bool running = true;
  while (running) {
    Arena scratch_ = scratch;
    JsonToken token = JsonParser_ParseToken(parser, &scratch_);
    switch (token.type) {
      case JsonTokenType_StringLiteral: {
        if (Str_IsEqual(token.value, STR_C("traceEvents"))) {
          JsonToken token = JsonParser_ParseToken(parser, &scratch_);
          switch (token.type) {
            case JsonTokenType_Colon: {
              running = !ExpectingOpenBracket(self, arena, parser, scratch);
              if (running) {
                has_value = true;
              }
            } break;

            case JsonTokenType_Error: {
              self->error = Str_Dup(arena, token.value);
              running = false;
            } break;
            default: {
              self->error = Str_Format(arena, "expecting ':', but got '%.*s'",
                                       (int)token.value.len, token.value.ptr);
              running = false;
            } break;
          }
        } else {
          running = !SkipObjectValue(self, arena, parser, scratch);
          if (running) {
            has_value = true;
          }
        }
      } break;

      case JsonTokenType_Comma: {
        if (!has_value) {
          self->error =
              Str_Format(arena, "expecting 'string' or '}', but got ','");
          running = false;
        }
      } break;

      case JsonTokenType_CloseBrace: {
        running = false;
      } break;

      case JsonTokenType_Error: {
        self->error = Str_Dup(arena, token.value);
        running = false;
      } break;

      default: {
        self->error = Str_Format(arena, "expecting 'string' or '}', got '%.*s'",
                                 (int)token.value.len, token.value.ptr);
        running = false;
      } break;
    }
  }
}

JsonTraceProfile *JsonTraceProfile_Parse(Arena *arena, JsonParser *parser) {
  JsonTraceProfile *self = Arena_PushStruct(arena, JsonTraceProfile);
  *self = (JsonTraceProfile){
      .min_time_ns = I64_MAX,
      .max_time_ns = I64_MIN,
  };

  Arena *scratch = Arena_Create(&(ArenaOptions){
      .allocator = Arena_GetAllocator(arena),
  });
  JsonToken token = JsonParser_ParseToken(parser, scratch);
  switch (token.type) {
    case JsonTokenType_OpenBrace: {
      ParseObjectFormat(self, arena, parser, *scratch);
    } break;

    case JsonTokenType_OpenBracket: {
      ParseArrayFormat(self, arena, parser, *scratch);
    } break;

    case JsonTokenType_Error: {
      self->error = Str_Dup(arena, token.value);
    } break;

    default: {
      self->error = Str_Format(arena, "expecting '{' or '[', got '%.*s'",
                               (int)token.value.len, token.value.ptr);
    } break;
  }
  Arena_Destroy(scratch);

  self->min_time_ns = MinI64(self->min_time_ns, 0);
  self->max_time_ns = MaxI64(self->max_time_ns, self->min_time_ns);

  return self;
}
