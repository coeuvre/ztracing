#include "ztracing.h"

#include <zlib.h>

static void WaitAndDestroyTask(Arena *arena, TracingLoading *loading) {
    TaskWait(loading->task);
    OsLoadingFileClose(loading->data->file);
    ArenaFree(arena, loading->data);
}

static void CancelTask(TracingLoading *loading) {
    loading->data->cancelled = true;
}

static void TracingDestroy(Tracing *tracing) {
    if (tracing->state == TracingState_Loading) {
        CancelTask(&tracing->loading);
        WaitAndDestroyTask(tracing->arena, &tracing->loading);
    }
    ArenaDestroy(tracing->arena);
}

static App *AppCreate() {
    Arena *arena = ArenaCreate();
    App *app = ArenaAllocStruct(arena, App);
    app->arena = arena;
    return app;
}

static void AppDestroy(App *app) {
    Tracing *tracing = app->tracing;
    while (tracing) {
        Tracing *next = tracing->next;
        TracingDestroy(tracing);
        tracing = next;
    }
    ArenaDestroy(app->arena);
}

static const char *WELCOME_MESSAGE =
    R"(
 ________  _________  _______          _        ______  _____  ____  _____   ______
|  __   _||  _   _  ||_   __ \        / \     .' ___  ||_   _||_   \|_   _|.' ___  |
|_/  / /  |_/ | | \_|  | |__) |      / _ \   / .'   \_|  | |    |   \ | | / .'   \_|
   .'.' _     | |      |  __ /      / ___ \  | |         | |    | |\ \| | | |   ____
 _/ /__/ |   _| |_    _| |  \ \_  _/ /   \ \_\ `.___.'\ _| |_  _| |_\   |_\ `.___]  |
|________|  |_____|  |____| |___||____| |____|`.____ .'|_____||_____|\____|`._____.'


                        Drag & Drop a trace file to start.
)";

static void MaybeDrawWelcome(App *app) {
    if (!app->tracing) {
        Vec2 window_size = ImGui::GetWindowSize();
        Vec2 logo_size = ImGui::CalcTextSize(WELCOME_MESSAGE);
        ImGui::SetCursorPos((window_size - logo_size) / 2.0f);
        ImGui::Text("%s", WELCOME_MESSAGE);
    }
}

static void TracingUpdate(Tracing *tracing) {
    switch (tracing->state) {
    case TracingState_Loading: {
        TracingLoading *loading = &tracing->loading;
        {
            char *text = ArenaFormatString(tracing->arena, "Loading ...");
            Vec2 window_size = ImGui::GetWindowSize();
            Vec2 text_size = ImGui::CalcTextSize(text);
            ImGui::SetCursorPos((window_size - text_size) / 2.0f);
            ImGui::Text("%s", text);
            ArenaFree(tracing->arena, text);
        }

        if (TaskIsDone(loading->task)) {
            WaitAndDestroyTask(tracing->arena, loading);
            tracing->state = TracingState_View;
        }
    } break;

    case TracingState_View: {

    } break;

    default: {
        UNREACHABLE;
    } break;
    }
}

static void DrawMenuBar(App *app) {
    ImGuiIO *io = &ImGui::GetIO();
    ImGuiStyle *style = &ImGui::GetStyle();
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("About")) {
            ImGui::MenuItem("Dear ImGui", "", &app->show_demo_window);
            ImGui::EndMenu();
        }

        f32 left = ImGui::GetCursorPosX();
        f32 right = left;
        {
            char *text = ArenaFormatString(
                app->arena,
                "%.1f MB  %.0f",
                MemoryGetAlloc() / 1024.0f / 1024.0f,
                io->Framerate
            );
            Vec2 size = ImGui::CalcTextSize(text);
            right = ImGui::GetWindowContentRegionMax().x - size.x -
                    style->ItemSpacing.x;
            ImGui::SetCursorPosX(right);
            ImGui::Text("%s", text);
            ArenaFree(app->arena, text);
        }

        ImGui::EndMenuBar();
    }
}

