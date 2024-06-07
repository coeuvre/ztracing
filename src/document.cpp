#include "document.h"

#include <zlib.h>

#include "core.h"
#include "json.h"
#include "memory.h"
#include "task.h"
#include "ui.h"

static voidpf
ZLibAlloc(voidpf opaque, uInt items, uInt size) {
    Arena *arena = (Arena *)opaque;
    return PushSize(arena, items * size);
}

static void
ZLibFree(voidpf opaque, voidpf address) {}

enum LoadProgress {
    LoadProgress_Init,
    LoadProgress_Regular,
    LoadProgress_Gz,
    LoadProgress_Done,
};

struct Load {
    TempArena temp_arena;
    OsLoadingFile *file;

    LoadProgress progress;

    z_stream zstream;
    isize zstream_buf_size;
    u8 *zstream_buf;
};

static Load *
BeginLoadFile(Arena *arena, OsLoadingFile *file) {
    TempArena temp_arena = BeginTempArena(arena);
    Load *load = PushStruct(arena, Load);
    load->file = file;
    load->temp_arena = temp_arena;
    load->zstream.zalloc = ZLibAlloc;
    load->zstream.zfree = ZLibFree;
    load->zstream.opaque = arena;
    return load;
}

static u32
LoadIntoBuffer(Load *load, Buffer buf) {
    u32 nread = 0;
    bool done = false;
    while (!done) {
        switch (load->progress) {
        case LoadProgress_Init: {
            nread = OsReadFile(load->file, buf.data, buf.size);
            if (nread >= 2 && (buf.data[0] == 0x1F && buf.data[1] == 0x8B)) {
                int zret = inflateInit2(&load->zstream, MAX_WBITS | 32);
                // TODO: Error handling.
                ASSERT(zret == Z_OK);
                load->zstream_buf_size = MAX(4096, buf.size);
                load->zstream_buf = PushArray(
                    load->temp_arena.arena,
                    u8,
                    load->zstream_buf_size
                );
                CopyMemory(load->zstream_buf, buf.data, nread);
                load->zstream.avail_in = nread;
                load->zstream.next_in = load->zstream_buf;
                load->progress = LoadProgress_Gz;
            } else {
                load->progress = LoadProgress_Regular;
                done = true;
            }
        } break;

        case LoadProgress_Regular: {
            nread = OsReadFile(load->file, buf.data, buf.size);
            if (nread == 0) {
                load->progress = LoadProgress_Done;
            }
            done = true;
        } break;

        case LoadProgress_Gz: {
            if (load->zstream.avail_in == 0) {
                load->zstream.avail_in = OsReadFile(
                    load->file,
                    load->zstream_buf,
                    load->zstream_buf_size
                );
                load->zstream.next_in = load->zstream_buf;
            }

            if (load->zstream.avail_in != 0) {
                load->zstream.avail_out = buf.size;
                load->zstream.next_out = buf.data;

                int zret = inflate(&load->zstream, Z_NO_FLUSH);
                switch (zret) {
                case Z_OK: {
                } break;

                case Z_STREAM_END: {
                    load->progress = LoadProgress_Done;
                } break;

                default: {
                    // TODO: Error handling.
                    ABORT("inflate returned %d", zret);
                } break;
                }

                nread = buf.size - load->zstream.avail_out;
            } else {
                load->progress = LoadProgress_Done;
            }

            done = true;
        } break;

        case LoadProgress_Done: {
            done = true;
        } break;

        default:
            UNREACHABLE;
        }
    }
    return nread;
}

static void
EndLoadFile(Load *load) {
    EndTempArena(load->temp_arena);
}

struct GetJsonInputData {
    Task *task;
    Load *load;
    Buffer buf;
};

static Buffer
GetJsonInput(void *data_) {
    Buffer result = {};
    GetJsonInputData *data = (GetJsonInputData *)data_;
    if (!IsTaskCancelled(data->task)) {
        result.size = LoadIntoBuffer(data->load, data->buf);
        result.data = data->buf.data;

        LoadState *state = (LoadState *)data->task->data;
        isize loaded = state->loaded;
        loaded += result.size;
        state->loaded = loaded;
    }
    return result;
}

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
    isize series_count;
};

static SeriesResult *
UpsertSeries(Arena *arena, CounterResult *counter, Buffer name) {
    void **value_ptr = Upsert(arena, &counter->series, name);
    SeriesResult **series = (SeriesResult **)value_ptr;
    if (!*series) {
        SeriesResult *new_series = PushStruct(arena, SeriesResult);
        new_series->name = GetKey(value_ptr);
        *series = new_series;
        counter->series_count += 1;
    }
    return *series;
}

