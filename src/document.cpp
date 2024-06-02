#include "document.h"

#include <iostream>
#include <zlib.h>

#include "json.h"
#include "ui.h"

static voidpf
ZLibAlloc(voidpf opaque, uInt items, uInt size) {
    return AllocateMemoryNoZero(items * size);
}

static void
ZLibFree(voidpf opaque, voidpf address) {
    DeallocateMemory(address);
}

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
    usize zstream_buf_size;
    u8 *zstream_buf;
};

static Load *
BeginLoad(Arena *arena, OsLoadingFile *file) {
    TempArena temp_arena = BeginTempArena(arena);
    Load *load = PushStruct(arena, Load);
    load->file = file;
    load->temp_arena = temp_arena;
    load->zstream.zalloc = ZLibAlloc;
    load->zstream.zfree = ZLibFree;
    return load;
}

static u32
LoadIntoBuffer(Load *load, Buffer buf) {
    u32 nread = 0;
    bool done = false;
    while (!done) {
        switch (load->progress) {
        case LoadProgress_Init: {
            nread = OsLoadingFileNext(load->file, buf.data, buf.size);
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
            nread = OsLoadingFileNext(load->file, buf.data, buf.size);
            if (nread == 0) {
                load->progress = LoadProgress_Done;
            }
            done = true;
        } break;

        case LoadProgress_Gz: {
            if (load->zstream.avail_in == 0) {
                load->zstream.avail_in = OsLoadingFileNext(
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
EndLoad(Load *load) {
    if (load->zstream_buf) {
        inflateEnd(&load->zstream);
    }
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
        state->loaded += result.size;
    }
    return result;
}

// Skip tokens until next key-value pair in this object. Returns true if EOF
// reached.
static bool
SkipObjectValue(JsonParser *parser) {
    bool eof = false;
    u32 open = 0;
    bool done = false;
    while (!done) {
        JsonToken token = GetJsonToken(parser);
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
            ERROR("%.*s", token.value.size, token.value.data);
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
SkipToken(JsonParser *parser, JsonTokenType type) {
    JsonToken token = GetJsonToken(parser);
    return token.type == type;
}

static void
ProcessTraceEvent(JsonValue *value) {
    for (JsonValue *entry = value->child; entry; entry = entry->next) {
        if (AreEqual(entry->label, STRING_LITERAL("name"))) {
        } else if (AreEqual(entry->label, STRING_LITERAL("ph"))) {
        } else if (AreEqual(entry->label, STRING_LITERAL("ts"))) {
        } else if (AreEqual(entry->label, STRING_LITERAL("pid"))) {
        } else if (AreEqual(entry->label, STRING_LITERAL("tid"))) {
        } else if (AreEqual(entry->label, STRING_LITERAL("args"))) {
        }
    }
}

static bool
ParseJsonTraceEventArray(JsonParser *parser) {
    bool eof = false;
    JsonToken token = GetJsonToken(parser);
    switch (token.type) {
    case JsonToken_OpenBracket: {
        bool done = false;
        while (!done) {
            JsonValue *value = GetJsonValue(parser);
            if (value) {
                if (value->type == JsonValue_Object) {
                    ProcessTraceEvent(value);
                }

                token = GetJsonToken(parser);
                switch (token.type) {
                case JsonToken_Comma: {
                } break;

                case JsonToken_CloseBracket: {
                    done = true;
                } break;

                default: {
                    ERROR(
                        "Unexpected token '%.*s'",
                        token.value.size,
                        token.value.data
                    );
                    done = true;
                    eof = true;
                } break;
                }
            } else {
                Buffer error = GetJsonError(parser);
                ERROR("%.*s", error.size, error.data);
                done = true;
                eof = true;
            }
        }
    } break;

    case JsonToken_Error: {
        ERROR("%.*s", token.value.size, token.value.data);
        eof = true;
    } break;

    default: {
        ERROR("Unexpected token: '%.*s'", token.value.size, token.value.data);
        eof = true;
    } break;
    }

    return eof;
}

static void
ParseJsonTrace(JsonParser *parser) {
    JsonToken token = GetJsonToken(parser);
    switch (token.type) {
    case JsonToken_OpenBrace: {
        bool done = false;
        while (!done) {
            token = GetJsonToken(parser);
            switch (token.type) {
            case JsonToken_StringLiteral: {
                if (AreEqual(token.value, STRING_LITERAL("traceEvents"))) {
                    if (SkipToken(parser, JsonToken_Colon)) {
                        done = ParseJsonTraceEventArray(parser);
                    } else {
                        ERROR(
                            "Unexpected token: '%.*s'",
                            token.value.size,
                            token.value.data
                        );
                        done = true;
                    }
                } else {
                    done = SkipObjectValue(parser);
                }
            } break;

            case JsonToken_CloseBrace: {
                done = true;
            } break;

            case JsonToken_Error: {
                ERROR("%.*s", token.value.size, token.value.data);
                done = true;
            } break;

            default: {
                ERROR(
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
        ERROR("%.*s", token.value.size, token.value.data);
    } break;

    default: {
        ERROR("Unexpected token: '%.*s'", token.value.size, token.value.data);
    } break;
    }
}

static void
DoLoadDocument(Task *task) {
    LoadState *state = (LoadState *)task->data;
    INFO("Loading file %s ...", OsLoadingFileGetPath(state->file));

    u64 start_counter = OsGetPerformanceCounter();

    Load *load = BeginLoad(&task->arena, state->file);
    usize size = 4096;
    Buffer buf = PushBuffer(&task->arena, size);

    GetJsonInputData get_json_input_data = {};
    get_json_input_data.task = task;
    get_json_input_data.load = load;
    get_json_input_data.buf = buf;

    JsonParser *parser =
        BeginJsonParse(&task->arena, GetJsonInput, &get_json_input_data);
    ParseJsonTrace(parser);
    EndJsonParse(parser);

    EndLoad(load);

    u64 end_counter = OsGetPerformanceCounter();
    f32 seconds =
        (f64)(end_counter - start_counter) / (f64)OsGetPerformanceFrequency();
    INFO(
        "Loaded %.1f MB in %.2f s (%.1f MB/s).",
        state->loaded / 1024.0f / 1024.0f,
        seconds,
        state->loaded / seconds / 1024.0f / 1024.0f
    );
}

static void
WaitLoading(Document *document) {
    ASSERT(document->state == DocumentState_Loading);
    WaitTask(document->loading.task);
    OsLoadingFileClose(document->loading.state.file);
}

static Document *
LoadDocument(OsLoadingFile *file) {
    Document *document = BootstrapPushStruct(Document, arena);
    document->path =
        PushFormatZ(&document->arena, "%s", OsLoadingFileGetPath(file));
    document->state = DocumentState_Loading;
    document->loading.task =
        CreateTask(DoLoadDocument, &document->loading.state);
    document->loading.state.file = file;
    return document;
}

static void
UnloadDocument(Document *document) {
    if (document->state == DocumentState_Loading) {
        CancelTask(document->loading.task);
        WaitLoading(document);
    }
    Clear(&document->arena);
}

static void
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
            document->state = DocumentState_View;
        }
    } break;

    case DocumentState_View: {

    } break;

    default: {
        UNREACHABLE;
    } break;
    }
}