static void AppUpdate(App *app) {
    ImGuiID dockspace_id = ImGui::GetID("DockSpace");

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("Background", 0, window_flags);
    ImGui::PopStyleVar(3);
    {
        ImGui::DockSpace(
            dockspace_id,
            ImVec2(0.0f, 0.0f),
            ImGuiDockNodeFlags_PassthruCentralNode
        );

        DrawMenuBar(app);
        MaybeDrawWelcome(app);
    }
    ImGui::End();

    for (Tracing *tracing = app->tracing; tracing;) {
        char *title = ArenaFormatString(
            tracing->arena,
            "%s##%d",
            tracing->title,
            tracing->id
        );
        ImGui::SetNextWindowDockID(dockspace_id, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(title, &tracing->open)) {
            TracingUpdate(tracing);
        }
        ImGui::End();
        ArenaFree(tracing->arena, title);

        if (!tracing->open) {
            Tracing *next = tracing->next;

            if (tracing->prev) {
                tracing->prev->next = tracing->next;
                tracing->prev = 0;
            } else {
                app->tracing = next;
            }
            if (tracing->next) {
                tracing->next->prev = tracing->prev;
                tracing->next = 0;
            }

            TracingDestroy(tracing);

            tracing = next;
        } else {
            tracing = tracing->next;
        }
    }

    if (app->show_demo_window) {
        ImGui::ShowDemoWindow(&app->show_demo_window);
    }
}

static voidpf ZLibAlloc(voidpf opaque, uInt items, uInt size) {
    return MemoryAllocNoZero(items * size);
}

static void ZLibFree(voidpf opaque, voidpf address) {
    MemoryFree(address);
}

static void ProcessFileContent(u8 *buf, u32 len) {}

static void DoLoadFile(void *data_) {
    LoadFileData *data = (LoadFileData *)data_;
    INFO("Loading file %s ...", OsLoadingFileGetPath(data->file));

    u64 start_counter = OsGetPerformanceCounter();

    z_stream stream = {};
    stream.zalloc = ZLibAlloc;
    stream.zfree = ZLibFree;
    usize zstream_buf_len = 0;
    u8 *zstream_buf = 0;

    usize file_offset = 0;
    u8 file_buf[4096];

    usize total = 0;
    for (bool need_more_read = true; need_more_read && !data->cancelled;) {
        u32 nread =
            OsLoadingFileNext(data->file, file_buf, ARRAY_SIZE(file_buf));

        if (file_offset == 0 && nread >= 2 && file_buf[0] == 0x1F &&
            file_buf[1] == 0x8B) {
            int zret = inflateInit2(&stream, MAX_WBITS | 32);
            // TODO: Error handling.
            ASSERT(zret == Z_OK, "");
            zstream_buf_len = 16 * 1024;
            zstream_buf = (u8 *)MemoryAlloc(zstream_buf_len);
        }

        file_offset += nread;
        if (nread) {
            if (zstream_buf) {
                stream.avail_in = nread;
                stream.next_in = file_buf;

                do {
                    stream.avail_out = zstream_buf_len;
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

                    u32 have = zstream_buf_len - stream.avail_out;
                    ProcessFileContent(zstream_buf, have);
                    total += have;
                } while (stream.avail_out == 0);
            } else {
                ProcessFileContent(file_buf, nread);
                total += nread;
            }
        } else {
            need_more_read = false;
        }
    }

    if (zstream_buf) {
        MemoryFree(zstream_buf);
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

static bool AppCanLoadFile(App *app) {
    return true;
}

static void AppLoadFile(App *app, OsLoadingFile *file) {
    Arena *arena = ArenaCreate();
    Tracing *tracing = ArenaAllocStruct(arena, Tracing);
    tracing->arena = arena;
    tracing->title = ArenaFormatString(arena, "%s", OsLoadingFileGetPath(file));
    tracing->id = app->next_tracing_id++;
    tracing->state = TracingState_Loading;
    tracing->open = true;

    LoadFileData *data = ArenaAllocStruct(arena, LoadFileData);
    data->file = file;

    Task *task = TaskCreate(DoLoadFile, data);

    tracing->loading.data = data;
    tracing->loading.task = task;

    tracing->next = app->tracing;
    if (app->tracing) {
        app->tracing->prev = tracing;
    }
    app->tracing = tracing;
}
