#include "ztracing.h"

#include <zlib.h>

static void TransitToWelcome(App *app) {
    app->state = AppState_Welcome;
}

static App *AppCreate() {
    Arena *arena = ArenaCreate();
    App *app = ArenaPushStruct(arena, App);
    app->arena = arena;
    app->frame_arena = ArenaCreate();
    TransitToWelcome(app);
    return app;
}

static void AppDestroy(App *app) {
    ArenaDestroy(app->frame_arena);
    ArenaDestroy(app->arena);
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

        f32 left = ImGui::GetCursorPosX();
        f32 right = left;
        {
            char *text = ArenaPushString(
                app->arena,
                "%.1f MB  %.0f",
                MemoryGetAlloc() / 1024.0f / 1024.0f,
                io->Framerate
            );
            Vec2 size = ImGui::CalcTextSize(text);
            right = ImGui::GetWindowContentRegionMax().x - size.x;
            ImGui::SetCursorPosX(right);
            ImGui::Text("%s", text);
            ArenaPopString(app->arena, text);
        }

        {
            char *text = 0;
            switch (app->state) {
            case AppState_Loading: {
                OsLoadingFile *file = app->loading.data->file;
                text = OsLoadingFileGetPath(file);
            } break;

            default: {
            } break;
            }

            if (text) {
                Vec2 size = ImGui::CalcTextSize(text);
                left += (right - left - size.x) / 2.0f;
                ImGui::SetCursorPosX(left);
                ImGui::Text("%s", text);
            }
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
        char *text = ArenaPushString(app->arena, "Loading ...");
        Vec2 window_size = ImGui::GetWindowSize();
        Vec2 text_size = ImGui::CalcTextSize(text);
        ImGui::SetCursorPos((window_size - text_size) / 2.0f);
        ImGui::Text("%s", text);
        ArenaPopString(app->arena, text);
    }

    if (TaskIsDone(loading->task)) {
        TaskWait(loading->task);
        OsLoadingFileClose(loading->data->file);
        MemoryFree(loading->data);
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
    for (bool need_more_read = true; need_more_read;) {
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
    bool result = app->state != AppState_Loading;
    return result;
}

static void AppLoadFile(App *app, OsLoadingFile *file) {
    ASSERT(AppCanLoadFile(app), "");

    LoadFileData *data = (LoadFileData *)MemoryAlloc(sizeof(LoadFileData));
    data->file = file;

    Task *task = TaskCreate(DoLoadFile, data);

    AppLoading loading = {};
    loading.data = data;
    loading.task = task;
    TransitToLoading(app, loading);
}
