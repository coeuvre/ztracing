#include "document.h"

#include "ui.h"

#include <iostream>
#include <zlib.h>

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
LoadIntoBuffer(Load *load, u8 *buf, usize size) {
    u32 nread = 0;
    bool done = false;
    while (!done) {
        switch (load->progress) {
        case LoadProgress_Init: {
            nread = OsLoadingFileNext(load->file, buf, size);
            if (nread >= 2 && (buf[0] == 0x1F && buf[1] == 0x8B)) {
                int zret = inflateInit2(&load->zstream, MAX_WBITS | 32);
                // TODO: Error handling.
                ASSERT(zret == Z_OK, "");
                load->zstream_buf_size = MAX(4096, size);
                load->zstream_buf = PushArray(
                    load->temp_arena.arena,
                    u8,
                    load->zstream_buf_size
                );
                memcpy(load->zstream_buf, buf, size);
                load->zstream.avail_in = size;
                load->zstream.next_in = load->zstream_buf;
                load->progress = LoadProgress_Gz;
            } else {
                load->progress = LoadProgress_Regular;
                done = true;
            }
        } break;

        case LoadProgress_Regular: {
            nread = OsLoadingFileNext(load->file, buf, size);
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
                load->zstream.avail_out = size;
                load->zstream.next_out = buf;

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

                nread = size - load->zstream.avail_out;
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

static void
DoLoadDocument(Task *task) {
    LoadState *state = (LoadState *)task->data;
    INFO("Loading file %s ...", OsLoadingFileGetPath(state->file));

    u64 start_counter = OsGetPerformanceCounter();

    Load *load = BeginLoad(&task->arena, state->file);
    usize size = 4096;
    u8 *buf = PushArray(&task->arena, u8, size);
    u32 nread = 0;
    while (!IsTaskCancelled(task) &&
           ((nread = LoadIntoBuffer(load, buf, size)) != 0)) {
        state->loaded += nread;
    }
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
    ASSERT(document->state == DocumentState_Loading, "");
    WaitTask(document->loading.task);
    OsLoadingFileClose(document->loading.state.file);
}

static Document *
LoadDocument(OsLoadingFile *file) {
    Document *document = BootstrapPushStruct(Document, arena);
    document->path =
        PushFormat(&document->arena, "%s", OsLoadingFileGetPath(file));
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
            char *text = PushFormat(
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
