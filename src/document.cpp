#include "document.h"

#include "ui.h"

#include <zlib.h>

static voidpf
ZLibAlloc(voidpf opaque, uInt items, uInt size) {
    return AllocateMemoryNoZero(items * size);
}

static void
ZLibFree(voidpf opaque, voidpf address) {
    DeallocateMemory(address);
}

static void
ProcessFileContent(u8 *buf, u32 len) {}

static void
DoLoadDocument(Task *task) {
    LoadState *state = (LoadState *)task->data;
    INFO("Loading file %s ...", OsLoadingFileGetPath(state->file));

    u64 start_counter = OsGetPerformanceCounter();

    z_stream stream = {};
    stream.zalloc = ZLibAlloc;
    stream.zfree = ZLibFree;
    usize zstream_buf_size = 4096;
    u8 *zstream_buf = 0;

    usize file_offset = 0;
    usize file_buf_size = 4096;
    u8 *file_buf = PushArray(&task->arena, u8, file_buf_size);

    usize total = 0;
    for (bool need_more_read = true;
         need_more_read && !IsTaskCancelled(task);) {
        u32 nread = OsLoadingFileNext(state->file, file_buf, file_buf_size);

        if (file_offset == 0 && nread >= 2 &&
            (file_buf[0] == 0x1F && file_buf[1] == 0x8B)) {
            int zret = inflateInit2(&stream, MAX_WBITS | 32);
            // TODO: Error handling.
            ASSERT(zret == Z_OK, "");
            zstream_buf = PushArray(&task->arena, u8, zstream_buf_size);
        }

        file_offset += nread;
        if (nread) {
            if (zstream_buf) {
                stream.avail_in = nread;
                stream.next_in = file_buf;

                do {
                    stream.avail_out = zstream_buf_size;
                    stream.next_out = zstream_buf;

                    int zret = inflate(&stream, Z_NO_FLUSH);
                    switch (zret) {
                    case Z_OK: {
                    } break;

                    case Z_STREAM_END: {
                        need_more_read = false;
                    } break;

                    default: {
                        // TODO: Error handling.
                        ABORT("inflate returned %d", zret);
                    } break;
                    }

                    u32 have = zstream_buf_size - stream.avail_out;
                    ProcessFileContent(zstream_buf, have);
                    total += have;
                    state->loaded += have;
                } while (stream.avail_out == 0);
            } else {
                ProcessFileContent(file_buf, nread);
                total += nread;
                state->loaded += nread;
            }
        } else {
            need_more_read = false;
        }
    }

    if (zstream_buf) {
        inflateEnd(&stream);
    }

    u64 end_counter = OsGetPerformanceCounter();
    f32 seconds =
        (f64)(end_counter - start_counter) / (f64)OsGetPerformanceFrequency();
    INFO(
        "Loaded %.1f MB in %.2f s (%.1f MB/s).",
        total / 1024.0f / 1024.0f,
        seconds,
        total / seconds / 1024.0f / 1024.0f
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
    document->loading.task = CreateTask(DoLoadDocument, &document->loading.state);
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
