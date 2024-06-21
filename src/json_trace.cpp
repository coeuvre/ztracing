struct SampleResult {
  i64 time;
  f64 value;
  SampleResult *next;
  isize sample_count;
};

struct SeriesResult {
  Buffer name;
  SampleResult *first;
  SampleResult *last;
  isize sample_size;
};

struct CounterResult {
  Buffer name;
  HashMap series;
  isize series_size;
};

static SeriesResult *UpsertSeries(Arena *arena, CounterResult *counter,
                                  Buffer name) {
  void **value_ptr = Upsert(arena, &counter->series, name);
  SeriesResult **series = (SeriesResult **)value_ptr;
  if (!*series) {
    SeriesResult *new_series = PushStruct(arena, SeriesResult);
    new_series->name = GetKey(value_ptr);
    *series = new_series;
    counter->series_size += 1;
  }
  return *series;
}

static void AppendSample(Arena *arena, CounterResult *counter, Buffer name,
                         i64 time, f64 value) {
  SeriesResult *series = UpsertSeries(arena, counter, name);
  SampleResult *sample = PushStruct(arena, SampleResult);
  sample->time = time;
  sample->value = value;
  if (series->last) {
    series->last->next = sample;
  } else {
    series->first = sample;
  }
  series->last = sample;
  ++series->sample_size;
}

struct ProcessResult {
  i64 pid;
  HashMap counters;
  isize counter_size;
};

static CounterResult *UpsertCounterResult(Arena *arena, ProcessResult *process,
                                          Buffer name) {
  void **value_ptr = Upsert(arena, &process->counters, name);
  CounterResult **counter = (CounterResult **)value_ptr;
  if (!*counter) {
    CounterResult *new_counter = PushStruct(arena, CounterResult);
    new_counter->name = GetKey(value_ptr);
    *counter = new_counter;
    process->counter_size += 1;
  }
  return *counter;
}

struct ProfileResult {
  i64 min_time;
  i64 max_time;
  HashMap processes;
  isize process_size;
  Buffer error;
};

static ProcessResult *UpsertProcessResult(Arena *arena, ProfileResult *profile,
                                          i64 pid) {
  ProcessResult **process = (ProcessResult **)Upsert(
      arena, &profile->processes, Buffer{(u8 *)&pid, sizeof(pid)});
  if (!*process) {
    ProcessResult *new_process = PushStruct(arena, ProcessResult);
    new_process->pid = pid;
    *process = new_process;
    profile->process_size += 1;
  }
  return *process;
}

// Skip tokens until next key-value pair in this object. Returns true if EOF
// reached.
static bool SkipObjectValue(Arena *arena, Arena scratch, JsonParser *parser,
                            ProfileResult *result) {
  bool eof = false;
  u32 open = 0;
  bool done = false;
  while (!done) {
    JsonToken token = GetJsonToken(&scratch, parser);
    switch (token.type) {
      case JsonToken_Comma: {
        if (open == 0) {
          done = true;
        }
      } break;

      case JsonToken_OpenBrace:
      case JsonToken_OpenBracket: {
        open++;
      } break;

      case JsonToken_CloseBrace:
      case JsonToken_CloseBracket: {
        open--;
      } break;

      case JsonToken_Eof: {
        done = true;
        eof = true;
      } break;

      case JsonToken_Error: {
        result->error = PushBuffer(arena, token.value);
        done = true;
        eof = true;
      } break;

      default: {
      } break;
    }
  }
  return eof;
}

struct TraceEvent {
  Buffer name;
  Buffer id;
  Buffer cat;
  u8 ph;
  i64 ts;
  i64 tts;
  i64 pid;
  i64 tid;
  i64 dur;
  JsonValue *args;
};

