enum DocumentState {
    DocumentState_Loading,
    DocumentState_Error,
    DocumentState_View,
};

struct LoadState {
    Arena *document_arena;
    OsLoadingFile *file;
    volatile isize loaded;
    Buffer error;
};

struct Document {
    Arena arena;
    Buffer path;

    DocumentState state;
    union {
        struct {
            Task *task;
            LoadState state;
        } loading;

        struct {
            Buffer message;
        } error;
    };
};

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

static void
DoLoadDocument(Task *task) {
    LoadState *state = (LoadState *)task->data;
    Buffer path = OsGetFilePath(state->file);
    INFO("Loading file %.*s ...", (int)path.size, path.data);

    u64 start_counter = OsGetPerformanceCounter();

    Buffer buf = PushBufferNoZero(&task->arena, 4096);
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

static Document *
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

static void
UnloadDocument(Document *document) {
    if (document->state == DocumentState_Loading) {
        CancelTask(document->loading.task);
        WaitLoading(document);
    }
    ClearArena(&document->arena);
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