static void
AppendSample(
    Arena *arena,
    CounterResult *counter,
    Buffer name,
    i64 time,
    f64 value
) {
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

static CounterResult *
UpsertCounterResult(Arena *arena, ProcessResult *process, Buffer name) {
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
    HashMap processes;
    isize process_size;
};

static ProcessResult *
UpsertProcessResult(Arena *arena, ProfileResult *profile, i64 pid) {
    ProcessResult **process = (ProcessResult **)
        Upsert(arena, &profile->processes, Buffer{(u8 *)&pid, sizeof(pid)});
    if (!*process) {
        ProcessResult *new_process = PushStruct(arena, ProcessResult);
        new_process->pid = pid;
        *process = new_process;
        profile->process_size += 1;
    }
    return *process;
}

struct ParseResult {
    Buffer error;
    ProfileResult profile;
};

// Skip tokens until next key-value pair in this object. Returns true if EOF
// reached.
static bool
SkipObjectValue(Arena *arena, JsonParser *parser, ParseResult *result) {
    bool eof = false;
    u32 open = 0;
    bool done = false;
    while (!done) {
        JsonToken token = GetJsonToken(&parser->tokenizer);
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

static bool
SkipToken(
    JsonParser *parser,
    JsonTokenType type,
    Arena *arena,
    ParseResult *result
) {
    bool skipped = true;
    JsonToken token = GetJsonToken(&parser->tokenizer);
    if (token.type != type) {
        skipped = false;
        if (token.type == JsonToken_Error) {
            result->error = PushBuffer(arena, token.value);
        } else {
            result->error = PushFormat(
                arena,
                "Unexpected token: '%.*s'",
                token.value.size,
                token.value.data
            );
        }
    }
    return skipped;
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

static void
ProcessTraceEvent(Arena *arena, JsonValue *value, ProfileResult *profile) {
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

            for (JsonValue *arg = trace_event.args->child; arg;
                 arg = arg->next) {
                f64 value = ConvertJsonValueToF64(arg);
                AppendSample(arena, counter, arg->label, trace_event.ts, value);
            }
        }
    } break;

    default: {
    } break;
    }
}

static bool
ParseJsonTraceEventArray(
    Arena *arena,
    JsonParser *parser,
    ParseResult *result
) {
    bool eof = false;
    JsonToken token = GetJsonToken(&parser->tokenizer);
    switch (token.type) {
    case JsonToken_OpenBracket: {
        bool done = false;
        while (!done) {
            JsonValue *value = GetJsonValue(parser);
            if (value) {
                if (value->type == JsonValue_Object) {
                    ProcessTraceEvent(arena, value, &result->profile);
                }

                token = GetJsonToken(&parser->tokenizer);
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
                    result->error = PushFormat(
                        arena,
                        "Unexpected token '%.*s'",
                        token.value.size,
                        token.value.data
                    );
                    done = true;
                    eof = true;
                } break;
                }
            } else {
                result->error = PushBuffer(arena, GetJsonError(parser));
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
        result->error = PushFormat(
            arena,
            "Unexpected token: '%.*s'",
            token.value.size,
            token.value.data
        );
        eof = true;
    } break;
    }

    return eof;
}

static ParseResult
ParseJsonTrace(Arena *arena, JsonParser *parser) {
    ParseResult result = {};
    JsonToken token = GetJsonToken(&parser->tokenizer);
    switch (token.type) {
    case JsonToken_OpenBrace: {
        bool done = false;
        while (!done) {
            token = GetJsonToken(&parser->tokenizer);
            switch (token.type) {
            case JsonToken_StringLiteral: {
                if (Equal(token.value, STRING_LITERAL("traceEvents"))) {
                    if (SkipToken(parser, JsonToken_Colon, arena, &result)) {
                        done = ParseJsonTraceEventArray(arena, parser, &result);
                    } else {
                        done = true;
                    }
                } else {
                    done = SkipObjectValue(arena, parser, &result);
                }
            } break;

            case JsonToken_CloseBrace: {
                done = true;
            } break;

            case JsonToken_Error: {
                result.error = PushBuffer(arena, token.value);
                done = true;
            } break;

            default: {
                result.error = PushFormat(
                    arena,
                    "Unexpected token: '%.*s'",
                    token.value.size,
                    token.value.data
                );
                done = true;
            } break;
            }
        }
    } break;

    case JsonToken_Error: {
        result.error =
            PushFormat(arena, "%.*s", token.value.size, token.value.data);
    } break;

    default: {
        result.error = PushFormat(
            arena,
            "Unexpected token: '%.*s'",
            token.value.size,
            token.value.data
        );
    } break;
    }
    return result;
}

struct SeriesValue {
    i64 time;
    f64 value;
};

struct Series {
    Buffer name;
    SeriesValue *values;
    isize value_size;
};

struct Counter {
    Series *series;
    isize series_size;
};

struct Process {
    i64 pid;
    Counter *counters;
    isize counter_size;
};

struct Profile {
    Process *processes;
    isize process_size;
};

static void
BuildCounter(
    Arena *perm,
    Arena *scratch,
    CounterResult *counter_result,
    Counter *counter
) {
    // TODO
    INFO("Counter: %.*s", counter_result->name.size, counter_result->name.data);
}

static Profile *
BuildProfile(Arena *perm, Arena *scratch, ProfileResult *profile_result) {
    Profile *profile = PushStruct(perm, Profile);
    profile->process_size = profile_result->process_size;
    profile->processes = PushArray(perm, Process, profile->process_size);

    HashMapIter process_result_iter =
        IterateHashMap(scratch, &profile_result->processes);

    for (isize process_index = 0; process_index < profile->process_size;
         ++process_index) {
        HashNode *process_result_node = GetNext(&process_result_iter);
        ASSERT(process_result_node);
        ProcessResult *process_result =
            (ProcessResult *)process_result_node->value;

        Process *process = profile->processes + process_index;
        process->pid = process->pid;
        process->counter_size = process_result->counter_size;
        process->counters = PushArray(perm, Counter, process->counter_size);

        HashMapIter counter_result_iter =
            IterateHashMap(scratch, &process_result->counters);
        for (isize counter_index = 0; counter_index < process->counter_size;
             ++counter_index) {
            HashNode *counter_result_node = GetNext(&counter_result_iter);
            ASSERT(counter_result_node);
            CounterResult *counter_result =
                (CounterResult *)counter_result_node->value;
            Counter *counter = process->counters + counter_index;
            BuildCounter(perm, scratch, counter_result, counter);
        }
    }

    return profile;
}

static void
DoLoadDocument(Task *task) {
    LoadState *state = (LoadState *)task->data;
    Buffer path = OsGetFilePath(state->file);
    INFO("Loading file %.*s ...", (int)path.size, path.data);

    u64 start_counter = OsGetPerformanceCounter();

    isize size = 4096;
    Buffer buf = PushBuffer(&task->arena, size);
    Load *load = BeginLoadFile(&task->arena, state->file);

    GetJsonInputData get_json_input_data = {};
    get_json_input_data.task = task;
    get_json_input_data.load = load;
    get_json_input_data.buf = buf;

    Arena json_arena = {};
    Arena parse_arena = {};
    JsonParser *parser =
        BeginJsonParse(&json_arena, GetJsonInput, &get_json_input_data);
    ParseResult parse_result = ParseJsonTrace(&parse_arena, parser);
    EndJsonParse(parser);

    EndLoadFile(load);
    ClearArena(&json_arena);

    if (!IsTaskCancelled(task)) {
        if (parse_result.error.data) {
            state->error =
                PushBuffer(state->document_arena, parse_result.error);
        } else
            BuildProfile(
                state->document_arena,
                &task->arena,
                &parse_result.profile
            );
    }

    ClearArena(&parse_arena);

    u64 end_counter = OsGetPerformanceCounter();
    f32 seconds =
        (f64)(end_counter - start_counter) / (f64)OsGetPerformanceFrequency();

    OsCloseFile(state->file);

    if (!IsTaskCancelled(task)) {
        if (state->error.data) {
            ERROR("%s", state->error.data);
        } else {
            INFO(
                "Loaded %.1f MB in %.2f s (%.1f MB/s).",
                state->loaded / 1024.0f / 1024.0f,
                seconds,
                state->loaded / seconds / 1024.0f / 1024.0f
            );
        }
    }
}

static void
WaitLoading(Document *document) {
    ASSERT(document->state == DocumentState_Loading);
    WaitTask(document->loading.task);

    LoadState *state = &document->loading.state;

    if (state->error.data) {
        Buffer message = state->error;
        document->state = DocumentState_Error;
        document->error.message = message;
    } else {
        document->state = DocumentState_View;
    }
}

Document *
LoadDocument(OsLoadingFile *file) {
    Document *document = BootstrapPushStruct(Document, arena);
    document->path = PushBuffer(&document->arena, OsGetFilePath(file));
    document->state = DocumentState_Loading;
    document->loading.task =
        CreateTask(DoLoadDocument, &document->loading.state);
    document->loading.state.document_arena = &document->arena;
    document->loading.state.file = file;
    return document;
}

void
UnloadDocument(Document *document) {
    if (document->state == DocumentState_Loading) {
        CancelTask(document->loading.task);
        WaitLoading(document);
    }
    ClearArena(&document->arena);
}

void
RenderDocument(Document *document, Arena *frame_arena) {
    switch (document->state) {
    case DocumentState_Loading: {
        {
            char *text = PushFormatZ(
                frame_arena,
                "Loading %.1f MB ...",
                document->loading.state.loaded / 1024.0f / 1024.0f
            );
            Vec2 window_size = ImGui::GetWindowSize();
            Vec2 text_size = ImGui::CalcTextSize(text);
            ImGui::SetCursorPos((window_size - text_size) / 2.0f);
            ImGui::Text("%s", text);
        }

        if (IsTaskDone(document->loading.task)) {
            WaitLoading(document);
        }
    } break;

    case DocumentState_Error: {
        ImGui::Text(
            "Failed to load \"%.*s\": %s",
            (int)document->path.size,
            document->path.data,
            document->error.message.data
        );
    } break;

    case DocumentState_View: {

    } break;

    default: {
        UNREACHABLE;
    } break;
    }
}
