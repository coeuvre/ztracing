#include "ztracing.h"

#include <zlib.h>

static App *AppCreate() {
    Arena *arena = ArenaCreate();
    App *app = ArenaPushStruct(arena, App);
    app->arena = arena;
    app->frame_arena = ArenaCreate();
    return app;
}

static void AppDestroy(App *app) {
    ArenaDestroy(app->frame_arena);
    ArenaDestroy(app->arena);
}

static void TransitToWelcome(App *app) {
    app->state = AppState_Welcome;
}

static void TransitToLoading(App *app, AppLoading loading) {
    switch (app->state) {
    case AppState_Welcome: {
        app->state = AppState_Loading;
        app->loading = loading;
    } break;

    default: {
        UNREACHABLE;
    } break;
    }
}

static void MainMenu(App *app) {
    ImGuiIO *io = &ImGui::GetIO();
    ImGuiStyle *style = &ImGui::GetStyle();
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("About")) {
            ImGui::MenuItem("Dear ImGui", "", &app->show_demo_window);
            ImGui::EndMenu();
        }

        {
            char *text = ArenaPushStr(
                app->arena,
                "%.1f MB  %.0f",
                MemGetAllocatedBytes() / 1024.0f / 1024.0f,
                io->Framerate
            );
            Vec2 size = ImGui::CalcTextSize(text);
            ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - size.x);
            ImGui::Text("%s", text);
            ArenaPopStr(app->arena, text);
        }

        ImGui::EndMainMenuBar();
    }

    if (app->show_demo_window) {
        ImGui::ShowDemoWindow(&app->show_demo_window);
    }
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

static void MainWindowWelcome() {
    Vec2 window_size = ImGui::GetWindowSize();
    Vec2 logo_size = ImGui::CalcTextSize(WELCOME_MESSAGE);
    ImGui::SetCursorPos((window_size - logo_size) / 2.0f);
    ImGui::Text("%s", WELCOME_MESSAGE);
}

static void MainWindowLoading(App *app) {
    AppLoading *loading = &app->loading;

    {
        char *text = ArenaPushStr(app->arena, "Loading ...");
        Vec2 window_size = ImGui::GetWindowSize();
        Vec2 text_size = ImGui::CalcTextSize(text);
        ImGui::SetCursorPos((window_size - text_size) / 2.0f);
        ImGui::Text("%s", text);
        ArenaPopStr(app->arena, text);
    }

    if (loading->task->done) {
        OsThreadJoin(loading->thread);
        MemFree(loading->task);
        TransitToWelcome(app);
    }
}

static void MainWindow(App *app) {
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    if (ImGui::Begin(
            "MainWindow",
            0,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDocking
        )) {
        switch (app->state) {
        case AppState_Welcome: {
            MainWindowWelcome();
        } break;

        case AppState_Loading: {
            MainWindowLoading(app);
        } break;

        default: {
            UNREACHABLE;
        } break;
        }
    }
    ImGui::End();
}

static void AppUpdate(App *app) {
    MainMenu(app);
    MainWindow(app);

    ArenaClear(app->frame_arena);
}

static voidpf ZLibAlloc(voidpf opaque, uInt items, uInt size) {
    return MemAlloc(items * size);
}

static void ZLibFree(voidpf opaque, voidpf address) {
    MemFree(address);
}

static void ProcessFileContent(u8 *buf, u32 len) {}

static int DoLoadFile(void *data) {
    LoadFileTask *task = (LoadFileTask *)data;
    INFO("Loading file ...");

    u64 start_counter = OsGetPerformanceCounter();

    z_stream stream = {};
    stream.zalloc = ZLibAlloc;
    stream.zfree = ZLibFree;
    u8 zstream_buf[4096];
    bool is_gz = false;

    usize file_offset = 0;
    u8 file_buf[4096];

    usize total = 0;
    for (bool need_more_read = true; need_more_read;) {
        u32 nread =
            OsLoadingFileNext(task->file, file_buf, ARRAY_SIZE(file_buf));

        if (file_offset == 0 && nread >= 2 && file_buf[0] == 0x1F &&
            file_buf[1] == 0x8B) {
            int zret = inflateInit2(&stream, MAX_WBITS | 32);
            // TODO: Error handling.
            ASSERT(zret == Z_OK, "");
            is_gz = true;
        }

        file_offset += nread;
        if (nread) {
            if (is_gz) {
                stream.avail_in = nread;
                stream.next_in = file_buf;

                do {
                    stream.avail_out = ARRAY_SIZE(zstream_buf);
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

                    u32 have = ARRAY_SIZE(zstream_buf) - stream.avail_out;
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

    if (is_gz) {
        inflateEnd(&stream);
    }

    OsLoadingFileClose(task->file);

    task->done = true;

    u64 end_counter = OsGetPerformanceCounter();
    f32 seconds =
        (f64)(end_counter - start_counter) / (f64)OsGetPerformanceFrequency();
    INFO(
        "Loaded %.1f MB in %.2f s (%.1f MB/s).",
        total / 1024.0f / 1024.0f,
        seconds,
        total / seconds / 1024.0f / 1024.0f
    );

    return 0;
}

static bool AppCanLoadFile(App *app) {
    bool result = app->state != AppState_Loading;
    return result;
}

static void AppLoadFile(App *app, OsLoadingFile *file) {
    ASSERT(AppCanLoadFile(app), "");

    LoadFileTask *task = (LoadFileTask *)MemAlloc(sizeof(LoadFileTask));
    task->file = file;

    OsThread *thread = OsThreadCreate(DoLoadFile, task);

    AppLoading loading = {};
    loading.task = task;
    loading.thread = thread;
    TransitToLoading(app, loading);
}