static void ProcessTraceEvent(Arena *arena, JsonValue *value,
                              ProfileResult *profile) {
  TraceEvent trace_event = {};
  for (JsonValue *entry = value->child; entry; entry = entry->next) {
    if (Equal(entry->label, STRING_LITERAL("name"))) {
      trace_event.name = entry->value;
    } else if (Equal(entry->label, STRING_LITERAL("ph"))) {
      if (entry->value.size > 0) {
        trace_event.ph = entry->value.data[0];
      }
    } else if (Equal(entry->label, STRING_LITERAL("ts"))) {
      trace_event.ts = ConvertJsonValueToF64(entry);
    } else if (Equal(entry->label, STRING_LITERAL("tts"))) {
      trace_event.tts = ConvertJsonValueToF64(entry);
    } else if (Equal(entry->label, STRING_LITERAL("pid"))) {
      trace_event.pid = ConvertJsonValueToF64(entry);
    } else if (Equal(entry->label, STRING_LITERAL("tid"))) {
      trace_event.tid = ConvertJsonValueToF64(entry);
    } else if (Equal(entry->label, STRING_LITERAL("dur"))) {
      trace_event.dur = ConvertJsonValueToF64(entry);
    } else if (Equal(entry->label, STRING_LITERAL("args"))) {
      trace_event.args = entry;
    }
  }

  switch (trace_event.ph) {
    // Counter event
    case 'C': {
      // TODO: handle trace_event.id
      if (trace_event.args->type == JsonValue_Object) {
        ProcessResult *process =
            UpsertProcessResult(arena, profile, trace_event.pid);
        CounterResult *counter =
            UpsertCounterResult(arena, process, trace_event.name);

        i64 time = trace_event.ts * 1000;

        for (JsonValue *arg = trace_event.args->child; arg; arg = arg->next) {
          f64 value = ConvertJsonValueToF64(arg);
          AppendSample(arena, counter, arg->label, time, value);
        }

        profile->min_time = MIN(profile->min_time, time);
        profile->max_time = MAX(profile->max_time, time);
      }
    } break;

    default: {
    } break;
  }
}

static bool ParseJsonTraceEventArray(Arena *arena, Arena value_scratch,
                                     Arena token_scratch, JsonParser *parser,
                                     ProfileResult *result) {
  bool eof = false;
  JsonToken token = GetJsonToken(&token_scratch, parser);
  switch (token.type) {
    case JsonToken_OpenBracket: {
      bool done = false;
      while (!done) {
        Arena value_scratch_copy = value_scratch;
        JsonValue *value = GetJsonValue(&value_scratch_copy, token_scratch, parser);
        if (value->type != JsonValue_Error) {
          if (value->type == JsonValue_Object) {
            ProcessTraceEvent(arena, value, result);
          }

          token = GetJsonToken(&token_scratch, parser);
          switch (token.type) {
            case JsonToken_Comma: {
            } break;

            case JsonToken_CloseBracket: {
              done = true;
            } break;

            case JsonToken_Error: {
              result->error = PushBuffer(arena, token.value);
              done = true;
              eof = true;
            } break;

            default: {
              result->error = PushFormat(arena, "Unexpected token '%.*s'",
                                         token.value.size, token.value.data);
              done = true;
              eof = true;
            } break;
          }
        } else {
          result->error = PushBuffer(arena, value->value);
          done = true;
          eof = true;
        }
      }
    } break;

    case JsonToken_Error: {
      result->error = PushBuffer(arena, token.value);
      eof = true;
    } break;

    default: {
      result->error = PushFormat(arena, "Unexpected token: '%.*s'",
                                 token.value.size, token.value.data);
      eof = true;
    } break;
  }

  return eof;
}

static ProfileResult *ParseJsonTrace(Arena *arena, Arena value_scratch,
                                     Arena token_scratch, JsonParser *parser) {
  ProfileResult *result = PushStruct(arena, ProfileResult);
  result->min_time = INT64_MAX;
  result->max_time = INT64_MIN;
  JsonToken token = GetJsonToken(&token_scratch, parser);
  switch (token.type) {
    case JsonToken_OpenBrace: {
      bool done = false;
      while (!done) {
        token = GetJsonToken(&token_scratch, parser);
        switch (token.type) {
          case JsonToken_StringLiteral: {
            if (Equal(token.value, STRING_LITERAL("traceEvents"))) {
              token = GetJsonToken(&token_scratch, parser);
              if (token.type == JsonToken_Colon) {
                done = ParseJsonTraceEventArray(arena, value_scratch,
                                                token_scratch, parser, result);
              } else if (token.type == JsonToken_Error) {
                result->error = PushBuffer(arena, token.value);
              } else {
                result->error = PushFormat(arena, "expecting ':', but got %.*s",
                                           token.value.size, token.value.data);
                done = true;
              }
            } else {
              done = SkipObjectValue(arena, token_scratch, parser, result);
            }
          } break;

          case JsonToken_CloseBrace: {
            done = true;
          } break;

          case JsonToken_Error: {
            result->error = PushBuffer(arena, token.value);
            done = true;
          } break;

          default: {
            result->error = PushFormat(arena, "Unexpected token: '%.*s'",
                                       token.value.size, token.value.data);
            done = true;
          } break;
        }
      }
    } break;

    case JsonToken_Error: {
      result->error =
          PushFormat(arena, "%.*s", token.value.size, token.value.data);
    } break;

    default: {
      result->error = PushFormat(arena, "Unexpected token: '%.*s'",
                                 token.value.size, token.value.data);
    } break;
  }
  return result;
}
