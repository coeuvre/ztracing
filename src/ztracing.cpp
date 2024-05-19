#include "ztracing.h"

#include <stdio.h>

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

static int DoLoadFile(void *data) {
    LoadFileTask *task = (LoadFileTask *)data;
    INFO("Loading file ...");

    isize total = 0;
    u8 buf[4096];
    for (bool need_more_read = true; need_more_read;) {
        u32 nread = OsLoadingFileNext(task->file, buf, ARRAY_SIZE(buf) - 1);
        buf[nread] = 0;
        total += nread;
        // INFO("(%d): %s", nread, buf);
        // TODO: process buf[0..nread]
        need_more_read = nread > 0;
    }

    INFO("Processed %zd bytes.", total);

    OsLoadingFileClose(task->file);

    task->done = true;

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

static OsLoadingFile *AppGetLoadingFile(App *app) {
    OsLoadingFile *file = 0;
    if (app->state == AppState_Loading) {
        file = app->loading.task->file;
    }
    return file;
}
